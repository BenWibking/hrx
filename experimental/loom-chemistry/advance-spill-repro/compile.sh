#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Compile both chemistry kernels from chemistry.loom for gfx942 and report
# static spill, branch and resource metrics beside the pinned HIP code object.
# Needs no GPU. Usage, from anywhere:
#
#   compile.sh [--reimport] [--replicas N] [--out DIR]   (or CHEM_WORK_DIR=DIR)
#
# --reimport regenerates chemistry.loom from ../reproducer.cpp first (in DIR,
# leaving the committed file untouched). --replicas sets workgroup_count.x for
# replay runs with N copies of the 128-cell snapshot.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
chem=$(cd -- "$here/.." && pwd)
# Inside the HRX checkout, default to its tools and build/ directory. In an
# extracted tarball, set LOOM_COMPILE (and LOOM_IMPORT_CXX for --reimport).
if repo=$(git -C "$chem" rev-parse --show-toplevel 2>/dev/null); then
  default_out=${CHEM_WORK_DIR:-$default_out}
else
  repo=$here default_out=$here/out
fi
tools=${LOOM_BUILD_DIR:-$repo/build/cmake}/loom/src/loom/tools
importer=${LOOM_IMPORT_CXX:-$tools/loom-import-cxx/loom-import-cxx}
compiler=${LOOM_COMPILE:-$tools/loom-compile/loom-compile}
out=${CHEM_WORK_DIR:-$default_out}
replicas=1
reimport=0
while (($#)); do
  case $1 in
    --reimport) reimport=1; shift ;;
    --replicas) replicas=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ $replicas =~ ^[1-9][0-9]*$ ]] || { echo "--replicas must be a positive integer" >&2; exit 2; }
[[ -x $compiler ]] || { echo "loom-compile missing: $compiler (set LOOM_BUILD_DIR or LOOM_COMPILE)" >&2; exit 2; }
mkdir -p -- "$out"

module=$here/chemistry.loom
if ((reimport)); then
  [[ -x $importer ]] || { echo "loom-import-cxx missing: $importer" >&2; exit 2; }
  python3 "$chem/generate.py" --check
  module=$out/chemistry.loom
  "$importer" --data-model=lp64 --approximate-functions=false \
    --root=chemistry::prepare_grid_timestep_kernel \
    --root=chemistry::advance_collapse_gridwide_kernel \
    --output="$module" "$chem/reproducer.cpp"
fi

compile() {  # role root
  local role=$1 root=chemistry.$2 start end status=0
  echo "Compiling $role for gfx942 (workgroup_count.x=$replicas)..."
  start=$(date +%s)
  "$compiler" "$module" --root="$root" --format=amdgpu-hsaco --target=amdgpu:gfx942 \
    --config="$root.workgroup_count.x=$replicas" \
    --config="$root.workgroup_count.y=1" \
    --config="$root.workgroup_count.z=1" \
    --output="$out/chemistry-$role.hsaco" \
    --compile-report=summary --compile-report-output="$out/$role-report.json" \
    2>"$out/$role-compile.log" || status=$?
  end=$(date +%s)
  echo "$role $status $((end - start)) $(grep -c 'BACKEND/009' "$out/$role-compile.log" || true)" >>"$out/timings.txt"
  if ((status)); then tail -n 20 "$out/$role-compile.log" >&2; exit "$status"; fi
}
: >"$out/timings.txt"
compile prepare prepare_grid_timestep_kernel
compile advance advance_collapse_gridwide_kernel

python3 "$here/static_counts.py" --json "$out/static-counts.json" \
  --log "Loom:advance=$out/advance-compile.log" \
  "Loom=$out/chemistry-advance.hsaco" "Loom=$out/chemistry-prepare.hsaco" \
  "HIP=$chem/reference-gfx942-device.hsaco" | tee "$out/static-counts.md"

# Provenance: inputs, tools and outputs with hashes.
python3 - "$out" "$module" "$compiler" "$repo" "$replicas" <<'EOF'
import hashlib, json, subprocess, sys
from pathlib import Path
out, module, compiler, repo, replicas = sys.argv[1:]
def digest(p):
    return {'path': str(p), 'bytes': Path(p).stat().st_size,
            'sha256': hashlib.sha256(Path(p).read_bytes()).hexdigest()}
def git(*a):
    return subprocess.run(['git', '-C', repo, *a], capture_output=True, text=True).stdout.strip()
stages = {}
for line in Path(out, 'timings.txt').read_text().splitlines():
    role, status, seconds, spills = line.split()
    stages[role] = {'exit_code': int(status), 'wall_seconds': int(seconds), 'spill_warnings': int(spills)}
manifest = {
    'head': git('rev-parse', 'HEAD'), 'dirty': bool(git('status', '--porcelain', '--untracked-files=no')),
    'target': 'amdgpu:gfx942', 'workgroup_count': [int(replicas), 1, 1],
    'module': digest(module), 'loom-compile': digest(compiler), 'stages': stages,
    'artifacts': {r: digest(Path(out, f'chemistry-{r}.hsaco')) for r in ('prepare', 'advance')},
}
Path(out, 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
EOF
echo "Outputs in $out"
