#!/usr/bin/env python3
"""Exit 0 only when high-IR corruption reproduces and the same-kernel control passes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runner', type=Path, default=here.parents[2] / 'build/cmake/loom/src/loom/tools/iree-test-loom/iree-test-loom')
parser.add_argument('--log', type=Path)
args = parser.parse_args()
result = subprocess.run([str(args.runner), '--device=amdgpu', str(here / 'high-pressure.loom')], capture_output=True, text=True)
if args.log:
    args.log.write_text(result.stdout + result.stderr)
try:
    report = json.loads(result.stdout)
except ValueError:
    sys.stderr.write(result.stdout + result.stderr)
    raise SystemExit('Native runner did not return a test report')
samples = {s['case']: s for s in report['samples']}
broken = samples.get('divergent_pressure', {})
control = samples.get('uniform_trip_count', {})
failures = broken.get('expectations', {}).get('failures', [])
reproduced = (len(samples) == 2 and not report['skipped_case_count'] and not report['planning_issue_count']
              and broken.get('passed') is False and not broken.get('issues')
              and len(failures) == 1 and failures[0]['diagnostic'] == 'EXPECT/001'
              and 'element at index 256 (-5)' in failures[0]['detail']
              and control.get('passed') is True)
if not reproduced:
    sys.stderr.write(result.stdout + result.stderr)
    raise SystemExit('Expected runtime corruption plus passing uniform-trip control was not observed')
print('High-IR runtime failure reproduced: lane 1, state 86 is 3 instead of 8.')
print('Same-kernel uniform-trip control: PASS. No explicit spills or native IR in the input.')
