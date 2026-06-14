#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../.." && pwd)"
SOURCE="${ROOT_DIR}/loom/src/loom/test/corpus/authoring/robertson_alpha_qss.loom"
OUT_DIR="${1:-/tmp/loom-robertson-alpha-qss-gfx90a}"
PYTHON_BIN="${PYTHON:-python3}"

mkdir -p "${OUT_DIR}"

"${PYTHON_BIN}" "${ROOT_DIR}/dev.py" bazel run \
  //loom/src/loom/tools/iree-benchmark-loom:iree-benchmark-loom -- \
  "${SOURCE}" \
  --dry-run \
  --output="${OUT_DIR}/plan.json"

"${PYTHON_BIN}" "${ROOT_DIR}/dev.py" bazel run \
  //loom/src/loom/tools/loom-compile:loom-compile -- \
  "${SOURCE}" \
  --backend=amdgpu-hal \
  --target=gfx90a \
  --output="${OUT_DIR}/robertson_alpha_qss.vmfb" \
  --emit-target-artifact="${OUT_DIR}/robertson_alpha_qss.hsaco" \
  --artifact-manifest=summary \
  --emit-artifact-manifest="${OUT_DIR}/artifact.manifest.json" \
  --compile-report=summary \
  --compile-report-output="${OUT_DIR}/compile-report.json"

"${PYTHON_BIN}" "${ROOT_DIR}/dev.py" bazel run \
  --//runtime/config/hal:drivers=amdgpu,local-sync,local-task,null \
  //loom/src/loom/tools/iree-benchmark-loom:iree-benchmark-loom -- \
  "${SOURCE}" \
  --device=amdgpu \
  --measure=dispatch_complete \
  --sample-compilation=per_sample \
  --iterations=1 \
  --warmup-iterations=0 \
  --batch-size=1 \
  --min-time-ms=0 \
  --max-batches=1 \
  --input-ring-count=1 \
  --artifact-bundle-dir="${OUT_DIR}/run" \
  --artifact-bundle-policy=debug \
  --artifact-manifest=summary \
  --output-format=jsonl

printf 'Robertson alpha-QSS gfx90a outputs written to %s\n' "${OUT_DIR}"
