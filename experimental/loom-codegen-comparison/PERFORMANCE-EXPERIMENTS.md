# Rules for Loom performance experiments

These rules come from the first advance-kernel campaign on the Hot Aisle MI300X
VM, run on 2026-10-10. It ran 15 of 20 experiments and accepted two, about
1.09x combined. A third, at a further 1.10x, was left unvalidated when the run
stopped. Most of the lost time came from harness failures, not compiler ideas.
Each full experiment costs about 7 minutes, and the 270 s advance-kernel
compile is the floor.

## Setup

1. **Bootstrap with one script.** Assume the VM can be reprovisioned at any
   time. `bootstrap_hotaisle.sh` installs the toolchain, syncs the source,
   configures with ccache, builds the tools and uploads the harness.
   `restore_and_selftest.sh` then restores the archive. Archive ccache, the
   board and the best run directories off the VM after each session, and
   again before any known shutdown.
2. **Never auto-accept a changed host key.** Report it and wait for the user.
3. **Pass the host self-test before any experiment.** After bootstrap, run
   `restore_and_selftest.sh`. It restores the archive and runs `selftest.sh`,
   which checks:
   - the GPU-to-NUMA and CPU-set mapping against `amd-smi` and `nproc`;
   - the board in a sandbox;
   - `iter.sh`'s source-hash and self-test gates;
   - one A/A timing pair per GPU, with non-empty telemetry for that GPU;
   - an LU screen.

   It writes `~/selftest.ok` with the host signature: hostname, GPU BDFs and
   core count. `iter.sh` refuses to run without a matching file, so a
   reprovisioned or reshaped host is blocked until it passes again.
4. **Discover host shape, never assume it.** GPU count, CPU sets, build jobs
   and telemetry targets all come from `lane_env.sh`. No script may hardcode
   GPU indices, core counts or host addresses; the host comes from
   `HOTAISLE_HOST`.

## Ownership and isolation

5. **One writer per checkout.** Each lane owns one VM checkout, one build tree,
   one local worktree and one GPU. Nothing else syncs into, builds or runs on
   that lane's resources. Helper agents and forks get read-only instructions.
6. **Build from a proven snapshot, never the live worktree.** `exp.sh` and
   `scr.sh` freeze the worktree with `snapshot.sh`, which writes a cumulative
   patch against HEAD and proves that applying it to a clean HEAD reproduces
   the snapshot's hash. The snapshot, not the worktree, is synced by content
   (`rsync -rlpD --checksum`, fresh mtimes), so edits made during a run
   cannot leak in. On the VM, `iter.sh` refuses to build if the tree's hash
   differs from the staged hash, or if compiler sources changed but the
   `loom-compile` md5 did not.
7. **The run directory is the only source of a patch.** Each run directory
   holds its `source.patch`, `source.hash`, base commit and own
   `loom-compile`. Its provenance records the patch checksum, source and
   compiler hashes, lane and host signature. The board accepts a version
   only from that run's own `source.patch`, after checking it against the
   provenance, and refuses any hand-made patch.

## GPU access

Both GPUs are whole physical MI300X devices, each in SPX/NPS1 mode with 304
CUs and 192 GB, on separate guest NUMA nodes. Measured noise is about 1.5% per
paired run, and timing on one GPU can slow Loom on the other by 2 to 3%. The
5% acceptance bar sits well above both, so the lanes time concurrently without
validation runs.

8. **Pin GPUs with `ROCR_VISIBLE_DEVICES` only.** Adding
   `HIP_VISIBLE_DEVICES` masks twice and hides GPU 1.
9. **Isolate the host side of each GPU.**
   - Run each lane's timing under `numactl --cpunodebind --membind`, on the
     NUMA node attached to that lane's GPU.
   - Confine builds and tests to a CPU set that excludes every lane's
     reserved timing cores.
   - If contention still shows in telemetry, pause the lane's own builds
     during its timing window.
10. **Bound every GPU run.**
   - Timed runs get a 600 s timeout, and ad hoc runs get 60 s.
   - Take the lane's GPU lock for one run at a time, and release it in
     between.
   - Run any change to control flow, uniformity or loops on a few cells with
     a 30 s timeout before the full grid.
11. **Record telemetry with every timed run.** Sample GPU clock, power,
    temperature and utilization with `amd-smi` every second for both GPUs.
    Flag a run whose mean busy GFX clock, over samples at 50% utilization or
    more, falls below 97% of the job's median, and repeat its pair instead of
    averaging it in.
12. **Time both GPUs concurrently.** Each lane holds only its own GPU's lock.
    No A/A runs, known-effect runs or sentinel re-times are required.

## Choosing and screening experiments

13. **Start from profile evidence.** After each accepted version, re-profile
    and map the hottest waits to their wait-plan reasons in the compile
    report. Each experiment names the hot instruction or wait it targets.
14. **Screen cheaply before the full compile.** Run `scr.sh` on the change and
    compare it with the current best's screen. It compiles the production LU
    routines in two roots, alone and under full VGPR pressure, in under a
    second after the tool build. A change that targets the LU loops must show
    up there and still compile before it gets the 270 s full compile. The
    screen sees wait and spill-traffic changes. It does not reproduce scalar
    (SGPR) spills, so changes aimed only at scalar spills go straight to the
    full compile.
15. **Skip dead families and time-box ideas.** Do not retry a family recorded
    as dead without a written reason this attempt differs. Allow each idea
    two attempts or 30 minutes, then record a verdict. Currently dead:
    - pre-allocation constant rematerialization or localization, which broke
      allocation 5 of 5 times;
    - spill-victim cost weighting, a no-op;
    - branch-island placement, with no timing effect;
    - optimistic loop-header uniformity, which hung the GPU.

## Running an experiment

16. **Overlap the work.** Run the test subset concurrently with the
    single-threaded kernel compile, and emit the compile report every time.
    Count only build failures under `loom/`.
17. **Skip timing for identical code.** If the advance code object matches
    the current best's checksum, record a no-op and move on.

## Acceptance

18. **Compare against the current best, interleaved on the same GPU.**
    Run the candidate and the current best alternately on the same GPU in one
    job, as A B B A A B, which is three pairs. Each pair yields a candidate-to-best speedup.
    Interference then hits both arms equally. Record the Loom/HIP ratio as a
    secondary reference only, because HIP does not respond to interference
    the way Loom does.
19. **Accept at 5% or better.** Accept when the median paired speedup over
    the current best exceeds 1.05, every clean pair agrees in direction, and
    tests and correctness pass. The timing summary reports this as
    `meets_bar=1`.
20. **Strengthen correctness for risky changes.** Changes to waits, fences,
    memory ordering or synchronization need a written hardware ordering
    argument and a five-step correctness run.
21. **Re-baseline tests narrowly.** A lit-test expectation may be updated
    only when its diff changes nothing but the intended kind of instruction.
    Record that kind in the log.
22. **Finish what wins.** A candidate that beats the bar on timing completes
    its tests, re-baselining and correctness checks before the campaign
    stops.

## Coordination

23. **Use the board for all shared state.** Before choosing work, check the
    board's inbox and status. Claim an idea before writing code, and report
    every verdict there. Duplicate claims and dead-family claims are refused.
24. **Rebase on every new version.** An accept on an outdated base must be
    re-run against the current version. When a version is accepted, the other
    lane rebases onto it before its next experiment. Later paired timing uses
    the new version as the best.
25. **Supervise on a schedule.** The orchestrator reads the board every 15
    minutes. It intervenes when a lane goes 30 minutes without a verdict,
    holds the timing lock over 5 minutes, or skips a baseline.

## Bookkeeping

26. **The log and board are the source of truth.** Never report status from
    memory. Each log row records the idea, the base version and GPU, the test
    result, correctness, the ratio, the code-object checksum and the verdict.
27. **Keep every run's patch and revert cleanly.** An experiment's patch is
    its run's `source.patch`; copy it to the notes as
    `<run tag>-<key>.patch`. To reject an experiment, restore `loom/` from
    the current best version's patch, leaving unrelated changes alone. An
    experiment counts toward the budget once, when its verdict is recorded.
28. **Use self-safe process control.** Kill recorded PIDs, or use patterns
    that can't match the issuing shell, such as `pgrep -f '[i]ter.sh'`.
29. **No inline orchestration.** Waits, cutoffs, retries and chained steps
    live in bash scripts under `harness/`, run with `bash`. Inline shell is
    for one-shot reads only. zsh word-splitting and `=word` expansion broke
    both an orchestrator chain and a lane's cutoff check.
30. **Deadlines live on the board.** Record the campaign end, including any
    known VM shutdown, with `board.py deadline set-at <ISO time>` or
    `set-in <minutes>`. `iter.sh` skips an experiment that cannot finish
    before it, estimating duration from this host's recent runs. Lanes
    read `board.py deadline show` instead of doing their own date
    arithmetic, so a late experiment is skipped, not interrupted.
