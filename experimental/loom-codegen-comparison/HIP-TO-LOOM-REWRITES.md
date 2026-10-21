# HIP to Loom f64 translation

[generate.py](generate.py) copies the device program from the pinned
[reference.cpp](reference.cpp) section by section and emits `reproducer.cpp` and
`reference_kernels.inc` into the directory given by `--output-dir`; they are not
checked in. The source digest and exact-match replacements reject source drift.
Keep the original HIP snapshot intact and edit the translation rules to change
generated files.

The selected HIP path has 15 equations, 14 species, device redshift 30, LP64
layout, and 128-thread blocks. The translation preserves chemistry coefficients,
ROS2S coefficients, solver defaults, and floating-point expression order.

The generated code keeps the source's types and calls: `burn_t`,
`CollapseState`, `RODASState<N>`, `RODAS<Problem>`, `PrimordialChem`, the
one-based `Array1D` `X(i)`/`ydot(i)` adapters, `JacobianAdapter`, nested
`std::array` matrices, references, member functions, and the deeply nested
conditional expressions in the generated RHS and Jacobian. `CellRecord` and
`BurnRecord` are aliases for the record types that `compare_rocm.cpp` and the
autoresearch harness use. These rewrites remain, each needed by
loom-import-cxx as of 8d91c11750:

| HIP source | Loom translation | Importer limitation |
| --- | --- | --- |
| `std::array`, `std::numeric_limits<double>::max()` | `math::array` and `math::numeric_limits` in `support.h` | No C++ standard library headers |
| Standard math functions | `support.h` binds sqrt/abs to Loom scalar operations, exp/log/cbrt to `f64_math.h`, and tests the binary64 exponent for finite values | No `<cmath>`; `__builtin_*` math is rejected; f64 exp/log/cbrt do not lower |
| `std::min` and `std::max` | By-value helpers retaining the first operand on ties and unordered comparisons | No `<algorithm>` |
| HIP integer `atomicCAS` and `atomicAdd` | Same names in `support.h`: typed Loom compare-exchange and add RMW, relaxed ordering, device scope, returning the old value | The embedded `hip_runtime.h` has no atomics |
| Range-for loops | Indexed loops binding the same element reference, in the original order | `for-range-statement` is unsupported |
| Integrator returns inside its loops, `for (;;)` | `done`, `retry`, and a result variable retaining the original early-exit points | Returns inside loops need a structured loop-exit projection; `for` needs a condition |
| Record reference parameters (`burn_t&`, `CollapseState&`, `RODASState&`, `IntegratorStats&`) | Pointer parameters rebound to the source's reference name on entry; callers pass `&` | Binding a member of a record reference parameter to a reference rejects it ("mutation requires an owned automatic source binding") |
| Default-initialized class locals (`Ros2sIntegrator integrator;`) | `{}` value initialization; every member already has a default initializer | Locals require initializers |
| `redshift()`, problem traits, HIP launch code | Device branch only; `Problem::neqs` directly; kernels as Loom roots | Host-only code does not parse (`<<<>>>`, std headers) |

`__builtin_assume` states pivot bounds that LU decomposition guarantees, and
the tolerance scan's index bound. The perturbation step test uses an unsigned
remainder, which is equivalent because steps count up from zero. Every other
statement is the source's.

`f64_math.h` carries f64 exp/log/cbrt source recipes transcribed from AMD's
OCML, with explicit `loom::scalar::fmaf`, `loom::scalar::frexpf` and
`loom::scalar::ldexpf` in exp and cbrt, and bit-level replacements for f64
rint, which Loom does not lower, and for frexp and ldexp in log, where the
native operations are slower on the advance kernel. The device path uses these
while native differential validation uses system math. As in OCML, only the
cbrt seed uses f32 approximate log2 and exp2; a Halley step in f64 corrects it.
No other f32 substitution or approximate-function permission is enabled.

[validate.cpp](validate.cpp) compares original and translated routines, the
problem RHS and Jacobian, LU, the integrator (including a singular-Jacobian
`RODAS<Problem>` fixture), and serial kernel bodies with exact double-bit and
integer comparisons. [check_import.py](check_import.py) checks both imported
roots, private storage, f64 math, and real device-scope atomics. The GPU harness
in [compare_rocm.cpp](compare_rocm.cpp) compares the original HIP kernels with
Loom code objects. Host validation, import verification, code-object emission,
and GPU execution provide separate evidence; see [README.md](README.md) for
commands.
