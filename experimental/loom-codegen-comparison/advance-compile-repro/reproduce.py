#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check the expected allocation failure and two passing controls."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import time

from generate import generate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path, help="Path to loom-compile")
    parser.add_argument("--output-dir", type=Path,
                        help="Keep inputs, diagnostics, and successful HSACOs here")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        parser.error(f"compiler does not exist: {compiler}")
    temporary = None
    if args.output_dir is None:
        temporary = tempfile.TemporaryDirectory(prefix="loom-advance-repro-")
        output = Path(temporary.name)
    else:
        output = args.output_dir.resolve()
        output.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    fixture = (here / "repro.loom").read_text()
    if fixture != generate():
        raise RuntimeError("repro.loom differs from generate.py's default output")

    cases = [
        ("249-slots", fixture, True),
        ("248-slots", generate(248), False),
        ("249-interleaved", generate(249, True), False),
    ]
    for name, source, expect_failure in cases:
        input_path = output / f"{name}.loom"
        input_path.write_text(source)
        command = [str(compiler), str(input_path), "--pipeline=none",
                   "--format=amdgpu-hsaco", "--target=amdgpu:gfx90a",
                   f"--output={output / (name + '.hsaco')}"]
        started = time.monotonic()
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        elapsed = time.monotonic() - started
        (output / f"{name}.log").write_text(result.stdout + result.stderr)
        expected_diagnostic = re.search(
            r"error \[BACKEND/005\].*amdgpu\.vgpr.*budget 256.*"
            r"failure code 'spill-traffic-register-exhausted'", result.stderr)
        if expect_failure:
            passed = result.returncode != 0 and expected_diagnostic is not None
        else:
            artifact = output / f"{name}.hsaco"
            passed = result.returncode == 0 and artifact.is_file() and artifact.stat().st_size > 0
        print(f"{'PASS' if passed else 'FAIL'} {name}: "
              f"exit={result.returncode}, {elapsed:.3f}s", flush=True)
        if not passed:
            raise RuntimeError(f"unexpected result for {name}:\n{result.stdout}{result.stderr}")
    if temporary is not None:
        temporary.cleanup()


if __name__ == "__main__":
    main()
