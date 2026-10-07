// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOMCXX_PREDICATE_H_
#define LOOMCXX_PREDICATE_H_

// Predicate expressions describe compile-time contracts. They are admitted
// only inside loom::where attributes and loom::assume calls and have no runtime
// implementation. The helper names match Loom's textual predicate vocabulary.
namespace loom::predicate {

template <class T>
[[loom::predicate("eq")]] bool eq(T left, T right);
template <class T>
[[loom::predicate("ne")]] bool ne(T left, T right);
template <class T>
[[loom::predicate("lt")]] bool lt(T left, T right);
template <class T>
[[loom::predicate("le")]] bool le(T left, T right);
template <class T>
[[loom::predicate("gt")]] bool gt(T left, T right);
template <class T>
[[loom::predicate("ge")]] bool ge(T left, T right);
template <class T>
[[loom::predicate("ult")]] bool ult(T left, T right);
template <class T>
[[loom::predicate("ule")]] bool ule(T left, T right);
template <class T>
[[loom::predicate("ugt")]] bool ugt(T left, T right);
template <class T>
[[loom::predicate("uge")]] bool uge(T left, T right);
template <class T>
[[loom::predicate("multiple_of")]] bool multiple_of(T value, T divisor);
template <class T>
[[loom::predicate("power_of_two")]] bool power_of_two(T value);
template <class T>
[[loom::predicate("range")]] bool range(T value, T lower, T upper);
template <class T>
[[loom::predicate("not_nan")]] bool not_nan(T value);
template <class T>
[[loom::predicate("not_inf")]] bool not_inf(T value);
template <class T>
[[loom::predicate("finite")]] bool finite(T value);

// Returns the typed result identity of the function carrying the enclosing
// trailing loom::where attribute. A member access selects one scalar component
// of a returned record.
template <class T>
[[loom::predicate_result]] T result();

// Subject-implicit overloads constrain a loom::config declaration. The
// omitted first operand is the declared config value.
template <class T>
[[loom::predicate("eq")]] bool eq(T value);
template <class T>
[[loom::predicate("ne")]] bool ne(T value);
template <class T>
[[loom::predicate("lt")]] bool lt(T value);
template <class T>
[[loom::predicate("le")]] bool le(T value);
template <class T>
[[loom::predicate("gt")]] bool gt(T value);
template <class T>
[[loom::predicate("ge")]] bool ge(T value);
template <class T>
[[loom::predicate("ult")]] bool ult(T value);
template <class T>
[[loom::predicate("ule")]] bool ule(T value);
template <class T>
[[loom::predicate("ugt")]] bool ugt(T value);
template <class T>
[[loom::predicate("uge")]] bool uge(T value);
template <class T>
[[loom::predicate("multiple_of")]] bool multiple_of(T divisor);
[[loom::predicate("power_of_two")]] bool power_of_two();
template <class T>
[[loom::predicate("range")]] bool range(T lower, T upper);
[[loom::predicate("not_nan")]] bool not_nan();
[[loom::predicate("not_inf")]] bool not_inf();
[[loom::predicate("finite")]] bool finite();

}  // namespace loom::predicate

#endif  // LOOMCXX_PREDICATE_H_
