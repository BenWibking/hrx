# HIP to Loom f64 translation

[generate.py](generate.py) extracts the device bodies from the pinned
[reference.cpp](reference.cpp) and emits [reproducer.cpp](reproducer.cpp),
[integrate.inc](integrate.inc), and [reference_kernels.inc](reference_kernels.inc).
The source digest and exact-match replacements reject source drift. Keep the
original HIP snapshot intact and edit the translation rules to change generated
files.

The selected HIP path has 15 equations, 14 species, device redshift 30, LP64
layout, and 128-thread blocks. The translation preserves chemistry coefficients,
ROS2S coefficients, solver defaults, and floating-point expression order.

| HIP source | Loom translation |
| --- | --- |
| `std::array`, species adapters, and one-based RHS/Jacobian output indices | Fixed arrays, named `BurnRecord`/`CellRecord` fields, and direct zero-based accesses |
| Deeply nested conditional expressions | Small outlined helpers preserving lazy evaluation and captured inputs |
| Standard math functions | `support.h` binds sqrt/abs to Loom scalar operations, exp/log/cbrt to `f64_math.h`, and tests the binary64 exponent for finite values |
| `std::min` and `std::max` | By-value helpers retaining the first operand on ties and unordered comparisons |
| HIP integer `atomicCAS` and `atomicAdd` | Typed Loom compare-exchange and add RMW, relaxed ordering, device scope, returning the old value |
| Range-for loops over species | Indexed loops retaining the original visit order |
| Automatic `RODASState` and solver methods | Initialized local scalars and arrays passed through pointer-only `ScratchView`; matrices use flat row-major storage |
| Integrator returns/breaks/continues inside loops | `done`, `retry`, and a result code retaining the original early-exit points |
| HIP gridwide kernels | Two Loom roots with the same cell index, launch bounds, failure publication, and prepare/advance policy |

Each burn initializes solver storage at the original state-construction point.
`ScratchRecord` is a native inspection fixture, not a kernel argument. Recursive
`powi`, by-value `EosSums`, compound assignments, and `if constexpr` remain in
source-like form. Loop, pivot, and matrix-index assumptions describe valid
private-array bounds.

`f64_math.h` carries strict f64 exp/log/cbrt source recipes adapted from the
compiler math recipes (OpenLibm/fdlibm exp and log, Newton cbrt). The device path
uses these while native differential validation uses system math. No f32
substitution or approximate-function permission is enabled.

[validate.cpp](validate.cpp) compares original and translated routines, solver
fields, and serial kernel bodies with exact double-bit and integer comparisons.
[check_import.py](check_import.py) checks both imported roots, private storage,
f64 math, and real device-scope atomics. The GPU harness in
[compare_rocm.cpp](compare_rocm.cpp) compares the original HIP kernels with Loom
code objects. Host validation, import verification, code-object emission, and
GPU execution provide separate evidence; see [README.md](README.md) for commands.
