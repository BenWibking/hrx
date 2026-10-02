#!/usr/bin/env python3
"""Run the reduced edge-store experiment; exit 0 means the bug was reproduced."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
root = here.parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runner', type=Path, default=root / 'build/cmake/loom/src/loom/tools/iree-test-loom/iree-test-loom')
parser.add_argument('--log', type=Path, help='Save unmodified runner stdout and stderr')
args = parser.parse_args()
result = subprocess.run([str(args.runner), '--device=amdgpu', str(here / 'spill-edge.loom')], capture_output=True, text=True)
if args.log:
    args.log.write_text(result.stdout + result.stderr)
try:
    report = json.loads(result.stdout)
except ValueError:
    sys.stderr.write(result.stdout + result.stderr)
    raise SystemExit('Runner did not return a test report')
expected = {'jacobian': (5, 43100), 'internal_steps': (8, 0), 'decompositions': (8, 0), 'linear_solves': (24, 5)}
samples = {sample['case']: sample for sample in report['samples']}
errors = []
if len(samples) != 8 or report['skipped_case_count'] or report['planning_issue_count']:
    errors.append('Expected eight executed cases without skips or planning issues')
for name, (good, bad) in expected.items():
    broken = samples.get('spill_' + name, {})
    control = samples.get('masked_' + name, {})
    failures = broken.get('expectations', {}).get('failures', [])
    signature = f'element at index 2 ({bad}) does not match the expected ({good})'
    reproduced = broken.get('passed') is False and len(failures) == 1 and failures[0]['diagnostic'] == 'EXPECT/001' and signature in failures[0]['detail']
    passed = control.get('passed') is True
    print(f'{name}: spill {bad} vs expected {good}: {"REPRODUCED" if reproduced else "UNEXPECTED"}; masked control: {"PASS" if passed else "FAIL"}')
    if not reproduced or not passed:
        errors.append(name)
if errors:
    sys.stderr.write(result.stdout + result.stderr)
    raise SystemExit('Unexpected outcome: ' + ', '.join(errors))
print('Four clobbers reproduced; four masked controls passed. This is not a compiler-fix validation.')
