// SPDX-License-Identifier: BSD-3-Clause
#pragma once

// The host test uses the system's original double math. The Loom translation
// retains f64 operations, without AFN/ARCP or any replacement approximation.
#if defined(__loom__)
#include <hip/hip_runtime.h>
#include <loomcxx/atomic.h>
#define M_PI 3.14159265358979323846264338327950288
#define M_LN10 2.30258509299404568401799145468436421
#define DEVICE __device__
#define KERNEL __global__ [[loom::workgroup_size(128, 1, 1)]]
#define CELL_PARAMETER
#define CELL_INDEX (blockIdx.x * blockDim.x + threadIdx.x)
#else
#include <cmath>
#define DEVICE inline
#define KERNEL
#define CELL_PARAMETER , int cell_index
#define CELL_INDEX cell_index
#endif

namespace chemistry {
using Real = double;
using size_type = unsigned long;
using u64 = unsigned long long;
#define MAX_DOUBLE __DBL_MAX__

namespace math {
// Preserve the source template's multiplication grouping.
template<int N> DEVICE Real powi(Real x) {
    if constexpr (N < 0) return 1.0 / powi<-N>(x);
    else if constexpr (N == 0) return 1.0;
    else if constexpr (N == 1) return x;
    else if constexpr (N == 2) return x * x;
    else if constexpr (N % 2 == 0) return powi<2>(powi<N / 2>(x));
    else return x * powi<N - 1>(x);
}
// std::min/max choose their FIRST operand on equality or unordered comparison.
// Do not substitute floating minnum/maxnum (different NaN/signed-zero behavior).
template<typename T> DEVICE T min(T a, T b) { return b < a ? b : a; }
template<typename T> DEVICE T max(T a, T b) { return a < b ? b : a; }
#if defined(__loom__)
#define CHEM_MATH(name, op) DEVICE Real name(Real x) { return loom::scalar::op(x); }
CHEM_MATH(abs, absf)
CHEM_MATH(fabs, absf)
CHEM_MATH(exp, expf)
CHEM_MATH(log, logf)
CHEM_MATH(sqrt, sqrtf)
CHEM_MATH(cbrt, cbrtf)
#undef CHEM_MATH
DEVICE bool isfinite(Real x) {
    unsigned long long bits = __builtin_bit_cast(unsigned long long, x);
    return (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
#else
using std::abs;
using std::fabs;
using std::exp;
using std::log;
using std::sqrt;
using std::cbrt;
using std::isfinite;
#endif
} // namespace math

// Loom bindings preserve the original HIP relaxed device-scope RMW operations.
#if defined(__loom__)
[[loom::force_inline]] DEVICE int atomic_cas(int* address, int expected, int desired) {
    return loom::view::atomic::cmpxchg<loom::atomic::ordering::relaxed,
        loom::atomic::ordering::relaxed, loom::atomic::scope::device>(
        expected, desired, address);
}
[[loom::force_inline]] DEVICE int atomic_add(int* address, int value) {
    return loom::view::atomic::rmw<loom::atomic::kind::addi,
        loom::atomic::ordering::relaxed, loom::atomic::scope::device>(
        value, address);
}
#else
// Native CPU validation retains atomic RMW semantics; tests choose an explicit
// serial lane ordering for comparison against the original HIP kernel bodies.
DEVICE int atomic_cas(int* address, int expected, int desired) {
    __atomic_compare_exchange_n(address, &expected, desired, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}
DEVICE int atomic_add(int* address, int value) {
    return __atomic_fetch_add(address, value, __ATOMIC_SEQ_CST);
}
#endif
} // namespace chemistry
