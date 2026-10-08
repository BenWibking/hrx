# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Descriptor invariants consumed by the native Wasm immediate readers."""

from loom.target.arch.wasm.descriptors import WASM_CORE_SIMD128_DESCRIPTOR_SET
from loom.target.low_descriptors import ImmediateFlag, ImmediateKind


def test_single_immediates_are_numeric_with_only_a_zero_offset_default():
    for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors:
        if descriptor.key in ("wasm.v128.const", "wasm.i8x16.shuffle"):
            continue
        assert len(descriptor.immediates) <= 1
        for immediate in descriptor.immediates:
            if descriptor.key in ("wasm.br", "wasm.br_if.i32"):
                # Native control flow uses structural Low operations; symbolic
                # branch packets are rejected before immediate emission.
                assert immediate.kind is ImmediateKind.ORDINAL
                assert immediate.flags == (ImmediateFlag.SYMBOLIC,)
                continue
            assert immediate.kind in (ImmediateKind.SIGNED, ImmediateKind.UNSIGNED)
            if ImmediateFlag.DEFAULT_VALUE in immediate.flags:
                assert immediate.field_name == "offset"
                assert immediate.kind is ImmediateKind.UNSIGNED
                assert immediate.bit_width == 32
                assert immediate.unsigned_max == (1 << 32) - 1
                assert immediate.default_value == 0
            else:
                assert not immediate.flags


def test_vector_constant_words_are_required_full_width_bits():
    descriptor = next(
        descriptor
        for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors
        if descriptor.key == "wasm.v128.const"
    )
    assert sorted(value.field_name for value in descriptor.immediates) == [
        "hi64",
        "lo64",
    ]
    assert [value.field_name for value in descriptor.asm_forms[0].immediates] == [
        "lo64",
        "hi64",
    ]
    for immediate in descriptor.immediates:
        assert immediate.kind is ImmediateKind.UNSIGNED
        assert immediate.bit_width == 64
        assert immediate.unsigned_max == (1 << 64) - 1
        assert not immediate.flags


def test_shuffle_has_sixteen_required_lanes_from_both_inputs():
    descriptor = next(
        descriptor
        for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors
        if descriptor.key == "wasm.i8x16.shuffle"
    )
    wire_names = [f"lane{lane}" for lane in range(16)]
    assert sorted(value.field_name for value in descriptor.immediates) == sorted(
        wire_names
    )
    assert [
        value.field_name for value in descriptor.asm_forms[0].immediates
    ] == wire_names
    for immediate in descriptor.immediates:
        assert immediate.kind is ImmediateKind.UNSIGNED
        assert immediate.unsigned_max == 31
        assert not immediate.flags


def test_narrow_lanes_use_unsigned_extract_encodings_and_exact_lane_domains():
    descriptors = {
        descriptor.key: descriptor
        for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors
    }
    for shape, lanes, splat_opcode, extract_opcode, replace_opcode in (
        ("i8x16", 16, 0xFD0F, 0xFD16, 0xFD17),
        ("i16x8", 8, 0xFD10, 0xFD19, 0xFD1A),
    ):
        assert descriptors[f"wasm.{shape}.splat"].encoding_id == splat_opcode
        for operation, opcode in (
            ("extract_lane_u", extract_opcode),
            ("replace_lane", replace_opcode),
        ):
            descriptor = descriptors[f"wasm.{shape}.{operation}"]
            assert descriptor.encoding_id == opcode
            (immediate,) = descriptor.immediates
            assert immediate.field_name == "lane"
            assert immediate.kind is ImmediateKind.UNSIGNED
            assert immediate.unsigned_max == lanes - 1
            assert not immediate.flags


def test_byte_equality_is_an_immediate_free_simd_binary_operation():
    descriptor = next(
        descriptor
        for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors
        if descriptor.key == "wasm.i8x16.eq"
    )
    assert descriptor.encoding_id == 0xFD23
    assert not descriptor.immediates
    assert len(descriptor.operands) == 3


def test_simd_shifts_have_one_i32_count_for_every_integer_lane_width():
    descriptors = {
        descriptor.key: descriptor
        for descriptor in WASM_CORE_SIMD128_DESCRIPTOR_SET.descriptors
    }
    for shape, encodings in (
        ("i8x16", (0xFD6B, 0xFD6C, 0xFD6D)),
        ("i16x8", (0xFD8B, 0xFD8C, 0xFD8D)),
        ("i32x4", (0xFDAB, 0xFDAC, 0xFDAD)),
        ("i64x2", (0xFDCB, 0xFDCC, 0xFDCD)),
    ):
        for operation, encoding in zip(
            ("shl", "shr_s", "shr_u"), encodings, strict=True
        ):
            descriptor = descriptors[f"wasm.{shape}.{operation}"]
            assert descriptor.encoding_id == encoding
            assert not descriptor.immediates
            assert [operand.field_name for operand in descriptor.operands] == [
                "dst",
                "value",
                "count",
            ]
            assert [
                alternative.reg_class
                for operand in descriptor.operands
                for alternative in operand.reg_alts
            ] == ["wasm.v128", "wasm.v128", "wasm.i32"]
