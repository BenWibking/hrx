# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AVX2 full-register vector arithmetic contract rules."""

from __future__ import annotations

from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.contracts.rule_builders import (
    full_vector_type as _full_vector_type,
)
from loom.target.arch.x86.contracts.rule_builders import (
    value_type_guards as _typed_guards,
)
from loom.target.arch.x86.vector_families import (
    AVX2_BITWISE_FAMILIES,
    AVX2_FLOAT_BINARY_FAMILIES,
    AVX2_FLOAT_FMA_MNEMONICS,
    AVX2_INTEGER_BINARY_FAMILIES,
    AVX2_PAYLOAD_ELEMENT_NAMES,
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
)
from loom.target.contracts import (
    ContractCase,
    DescriptorEmitForm,
    DescriptorRule,
    Guard,
    ValueRef,
    Vector,
)
from loom.target.contracts.templates import (
    DirectDescriptorCase,
    binary_descriptor_rules,
    ternary_descriptor_rules,
)

_REGISTER_SUFFIXES = {128: "xmm", 256: "ymm"}
_REGISTER_CLASSES = {128: "x86.xmm", 256: "x86.ymm"}
_INTEGER_SOURCE_OPS = {
    "addi": vector.vector_addi,
    "subi": vector.vector_subi,
    "muli": vector.vector_muli,
    "minsi": vector.vector_minsi,
    "maxsi": vector.vector_maxsi,
    "minui": vector.vector_minui,
    "maxui": vector.vector_maxui,
    "shli": vector.vector_shli,
    "shrsi": vector.vector_shrsi,
    "shrui": vector.vector_shrui,
}
_FLOAT_SOURCE_OPS = {
    "addf": vector.vector_addf,
    "subf": vector.vector_subf,
    "mulf": vector.vector_mulf,
    "divf": vector.vector_divf,
}
_BITWISE_SOURCE_OPS = {
    "andi": vector.vector_andi,
    "ori": vector.vector_ori,
    "xori": vector.vector_xori,
}


def _direct_vector_family_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    cases = tuple(
        DirectDescriptorCase(
            _INTEGER_SOURCE_OPS[family.source_operation],
            descriptor_lookup(
                f"x86.avx2.{family.mnemonic}.{_REGISTER_SUFFIXES[vector_bit_width]}"
            ),
            _full_vector_type(family.element, vector_bit_width),
        )
        for family in AVX2_INTEGER_BINARY_FAMILIES
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    ) + tuple(
        DirectDescriptorCase(
            _FLOAT_SOURCE_OPS[family.source_operation],
            descriptor_lookup(
                f"x86.avx2.{family.mnemonic}.{_REGISTER_SUFFIXES[vector_bit_width]}"
            ),
            _full_vector_type(family.element, vector_bit_width),
        )
        for family in AVX2_FLOAT_BINARY_FAMILIES
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
    return binary_descriptor_rules(cases, form=DescriptorEmitForm.OP)


def _bitwise_vector_family_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    value_type = Vector(
        (*AVX2_PAYLOAD_ELEMENT_NAMES, "i1"),
        minimum_lanes=2,
        maximum_lanes=32,
    )
    rules: list[DescriptorRule] = []
    for source_operation, mnemonic, _ in AVX2_BITWISE_FAMILIES:
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS:
            descriptor = descriptor_lookup(
                f"x86.avx2.{mnemonic}.{_REGISTER_SUFFIXES[vector_bit_width]}"
            )
            register_class = _REGISTER_CLASSES[vector_bit_width]
            rules.append(
                DescriptorRule(
                    source_op=_BITWISE_SOURCE_OPS[source_operation],
                    descriptor=descriptor,
                    guards=(
                        *_typed_guards(("lhs", "rhs", "result"), value_type),
                        Guard.low_value_register_class("lhs", register_class),
                        Guard.low_value_register_class("rhs", register_class),
                        Guard.low_value_register_class("result", register_class),
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
                )
            )
    return tuple(rules)


def _vector_fma_family_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    return ternary_descriptor_rules(
        tuple(
            DirectDescriptorCase(
                vector.vector_fmaf,
                descriptor_lookup(
                    f"x86.avx2.{AVX2_FLOAT_FMA_MNEMONICS[element.name]}."
                    f"{_REGISTER_SUFFIXES[vector_bit_width]}"
                ),
                _full_vector_type(element, vector_bit_width),
            )
            for element in FLOAT_ELEMENTS
            for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        ),
        form=DescriptorEmitForm.OP,
        descriptor_a="lhs",
        descriptor_b="rhs",
        descriptor_c="acc",
    )


def avx2_vector_arithmetic_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[ContractCase, ...]:
    return (
        *_direct_vector_family_rules(descriptor_lookup),
        *_bitwise_vector_family_rules(descriptor_lookup),
        *_vector_fma_family_rules(descriptor_lookup),
    )
