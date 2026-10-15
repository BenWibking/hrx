#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Generate the scratch-store/read pattern isolated from advance compilation."""
import argparse
from pathlib import Path


def generate(slots=249, interleave=False):
    lines = [
        "// Compile-only reproducer; no GPU execution is required.",
        "amdgpu.target<gfx90a> @target",
        "low.kernel.def target<amdgpu.cdna2.core>(@target) "
        "workgroup_size(128, 1, 1) @repro() asm {",
        "  %kernarg = live_in<amdgpu.kernarg_segment_ptr> : reg<amdgpu.sgpr x2>",
        "  %base = s_load_dwordx2_offset_only %kernarg",
        "  %zero = v_mov_b32 0",
    ]
    for i in range(slots):
        lines.append(f"  %s{i} = storage {{byte_alignment = 8, byte_length = 8}} : low.storage<scratch>")

    def read(i):
        return [
            f"  %r{i} = low.op<amdgpu.scratch_load_b64_vaddr>(%a{i}) "
            "{offset = 0} : (reg<amdgpu.vgpr>) -> reg<amdgpu.vgpr x2>",
            f"  global_store_dwordx2_saddr %zero, %r{i}, %base",
        ]

    for i in range(slots):
        lines.extend([
            f"  %a{i} = low.storage.address %s{i} : low.storage<scratch> -> reg<amdgpu.vgpr>",
            f"  %v{i} = v_mov_b32 {i + 1}",
            f"  %w{i} = concat(%v{i}, %v{i}) : "
            "(reg<amdgpu.vgpr>, reg<amdgpu.vgpr>) -> reg<amdgpu.vgpr x2>",
            f"  low.op<amdgpu.scratch_store_b64_vaddr>(%a{i}, %w{i}) "
            "{offset = 0} : (reg<amdgpu.vgpr>, reg<amdgpu.vgpr x2>)",
        ])
        if interleave:
            lines.extend(read(i))
    if not interleave:
        for i in range(slots):
            lines.extend(read(i))
    return "\n".join(lines + ["  return", "}", ""])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--slots", type=int, default=249)
    parser.add_argument("--interleave", action="store_true")
    args = parser.parse_args()
    if args.slots < 1:
        parser.error("--slots must be positive")
    args.output.write_text(generate(args.slots, args.interleave))
