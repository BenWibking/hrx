# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.dialect.index import defs as index
from loom.dialect.scalar import bitwise, conversion
from loom.dialect.scf import defs as scf
from loom.dialect.vector import defs as vector
from loom.ir import ScalarType
from loom.scalar_type import ScalarTypeKind, scalar_type_name
from loom.target.contracts import DescriptorRule, GuardKind, ValueAliasRule
from loom.target.emit.wasm.contracts import WASM_CORE_SIMD128_CONTRACT_FRAGMENT

_FLOAT_KINDS = frozenset(
    {
        ScalarTypeKind.F8E4M3,
        ScalarTypeKind.F8E5M2,
        ScalarTypeKind.F16,
        ScalarTypeKind.BF16,
        ScalarTypeKind.F32,
        ScalarTypeKind.F64,
    }
)
_NON_FLOAT_KINDS = frozenset(
    {
        ScalarTypeKind.INDEX,
        ScalarTypeKind.OFFSET,
        ScalarTypeKind.I1,
        ScalarTypeKind.I8,
        ScalarTypeKind.I16,
        ScalarTypeKind.I32,
        ScalarTypeKind.I64,
    }
)


def _narrowing_pair(rule: DescriptorRule) -> tuple[str, str]:
    type_guards = {
        guard.field: guard.type_pattern
        for guard in rule.guards
        if guard.kind is GuardKind.VALUE_TYPE
    }
    input_type = type_guards["input"]
    result_type = type_guards["result"]
    assert input_type is not None and input_type.kind == "scalar"
    assert result_type is not None and result_type.kind == "scalar"
    assert len(input_type.elements) == 1
    assert len(result_type.elements) == 1
    return input_type.elements[0], result_type.elements[0]


def _expected_narrowing_pairs() -> frozenset[tuple[str, str]]:
    return frozenset(
        (scalar_type_name(source), scalar_type_name(result))
        for source in _FLOAT_KINDS
        for result in _FLOAT_KINDS
        if ScalarType(source).bitwidth > ScalarType(result).bitwidth
    )


def test_scalar_float_narrowing_rules_cover_the_complete_type_matrix() -> None:
    assert _FLOAT_KINDS | _NON_FLOAT_KINDS == frozenset(ScalarTypeKind)
    assert _FLOAT_KINDS.isdisjoint(_NON_FLOAT_KINDS)

    expected_pairs = _expected_narrowing_pairs()
    actual_pairs = {
        _narrowing_pair(rule)
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op is conversion.scalar_fptrunc
    }

    assert len(expected_pairs) == 13
    assert actual_pairs == expected_pairs


def test_exact_recipes_cover_every_non_native_pair() -> None:
    native_pairs = frozenset({("f64", "f32")})
    exact_recipe_pairs = {
        _narrowing_pair(rule)
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule)
        and rule.source_op is conversion.scalar_fptrunc
        and rule.report_key.startswith("exact_")
    }

    assert exact_recipe_pairs == _expected_narrowing_pairs() - native_pairs


def test_bit_operations_use_native_instructions_for_each_integer_carrier() -> None:
    for scalar_op, index_op, instruction in (
        (bitwise.scalar_rotli, index.index_rotli, "rotl"),
        (bitwise.scalar_rotri, index.index_rotri, "rotr"),
        (bitwise.scalar_ctlzi, index.index_ctlzi, "clz"),
        (bitwise.scalar_cttzi, index.index_cttzi, "ctz"),
        (bitwise.scalar_ctpopi, index.index_ctpopi, "popcnt"),
    ):
        actual = {}
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases:
            if not isinstance(rule, DescriptorRule):
                continue
            if rule.source_op not in (scalar_op, index_op):
                continue
            result_guard = next(
                guard
                for guard in rule.guards
                if guard.kind is GuardKind.VALUE_TYPE and guard.field == "result"
            )
            assert len(rule.emit) == 1
            actual[(rule.source_op, result_guard.type_pattern.elements[0])] = rule.emit[
                0
            ].descriptor.key
        assert actual == {
            (scalar_op, "i32"): f"wasm.i32.{instruction}",
            (scalar_op, "i64"): f"wasm.i64.{instruction}",
            (index_op, "index"): f"wasm.i32.{instruction}",
        }


def _numeric_vector_shapes(rule, field):
    for guard in rule.guards:
        if guard.kind is not GuardKind.VALUE_TYPE or guard.field != field:
            continue
        pattern = guard.type_pattern
        if pattern.kind == "vector":
            yield from (
                (element, pattern.lanes)
                for element in pattern.elements
                if element != "i1"
            )


def _expected_numeric_vector_shapes():
    return {
        (scalar_type_name(kind), 128 // ScalarType(kind).bitwidth)
        for kind in ScalarTypeKind
        if kind not in (ScalarTypeKind.I1, ScalarTypeKind.INDEX, ScalarTypeKind.OFFSET)
    }


def test_full_numeric_carriers_have_complete_structural_rules() -> None:
    expected = _expected_numeric_vector_shapes()
    for source_op, field in (
        (vector.vector_constant, "result"),
        (vector.vector_splat, "result"),
        (vector.vector_extract, "source"),
        (vector.vector_insert, "dest"),
        (vector.vector_shuffle, "result"),
        (scf.scf_select, "result"),
    ):
        actual = {
            shape
            for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
            if isinstance(rule, DescriptorRule) and rule.source_op is source_op
            for shape in _numeric_vector_shapes(rule, field)
        }
        assert actual == expected, source_op.name


def test_numeric_memory_shapes_share_exact_v128_accesses() -> None:
    for source_op, field, descriptor_key in (
        (vector.vector_load, "result", "wasm.v128.load"),
        (vector.vector_store, "value", "wasm.v128.store"),
    ):
        counts = {}
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases:
            if not isinstance(rule, DescriptorRule) or rule.source_op is not source_op:
                continue
            assert rule.descriptor.key == descriptor_key
            memory = rule.emit[-1].source_memory
            assert memory.element_byte_count * memory.vector_lane_count == 16
            assert memory.vector_lane_byte_stride == memory.element_byte_count
            for shape in _numeric_vector_shapes(rule, field):
                counts[shape] = counts.get(shape, 0) + 1
        # Static, dynamic with immediate bias, and dynamic with register bias.
        assert counts == {shape: 3 for shape in _expected_numeric_vector_shapes()}


def test_numeric_bitcasts_alias_every_full_width_shape_pair() -> None:
    actual = {
        (source, result)
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
        if isinstance(rule, ValueAliasRule) and rule.source_op is vector.vector_bitcast
        for source in _numeric_vector_shapes(rule, "input")
        for result in _numeric_vector_shapes(rule, "result")
    }
    shapes = _expected_numeric_vector_shapes()
    assert actual == {(source, result) for source in shapes for result in shapes}


def test_narrow_scalar_selection_uses_integer_payloads() -> None:
    actual = {
        element: rule.descriptor.key
        for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
        if isinstance(rule, DescriptorRule) and rule.source_op is scf.scf_select
        for guard in rule.guards
        if guard.kind is GuardKind.VALUE_TYPE
        and guard.field == "result"
        and guard.type_pattern.kind == "scalar"
        for element in guard.type_pattern.elements
        if element in ("i8", "f8E4M3", "f8E5M2", "i16", "f16", "bf16")
    }
    assert actual == {
        element: "wasm.i32.select"
        for element in ("i8", "f8E4M3", "f8E5M2", "i16", "f16", "bf16")
    }


def test_integer_bitwise_operations_cover_every_full_width_lane_shape() -> None:
    for source_op, instruction in (
        (vector.vector_andi, "and"),
        (vector.vector_ori, "or"),
        (vector.vector_xori, "xor"),
    ):
        actual = {
            shape: rule.descriptor.key
            for rule in WASM_CORE_SIMD128_CONTRACT_FRAGMENT.cases
            if isinstance(rule, DescriptorRule) and rule.source_op is source_op
            for shape in _numeric_vector_shapes(rule, "result")
        }
        assert actual == {
            (f"i{bits}", 128 // bits): f"wasm.v128.{instruction}"
            for bits in (8, 16, 32, 64)
        }
