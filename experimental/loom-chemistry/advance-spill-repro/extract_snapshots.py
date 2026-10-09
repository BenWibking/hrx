#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Extract replay inputs from a saved full-collapse HIP run.

The full run (`full_driver.cpp`, 1,000 grid steps, 128 cells) writes one
128-cell `CellRecord` snapshot after every prepare and every advance launch to
HIP-states.bin, and one CSV row per launch to HIP-steps.csv. Snapshot ordinal
2*s holds the advance input for step s; ordinal 2*s-1 holds the prepare input.
The prepare row of step s supplies the grid time and timestep for both launches.

This is a provenance tool. The committed snapshots were produced with:

  extract_snapshots.py HIP-states.bin HIP-steps.csv snapshots/
"""
import argparse
import csv
import hashlib
from pathlib import Path

CELLS = 128
RECORD_BYTES = 216

# (step, phase): phase 1 is advance, phase 0 is prepare.
SELECTED = [(0, 1), (100, 1), (500, 1), (920, 1), (999, 1), (500, 0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('states', type=Path)
    parser.add_argument('steps', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()

    blob = args.states.read_bytes()
    stride = CELLS * RECORD_BYTES
    if len(blob) % stride:
        raise SystemExit(f'{args.states}: size {len(blob)} is not a multiple of {stride}')
    with args.steps.open(newline='') as f:
        rows = list(csv.DictReader(f))

    args.output.mkdir(parents=True, exist_ok=True)
    manifest = []
    for step, phase in SELECTED:
        ordinal = 2 * step if phase else 2 * step - 1
        if not 0 <= ordinal < len(blob) // stride:
            raise SystemExit(f'step {step} phase {phase}: ordinal {ordinal} out of range')
        row = rows[2 * step]
        if int(row['step']) != step or int(row['phase']) != 0:
            raise SystemExit(f'unexpected CSV row for step {step}: {row}')
        name = f'{"advance" if phase else "prepare"}-step{step:04d}.bin'
        data = blob[ordinal * stride:(ordinal + 1) * stride]
        (args.output / name).write_bytes(data)
        # Keep time and dt as the original decimal text; the replay parses them
        # with strtod, which round-trips the values written by the full run.
        manifest.append([name, step, phase, row['time'], row['dt'],
                         hashlib.sha256(data).hexdigest()])

    with (args.output / 'snapshots.csv').open('w', newline='') as f:
        writer = csv.writer(f, lineterminator='\n')
        writer.writerow(['file', 'step', 'phase', 'time', 'dt', 'sha256'])
        writer.writerows(manifest)


if __name__ == '__main__':
    main()
