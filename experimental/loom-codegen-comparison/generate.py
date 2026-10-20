#!/usr/bin/env python3
"""Specialize the pinned chemistry source; never rewrite floating expressions.

Writes reproducer.cpp and reference_kernels.inc to --output-dir. The output is
ordinary C++ and is not checked in; builds and scripts generate it. The device
program is copied from reference.cpp section by section. Only the rewrites that
loom-import-cxx still requires are applied, and exact replacements fail on
source drift.
"""
import argparse
import hashlib
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
SOURCE = (HERE / "reference.cpp").read_text()
SOURCE_SHA256 = '6c7ca23933b211980e831e8d2bfc4328245ab0e3c75f6b07a5e0ed40307843d0'
assert hashlib.sha256(SOURCE.encode()).hexdigest() == SOURCE_SHA256, (
    'reference.cpp changed: review every specialization rule before updating the pinned digest')


def section(start, end):
    """Source text from the unique start marker up to (not including) end."""
    assert SOURCE.count(start) == 1, start
    first = SOURCE.index(start)
    return SOURCE[first:SOURCE.index(end, first)]


def body(name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", SOURCE)
    if not match:
        raise ValueError(name)
    start = match.end()
    depth = 1
    end = start
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end - 1]


def replace(text, old, new, count=1):
    assert text.count(old) == count, (old, text.count(old), count)
    return text.replace(old, new)


def common(text):
    """Bind the source's namespaces, host/device macros, and std facilities."""
    for macro in ('INTEGRATORS_HOST_DEVICE', 'PRIMORDIAL_HOST_DEVICE'):
        text = text.replace(macro, 'DEVICE')
    text = text.replace('__global__ void', 'KERNEL void')
    for qualifier in ('integrators::', 'primordial_chem::', 'pc::', 'linalg::'):
        text = text.replace(qualifier, '')
    text = text.replace('std::size_t', 'size_type').replace('std::uint64_t', 'u64')
    # support.h supplies array, numeric_limits, and the math functions.
    text = text.replace('std::', 'math::')
    # loom-import-cxx has no range-for: iterate the same elements in order.
    text = re.sub(r'^( *)for \((?:auto|Real)& (\w+) : ([\w.>-]+)\) \{',
                  lambda m: f'{m[1]}for (size_type {m[2]}_index = 0; {m[2]}_index < {m[3]}.size(); '
                            f'++{m[2]}_index) {{\n{m[1]}    auto& {m[2]} = {m[3]}[{m[2]}_index];',
                  text, flags=re.MULTILINE)
    # loom-import-cxx requires an initializer on every local. Each of these
    # types has default member initializers, so {} leaves the same values.
    text = re.sub(r'^( +(?:Ros2sIntegrator(?:::State)?|burn_t|Array1D<[^;]*>) \w+);$', r'\1{};',
                  text, flags=re.MULTILINE)
    return text


def matching(text, open_paren):
    depth = 0
    for i in range(open_paren, len(text)):
        depth += (text[i] == '(') - (text[i] == ')')
        if not depth:
            return i
    raise ValueError(open_paren)


def arguments(text):
    """Split a parameter or argument list at its top-level commas."""
    pieces, depth, start = [], 0, 0
    for i, char in enumerate(text):
        depth += (char in '(<[{') - (char in ')>]}')
        if char == ',' and not depth:
            pieces.append(text[start:i])
            start = i + 1
    return pieces + [text[start:]]


RECORD_PARAMETER = re.compile(
    r'(const )?\b((?:Ros2sIntegrator::)?State|burn_t|CollapseState|IntegratorStats)& (\w+)(?=\s*[,)])')


def record_pointers(text):
    """Pass records by pointer and rebind the source's reference on entry.

    loom-import-cxx rejects a reference parameter to a record once a member is
    bound to a reference, as for a call argument or an overloaded operator[]
    ("mutation requires an owned automatic source binding"). A local reference
    to the pointee imports, so function bodies keep their source form.
    """
    converted = {}
    while match := RECORD_PARAMETER.search(text):
        depth, open_paren = 0, match.start()
        while depth or text[open_paren] != '(':
            open_paren -= 1
            depth += (text[open_paren] == ')') - (text[open_paren] == '(') * bool(depth)
        name = re.search(r'(\w+)\s*$', text[max(0, open_paren - 200):open_paren])[1]
        close = matching(text, open_paren)
        brace = re.compile(r'\s*(?:const\s*)?\{').match(text, close + 1)
        assert brace, name
        params = arguments(text[open_paren + 1:close])
        bindings = []
        for index, param in enumerate(params):
            if record := RECORD_PARAMETER.search(param + ')'):
                const, kind, variable = record.groups()
                const = const or ''
                params[index] = param.replace(record[0], f'{const}{kind}* {variable}_pointer')
                bindings.append(f'{const}{kind}& {variable} = *{variable}_pointer;')
                converted.setdefault(name, set()).add(index)
        indent = re.match(r'\n*( *)', text[brace.end():])[1]
        text = (text[:open_paren + 1] + ','.join(params) + ')' + text[close + 1:brace.end()]
                + ''.join('\n' + indent + binding for binding in bindings) + text[brace.end():])
    for name, indexes in converted.items():
        position = 0
        while match := re.compile(r'\b' + name + r'\s*\(').search(text, position):
            open_paren = match.end() - 1
            close = matching(text, open_paren)
            args = arguments(text[open_paren + 1:close])
            if not any('_pointer' in arg for arg in args):
                for index in indexes:
                    args[index] = re.sub(r'^(\s*)', r'\1&', args[index], count=1)
                text = text[:open_paren + 1] + ','.join(args) + text[close:]
            position = open_paren + 1
    return text


parts = ['// SPDX-License-Identifier: BSD-3-Clause\n'
         '// Generated by generate.py from reference.cpp; do not hand edit.\n'
         '// Source SHA256: ' + SOURCE_SHA256 + '\n'
         '#include "support.h"\nnamespace chemistry {\n']
parts.append(section('enum class IntegratorResult', '\nnamespace detail {') + '\n')

# Linear algebra. The assumptions state pivot bounds the source guarantees.
linalg = section('template<size_type N>\nINTEGRATORS_HOST_DEVICE int lu_decomposition',
                 '} // namespace linalg')
linalg = replace(linalg, 'ipvt[k] = static_cast<int>(pivot_row);',
                 'ipvt[k] = static_cast<int>(pivot_row);\n            __builtin_assume(pivot_row < N);')
linalg = replace(linalg, 'const auto pivot_row = static_cast<size_type>(ipvt[k]);',
                 'const auto pivot_row = static_cast<size_type>(ipvt[k]);\n            __builtin_assume(pivot_row < N);')
parts.append(common(linalg))

# Chemistry network: constants, EOS, generated RHS and Jacobian, and problem.
parts.append(section('constexpr int NumSpec = 14;', 'inline Real& redshift_storage()'))
# The device branch of redshift() returns the compile-time default.
parts.append('DEVICE Real redshift() { return default_redshift; }\n\n')
chemistry = section('INTEGRATORS_HOST_DEVICE constexpr Real small_number_density_floor',
                    '} // namespace integrators::primordial_chem')
# The problem's neqs names the namespace constant, which loses its qualifier.
chemistry = replace(chemistry, 'neqs = primordial_chem::neqs;', 'neqs = chemistry::neqs;')
parts.append(common(chemistry))

# ROS2S integrator.
rodas = section('template<size_type N>\nstruct RODASState', '} // namespace integrators\n\n#endif // RODAS_HPP')
rodas = replace(rodas, 'ProblemTraits<Problem>::neqs', 'Problem::neqs')
rodas = replace(rodas, '    static_assert(ProblemTraits<Problem>::has_analytic_jacobian,\n'
                       '                  "RODAS requires Problem::jacobian_type and Problem::jacobian");\n', '')
# Returns inside loops: record the result and leave both solver loops through
# done/retry flags at the original exit points.
rodas = replace(rodas, '        for (size_type i = 0; i < N; ++i) {\n'
                       '            if (atol_for(s, i) <= 0.0 || rtol_for(s, i) <= 10.0 * s.uround) {\n'
                       '                return IntegratorResult::TOO_MUCH_ACCURACY_REQUESTED;\n'
                       '            }\n'
                       '        }\n',
                       '        bool too_accurate = false;\n'
                       '        for (size_type i = 0; i < N && !too_accurate; ++i) {\n'
                       '            __builtin_assume(i < N);\n'
                       '            if (atol_for(s, i) <= 0.0 || rtol_for(s, i) <= 10.0 * s.uround) {\n'
                       '                too_accurate = true;\n'
                       '            }\n'
                       '        }\n'
                       '        if (too_accurate) {\n'
                       '            return IntegratorResult::TOO_MUCH_ACCURACY_REQUESTED;\n'
                       '        }\n')
rodas = replace(rodas, '        int n_accept = 0;\n',
                '        int n_accept = 0;\n'
                '        IntegratorResult result = IntegratorResult::SUCCESS;\n'
                '        bool done = false;\n')
rodas = replace(rodas, 'for (;;) {', 'while (!done) {', count=2)
for code in ('TOO_MANY_STEPS', 'DT_UNDERFLOW', 'SUCCESS'):
    rodas = replace(rodas, f'return IntegratorResult::{code};',
                    f'result = IntegratorResult::{code};\n                done = true;')
rodas = replace(rodas, '            if (0.1 *', '            else if (0.1 *')
rodas = replace(rodas, '            if (last) {', '            else if (last) {')
rodas = replace(rodas, '\n            hopt = h;', '\n            if (!done) {\n            hopt = h;')
rodas = replace(rodas, 'eval_jacobian(s, x);\n', 'eval_jacobian(s, x);\n            bool retry = true;\n')
retry = rodas.index('bool retry = true;')
rodas = rodas[:retry] + replace(rodas[retry:], 'while (!done)', 'while (retry && !done)')
rodas = replace(rodas, 'return IntegratorResult::LU_DECOMPOSITION_ERROR;',
                'result = IntegratorResult::LU_DECOMPOSITION_ERROR;\n'
                '                        done = true;\n'
                '                        continue;')
rodas = replace(rodas, '                    break;\n                }',
                '                    retry = false;\n                } else {')
tail = ('                if (n_accept >= 1) {\n                    record_reject(s);\n'
        '                }\n            }\n        }\n    }\n};')
rodas = replace(rodas, tail,
                '                if (n_accept >= 1) {\n                    record_reject(s);\n'
                '                }\n                }\n            }\n            }\n        }\n'
                '        return result;\n    }\n};')
# Reindent the solver class so the new control nesting is reviewable. It has
# no string literals or comments containing braces or parentheses.
solver = rodas.index('template<typename Problem>\nclass RODAS')
lines = []
depth = parens = 0
for line in rodas[solver:].splitlines():
    line = line.strip()
    if line:
        leading_close = int(line.startswith('}'))
        label = int(line in ('public:', 'private:'))
        lines.append('    ' * (depth - leading_close - label + int(parens > 0)) + line)
        depth += line.count('{') - line.count('}')
        parens += line.count('(') - line.count(')')
    else:
        lines.append('')
assert depth == parens == 0
rodas = rodas[:solver] + '\n'.join(lines) + '\n'
parts.append(common(rodas))

# Collapse driver.
for name in ('perturbation_interval', 'perturbation_amplitude', 'tff_reduc', 'rtol_spec',
             'atol_spec', 'rtol_energy', 'atol_energy'):
    match = re.search(r'constexpr (?:integrators::)?(Real|int) ' + name + r' = ([^;]+);', SOURCE)
    assert match, name
    parts.append(f'constexpr {match[1]} {name} = {match[2]};\n')
parts.append(common(section('struct IntegratorStats {', 'struct CounterSummary')))
parts.append(common(section('struct CollapseState {', 'struct Options')))
parts.append('using Ros2sIntegrator = RODAS<PrimordialChem>;\n')
parts.append('// Interface names shared with compare_rocm.cpp and the autoresearch harness.\n'
             'using BurnRecord = burn_t;\nusing CellRecord = CollapseState;\n'
             'static_assert(sizeof(CellRecord) == 216);\n\n')
driver = section('PRIMORDIAL_HOST_DEVICE std::uint64_t splitmix64',
                 '[[maybe_unused]] PRIMORDIAL_HOST_DEVICE bool advance_collapse_step')
# Steps count up from zero, so the unsigned remainder is equivalent and avoids
# signed remainder lowering.
driver = replace(driver, 'step % perturbation_interval',
                 'static_cast<unsigned>(step) % static_cast<unsigned>(perturbation_interval)')
parts.append(common(driver))

for name in ('prepare_grid_timestep_kernel', 'advance_collapse_gridwide_kernel'):
    kernel = section('__global__ void ' + name, '\n}\n') + '\n}\n'
    kernel = replace(kernel, 'blockIdx.x * blockDim.x + threadIdx.x', 'CELL_INDEX')
    kernel = replace(kernel, 'int* failure_code)', 'int* failure_code CELL_PARAMETER)')
    parts.append(common(kernel))
parts.append('} // namespace chemistry\n')

parser = argparse.ArgumentParser()
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
reference_kernels = ['// SPDX-License-Identifier: BSD-3-Clause\n// Generated original kernel bodies for serial-lane differential testing.\n']
for name in ('prepare_grid_timestep_kernel', 'advance_collapse_gridwide_kernel'):
    start = SOURCE.index('__global__ void ' + name)
    end = SOURCE.index('{', start)
    signature = SOURCE[start:end].replace('__global__ void ' + name, 'void reference_' + name)
    signature = signature.rstrip()[:-1] + ', int cell_index)'
    text = body(name).replace('blockIdx.x * blockDim.x + threadIdx.x', 'cell_index')
    text = text.replace('atomicCAS(', 'chemistry::atomicCAS(').replace('atomicAdd(', 'chemistry::atomicAdd(')
    reference_kernels.append(signature + '{' + text + '}\n')
for name, content in [('reproducer.cpp', record_pointers(''.join(parts))),
                      ('reference_kernels.inc', ''.join(reference_kernels))]:
    if name == 'reproducer.cpp':
        assert '#include' not in content.split('#include "support.h"', 1)[1], 'source include copied'
        assert not re.search(r'\bstd::|for \([^;]*:', content), 'unbound std facility or range-for'
    content = '\n'.join(line.rstrip() for line in content.splitlines()) + '\n'
    args.output_dir.mkdir(parents=True, exist_ok=True)
    path = args.output_dir / name
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
