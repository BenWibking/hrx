Diagnostic experiment: retired-lane preservation by masked register copies
=======================================================================

Compiler: bfff52cfd59049518a32576c8bd5dcef98b651be, clean Release build.
Hardware: MI300X, gfx942. Tested 2026-10-01.

This is a causal experiment, NOT an unmodified-compiler miscompilation.
The original high-IR program passes. patch.py deliberately changes its binary.
No production compiler source is changed.

The kernel rotates three i32 values per lane, initially [1,2,3].
Mode 0 runs four iterations in every lane; mode 1 runs 4-(lane&3).
The kernel outputs residuals against an independently generated exact oracle.
The original backedge rotates v6,v7,v2 through temporary v8 at 0x1084.
All four moves obey EXEC; there is no scratch traffic.

patch.py redirects these copies to a trampoline in existing ELF padding.
It preserves the original allocation, counters, loop condition, and metadata.
All variants save EXEC in s[8:9] (unused during this loop), execute the same
four copies, restore EXEC, and branch to the original loop condition.
Variants differ only in the instruction before the copies:

masked: s_nop 0
all:    s_mov_b64 exec, -1
lane1:  s_or_b64 exec, exec, 2

Results (three repeats of all eight cases; 24 launches total):

variant       uniform mismatches   divergent mismatches
original      0                    0
masked        0                    0
all           0                    96 (32 lanes, 3 values each)
lane1         0                    3 (only lane 1)

All exact residuals matched the predictions in run.py, not merely the counts.
For 'all', lane classes 1 and 2 have residuals [1,1,-2] and [-1,2,-1].
Class 3 is updated incorrectly too, but three extra rotations return it to
the expected state; it is deliberately not counted as a visible mismatch.
For 'lane1', only physical lane 1 has residuals [1,1,-2].
All four disassembled binaries contain no scratch instructions.

Conclusion: this particular spill-free lowering uses masked physical copies
to preserve retired-lane state. Widening those copies destroys that state in
the exact lanes predicted. This supports the explanation of the known spill
failure, but does not establish that all spill-free Loom lowerings are correct.
In particular, correctness also requires proper liveness across reconvergence
and that no other operation writes the retained components under a wider mask.

Run on the existing remote evidence tree:
  cd /root/loom-main-bfff52cfd5/evidence/masked-copy-probe
  python3 patch.py ../spill-free/rotate-32-3.hsaco variants
  /opt/rocm/bin/hipcc driver.cpp -o driver
  python3 run.py

Rebuild the original from rotate-32-3.loom using the pinned compiler:
  loom-compile rotate-32-3.loom --format=amdgpu-hsaco \
    --target=amdgpu:gfx942 --output=../spill-free/rotate-32-3.hsaco
The binary patcher checks the exact expected original instruction words.

Raw results and disassemblies are retained under:
  build/benchmarks/mi300x-main-bfff52cfd5/masked-copy-probe/
