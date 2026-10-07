# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.dialect.scalar import arithmetic as scalar_arithmetic
from loom.dialect.scalar import math as scalar_math
from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.avx2 import X86_AVX2_CONTRACT_FRAGMENT
from loom.target.arch.x86.descriptors import X86_AVX2_DESCRIPTOR_SET
from loom.target.arch.x86.vector_families import (
    AVX2_FLOAT_EXTREMA_OPERATIONS,
    AVX2_FLOAT_REDUCTION_OPERATIONS,
    AVX2_INTEGER_REDUCTION_FAMILIES,
    AVX2_LANE_FAMILIES,
    AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS,
    AVX2_SCALAR_FLOAT_BINARY_FAMILIES,
    AVX2_SCALAR_FLOAT_FMA_MNEMONICS,
    AVX2_SHUFFLE_FAMILIES,
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    INTEGER_ELEMENTS,
)
from loom.target.contracts import (
    DescriptorRule,
    GuardKind,
    Scalar,
    ValueAliasRule,
    Vector,
)


def _value_type_guard(rule: DescriptorRule, field: str):
    return next(
        guard.type_pattern
        for guard in rule.guards
        if guard.kind == GuardKind.VALUE_TYPE and guard.field == field
    )


def _static_index_range(rule: DescriptorRule) -> tuple[int, int]:
    guard = next(
        guard
        for guard in rule.guards
        if guard.kind == GuardKind.I64_ARRAY_ELEMENT_RANGE
        and guard.field == "static_indices"
        and guard.element == 0
    )
    assert guard.minimum is not None
    assert guard.maximum is not None
    return guard.minimum, guard.maximum


def test_avx2_scalar_float_arithmetic_covers_f32_and_f64() -> None:
    source_operations = {
        scalar_arithmetic.scalar_addf: "addf",
        scalar_arithmetic.scalar_subf: "subf",
        scalar_arithmetic.scalar_mulf: "mulf",
        scalar_arithmetic.scalar_divf: "divf",
    }
    rules = tuple(
        case
        for case in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(case, DescriptorRule)
        and case.source_op in (*source_operations, scalar_math.scalar_fmaf)
    )
    assert {
        (
            source_operations[rule.source_op],
            _value_type_guard(rule, "result"),
        )
        for rule in rules
        if rule.source_op in source_operations
    } == {
        (row.source_operation, Scalar(row.element.name))
        for row in AVX2_SCALAR_FLOAT_BINARY_FAMILIES
    }
    assert {
        _value_type_guard(rule, "result")
        for rule in rules
        if rule.source_op is scalar_math.scalar_fmaf
    } == {Scalar(element.name) for element in FLOAT_ELEMENTS}
    descriptor_keys = {
        descriptor.key for descriptor in X86_AVX2_DESCRIPTOR_SET.descriptors
    }
    assert {
        f"x86.avx2.{row.mnemonic}.xmm" for row in AVX2_SCALAR_FLOAT_BINARY_FAMILIES
    } <= descriptor_keys
    assert {
        f"x86.avx2.{mnemonic}.xmm"
        for mnemonic in AVX2_SCALAR_FLOAT_FMA_MNEMONICS.values()
    } <= descriptor_keys
    assert sum(rule.source_op in source_operations for rule in rules) == len(
        AVX2_SCALAR_FLOAT_BINARY_FAMILIES
    )
    assert sum(rule.source_op is scalar_math.scalar_fmaf for rule in rules) == len(
        FLOAT_ELEMENTS
    )


def test_avx2_lane_movement_covers_every_payload_type_and_width() -> None:
    rules = tuple(
        case
        for case in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(case, DescriptorRule)
        and case.source_op in (vector.vector_extract, vector.vector_insert)
        and _value_type_guard(
            case, "result" if case.source_op is vector.vector_extract else "value"
        )
        != Scalar("i1")
    )
    expected = {
        (
            source_op.name,
            Vector(row.element_names, lanes=vector_bit_width // row.element_bit_width),
            Scalar(row.element_names),
            (0, vector_bit_width // row.element_bit_width - 1),
        )
        for row in AVX2_LANE_FAMILIES
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for source_op in (vector.vector_extract, vector.vector_insert)
    }
    expected.update(
        (
            source_op.name,
            Vector(element_name, lanes=vector_bit_width // element_bit_width),
            Scalar(element_name),
            (0, vector_bit_width // element_bit_width - 1),
        )
        for element_name, element_bit_width in (("f32", 32), ("f64", 64))
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for source_op in (vector.vector_extract, vector.vector_insert)
        if not (
            element_name == "f64"
            and vector_bit_width == 128
            and source_op is vector.vector_insert
        )
    )
    expected.update(
        (
            vector.vector_insert.name,
            Vector("f64", lanes=2),
            Scalar("f64"),
            (lane, lane),
        )
        for lane in range(2)
    )
    actual = {
        (
            rule.source_op.name,
            _value_type_guard(
                rule, "source" if rule.source_op is vector.vector_extract else "dest"
            ),
            _value_type_guard(
                rule, "result" if rule.source_op is vector.vector_extract else "value"
            ),
            _static_index_range(rule),
        )
        for rule in rules
    }
    assert actual == expected
    assert len(rules) == len(expected)


def test_avx2_shuffles_cover_every_payload_type_and_width() -> None:
    rules = tuple(
        case
        for case in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(case, DescriptorRule) and case.source_op is vector.vector_shuffle
    )
    expected = {
        Vector(
            family.element_names,
            lanes=vector_bit_width // family.element_bit_width,
        )
        for family in AVX2_SHUFFLE_FAMILIES
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    }
    assert {_value_type_guard(rule, "result") for rule in rules} == expected
    assert len(rules) == len(expected)
    descriptor_keys = {
        descriptor.key for descriptor in X86_AVX2_DESCRIPTOR_SET.descriptors
    }
    assert {
        "x86.avx2.vpshufb.xmm",
        "x86.avx2.vpshufb.ymm",
        "x86.avx2.vpshufd.xmm",
        "x86.avx2.vpermps.ymm",
        "x86.avx2.vpermilpd.xmm",
        "x86.avx2.vpermq.ymm",
    } <= descriptor_keys


def test_avx2_float_bitcasts_cover_every_storage_family_and_width() -> None:
    rules = tuple(
        case
        for case in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(case, ValueAliasRule) and case.source_op is vector.vector_bitcast
    )
    expected = {
        (source_type, result_type)
        for float_elements, integer_element, element_bit_width in (
            (("f8E4M3", "f8E5M2"), "i8", 8),
            (("f16", "bf16"), "i16", 16),
            (("f32",), "i32", 32),
            (("f64",), "i64", 64),
        )
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for lane_count in (vector_bit_width // element_bit_width,)
        for source_type, result_type in (
            (
                Vector(float_elements, lanes=lane_count),
                Vector(integer_element, lanes=lane_count),
            ),
            (
                Vector(integer_element, lanes=lane_count),
                Vector(float_elements, lanes=lane_count),
            ),
        )
    }
    actual = {
        (
            _value_type_guard(rule, "input"),
            _value_type_guard(rule, "result"),
        )
        for rule in rules
    }
    assert actual == expected
    assert len(rules) == len(expected)


def test_avx2_iotas_cover_every_integer_element_and_width() -> None:
    rules = tuple(
        case
        for case in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(case, DescriptorRule) and case.source_op is vector.vector_iota
    )
    actual = {
        (
            _value_type_guard(rule, "base"),
            _value_type_guard(rule, "result"),
            rule.priority,
        )
        for rule in rules
    }
    expected = {
        (
            Scalar(element.name),
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
            priority,
        )
        for element in INTEGER_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for priority in (0, 1)
    }
    assert actual == expected
    assert len(rules) == len(expected)
    assert all(
        any(
            guard.kind == GuardKind.VALUE_I64_RANGE
            and guard.field == "step"
            and guard.minimum == 1
            and guard.maximum == 1
            for guard in rule.guards
        )
        for rule in rules
        if rule.priority == 1
    )


def test_avx2_float_extrema_cover_every_semantic_and_width() -> None:
    vector_source_operations = {
        vector.vector_minimumf: "minimumf",
        vector.vector_maximumf: "maximumf",
        vector.vector_minnumf: "minnumf",
        vector.vector_maxnumf: "maxnumf",
    }
    actual_vectors = {
        (
            vector_source_operations[rule.source_op],
            _value_type_guard(rule, "result"),
        )
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op in vector_source_operations
    }
    assert actual_vectors == {
        (
            operation,
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    }

    scalar_source_operations = {
        scalar_arithmetic.scalar_minimumf: "minimumf",
        scalar_arithmetic.scalar_maximumf: "maximumf",
        scalar_arithmetic.scalar_minnumf: "minnumf",
        scalar_arithmetic.scalar_maxnumf: "maxnumf",
    }
    actual_scalars = {
        (
            scalar_source_operations[rule.source_op],
            _value_type_guard(rule, "result"),
        )
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op in scalar_source_operations
    }
    assert actual_scalars == {
        (operation, Scalar(element.name))
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
    }

    extrema_rules = tuple(
        rule
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op in (*vector_source_operations, *scalar_source_operations)
    )
    rule_counts = {}
    for rule in extrema_rules:
        key = (rule.source_op, _value_type_guard(rule, "result"))
        rule_counts[key] = rule_counts.get(key, 0) + 1
    assert set(rule_counts.values()) == {2}
    fast_rules = tuple(rule for rule in extrema_rules if rule.priority == 1)
    assert len(fast_rules) * 2 == len(extrema_rules)
    assert all(
        {
            guard.enum_keyword
            for guard in rule.guards
            if guard.kind == GuardKind.INSTANCE_FLAGS_HAS_ALL
        }
        == {"nnan", "nsz"}
        for rule in fast_rules
    )


def test_avx2_integer_reductions_cover_every_native_combine_family() -> None:
    rules = tuple(
        rule
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule) and rule.source_op is vector.vector_reduce
    )
    integer_operations = {
        row.source_operation for row in AVX2_INTEGER_REDUCTION_FAMILIES
    }
    integer_rules = tuple(
        rule
        for rule in rules
        if any(
            guard.kind == GuardKind.ENUM_ATTR_EQUALS
            and guard.field == "kind"
            and guard.enum_keyword in integer_operations
            for guard in rule.guards
        )
    )
    actual = {
        (
            operation,
            _value_type_guard(rule, "input"),
        )
        for rule in integer_rules
        for operation in (
            next(
                guard.enum_keyword
                for guard in rule.guards
                if guard.kind == GuardKind.ENUM_ATTR_EQUALS and guard.field == "kind"
            ),
        )
    }
    expected = {
        (
            row.source_operation,
            Vector(
                row.element.name,
                lanes=vector_bit_width // row.element.bit_width,
            ),
        )
        for row in AVX2_INTEGER_REDUCTION_FAMILIES
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    }
    assert actual == expected
    assert len(integer_rules) == len(expected)


def test_avx2_float_reductions_and_dots_cover_both_types_and_widths() -> None:
    reduction_rules = tuple(
        rule
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op is vector.vector_reduce
        and any(
            guard.kind == GuardKind.ENUM_ATTR_EQUALS
            and guard.enum_keyword in AVX2_FLOAT_REDUCTION_OPERATIONS
            for guard in rule.guards
        )
    )
    packed_reduction_rules = tuple(
        rule
        for rule in reduction_rules
        if next(
            guard.enum_keyword
            for guard in rule.guards
            if guard.kind == GuardKind.ENUM_ATTR_EQUALS
        )
        in AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS
    )
    packed_expected = {
        (
            operation,
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
            fastmath_guard,
        )
        for operation in AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for fastmath_guard in (
            GuardKind.INSTANCE_FLAGS_HAS_NONE,
            GuardKind.INSTANCE_FLAGS_HAS_ALL,
        )
    }
    assert {
        (
            next(
                guard.enum_keyword
                for guard in rule.guards
                if guard.kind == GuardKind.ENUM_ATTR_EQUALS
            ),
            _value_type_guard(rule, "input"),
            next(
                guard.kind
                for guard in rule.guards
                if guard.kind
                in (
                    GuardKind.INSTANCE_FLAGS_HAS_ALL,
                    GuardKind.INSTANCE_FLAGS_HAS_NONE,
                )
            ),
        )
        for rule in packed_reduction_rules
    } == packed_expected
    assert len(packed_reduction_rules) == len(packed_expected)

    fast_extrema_rules = tuple(
        rule
        for rule in reduction_rules
        if next(
            guard.enum_keyword
            for guard in rule.guards
            if guard.kind == GuardKind.ENUM_ATTR_EQUALS
        )
        in AVX2_FLOAT_EXTREMA_OPERATIONS
    )
    fast_extrema_expected = {
        (
            operation,
            Vector(
                element.name,
                lanes=vector_bit_width // element.bit_width,
            ),
            frozenset(("reassoc", "nnan", "nsz")),
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    }
    assert {
        (
            next(
                guard.enum_keyword
                for guard in rule.guards
                if guard.kind == GuardKind.ENUM_ATTR_EQUALS
            ),
            _value_type_guard(rule, "input"),
            frozenset(
                guard.enum_keyword
                for guard in rule.guards
                if guard.kind == GuardKind.INSTANCE_FLAGS_HAS_ALL
            ),
        )
        for rule in fast_extrema_rules
    } == fast_extrema_expected
    assert len(fast_extrema_rules) == len(fast_extrema_expected)

    dot_rules = tuple(
        rule
        for rule in X86_AVX2_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule) and rule.source_op is vector.vector_dotf
    )
    dot_expected = {
        Vector(
            element.name,
            lanes=vector_bit_width // element.bit_width,
        )
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    }
    assert {_value_type_guard(rule, "lhs") for rule in dot_rules} == dot_expected
    assert len(dot_rules) == len(dot_expected)
