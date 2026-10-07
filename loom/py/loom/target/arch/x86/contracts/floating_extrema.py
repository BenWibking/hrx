# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""x86 IEEE floating-point extrema composition."""

from __future__ import annotations

from loom.dialect.scalar import arithmetic as scalar_arithmetic
from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.vector_families import (
    AVX2_FLOAT_COMPARE_MNEMONICS,
    AVX2_FLOAT_EXTREMA_MNEMONICS,
    AVX2_FLOAT_EXTREMA_OPERATIONS,
    AVX2_SCALAR_FLOAT_EXTREMA_MNEMONICS,
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    VectorElement,
)
from loom.target.contracts import (
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    TypePattern,
    ValueRef,
    Vector,
)
from loom.target.low_descriptors import Descriptor

_REGISTER_SUFFIXES = {128: "xmm", 256: "ymm"}
_FLOAT_COMPARE_IMMEDIATES = {
    "oeq": 0,
    "ogt": 30,
    "olt": 17,
    "uno": 3,
}
_SCALAR_SOURCE_OPS = {
    "minimumf": scalar_arithmetic.scalar_minimumf,
    "maximumf": scalar_arithmetic.scalar_maximumf,
    "minnumf": scalar_arithmetic.scalar_minnumf,
    "maxnumf": scalar_arithmetic.scalar_maxnumf,
}
_VECTOR_SOURCE_OPS = {
    "minimumf": vector.vector_minimumf,
    "maximumf": vector.vector_maximumf,
    "minnumf": vector.vector_minnumf,
    "maxnumf": vector.vector_maxnumf,
}


def avx2_float_extrema_emit_chain(
    operation: str,
    element: VectorElement,
    register_bit_width: int,
    lhs: ValueRef,
    rhs: ValueRef,
    result: ValueRef,
    descriptor_lookup: _DescriptorLookup,
    *,
    temporary_prefix: str = "",
) -> tuple[tuple[EmitDescriptorOp, ...], tuple[Descriptor, ...]]:
    """Emits exact IEEE extrema semantics in one AVX register."""
    register_suffix = {128: "xmm", 256: "ymm"}[register_bit_width]
    compare = descriptor_lookup(
        f"x86.avx2.{AVX2_FLOAT_COMPARE_MNEMONICS[element.name]}.{register_suffix}"
    )
    is_minimum = operation in ("minimumf", "minnumf")
    propagates_nan = operation in ("minimumf", "maximumf")
    tie = descriptor_lookup(
        f"x86.avx2.{'vpor' if is_minimum else 'vpand'}.{register_suffix}"
    )
    select = descriptor_lookup(f"x86.avx2.vpblendvb.{register_suffix}")

    def temporary(name: str) -> ValueRef:
        return ValueRef.temporary(f"{temporary_prefix}{name}")

    ordered = temporary("ordered")
    candidate = temporary("candidate")
    equal = temporary("equal")
    tie_value = temporary("tie_value")
    finite_result = temporary("finite_result")
    lhs_nan = temporary("lhs_nan")
    lhs_result = temporary("lhs_result")
    rhs_nan = temporary("rhs_nan")
    descriptor_result_type = {"dst": DescriptorResultType()}
    emits = (
        _op_emit(
            descriptor=compare,
            operands={"lhs": lhs, "rhs": rhs},
            results={"dst": ordered},
            result_types=descriptor_result_type,
            immediates={
                "predicate": _FLOAT_COMPARE_IMMEDIATES["olt" if is_minimum else "ogt"]
            },
        ),
        _op_emit(
            descriptor=select,
            operands={"false_value": rhs, "true_value": lhs, "mask": ordered},
            results={"dst": candidate},
            result_types=descriptor_result_type,
        ),
        _op_emit(
            descriptor=compare,
            operands={"lhs": lhs, "rhs": rhs},
            results={"dst": equal},
            result_types=descriptor_result_type,
            immediates={"predicate": _FLOAT_COMPARE_IMMEDIATES["oeq"]},
        ),
        _op_emit(
            descriptor=tie,
            operands={"lhs": lhs, "rhs": rhs},
            results={"dst": tie_value},
            result_types=descriptor_result_type,
        ),
        _op_emit(
            descriptor=select,
            operands={
                "false_value": candidate,
                "true_value": tie_value,
                "mask": equal,
            },
            results={"dst": finite_result},
            result_types=descriptor_result_type,
        ),
        _op_emit(
            descriptor=compare,
            operands={"lhs": lhs, "rhs": lhs},
            results={"dst": lhs_nan},
            result_types=descriptor_result_type,
            immediates={"predicate": _FLOAT_COMPARE_IMMEDIATES["uno"]},
        ),
        _op_emit(
            descriptor=select,
            operands={
                "false_value": finite_result,
                "true_value": lhs if propagates_nan else rhs,
                "mask": lhs_nan,
            },
            results={"dst": lhs_result},
            result_types=descriptor_result_type,
        ),
        _op_emit(
            descriptor=compare,
            operands={"lhs": rhs, "rhs": rhs},
            results={"dst": rhs_nan},
            result_types=descriptor_result_type,
            immediates={"predicate": _FLOAT_COMPARE_IMMEDIATES["uno"]},
        ),
        _op_emit(
            descriptor=select,
            operands={
                "false_value": lhs_result,
                "true_value": rhs if propagates_nan else lhs,
                "mask": rhs_nan,
            },
            results={"dst": result},
        ),
    )
    return emits, (compare, tie, select)


def _float_extrema_rule(
    operation: str,
    element: VectorElement,
    source_type: TypePattern,
    register_bit_width: int,
    source_op,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    emits, dependencies = avx2_float_extrema_emit_chain(
        operation,
        element,
        register_bit_width,
        ValueRef.operand("lhs"),
        ValueRef.operand("rhs"),
        ValueRef.result("result"),
        descriptor_lookup,
    )
    return DescriptorRule(
        source_op=source_op,
        descriptor=dependencies[-1],
        guards=(
            Guard.value_type("lhs", source_type),
            Guard.value_type("rhs", source_type),
            Guard.value_type("result", source_type),
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in dependencies[:-1]
            ),
        ),
        emit=emits,
    )


def _fast_float_extrema_rule(
    source_type: TypePattern,
    descriptor: Descriptor,
    source_op,
) -> DescriptorRule:
    return DescriptorRule(
        source_op=source_op,
        descriptor=descriptor,
        guards=(
            Guard.instance_flags_has_all("fastmath", "nnan"),
            Guard.instance_flags_has_all("fastmath", "nsz"),
            Guard.value_type("lhs", source_type),
            Guard.value_type("rhs", source_type),
            Guard.value_type("result", source_type),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={
                    "lhs": ValueRef.operand("lhs"),
                    "rhs": ValueRef.operand("rhs"),
                },
                results={"dst": ValueRef.result("result")},
            ),
        ),
        priority=1,
    )


def avx2_float_extrema_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    """Generates scalar and packed f32/f64 IEEE extrema families."""
    fast_scalar_rules = (
        _fast_float_extrema_rule(
            Scalar(element.name),
            descriptor_lookup(
                f"x86.avx2."
                f"{AVX2_SCALAR_FLOAT_EXTREMA_MNEMONICS[operation][element.name]}."
                "xmm"
            ),
            _SCALAR_SOURCE_OPS[operation],
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
    )
    exact_scalar_rules = (
        _float_extrema_rule(
            operation,
            element,
            Scalar(element.name),
            128,
            _SCALAR_SOURCE_OPS[operation],
            descriptor_lookup,
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
    )
    fast_vector_rules = (
        _fast_float_extrema_rule(
            Vector(element.name, lanes=vector_bit_width // element.bit_width),
            descriptor_lookup(
                f"x86.avx2."
                f"{AVX2_FLOAT_EXTREMA_MNEMONICS[operation][element.name]}."
                f"{_REGISTER_SUFFIXES[vector_bit_width]}"
            ),
            _VECTOR_SOURCE_OPS[operation],
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
    exact_vector_rules = (
        _float_extrema_rule(
            operation,
            element,
            Vector(element.name, lanes=vector_bit_width // element.bit_width),
            vector_bit_width,
            _VECTOR_SOURCE_OPS[operation],
            descriptor_lookup,
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
    return (
        *fast_scalar_rules,
        *exact_scalar_rules,
        *fast_vector_rules,
        *exact_vector_rules,
    )
