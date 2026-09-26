# Loom chemistry reassessment, 2026-09-20

Historical baseline before the subsequent C++ atomic-binding change. The current
README describes the now-implemented bindings; the f64 limitations below remain.

Compared HRX `628ec2d51` (the original assessment) with
`4599d79d8ab095263af01dbf6a1822271d11ccad` (249 intervening commits).
The compiler and importer were freshly built from the latter checkout.

**The complete double-precision chemistry application still cannot compile
through Loom to AMDGPU.** The current rewrite still imports successfully, and
new frontend/test infrastructure makes incremental compiler qualification
easier. The remaining work includes basic f64 lowering, transcendental math,
C++ atomic bindings, and eventual runtime integration/validation.

## Verified results

| Check | Result |
| --- | --- |
| Generated-source consistency | Pass |
| ASan/UBSan native differential, default run | 18,280 exact field comparisons |
| ASan/UBSan native differential, four cells / 1,000 steps | 235,705 exact field comparisons |
| Both kernel roots, structure-only import | Pass; f64 exp/log/sqrt/cbrt retained |
| Default import with real atomic interfaces | Still rejected at unresolved `atomic_cas` |
| Structure-only module compiled for `amdgpu:gfx942` | Rejected at `scalar.expf : f64`, `math.element.f32` |
| Existing AMDGPU global-atomic lowering fixture | All 13 cases pass, including i32 add and compare-exchange |
| Existing C++ control-rejection fixture | All 13 cases pass; `break` and loop-contained returns remain unsupported |

The structure-only module was used solely to inspect compiler diagnostics.
It was never executed. No executable chemistry artifact or GPU result was
produced. Native differential validation compares the source transformation
using host math; it does not qualify Loom code generation or device math.

Independent scalar probes, with no approximate-math permissions:

| f64 operation | AMDGPU gfx942 source-to-Low | VM core source-to-Low |
| --- | --- | --- |
| Add, multiply, divide | Rejected | Pass |
| Comparison/select, absolute value, u64 conversion | Rejected | Pass |
| Square root | Rejected | Rejected |
| Exponential, logarithm | Rejected by math policy | Rejected by math policy |
| Cube root | No target-Low contract | No target-Low contract |

A separate f32 addition control passed on both targets. These are lowering
checks, not numerical execution tests. A `gfx90a` probe was rejected at target
selection (`AMDGPU target 'gfx90a' is not supported`) in this build, before any
arithmetic was examined.

## Changes that help this reproducer

- Structured `continue` now imports (`b88b0e0031`). Some generated control flow
  could become simpler, but the solver still needs its explicit exit state for
  `break` and returns inside loops. There is no need to disturb the validated
  rewrite to obtain frontend acceptance.
- C++ configuration declarations and compound layout queries were added
  (`fa67b3e5ff`). Future launch/tuning choices can use named specialization
  values. `sizeof`, alignment, and member-offset support do not provide general
  aggregate state, references, or automatic solver arrays. Local arrays still
  require `__shared__`; that storage is not a substitute for per-cell scratch.
- Public callable symbols are stable (`b1c03903c7`). The current imported entries
  are `@chemistry.prepare_grid_timestep_kernel` and
  `@chemistry.advance_collapse_gridwide_kernel`; launch config keys use those
  dotted prefixes. C++ importer root options still use `chemistry::...`.
- C++ source-authored checks, numerical oracles, and source-to-Low tests are now
  integrated (`3cd8680e66`). They provide a practical home for small f64 and
  atomic regression cases as support is added.
- View-dependent helper inlining, argument-storage fixes, and AMDGPU CFG/index
  fixes improve relevant infrastructure. They do not remove the observed
  arithmetic failures or establish correctness of the full solver.

## What is still missing

The AMDGPU math policy's f32 restriction is unchanged. Basic f64 arithmetic,
comparison, conversion, absolute value, and square-root lowering also fail
independently of that policy. Adding exp/log alone cannot unblock chemistry.
VM lowering is useful for subsets but is not yet a complete f64 math fallback.

The HIP facade and C++ intrinsic bindings still expose no corresponding atomic
interface. Loom already has `view.atomic.rmw<addi>` and `view.atomic.cmpxchg`
with AMDGPU lowering; this was also present at the earlier revision. The next
atomic step is a typed frontend binding preserving pointer origin, returned old
value, device scope, and the original HIP ordering contract. A non-atomic
stand-in remains unsuitable for execution. Stable public names do not permit
undefined ordinary C++ helpers to be linked later: import still requires their
definitions.

The useful next milestone is to qualify f64 primitives and atomic bindings in
small source-authored tests, then compile the actual helper/kernel closure.
After that, connect the buffer ABI and host timestep sequence and compare
solver states, counters, retries, and failure publication on AMD hardware.
Retain double precision and existing evaluation semantics throughout.

## Reproduction and local evidence

Fresh tools:
`/private/tmp/loom-chemistry-recheck-20260920/loom/src/loom/tools/`.
Probe sources, IR, and diagnostic logs:
`/private/tmp/loom-chemistry-probes-20260920/`.
Full native log: `/private/tmp/loom-chemistry-native-full-20260920.log`.
These temporary paths are session artifacts, not maintained dependencies.

Run the maintained frontend check with the freshly built importer:

```sh
python3 experimental/loom-chemistry/check_import.py /path/to/loom-import-cxx
```

A minimal isolated target probe (`add.cxx-test`):

```cpp
// RUN: emit source-low @entry target=amdgpu:gfx942 output=low
double entry(double x, double y) { return x + y; }
```

`loom-check add.cxx-test` reports the missing f64 arithmetic contract. A
diagnostic may name the rejected f16/f32 candidate rule; the source operands
remain f64. Successful VM probe goldens were generated with `--update` and
then rerun normally.

Relevant source: `loom/src/loom/target/arch/amdgpu/math_policy.c`,
`loom/py/loom/target/arch/amdgpu/contracts/{arithmetic,compare}.py`,
`loom/src/loom/import/cxx/README.md`, and the importer
`test/control_invalid.cxx-test` and `value/types.cc`.
