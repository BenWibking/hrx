High-level IR reproducer (automatic spilling)
============================================

The new high-pressure.loom is a standalone source-level IR reproducer.
It contains no low.* operations, native AMDGPU instructions, explicit
spill/reload operations, fixed register assignments, or allocator overrides.
The ordinary compiler chooses and materializes all spills.

Run on the MI300X:

  python3 experimental/loom-chemistry/repro-spill-clobber/run_high_pressure.py

The wrapper exits 0 only if the divergent case fails at runtime with the
known bad value AND the uniform-trip-count control passes. To run it as a
normal regression, invoke the runner directly (current baseline exits
nonzero because the divergent expectation fails):

  build/cmake/loom/src/loom/tools/iree-test-loom/iree-test-loom \
    --device=amdgpu experimental/loom-chemistry/repro-spill-clobber/high-pressure.loom

The 170 independent state values are spelled out to create overlapping
SSA live ranges. Regenerate with generate_high_pressure.py; --width changes
pressure and --output selects the output file. Default output is the
checked-in high-pressure.loom. This is a small algorithm with mechanically
expanded IR, not a claim of globally minimal instruction count.

Algorithm (all state inputs are 1):

  lane = workitem_id.x                  // four lanes
  trips = 4 - lane * mode               // mode 1 fails, mode 0 is control
  state[0..169] = input[lane][0..169]
  repeat trips times:
    for each j, simultaneously:
      if lane < 2:
        next[j] = state[j] + state[(j+1) % 170]
      else:
        next[j] = 2 * (state[j] + state[(j+1) % 170])
    state = next

The simultaneous assignments are SSA loop-carried values, not a private
memory array. The nested scf.if creates overlapping branch-result ranges
and the allocator spills them under the ordinary 256-VGPR budget.

For mode=1, every element in each lane must finish as [16,8,16,4].
For mode=0, expected values are [16,16,256,256]. The test stores differences
from that exact integer oracle and compares every element with zero. The
same @pressure kernel is used for both tests; only a runtime mode buffer
changes. There is no floating-point arithmetic or numerical tolerance.

Observed on the MI300X VF, baseline compiler with existing sqrt changes:
- Three native executions of the final fixture: divergent case fails,
  uniform-trip case passes.
- First failure: output element 256 = lane 1, state 86; residual -5 means
  actual state 3 instead of expected 8.
- Standalone compiler reports BACKEND/009: y86 and y87 are automatically
  spilled VGPR block arguments, pressure_spill_storage_91 and _92, each
  with two stores and two reloads. Complete compile log is archived under
  build/benchmarks/mi300x-af901efb55/counter-diagnosis/.
- A generated width-64 control passes both cases. Width-169 with runtime
  mode still fails, so 170 is a convenient stable reproduction width,
  not a proven minimal threshold for this final parameterized fixture.

high-pressure-results.log saves an actual native report. The earlier
192-value prototype also failed. Masking all its vector spill stores was
not a valid fix, consistent with the original chemistry diagnosis. This
fixture is suitable for testing the compiler correction end-to-end; the
explicit-low diagnostic below intentionally remains separate.

No production compiler changes were made to obtain this failure.


Reduced AMDGPU spill-edge clobber reproducer
==========================================

Run on an MI300X/gfx942 machine with the checkout-local native test runner:

  python3 experimental/loom-chemistry/repro-spill-clobber/run.py

Override its path with --runner /absolute/path/to/iree-test-loom. Use --log
/path/to/results.json to save the raw output. The wrapper exits 0 only when
all four clobbers reproduce AND all four masked-store controls pass.

To see ordinary failing-test status, run the fixture directly:

  build/cmake/loom/src/loom/tools/iree-test-loom/iree-test-loom \
    --device=amdgpu experimental/loom-chemistry/repro-spill-clobber/spill-edge.loom

Expected baseline result: eight cases, four failures, four passes. Do not
force --target=amdgpu:gfx942 on the runtime runner: let it select the device's
feature-qualified target. The fixture requires the CDNA3 descriptor set.

What is reduced
---------------

The chemistry loop has already produced final counter increments in lanes
that exited. A subsequent backedge has only lanes 0 and 1 active (EXEC=3).
The allocator's incoming payload is valid for those lanes; the retained
spill slot must preserve lanes 2 and 3.

The failing helper is about 20 lines: initialize one 4-byte lane-private
slot, form an incoming payload, narrow EXEC, execute the allocator-style
low.spill edge update, restore EXEC, and reload. Four workitems are enough.
There is no chemistry, sqrt, HIP reference, large array, or pressure search.
A loop is unnecessary to reproduce its final edge update.

The same helper is parameterized for all four counters:

Counter           Saved increment   Stale incoming   Original failing store
Jacobian calls    5                 43100            0x1dde90 (slot 0x1450)
Internal steps    8                 0                0x1ddf08 (slot 0x1570)
Decompositions    8                 0                0x1dded0 (slot 0x1510)
Linear solves     24                5                0x1ddeec (slot 0x1568)

Those are per-advance increments, not cumulative statistics. Adding the
original cell-3 checkpoint counts produces the observed 43202, 147, 147,
and 446 respectively. The bad Jacobian value is supplied deterministically
instead of depending on an unrelated allocation to leave those bits behind.
Active lanes carry old+1; retired lanes carry the stale value. After the
edge update, the wrapper subtracts 1 only from active lanes. Every output
should therefore equal the original saved value. The failing tests first
report lane 2; lane 3 is retired too.

The control helper differs at the edge update: native scratch_store under
current EXEC replaces low.spill. It retains whole-register initialization
and reload, and all four controls pass. The helper schedules are locked so
this is an explicit sequence rather than a scheduling experiment.

Scope of this reproducer
------------------------

This is a reduced LOWERED-IR reproducer of an invalid allocator
transformation. low.spill itself is documented by its AMDGPU lowering as a
whole-register snapshot. The test intentionally recreates its incorrect
use as a masked loop-edge assignment; it does not assert that all low.spill
operations should become masked stores. A correct compiler fix may instead
change allocation materialization, leaving this explicit low.spill fixture
with exactly its current behavior.

Consequently this is a diagnostic fixture, not a passing regression in the
main test suite and not yet a tiny source-level test that forces automatic
spilling. The full chemistry checkpoint remains the end-to-end regression.
No production compiler changes accompany this reproducer.

Validated on the authorized MI300X VF droplet with the baseline compiler
plus the existing strict-f64-sqrt changes: all four expected failures and
all four controls reproduced in three native runs. See validated-results.json
for the runner's actual failure values and source locations.
