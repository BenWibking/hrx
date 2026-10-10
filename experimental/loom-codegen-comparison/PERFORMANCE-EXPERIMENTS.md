# Rules for Loom performance experiments

These rules come from the first advance-kernel autoresearch iterations on the
MI300X Hot Aisle VM. Each iteration costs 8 to 10 minutes, and the 270 s
advance-kernel compile is the floor. The rules aim to spend that time only on
experiments that can change the result, and to keep noise or hidden failures
from deciding acceptance.

## Before starting a series

1. **Record a clean baseline.** Run the full test subset and the benchmark on
   the unmodified tree. Save the test results, the code-object checksum, the
   compile report, and at least two timed runs.
2. **Measure run-to-run noise.** Time the same binary at least twice. Set the
   acceptance threshold well above the observed spread. The baseline varied
   about 1.7%, which nearly equals the 2% bar.
3. **Know the expected failures.** Write down the build and test failures that
   exist before any change. Treat any failure outside that list as new.

## Before paying for a full compile

4. **Prove the change is exercised.** Add a temporary counter, remark, or
   compile-report field that shows how often the new code path fires. Check it
   on a small input or a smaller kernel first. A change that never fires is a
   no-op, whatever the timing says.
5. **Check diagnostic output immediately.** After starting an IR dump or a
   report, confirm within seconds that it is writing data. A filter that
   doesn't match produces silent empty output.
6. **Draft each experiment in its own worktree.** Keep patches independent so
   rejecting one never disturbs another. Save every patch, accepted or not,
   next to the log.

## During each iteration

7. **Run tests and the kernel compile concurrently.** The VM has many cores
   and the compile is single-threaded, so serial chaining wastes time.
8. **Emit the compile report on every compile.** Spill counts, wait reasons,
   and instruction counts then come free with each run and explain the
   outcome.
9. **Skip timing when the code object is unchanged.** Compare the new hsaco
   checksum with the current best. If they match, log the experiment as a
   no-op and move on.
10. **Count only relevant build failures.** Filter build failures to `loom/`
    targets and require zero. Known runtime failures must not hide a new
    compiler break.

## Timing and acceptance

11. **Keep the machine quiet during GPU timing.** Run no diagnostic compiles,
    rebuilds, or other benchmarks while timed runs execute.
12. **Isolate binaries and outputs.** Give every job its own copy of
    `loom-compile` and its own output directory. Never copy a tool binary while
    a rebuild may be rewriting it.
13. **Judge against a same-run control.** Compare the Loom/HIP ratio from the
    same run, or re-time the current best alongside each candidate. An absolute
    median alone is too sensitive to drift.
14. **Accept only above the noise floor.** Require the improvement threshold
    and consistency across repeated runs. A single lucky median must not pass.

## Correctness

15. **Strengthen checks for ordering and synchronization changes.** Changes to
    waits, fences, barriers, or memory ordering can introduce races that pass a
    single gridwide step. Write down the hardware ordering argument, add a
    targeted lit test, and run several steps or repeated trials.
16. **Give new cached fields an explicit unset value.** Some tests pre-seed
    internal caches and bypass new fields. Fall back to the old behavior when a
    field was never computed.

## Bookkeeping

17. **Treat the log as the source of truth.** Read status from the log and
    task output, never from memory. Record each experiment's idea, tests,
    correctness, timing, code-object checksum, and verdict.
18. **Revert only compiler paths.** Restore `loom/` from the last accepted
    patch. Leave unrelated working-tree changes alone.
