#!/usr/bin/env python3
"""Verify both kernel roots with f64 math and real device-scope atomics."""
from pathlib import Path
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
tool = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='loom-chemistry-import-') as work:
    output = Path(work) / 'chemistry.loom'
    subprocess.run([sys.executable, str(here / 'generate.py'), f'--output-dir={work}'],
                   check=True)
    reproducer = Path(work) / 'reproducer.cpp'
    command = [tool, '--root=chemistry::prepare_grid_timestep_kernel',
               '--root=chemistry::advance_collapse_gridwide_kernel',
               '--data-model=lp64', '--approximate-functions=false',
               f'--I={here}', f'--output={output}', str(reproducer)]
    subprocess.run(command, check=True)
    ir = output.read_text()
    # sqrt stays a strict scalar operation; exp, log, and cbrt are f64_math.h
    # source recipes, so the IR must not request target math legalization.
    # Only the f32 cbrt seed, like OCML's, uses approximate log2 and exp2.
    assert any('scalar.sqrtf' in line and 'f64' in line for line in ir.splitlines())
    for operation in ('scalar.expf', 'scalar.logf', 'scalar.cbrtf'):
        assert operation not in ir, operation
    approximate = [line for line in ir.splitlines() if 'afn' in line]
    assert approximate, 'cbrt seed lost its approximate log2/exp2'
    assert all(('scalar.log2f<afn>' in line or 'scalar.exp2f<afn>' in line)
               and line.rstrip().endswith(': f32') for line in approximate), approximate
    assert ir.count('kernel.def ') == 2
    for name in ('fjac', 'e', 'y', 'mass', 'ip'):
        assert f'%{name}_storage = buffer.alloca<private>' in ir, name
    assert 'ScratchRecord* scratch' not in reproducer.read_text()
    atomics = [line for line in ir.splitlines() if 'view.atomic.' in line]
    assert any('view.atomic.rmw<addi>' in line for line in atomics), atomics
    assert any('view.atomic.cmpxchg' in line for line in atomics), atomics
    assert all('scope = device' in line and 'i32' in line for line in atomics), atomics
    for line in atomics:
        if 'cmpxchg' in line:
            assert 'success_ordering = relaxed' in line and 'failure_ordering = relaxed' in line, line
        else:
            assert 'ordering = relaxed' in line, line
    print('PASS: both kernel roots import and verify; f64 math and relaxed device-scope atomics retained')
