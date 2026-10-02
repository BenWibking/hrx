# Suggested improvements to Loom-generated AMDGPU code

These recommendations follow the MI300X investigation recorded in
[PERFORMANCE-RESULTS.txt](PERFORMANCE-RESULTS.txt). They are proposed compiler
experiments, not implemented or validated optimizations.

## Evidence and priorities

Identical-state chemistry replays show a 6.1–6.7× Loom slowdown with matching
solver work. At step 500, native HIP takes 4.68 ms and Loom takes 30.34 ms.
Compiling the rewritten chemistry source with HIP takes 3.19 ms in a separate
paired experiment, so the rewrite does not require this code expansion.

For the matched step-500 replay, Loom executes 3.72× as many vector-memory
instructions, 4.77× as many vector-ALU instructions, 4.98× as many scalar-ALU
instructions, and 22.97× as many branch instructions. Its private segment is
51,968 bytes/thread versus HIP's 7,504 bytes/thread. These measurements motivate
the priorities below; they do not establish each optimization's potential speedup.

## 1. Rematerialize cheap constants and reuse reloads

Start with the concrete redundant traffic visible in the current advance
kernel. A value stored to scratch offset `0x4d70` at PC `0x1bfc` is reloaded at
`0x1c1c`, `0x1c38`, and `0x1c7c` within one division sequence. Nearby constant
halves are also stored and reloaded. These addresses identify the measured
artifact and will change after recompilation.

- Investigate why cheap constants acquire spill slots instead of being
  rematerialized at their uses.
- Reuse an available reload within a block when intervening operations do not
  invalidate the value or its storage.
- Inspect spill fragmentation and lifetime splitting: avoid backing transfers
  when a value is already available in a suitable register.
- Use a cost model that accounts for register pressure. Keeping every reload
  live longer can introduce other spills and erase the benefit.

Measure scratch loads/stores, address-materialization instructions, register
pressure, dynamic VMEM counts, and replay time. Start with a small regression
derived from the repeated-load sequence, then test the full kernel.

## 2. Reduce spill-transfer setup and synchronization overhead

Loom repeatedly saves EXEC, sets full EXEC, materializes a scratch address,
performs a transfer, and restores EXEC. Scalar spills also require vector
transfers and `v_readfirstlane` operations.

- Coalesce adjacent compatible full-EXEC spill/reload regions when no intervening
  operation requires the original mask or observes the temporary EXEC state.
- Reuse scratch address calculations where lifetimes and register pressure
  permit; use encodable immediate offsets when legal.
- Inspect redundant waits and opportunities to overlap independent reloads
  with useful work using the actual dependency and completion model.

Preserve the whole-value semantics of `low.spill` and `low.reload`. Do not
remove waits without dependency proof or narrow EXEC merely to reduce traffic.
The explicit SSA loop-preservation fix and retired-lane behavior must remain
correct. Relevant implementation:
[`spill_lowering.c`](../../../loom/src/loom/target/emit/native/amdgpu/spill_lowering.c).

## 3. Improve branch and merge-block placement

The measured advance binary contains 40,987 unconditional branches versus
HIP's 19. Of Loom's unconditional branches, 10,694 immediately target another
unconditional branch. This is static evidence of branch chains, not a count of
their dynamic executions or proof that one pass created every chain.

- Place related branch arms, merge blocks, and loop continuations near each
  other before branch relaxation.
- Prefer fall-through edges where legal and simplify redundant branch chains.
- Inspect how function expansion and distant merge blocks increase the need
  for branch islands; measure the number of islands before and after changes.

Track code size, branch counts, dynamic branch instructions, register pressure,
and spills together. Better layout must preserve EXEC restoration and edge
argument semantics. Relevant implementation:
[`branch_layout.c`](../../../loom/src/loom/target/emit/native/amdgpu/branch_layout.c).

## 4. If-convert small, safe expressions

The prepare kernel has 337 unconditional branches versus HIP's one, despite
only ten static scratch instructions. Spill optimization alone therefore does
not address all of the observed inefficiency.

- Identify small conditional expressions that can legally lower to selects
  instead of full control-flow diamonds.
- Require proof that both arms are safe to evaluate before speculating them.
  Preserve lazy evaluation for unsafe loads, side effects, and operations whose
  semantics depend on conditional execution.
- Measure whether fewer branches and EXEC transitions outweigh any added
  arithmetic and longer live ranges.

Relevant implementation:
[`control.c`](../../../loom/src/loom/target/arch/amdgpu/lower/control.c).

## Validation workflow

Change one compiler mechanism at a time and retain the baseline artifacts.
Use the same imported program, target, numerical semantics, and launch geometry
for each comparison.

1. Add a focused regression at the affected lowering or allocation boundary.
   Run the spill-clobbering and divergent-loop preservation tests, including
   native predicates and masked entry.
2. Replay saved states for steps 0, 100, 500, 920, and 999. Reset state before
   every sample, discard warmups, alternate baseline/candidate order, and time
   only the kernels. Check numerical outputs and solver-work counters.
3. Repeat the 64-block replicated advance control. Its baseline still shows a
   6.50× gap; one-block underutilization is not the sole explanation.
4. Collect dynamic counters separately from unprofiled timings. Keep static
   instruction counts distinct from executed instructions, transferred bytes,
   and cache misses. `SQ_WAIT_ANY` does not isolate memory stalls.
5. For a promising candidate, rerun the 1,000-step collapse and compare with
   the accepted CPU/HIP reference criteria. Record deuterium differences
   separately, as in the existing validation; do not silently change tolerances.

The replay harness, measurements, hashes, and commands are documented in
[PERFORMANCE-RESULTS.txt](PERFORMANCE-RESULTS.txt). The accepted full-collapse
validation is recorded in
[SSA-PRESERVATION-RESULTS.txt](SSA-PRESERVATION-RESULTS.txt).

PC sampling did not yield usable target-kernel attribution on this droplet.
There is no measured breakdown assigning a fraction of the slowdown to each
mechanism, nor a controlled ablation establishing the cost of the recent loop
SSA preservation change itself. Treat these recommendations as experiments
to establish that attribution while improving performance.
