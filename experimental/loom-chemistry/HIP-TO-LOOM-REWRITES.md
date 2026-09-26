# HIP C++ to Loom C++ rewrites

This document compares the generated device program in [reproducer.cpp](reproducer.cpp)
with the **original HIP C++** in [reference.cpp](reference.cpp). It does not
compare against earlier Loom versions. [generate.py](generate.py) extracts the
HIP bodies under a pinned SHA256 and applies the transformations; the original
file is unchanged. The selected program is the ordinary gridwide HIP path with
15 equations, 14 species, fixed device redshift 30, LP64 layout, and 128-thread
blocks. The structured CUDA path and host driver are outside this translation.

“Needed” below means needed for this source to import and emit gfx942 code with
the compiler built from this checkout on 2026-09-26. It does not imply that a
different frontend, standard-library setup, or future compiler must use the
same representation.

## Rewrites that remain necessary

| Original HIP C++ | Generated Loom C++ | Reason and current evidence |
| --- | --- | --- |
| `burn_t` and `CollapseState` contain `std::array` fields; chemistry uses `Array1D`, `JacobianAdapter`, and one-based output indices. | `BurnRecord` and `CellRecord` use named scalar fields and fixed arrays. `X(n)`, `ydot(n)`, and `jac(i,j)` become direct zero-based accesses. Jacobian outputs use a flat row-major array. | The default Loom import environment cannot find `<array>`, and the custom containers/adapters are not part of the imported device subset. The index translation is mechanical: species `X(0)` stays element 0, while one-based output indices subtract one. |
| Generated RHS and Jacobian expressions contain deeply nested conditional operators. | `shallow_conditionals` outlines each nested conditional into a small helper with captured inputs. | Removing this outlining from the full advance root fails with the verifier's 32-region nesting limit. Each helper keeps the original conditional expression and lazy branch evaluation. |
| `std::exp`, `log`, `sqrt`, `cbrt`, `abs`, `isfinite`, `min`, and `max` are used through standard headers. | [support.h](support.h) binds the f64 functions to Loom scalar operations, implements `isfinite` from the binary64 exponent, and defines by-value `min`/`max`. | The default importer cannot find `<cmath>`. The `min`/`max` helpers select the first operand on ties and unordered comparisons, matching the source's `std::min`/`std::max` behavior. The translation retains `double` and disables approximate functions. |
| HIP calls `atomicCAS` and `atomicAdd` on integer status and count buffers. | Force-inlined helpers call Loom's typed compare-exchange and add RMW operations with relaxed ordering and device scope. | Direct HIP atomic calls are undeclared in the importer. The real atomic operations, old-value result, and failure-publication behavior are retained; replacing them with ordinary loads/stores would change concurrent semantics. |
| Source functions use range-for over species arrays and `std::array::size()`. | Indexed loops use the fixed species count; the normalization workspace is passed as a local `Real[14]` array. | A range-for over even a plain local array currently fails import as an unsupported `for-range-statement`. The normalization arithmetic and visit order are preserved. |
| `RODASState<15>` is an automatic object containing `std::array` vectors and two `std::array<std::array<Real,15>,15>` matrices. Solver methods take the state by reference. | The advance lane declares local scalar and one-dimensional array storage, including two 225-element matrices. A pointer-only `ScratchView` passes that storage to free solver functions. Matrix access uses `matrix_index(row,column)`. | A local record with array fields and a local `Real[15][15]` both fail import. The flat arrays import as private storage. Direct dynamic matrix indexing previously failed target bounds verification; the helper and the remaining loop/pivot assumptions make the current target compilation succeed. `ScratchRecord` remains only to inspect solver fields in native tests; it is not a kernel argument. |
| The generic `RODAS` integrator returns from within its tolerance and retry loops and breaks after acceptance. | [integrate.inc](integrate.inc) carries `done`, `retry`, and a result code, with explicit field updates and array copies. | Restoring the original loop returns fails import (`returns inside loops require a structured loop-exit projection`); replacing `retry` with `break` fails import (`unsupported statement: break-statement`). The rewrite keeps the original early-exit points and arithmetic order. |
| The kernel operates on `CollapseState&`, calls the templated integrator, and uses HIP's launch index directly. | Two Loom kernel roots operate on `CellRecord*`, call the specialized free functions, and use the same block/thread index and 128-thread workgroup. | The reachable generic C++ and host facilities are outside this import subset. The generated signatures expose only the buffers and scalar launch arguments needed by the selected device path. The prepare/host-minimum/advance sequence remains a host responsibility. |

The scalar solver defaults, ROS2S coefficients, chemistry coefficients, and
floating-point expression order come from the HIP source. The temporary HIP
`burn_t` is value-initialized; the generated RHS/Jacobian path explicitly zeros
its `rho`, overwrites every species and `e`, and computes `T` through EOS before
use. This is an initialization translation, not a change to the chemistry.

## Differences that are choices rather than compiler requirements

- The 15-equation/14-species specialization and fixed redshift select the
  original HIP device path. They do not claim that Loom requires those values.
- The current thread-local solver layout is a storage choice rather than a
  condition for parsing the solver. HIP constructs `RODASState` inside
  `burn_ros2s`; Loom declares the corresponding locals in the advance kernel
  and passes their addresses to `burn_ros2s`. The Loom compiler still reports
  substantial private-segment use.
- The pinned generator and exact-match replacements protect scientific
  expression order and reveal source drift. They are maintenance safeguards,
  not syntax required by the compiler.
- [support.h](support.h) now uses the compiler's `__DBL_MAX__` for the HIP
  `std::numeric_limits<Real>::max()` value, instead of a hand-written decimal.

Several constructs in the HIP source need no rewrite: by-value `EosSums`,
compound assignments, recursive `powi`, and `if constexpr` are accepted by
the current importer and retained in source-like form.

The upper-bound `__builtin_assume` inside `matrix_index` is individually
optional: removing it still emitted an advance code object with the same
static instruction count and reported LDS, SGPR, VGPR, and private-segment
sizes. It remains as an explicit statement of the valid matrix index range.
Removing **all** loop and pivot assumptions fails target verification with
`SUBRANGE/026` on private-array accesses.

## Evidence and limits

- [validate.cpp](validate.cpp) compares the generated routines and kernels
  against the pinned HIP source compiled natively. The full-grid run passed
  1,000 steps and 235,705 exact field comparisons, including solver retries,
  singular LU behavior, guards, and the step-20 perturbation.
- [check_import.py](check_import.py) checks both kernel roots, f64 math,
  device-scope atomics, and private solver allocations. Both roots emitted
  gfx942 HSACO files with the rebuilt compiler.
- These checks do not establish GPU numerical equality, concurrent atomic
  ordering in execution, or runtime speed. The code objects have not been run
  on an AMD GPU in this workspace.

The failed relaxation probes used isolated copies under `/private/tmp`; they
did not alter the pinned HIP source. Standard-library results describe the
tested importer include configuration, not a proof that no configured C++
standard library could ever work.
