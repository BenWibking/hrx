#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Pack this reproducer and the loom-chemistry sources it uses into one tarball
# that works outside the HRX checkout (supply LOOM_COMPILE and, on the GPU
# machine, ROCm).
#
#   package.sh [OUTPUT.tar.gz]
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
chem=$(cd -- "$here/.." && pwd)
repo=$(git -C "$chem" rev-parse --show-toplevel)
output=${1:-$repo/build/loom-chemistry-advance-spill-repro.tar.gz}
python3 "$chem/generate.py" --check

python3 - "$chem" "$output" "$(git -C "$repo" rev-parse HEAD)" <<'EOF'
import io, sys, tarfile, time
from pathlib import Path
chem, output, head = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
top = 'loom-chemistry-advance-spill-repro'
# Shared sources used by compile.sh --reimport, replay.cpp and the HIP controls.
shared = ['reference.cpp', 'reproducer.cpp', 'integrate.inc', 'support.h',
          'reference_kernels.inc', 'compare_rocm.cpp', 'generate.py',
          'reference-gfx942-device.hsaco', 'Dockerfile.rocm10']
repro = chem / 'advance-spill-repro'
files = [chem / f for f in shared]
files += sorted(p for p in repro.rglob('*') if p.is_file() and '__pycache__' not in p.parts
                and p.parent.name != 'out')
now = int(time.time())
with tarfile.open(output, 'w:gz') as tar:
    for path in files:
        info = tar.gettarinfo(str(path), f'{top}/loom-chemistry/{path.relative_to(chem)}')
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        with path.open('rb') as f:
            tar.addfile(info, f)
    note = (f'Packed from HRX {head} on {time.strftime("%Y-%m-%d", time.gmtime(now))}.\n'
            'Start with loom-chemistry/advance-spill-repro/README.md.\n').encode()
    info = tarfile.TarInfo(f'{top}/PROVENANCE.txt')
    info.size, info.mtime, info.mode = len(note), now, 0o644
    tar.addfile(info, io.BytesIO(note))
print(f'{output}: {len(files) + 1} files, {output.stat().st_size:,} bytes')
EOF
