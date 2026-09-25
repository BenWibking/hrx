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
    command = [tool, '--root=chemistry::prepare_grid_timestep_kernel',
               '--root=chemistry::advance_collapse_gridwide_kernel',
               '--data-model=lp64', '--approximate-functions=false',
               f'--output={output}', str(here / 'reproducer.cpp')]
    subprocess.run(command, check=True)
    ir = output.read_text()
    for operation in ('scalar.expf', 'scalar.logf', 'scalar.sqrtf', 'scalar.cbrtf'):
        assert any(operation in line and 'f64' in line for line in ir.splitlines()), operation
    assert ir.count('kernel.def ') == 2
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
