# f32 chemistry spill experiment

This separate reproducer converts both chemistry kernels and their reachable
helpers to f32 for compiler spill comparisons. The maintained f64 reproducer
and pinned HIP reference remain the numerical baseline.

From the repository root, regenerate or check the derived files:

```sh
python3 experimental/loom-chemistry/generate_f32.py
python3 experimental/loom-chemistry/generate_f32.py --check
```

The generator first checks the maintained f64 generated sources. It converts
`Real` to `float`, adds `f` suffixes to floating literals, uses `__FLT_MAX__`
for timestep candidates, updates record-size assertions, and implements
`isfinite` with the f32 exponent mask. Integer counters, hash operations,
atomics, control flow, expression grouping, and the 128-thread block stay as
authored. Perturbations use the upper 24 hash bits and a `2^-24` scale instead
of 53 bits and `2^-53`, matching f32 precision and avoiding the current native
backend's f64 intermediate for u64-to-f32 conversion. The 64-bit hash itself
is unchanged. Each derived file records its parent hash.

This is a precision experiment, not a validated single-precision chemistry
solver. Constants outside the f32 range follow C++ float-literal rounding;
very small coefficients and density floors can become zero. Tolerances and
`uround` keep their original values rounded to f32. Underflow and later
optimization can change both numerical behavior and the live program being
compared, so spill counts alone do not establish a precision-independent
allocator improvement. `CellRecord` is now 136 bytes and `ScratchRecord` is
2468 bytes under LP64; an f64 host buffer cannot be used for these kernels.

Import using the checkout-local tool:

```sh
mkdir -p build/loom-chemistry-f32
build/cmake/loom/src/loom/tools/loom-import-cxx/loom-import-cxx \
  --data-model=lp64 --approximate-functions=false \
  --root=chemistry::prepare_grid_timestep_kernel \
  --root=chemistry::advance_collapse_gridwide_kernel \
  --output=build/loom-chemistry-f32/chemistry.loom \
  experimental/loom-chemistry/f32/reproducer.cpp
```

Compile each root with `loom-compile`,
`--target=amdgpu:gfx942`, and `--format=amdgpu-hsaco`, as for the f64 version.
SPIR-V transcendental lowering additionally requires importing with
`--approximate-functions=true`; f32 does not by itself grant that permission
or resolve the private-memory and device-scope atomic restrictions encountered
in the SPIR-V build.

With the checkout used on 2026-10-02, both roots import with no f64 types and
the native gfx942 prepare kernel emits successfully. The native advance build
rejects strict f32 `exp`; enabling approximate functions gets past that operation
but still rejects f32 `cbrt`, which has no supported native math recipe. The
cube-root operation is retained rather than replacing its algorithm in this
precision experiment. Full advance spill statistics require that compiler
support. Imported source alone does not establish GPU numerical correctness.
