# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.dialect.scf import defs as scf
from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.predicate import avx2_predicate_rules
from loom.target.arch.x86.descriptors import X86_AVX2_DESCRIPTOR_SET
from loom.target.arch.x86.vector_families import (
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    INTEGER_ELEMENTS,
)
from loom.target.contracts import (
    AttrProject,
    DescriptorRule,
    GuardKind,
    Scalar,
    Vector,
    descriptor_by_key,
)

_INTEGER_PREDICATES = (
    "eq",
    "ne",
    "slt",
    "sle",
    "sgt",
    "sge",
    "ult",
    "ule",
    "ugt",
    "uge",
)
_FLOAT_COMPARE_IMMEDIATES = {
    "oeq": 0,
    "ogt": 30,
    "oge": 29,
    "olt": 17,
    "ole": 18,
    "one": 12,
    "ord": 7,
    "ueq": 8,
    "ugt": 22,
    "uge": 21,
    "ult": 25,
    "ule": 26,
    "une": 4,
    "uno": 3,
}
_PREDICATE_LANE_COUNTS = (2, 4, 8, 16, 32)


def _descriptor(key: str):
    return descriptor_by_key(X86_AVX2_DESCRIPTOR_SET, key)


_RULES = tuple(
    case
    for case in avx2_predicate_rules(_descriptor)
    if isinstance(case, DescriptorRule)
)


def _rules_for(source_op) -> tuple[DescriptorRule, ...]:
    return tuple(rule for rule in _RULES if rule.source_op is source_op)


def _type_guard(rule: DescriptorRule, field: str):
    return next(
        guard.type_pattern
        for guard in rule.guards
        if guard.kind == GuardKind.VALUE_TYPE and guard.field == field
    )


def _register_class_guard(rule: DescriptorRule, field: str) -> str:
    return next(
        guard.register_class
        for guard in rule.guards
        if guard.kind == GuardKind.LOW_VALUE_REGISTER_CLASS and guard.field == field
    )


def _enum_guard(rule: DescriptorRule, field: str) -> str:
    return next(
        guard.enum_keyword
        for guard in rule.guards
        if guard.kind == GuardKind.ENUM_ATTR_EQUALS and guard.field == field
    )


def _representation_bit_widths(lane_count: int) -> tuple[int, ...]:
    if lane_count == 2:
        return (128,)
    if lane_count == 32:
        return (256,)
    return (128, 256)


def _register_class(bit_width: int) -> str:
    return {128: "x86.xmm", 256: "x86.ymm"}[bit_width]


def test_predicate_construction_covers_every_callable_lane_count() -> None:
    constant_rules = _rules_for(vector.vector_constant)
    assert {
        (
            _type_guard(rule, "result"),
            _register_class_guard(rule, "result"),
            next(
                (guard.minimum, guard.maximum)
                for guard in rule.guards
                if guard.kind == GuardKind.I64_RANGE and guard.field == "value"
            ),
        )
        for rule in constant_rules
    } == {
        (
            Vector("i1", lanes=lane_count),
            _register_class(representation_bit_width),
            (value, value),
        )
        for lane_count in _PREDICATE_LANE_COUNTS
        for representation_bit_width in _representation_bit_widths(lane_count)
        for value in (0, 1)
    }

    splat_rules = _rules_for(vector.vector_splat)
    assert {
        (
            _type_guard(rule, "result"),
            _register_class_guard(rule, "result"),
        )
        for rule in splat_rules
    } == {
        (
            Vector("i1", lanes=lane_count),
            _register_class(representation_bit_width),
        )
        for lane_count in _PREDICATE_LANE_COUNTS
        for representation_bit_width in _representation_bit_widths(lane_count)
    }
    assert all(_type_guard(rule, "scalar") == Scalar("i1") for rule in splat_rules)


def test_predicate_lane_movement_covers_every_representation() -> None:
    assert {
        (
            rule.source_op,
            _type_guard(
                rule,
                "source" if rule.source_op is vector.vector_extract else "dest",
            ),
            _register_class_guard(
                rule,
                "source" if rule.source_op is vector.vector_extract else "result",
            ),
        )
        for rule in (
            *_rules_for(vector.vector_extract),
            *_rules_for(vector.vector_insert),
        )
    } == {
        (
            source_op,
            Vector("i1", lanes=lane_count),
            _register_class(representation_bit_width),
        )
        for source_op in (vector.vector_extract, vector.vector_insert)
        for lane_count in _PREDICATE_LANE_COUNTS
        for representation_bit_width in _representation_bit_widths(lane_count)
    }


def test_integer_comparisons_cover_every_element_predicate_and_carrier() -> None:
    assert {
        (
            _enum_guard(rule, "predicate"),
            _type_guard(rule, "lhs"),
            _type_guard(rule, "result"),
            _register_class_guard(rule, "result"),
        )
        for rule in _rules_for(vector.vector_cmpi)
    } == {
        (
            predicate,
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
            Vector(
                "i1",
                lanes=vector_bit_width // element.bit_width,
            ),
            _register_class(representation_bit_width),
        )
        for element in INTEGER_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for predicate in _INTEGER_PREDICATES
        for representation_bit_width in _representation_bit_widths(
            vector_bit_width // element.bit_width
        )
    }


def test_float_comparisons_cover_every_element_and_carrier() -> None:
    rules = _rules_for(vector.vector_cmpf)
    assert {
        (
            _type_guard(rule, "lhs"),
            _type_guard(rule, "result"),
            _register_class_guard(rule, "result"),
        )
        for rule in rules
    } == {
        (
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
            Vector(
                "i1",
                lanes=vector_bit_width // element.bit_width,
            ),
            _register_class(representation_bit_width),
        )
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for representation_bit_width in _representation_bit_widths(
            vector_bit_width // element.bit_width
        )
    }
    expected_projection = AttrProject.enum_remap("predicate", _FLOAT_COMPARE_IMMEDIATES)
    assert all(
        rule.emit[0].immediates["predicate"] == expected_projection for rule in rules
    )


def test_vector_select_covers_every_condition_and_payload_carrier_pair() -> None:
    assert {
        (
            _type_guard(rule, "condition"),
            _register_class_guard(rule, "condition"),
            _register_class_guard(rule, "result"),
        )
        for rule in _rules_for(vector.vector_select)
    } == {
        (
            Vector("i1", lanes=lane_count),
            _register_class(condition_bit_width),
            _register_class(payload_bit_width),
        )
        for lane_count in _PREDICATE_LANE_COUNTS
        for condition_bit_width in _representation_bit_widths(lane_count)
        for payload_bit_width in _representation_bit_widths(lane_count)
    }


def test_whole_vector_select_covers_both_register_widths() -> None:
    rules = _rules_for(scf.scf_select)
    assert {_register_class_guard(rule, "result") for rule in rules} == {
        "x86.xmm",
        "x86.ymm",
    }
    assert all(_type_guard(rule, "condition") == Scalar("i1") for rule in rules)


def test_predicate_rule_inventory_has_no_unclassified_rows() -> None:
    expected_count = sum(
        len(_representation_bit_widths(lane_count)) * 5
        for lane_count in _PREDICATE_LANE_COUNTS
    )
    expected_count += sum(
        len(_representation_bit_widths(vector_bit_width // element.bit_width))
        * len(_INTEGER_PREDICATES)
        for element in INTEGER_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
    expected_count += sum(
        len(_representation_bit_widths(vector_bit_width // element.bit_width))
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
    expected_count += sum(
        len(_representation_bit_widths(lane_count)) ** 2
        for lane_count in _PREDICATE_LANE_COUNTS
    )
    expected_count += len(AVX2_VECTOR_BIT_WIDTHS)
    assert len(_RULES) == expected_count
