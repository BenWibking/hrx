# Advance-kernel autoresearch on moth

This is an autoresearch-style loop: an agent edits only the Loom kernel
source, `ar eval` measures each commit on moth's MI210 (gfx90a), and the
harness keeps or discards it. The Loom compiler and HRX are built once per
campaign from a fixed commit and never change. `program.md` holds the
agent's instructions.

| File | Runs on | Purpose |
| --- | --- | --- |
| `ar` | Mac | Campaign CLI: init, setup, eval, resume, screen, status, import, deadline, target |
| `remote.sh` | moth | Frozen tool build, interface checks, compile, correctness, paired timing |
| `analyze.py` | moth | Paired-speedup verdict with clock and contention flags |
| `layout.cpp` | moth | `CellRecord` layout probe for the interface check |
| `program.md` | agent | Rules, loop, evidence and known levers |

## Start a campaign

From the main checkout:

```sh
A=$PWD/experimental/loom-codegen-comparison/autoresearch/ar
$A init adv1                     # worktree ../hrx-system-ar-adv1, branch autoresearch/adv1
cd ../hrx-system-ar-adv1
$A setup                         # ~3 min: frozen loom-import-cxx/loom-compile + compare_rocm on moth
$A deadline set-in 480           # optional
$A eval "baseline"               # seed vs itself: noise floor and acceptance bar
```

To check a candidate's static stats without timing it, run `ar screen`.
It compiles `kernel/` as it stands (or `--dir DIR`) on moth in about 2
minutes, without using the GPU. It logs the result to `screens.tsv` and
prints the change against the best run.

Then start an agent in the worktree:

```sh
claude "Read $A/../program.md and run the campaign."
```

Use `ar status` to check on it. State lives in `.notes/autoresearch/<TAG>/`
in the main checkout:
- `results.tsv`, `notes.md`, `campaign.env`, `best.env`;
- `runs/<RUN>/` for each eval;
- `screens.tsv` and `screens/<ID>/` for each `ar screen`.

Discarded commits stay reachable as tags `ar/<TAG>/<RUN>-<status>`.

On moth everything is under `/mnt/data/bwibking/ar/<TAG>/`. The GPU lock
`/mnt/data/bwibking/ar/gpu.lock` serializes timing across campaigns.

## Loom runs only on moth

Every Loom compiler invocation (`loom-import-cxx`, `loom-compile`) happens on
moth, inside `ar setup`, `ar import`, `ar screen` and `ar eval`. Agents must never run Loom
tools on the local machine (see rule 8 in `program.md`): parallel local
compiles of this kernel once exhausted the Mac's memory and caused a kernel
panic.

## What makes a result trustworthy

- **Fixed compiler.** The tools are built once from `BASE`, and every eval
  checks their md5.
- **Fixed host side.** `compare_rocm` is built once from the seed kernel.
  Candidates reach it only as code objects. The kernel headers in the IR
  and the `CellRecord` layout must match the seed.
- **Exact source.** Each eval builds `git archive` of the committed
  `kernel/`. Commits that touch anything else are refused.
- **Correctness.** Every eval runs 5 gridwide steps against the original HIP
  kernels, and every timing run checks correctness again.
- **Timing.** Pairs are interleaved against the current best on the same
  GPU, with the host side pinned to cores near the GPU (NUMA node 1,
  CPUs 46–47). Builds run on node 0.
- **Noise rules.**
  - A pair whose busy GFX clock falls below 97% of the median is re-run.
  - The default bar is max(1.02, 1 + 2 × the baseline's median A/A deviation).
  - Every clean pair must agree in direction, except that one of four or
    more may disagree. Loom's advance time is bimodal on the MI210 (about
    443 or 471 ms for the seed, even on an idle GPU).

## Sharing the GPU

moth's MI210 is shared. CI jobs from other users can hold it for long
stretches. Before timing, an eval waits up to `--gpu-wait` seconds (default
1800) for other users' GPU processes to finish. If they don't, it returns
`gpu-busy` and changes nothing.

`--contended` instead times under contention and keeps the contended pairs.
Interleaving cancels steady contention, but not contention that changes
mid-run. Never use it for the baseline.

## Requirements on moth

These are already in place:
- ROCm 10.1 at `/opt/rocm/core-10.1`;
- the `venv-py312` tools (cmake, ninja, Python 3.12);
- conda-forge GCC 14 at `/mnt/data/bwibking/gcc14`. Its libstdc++ provides
  the C++23 support that `loom-import-cxx` needs and moth's GCC 11 lacks.
