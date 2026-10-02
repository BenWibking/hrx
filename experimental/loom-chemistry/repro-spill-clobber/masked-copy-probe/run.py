"""Run unchanged and patched kernels and check exact predicted residuals."""
import pathlib
import re
import subprocess

cases = [('original', '../spill-free/rotate-32-3.hsaco'),
         ('masked', 'variants/masked.hsaco'),
         ('all', 'variants/all.hsaco'),
         ('lane1', 'variants/lane1.hsaco')]
log = []
for repeat in range(1, 4):
    for name, filename in cases:
        for mode in (0, 1):
            expected = {}
            if mode and name in ('all', 'lane1'):
                for lane in range(64):
                    if (name == 'lane1' and lane == 1) or (name == 'all' and lane % 4 == 1):
                        expected[lane] = (1, 1, -2)
                    elif name == 'all' and lane % 4 == 2:
                        expected[lane] = (-1, 2, -1)
            result = subprocess.run(['./driver', filename, str(mode)],
                                    text=True, capture_output=True)
            actual = {int(lane): (int(a), int(b), int(c)) for lane, a, b, c in
                      re.findall(r'lane=(\d+) residuals=(-?\d+),(-?\d+),(-?\d+)', result.stdout)}
            assert result.returncode == bool(expected), result.stderr
            assert actual == expected, (name, mode, actual, expected)
            assert f'mode={mode} mismatches={3 * len(expected)}' in result.stdout
            log.append(f'repeat={repeat} variant={name} mode={mode} predicted_mismatches={3 * len(expected)} VERIFIED\n' + result.stdout)
for name, filename in cases:
    dis = subprocess.check_output(['/opt/rocm/llvm/bin/llvm-objdump', '-d', filename], text=True)
    assert 'scratch_' not in dis
    pathlib.Path(name + '.dis').write_text(dis)
pathlib.Path('results.log').write_text('\n'.join(log))
print('24 launches matched exact predictions; all 4 binaries contain no scratch instructions.')
