#!/usr/bin/env python3
"""XFAIL: compile the production-sized advance kernel for gfx942.

The advance kernel uses 128-thread workgroups; a 64^3 grid needs 2048 of them.
Loom currently exhausts the 256-VGPR budget for this configuration, so the
expected failure passes and an unexpected success (XPASS) fails, prompting the
XFAIL to be removed. Any other failure also fails.
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = 'advance_collapse_gridwide_kernel'
WORKGROUP_SIZE = 128
WORKGROUP_COUNT = (64 * 64 * 64 + WORKGROUP_SIZE - 1) // WORKGROUP_SIZE
EXPECTED_ERROR = re.compile(
    r"error \[BACKEND/005\]: .*'@chemistry\.advance_collapse_gridwide_kernel'"
    r".*budget 256.*failure code 'spill-traffic-register-exhausted'")

here = Path(__file__).resolve().parent
importer, compiler = sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix='loom-chemistry-compile-') as work:
    module = Path(work) / 'chemistry.loom'
    subprocess.run([importer, f'--root=chemistry::{ROOT}', '--data-model=lp64',
                    '--approximate-functions=false', f'--output={module}',
                    str(here / 'reproducer.cpp')], check=True)
    kernel = module.read_text().split(f'kernel.def @chemistry.{ROOT}()', 1)[1]
    size = re.search(r'workgroup_size\((%\S+),', kernel).group(1)
    assert f'{size} = index.constant {WORKGROUP_SIZE} : index' in kernel, size
    config = [f'--config=chemistry.{ROOT}.workgroup_count.{axis}={count}'
              for axis, count in (('x', WORKGROUP_COUNT), ('y', 1), ('z', 1))]
    result = subprocess.run(
        [compiler, str(module), f'--root=chemistry.{ROOT}', *config,
         '--format=amdgpu-hsaco', '--target=amdgpu:gfx942',
         f'--output={Path(work) / "advance.hsaco"}'],
        capture_output=True, text=True)
if result.returncode == 0:
    sys.exit(f'XPASS: {WORKGROUP_COUNT} x {WORKGROUP_SIZE}-thread advance kernel '
             'now compiles; remove the XFAIL from check_compile.py and README.md')
errors = [line for line in result.stderr.splitlines() if 'error [' in line]
if len(errors) != 1 or not EXPECTED_ERROR.search(errors[0]):
    sys.exit(f'FAIL: unexpected loom-compile failure (exit {result.returncode}):\n' +
             '\n'.join(errors or result.stderr.splitlines()[-40:]))
print(f'XFAIL: {WORKGROUP_COUNT} x {WORKGROUP_SIZE}-thread advance kernel exhausts '
      'the gfx942 VGPR budget')
