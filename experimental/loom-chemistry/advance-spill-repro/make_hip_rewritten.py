#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the HIP-rewritten control: Loom's input source compiled by hipcc.

Copies reproducer.cpp and integrate.inc unchanged, and adds HIP declarations
to support.h: __host__ __device__ inline helpers, __global__ kernels, HIP
thread indexing, and HIP device atomicCAS/atomicAdd. The chemistry arithmetic
is exactly the source Loom compiles. compare_hip_rewritten.cpp is
compare_rocm.cpp with its HIP backend switched to these rewritten kernels and
the CellRecord layout. Build replay.cpp against it with
-DREPLAY_HARNESS='"<output>/compare_hip_rewritten.cpp"'.

This control shows whether a slowdown comes from the rewritten source or from
Loom code generation. Every replacement must match exactly once.
"""
import argparse
import shutil
from pathlib import Path

CHEMISTRY = Path(__file__).resolve().parent.parent


def replace_once(text, old, new, what):
    if text.count(old) != 1:
        raise SystemExit(f'{what}: expected one match for {old!r}, found {text.count(old)}')
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    rewritten = out / 'hip-rewritten'
    rewritten.mkdir(parents=True, exist_ok=True)
    for name in ['reproducer.cpp', 'integrate.inc']:
        shutil.copyfile(CHEMISTRY / name, rewritten / name)

    s = (CHEMISTRY / 'support.h').read_text()
    s = replace_once(s, '#else\n#include <cmath>', '''#elif defined(__HIPCC__)
#include <hip/hip_runtime.h>
#include <cmath>
#define DEVICE __host__ __device__ __attribute__((always_inline)) inline
#define KERNEL __global__
#define CELL_PARAMETER
#define CELL_INDEX (blockIdx.x * blockDim.x + threadIdx.x)
#else
#include <cmath>''', 'support.h')
    s = replace_once(s, '#else\n// Native CPU validation', '''#elif defined(__HIP_DEVICE_COMPILE__)
DEVICE int atomic_cas(int* address, int expected, int desired) { return ::atomicCAS(address, expected, desired); }
DEVICE int atomic_add(int* address, int value) { return ::atomicAdd(address, value); }
#else
// Native CPU validation''', 'support.h')
    (rewritten / 'support.h').write_text(s)

    h = (CHEMISTRY / 'compare_rocm.cpp').read_text()
    for old, new in [
        ('#include "reference.cpp"', f'#include "{CHEMISTRY / "reference.cpp"}"'),
        ('#include "reproducer.cpp"', '#include "hip-rewritten/reproducer.cpp"'),
        ('DeviceBuffer<CollapseState> original;', 'DeviceBuffer<lc::CellRecord> original;'),
        ('original.put(init.data(), n)', 'original.put(packed.data(), n)'),
        ('            prepare_grid_timestep_kernel<<<', '            lc::prepare_grid_timestep_kernel<<<'),
        ('            advance_collapse_gridwide_kernel<<<', '            lc::advance_collapse_gridwide_kernel<<<'),
        ('else { std::vector<CollapseState> temp(n); original.get(temp.data(), n);\n'
         '               for (int i = 0; i < n; ++i) result[i] = pack(temp[i]); }',
         'else original.get(result.data(), n);'),
    ]:
        h = replace_once(h, old, new, 'compare_rocm.cpp')
    (out / 'compare_hip_rewritten.cpp').write_text(h)


if __name__ == '__main__':
    main()
