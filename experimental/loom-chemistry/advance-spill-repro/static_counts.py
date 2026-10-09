#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Static code-object metrics for the chemistry kernels.

  static_counts.py [--json OUT] [--log LABEL[:ROLE]=compile.log ...] LABEL=code.hsaco ...

Each code object may contain one or both chemistry kernels; they are matched
by the substrings `prepare_grid_timestep_kernel` and
`advance_collapse_gridwide_kernel`. Requires llvm-objdump and llvm-readelf
with AMDGPU support (set LLVM_BIN to their directory if they are not on PATH).

All instruction counts are STATIC sites in the disassembly, not dynamic
execution counts. Scratch operand bytes are per-lane widths summed over sites.
HIP scratch includes ordinary private arrays as well as spills, so HIP
scratch counts are an upper bound on HIP spill traffic. Loom's spill
diagnostics (--log) count compiler-inserted spill storages; their store and
reload counts are not ISA instruction counts.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

KERNELS = {
    'prepare': 'prepare_grid_timestep_kernel',
    'advance': 'advance_collapse_gridwide_kernel',
}
WIDTH = {'ubyte': 1, 'sbyte': 1, 'byte': 1, 'ushort': 2, 'sshort': 2, 'short': 2,
         'dword': 4, 'dwordx2': 8, 'dwordx3': 12, 'dwordx4': 16}
METADATA = ['private_segment_fixed_size', 'group_segment_fixed_size', 'sgpr_count',
            'vgpr_count', 'agpr_count', 'sgpr_spill_count', 'vgpr_spill_count']

SYMBOL = re.compile(r'^([0-9a-f]+) <(.+)>:$')
# llvm-objdump prints `\tmnemonic operands  // ADDRESS: ENCODING [<sym+off>]`.
INSTRUCTION = re.compile(r'^\s+(\S+)(.*?)\s*// ([0-9A-F]+):[ 0-9A-F]*(?:<([^>+]+)(?:\+0x([0-9a-f]+))?>)?\s*$')
SPILL = re.compile(r'warning \[BACKEND/009\].*for amdgpu\.(\w+) value .* reserving (\d+) byte\(s\), '
                   r'(\d+) store\(s\) for (\d+) byte\(s\), and (\d+) reload\(s\) for (\d+) byte\(s\)')


def tool(name):
    for candidate in [os.environ.get('LLVM_BIN'), '/opt/homebrew/opt/llvm/bin', '/opt/rocm/llvm/bin']:
        if candidate and (Path(candidate) / name).exists():
            return str(Path(candidate) / name)
    found = shutil.which(name)
    if not found:
        sys.exit(f'{name} not found; set LLVM_BIN')
    return found


def kernel_role(symbol):
    for role, needle in KERNELS.items():
        if needle in symbol and not symbol.endswith('.kd'):
            return role
    return None


def disassembly(path):
    text = subprocess.run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', str(path)],
                          check=True, capture_output=True, text=True).stdout
    functions, symbols, current = {}, {}, None
    for line in text.splitlines():
        m = SYMBOL.match(line)
        if m:
            symbols[m.group(2)] = int(m.group(1), 16)
            role = kernel_role(m.group(2))
            current = functions.setdefault(role, []) if role else None
            continue
        if current is None:
            continue
        m = INSTRUCTION.match(line)
        if m:
            target = None
            if m.group(4) in symbols:
                target = symbols[m.group(4)] + int(m.group(5) or '0', 16)
            current.append((int(m.group(3), 16), m.group(1), m.group(2).strip(), target))
    return functions


def instruction_metrics(instructions):
    by_address = {address: mnemonic for address, mnemonic, _, _ in instructions}
    counts = Counter()
    for _, mnemonic, operands, target in instructions:
        counts['instructions'] += 1
        for kind in ('load', 'store'):
            prefix = f'scratch_{kind}_'
            if mnemonic.startswith(prefix):
                width = mnemonic[len(prefix):].removesuffix('_d16').removesuffix('_d16_hi')
                counts[f'scratch_{kind}s'] += 1
                counts[f'scratch_{kind}_bytes'] += WIDTH.get(width, 0)
                if width not in WIDTH:
                    counts['scratch_unknown_width'] += 1
            if mnemonic.startswith(f'buffer_{kind}_'):
                counts[f'buffer_{kind}s'] += 1
        if mnemonic == 's_waitcnt':
            counts['s_waitcnt'] += 1
        if mnemonic == 's_branch':
            counts['s_branch'] += 1
            if target is not None and by_address.get(target) == 's_branch':
                counts['s_branch_to_s_branch'] += 1
        elif mnemonic.startswith('s_cbranch_'):
            counts['s_cbranch'] += 1
        if 'saveexec' in mnemonic or operands.startswith('exec,') or operands.startswith('exec_lo,'):
            counts['exec_writes'] += 1
        if mnemonic.startswith('v_readfirstlane'):
            counts['v_readfirstlane'] += 1
        if mnemonic.startswith(('v_readlane', 'v_writelane')):
            counts['v_readlane_writelane'] += 1
    for key in ('scratch_loads', 'scratch_stores', 'scratch_load_bytes', 'scratch_store_bytes',
                'buffer_loads', 'buffer_stores', 's_waitcnt', 's_branch', 's_branch_to_s_branch',
                's_cbranch', 'exec_writes', 'v_readfirstlane', 'v_readlane_writelane'):
        counts.setdefault(key, 0)
    return dict(counts)


def metadata(path):
    text = subprocess.run([tool('llvm-readelf'), '--notes', str(path)],
                          check=True, capture_output=True, text=True).stdout
    result = {}
    # Each amdhsa.kernels entry starts with `  - .<first key>:`; its other
    # kernel-level keys are indented four spaces. Argument keys are deeper.
    for chunk in re.split(r'^(?=  - \.)', text, flags=re.M)[1:]:
        fields = dict(re.findall(r'^(?:    |  - )\.(\w+):\s+(.*)$', chunk, flags=re.M))
        role = kernel_role(fields.get('name', ''))
        if role:
            result[role] = {k: int(fields[k]) for k in METADATA if k in fields}
            result[role]['symbol'] = fields['name']
    return result


def spill_log(path):
    totals = Counter()
    for line in Path(path).read_text(errors='replace').splitlines():
        m = SPILL.search(line)
        if not m:
            continue
        cls = m.group(1)
        reserve, stores, store_bytes, reloads, reload_bytes = map(int, m.groups()[1:])
        totals['spill_storages'] += 1
        for key, value in [('storages', 1), ('reserved_bytes', reserve), ('stores', stores),
                           ('store_bytes', store_bytes), ('reloads', reloads), ('reload_bytes', reload_bytes)]:
            totals[f'{cls}_{key}'] += value
    return dict(totals)


def label_path(text):
    label, sep, path = text.partition('=')
    if not sep or not label or not path:
        raise argparse.ArgumentTypeError(f'expected LABEL=PATH, got {text!r}')
    return label, Path(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('objects', nargs='+', type=label_path, metavar='LABEL=HSACO')
    parser.add_argument('--log', action='append', default=[], type=label_path, metavar='LABEL=LOG',
                        help='Loom compile log with BACKEND/009 spill diagnostics')
    parser.add_argument('--json', type=Path, help='write all metrics as JSON')
    args = parser.parse_args()

    report = {}
    for label, path in args.objects:
        meta = metadata(path)
        for role, instructions in disassembly(path).items():
            entry = report.setdefault(f'{label}:{role}', {'object': str(path)})
            entry.update(meta.get(role, {}))
            entry.update(instruction_metrics(instructions))
    for label, path in args.log:
        matches = [k for k in report if k == label or k.startswith(f'{label}:')]
        if len(matches) != 1:
            sys.exit(f'--log {label}: matches {matches}; use LABEL:prepare or LABEL:advance')
        report[matches[0]]['spill_diagnostics'] = spill_log(path)

    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')

    rows = [('instructions', 'Total instructions'),
            ('scratch_stores', 'Scratch store sites'), ('scratch_store_bytes', 'Scratch store bytes'),
            ('scratch_loads', 'Scratch load sites'), ('scratch_load_bytes', 'Scratch load bytes'),
            ('s_waitcnt', 's_waitcnt'), ('s_branch', 's_branch (unconditional)'),
            ('s_branch_to_s_branch', 's_branch to s_branch'), ('s_cbranch', 's_cbranch_*'),
            ('exec_writes', 'EXEC writes'), ('v_readfirstlane', 'v_readfirstlane'),
            ('v_readlane_writelane', 'v_readlane/v_writelane'),
            ('private_segment_fixed_size', 'Private bytes per lane'),
            ('sgpr_count', 'SGPRs'), ('vgpr_count', 'VGPRs'), ('agpr_count', 'AGPRs')]
    for role in KERNELS:
        columns = [k for k in report if k.endswith(f':{role}')]
        if not columns:
            continue
        print(f'\n{role}\n')
        print('| Metric | ' + ' | '.join(c.split(':')[0] for c in columns) + ' |')
        print('| --- |' + ' ---: |' * len(columns))
        for key, title in rows:
            values = [report[c].get(key) for c in columns]
            if all(v is None for v in values):
                continue
            print(f'| {title} | ' + ' | '.join('' if v is None else f'{v:,}' for v in values) + ' |')
        for c in columns:
            spills = report[c].get('spill_diagnostics')
            if spills:
                print(f'\n{c} BACKEND/009 spill storages: {spills["spill_storages"]:,} '
                      f'(SGPR {spills.get("sgpr_storages", 0):,}, VGPR {spills.get("vgpr_storages", 0):,})')


if __name__ == '__main__':
    main()
