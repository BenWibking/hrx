# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""x86 vector construction and iota contract rules."""

from __future__ import annotations

from collections.abc import Iterable
from enum import Enum

from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.constants import (
    floating_vector_constant_bits_rule,
    floating_vector_zero_rule,
    integer_vector_constant_rule,
    integer_vector_splat_rule,
    integer_vector_zero_rule,
)
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.contracts.rule_builders import (
    full_vector_type as _full_vector_type,
)
from loom.target.arch.x86.vector_families import (
    AVX2_VECTOR_BIT_WIDTHS,
    AVX512_DIRECT_BROADCAST_VECTOR_BIT_WIDTHS,
    AVX512_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    INTEGER_ELEMENTS,
    STORAGE_ELEMENTS,
    VectorElement,
)
from loom.target.contracts import (
    ContractCase,
    DescriptorEmitForm,
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    TypePattern,
    ValueAliasRule,
    ValueRef,
    Vector,
)
from loom.target.low_descriptors import Descriptor

_I1 = Scalar("i1")
_I32 = Scalar("i32")
_I64 = Scalar("i64")
_V2I64 = Vector("i64", lanes=2)
_REGISTER_SUFFIXES = {128: "xmm", 256: "ymm", 512: "zmm"}
_REGISTER_CLASSES = {128: "x86.xmm", 256: "x86.ymm", 512: "x86.zmm"}
_INTEGER_BROADCAST_MNEMONICS = {
    8: "vpbroadcastb",
    16: "vpbroadcastw",
    32: "vpbroadcastd",
    64: "vpbroadcastq",
}


class _IntegerBroadcastSource(Enum):
    GPR = "gpr"
    XMM_LOW_LANE = "xmm-low-lane"


def _integer_lane_move_descriptor(
    element_bit_width: int,
    source: _IntegerBroadcastSource,
    descriptor_lookup: _DescriptorLookup,
) -> Descriptor | None:
    if source == _IntegerBroadcastSource.GPR:
        return None
    return descriptor_lookup(
        "x86.avx2.vmovq.xmm.gpr64"
        if element_bit_width == 64
        else "x86.avx2.vmovd.xmm.gpr32"
    )


def _vector_zero_rules(
    descriptor_lookup: _DescriptorLookup,
    *,
    descriptor_key_prefix: str,
    vector_bit_widths: tuple[int, ...],
) -> tuple[DescriptorRule, ...]:
    maximum_lanes = max(vector_bit_widths) // 8
    integer_vector_types = Vector(
        tuple(element.name for element in INTEGER_ELEMENTS),
        minimum_lanes=2,
        maximum_lanes=maximum_lanes,
    )
    float_vector_types = Vector(
        tuple(element.name for element in (*STORAGE_ELEMENTS, *FLOAT_ELEMENTS)),
        minimum_lanes=2,
        maximum_lanes=maximum_lanes,
    )
    rules: list[DescriptorRule] = []
    for vector_bit_width in vector_bit_widths:
        descriptor = descriptor_lookup(
            f"{descriptor_key_prefix}.vxorps.zero."
            f"{_REGISTER_SUFFIXES[vector_bit_width]}"
        )
        register_class = _REGISTER_CLASSES[vector_bit_width]
        rules.extend(
            (
                integer_vector_zero_rule(
                    integer_vector_types,
                    descriptor,
                    result_register_class=register_class,
                ),
                floating_vector_zero_rule(
                    float_vector_types,
                    descriptor,
                    result_register_class=register_class,
                ),
            )
        )
    return tuple(rules)


def _uniform_vector_constant_rules(
    descriptor_lookup: _DescriptorLookup,
    *,
    descriptor_key_prefix: str,
    vector_bit_widths: tuple[int, ...],
    broadcast_source: _IntegerBroadcastSource,
    priority: int = 0,
) -> tuple[DescriptorRule, ...]:
    rules: list[DescriptorRule] = []
    for element in INTEGER_ELEMENTS:
        lane_type = _full_vector_type(element, 128)
        rules.extend(
            integer_vector_constant_rule(
                _full_vector_type(element, vector_bit_width),
                lane_type,
                element.bit_width,
                descriptor_lookup(
                    "x86.scalar.movimm.gpr64"
                    if element.bit_width == 64
                    else "x86.scalar.movimm.gpr32"
                ),
                _integer_lane_move_descriptor(
                    element.bit_width,
                    broadcast_source,
                    descriptor_lookup,
                ),
                descriptor_lookup(
                    f"{descriptor_key_prefix}."
                    f"{_INTEGER_BROADCAST_MNEMONICS[element.bit_width]}."
                    f"{_REGISTER_SUFFIXES[vector_bit_width]}"
                ),
                priority=priority,
            )
            for vector_bit_width in vector_bit_widths
        )
    for element in (*STORAGE_ELEMENTS, *FLOAT_ELEMENTS):
        lane_type = _full_vector_type(element, 128)
        rules.extend(
            floating_vector_constant_bits_rule(
                _full_vector_type(element, vector_bit_width),
                lane_type,
                element.name,
                descriptor_lookup(
                    "x86.scalar.movimm.gpr64"
                    if element.bit_width == 64
                    else "x86.scalar.movimm.gpr32"
                ),
                _integer_lane_move_descriptor(
                    element.bit_width,
                    broadcast_source,
                    descriptor_lookup,
                ),
                descriptor_lookup(
                    f"{descriptor_key_prefix}."
                    f"{_INTEGER_BROADCAST_MNEMONICS[element.bit_width]}."
                    f"{_REGISTER_SUFFIXES[vector_bit_width]}"
                ),
                priority=priority,
            )
            for vector_bit_width in vector_bit_widths
        )
    return tuple(rules)


def _vector_splat_rules(
    descriptor_lookup: _DescriptorLookup,
    *,
    descriptor_key_prefix: str,
    integer_vector_bit_widths: tuple[int, ...],
    float_vector_bit_widths: tuple[int, ...],
    broadcast_source: _IntegerBroadcastSource,
    priority: int = 0,
) -> tuple[DescriptorRule, ...]:
    rules: list[DescriptorRule] = []
    for element in (*INTEGER_ELEMENTS, *STORAGE_ELEMENTS):
        scalar_type = Scalar(element.name)
        lane_type = _full_vector_type(element, 128)
        for vector_bit_width in integer_vector_bit_widths:
            result_type = _full_vector_type(element, vector_bit_width)
            rules.append(
                integer_vector_splat_rule(
                    scalar_type,
                    result_type,
                    _integer_lane_move_descriptor(
                        element.bit_width,
                        broadcast_source,
                        descriptor_lookup,
                    ),
                    descriptor_lookup(
                        f"{descriptor_key_prefix}."
                        f"{_INTEGER_BROADCAST_MNEMONICS[element.bit_width]}."
                        f"{_REGISTER_SUFFIXES[vector_bit_width]}"
                    ),
                    broadcast_operand="value",
                    lane_type=lane_type,
                    priority=priority,
                )
            )
    for element, mnemonic in (
        (FLOAT_ELEMENTS[0], "vbroadcastss"),
        (FLOAT_ELEMENTS[1], "vbroadcastsd"),
    ):
        rules.extend(
            _splat_rule(
                Scalar(element.name),
                _full_vector_type(element, vector_bit_width),
                f"{descriptor_key_prefix}.{mnemonic}."
                f"{_REGISTER_SUFFIXES[vector_bit_width]}",
                descriptor_lookup,
            )
            for vector_bit_width in float_vector_bit_widths
        )
    return tuple(rules)


def _vector_bitcast_alias_rules(
    vector_bit_widths: tuple[int, ...],
) -> tuple[ValueAliasRule, ...]:
    """Aliases bit-compatible full-register float and integer payloads."""
    rules: list[ValueAliasRule] = []
    for float_elements, integer_element, element_bit_width in (
        (("f8E4M3", "f8E5M2"), "i8", 8),
        (("f16", "bf16"), "i16", 16),
        (("f32",), "i32", 32),
        (("f64",), "i64", 64),
    ):
        for vector_bit_width in vector_bit_widths:
            lane_count = vector_bit_width // element_bit_width
            integer_type = Vector(integer_element, lanes=lane_count)
            float_type = Vector(float_elements, lanes=lane_count)
            for source_type, result_type in (
                (float_type, integer_type),
                (integer_type, float_type),
            ):
                rules.append(
                    ValueAliasRule(
                        source_op=vector.vector_bitcast,
                        source=ValueRef.operand("input"),
                        result=ValueRef.result("result"),
                        guards=(
                            Guard.value_type("input", source_type),
                            Guard.value_type("result", result_type),
                        ),
                    )
                )
    return tuple(rules)


def _splat_rule(
    scalar_type: TypePattern,
    result_type: TypePattern,
    descriptor_key: str,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    descriptor = descriptor_lookup(descriptor_key)
    return DescriptorRule(
        source_op=vector.vector_splat,
        descriptor=descriptor,
        guards=(
            Guard.value_type("scalar", scalar_type),
            Guard.value_type("result", result_type),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={"value": ValueRef.operand("scalar")},
                results={"dst": ValueRef.result("result")},
            ),
        ),
    )


def _packed_byte_ordinals(start: int, count: int) -> int:
    return sum((start + index) << (index * 8) for index in range(count))


def _iota_seed_emits(
    element_bit_width: int,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[list[EmitDescriptorOp], ValueRef]:
    lane_count = vector_bit_width // element_bit_width
    emits: list[EmitDescriptorOp] = []

    def build_qword(start: int, name: str) -> ValueRef:
        move_imm = descriptor_lookup("x86.scalar.movimm.gpr64")
        move = descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")
        bits = ValueRef.temporary(f"{name}_bits")
        value = ValueRef.temporary(name)
        emits.extend(
            (
                EmitDescriptorOp(
                    descriptor=move_imm,
                    results={"dst": bits},
                    result_types={"dst": _I64},
                    immediates={"imm64": _packed_byte_ordinals(start, 8)},
                    form=DescriptorEmitForm.CONST,
                ),
                _op_emit(
                    descriptor=move,
                    operands={"input": bits},
                    results={"dst": value},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
        return value

    if lane_count <= 4:
        move_imm = descriptor_lookup("x86.scalar.movimm.gpr32")
        move = descriptor_lookup("x86.avx2.vmovd.xmm.gpr32")
        bits = ValueRef.temporary("seed_bits")
        byte_seed = ValueRef.temporary("byte_seed")
        emits.extend(
            (
                EmitDescriptorOp(
                    descriptor=move_imm,
                    results={"dst": bits},
                    result_types={"dst": _I32},
                    immediates={"imm32": _packed_byte_ordinals(0, lane_count)},
                    form=DescriptorEmitForm.CONST,
                ),
                _op_emit(
                    descriptor=move,
                    operands={"input": bits},
                    results={"dst": byte_seed},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
    elif lane_count == 8:
        byte_seed = build_qword(0, "byte_seed")
    else:
        interleave = descriptor_lookup("x86.avx2.vpunpcklqdq.xmm")
        low_qword = build_qword(0, "seed_qword0")
        high_qword = build_qword(8, "seed_qword1")
        low_half = ValueRef.temporary("byte_seed_low")
        emits.append(
            _op_emit(
                descriptor=interleave,
                operands={"lhs": low_qword, "rhs": high_qword},
                results={"dst": low_half},
                result_types={"dst": DescriptorResultType()},
            )
        )
        if lane_count == 16:
            byte_seed = low_half
        else:
            qword2 = build_qword(16, "seed_qword2")
            qword3 = build_qword(24, "seed_qword3")
            high_half = ValueRef.temporary("byte_seed_high")
            emits.append(
                _op_emit(
                    descriptor=interleave,
                    operands={"lhs": qword2, "rhs": qword3},
                    results={"dst": high_half},
                    result_types={"dst": DescriptorResultType()},
                )
            )
            zero = descriptor_lookup("x86.avx2.vxorps.zero.ymm")
            insert_half = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
            empty = ValueRef.temporary("empty_seed")
            with_low = ValueRef.temporary("seed_with_low")
            byte_seed = ValueRef.temporary("byte_seed")
            emits.extend(
                (
                    _op_emit(
                        descriptor=zero,
                        results={"dst": empty},
                        result_types={"dst": DescriptorResultType()},
                    ),
                    _op_emit(
                        descriptor=insert_half,
                        operands={"dest": empty, "value": low_half},
                        results={"dst": with_low},
                        result_types={"dst": DescriptorResultType()},
                        immediates={"lane": 0},
                    ),
                    _op_emit(
                        descriptor=insert_half,
                        operands={"dest": with_low, "value": high_half},
                        results={"dst": byte_seed},
                        result_types={"dst": DescriptorResultType()},
                        immediates={"lane": 1},
                    ),
                )
            )

    if element_bit_width == 8:
        return emits, byte_seed
    suffix = {16: "w", 32: "d", 64: "q"}[element_bit_width]
    widen = descriptor_lookup(
        f"x86.avx2.vpmovzxb{suffix}.{_REGISTER_SUFFIXES[vector_bit_width]}.xmm"
    )
    seed = ValueRef.temporary("lane_seed")
    emits.append(
        _op_emit(
            descriptor=widen,
            operands={"source": byte_seed},
            results={"dst": seed},
            result_types={"dst": DescriptorResultType()},
        )
    )
    return emits, seed


def _iota_splat_emits(
    source: ValueRef,
    *,
    element_bit_width: int,
    vector_bit_width: int,
    name: str,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[list[EmitDescriptorOp], ValueRef]:
    if element_bit_width == 64:
        move = descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")
    else:
        move = descriptor_lookup("x86.avx2.vmovd.xmm.gpr32")
    broadcast = descriptor_lookup(
        f"x86.avx2.{_INTEGER_BROADCAST_MNEMONICS[element_bit_width]}."
        f"{_REGISTER_SUFFIXES[vector_bit_width]}"
    )
    lane = ValueRef.temporary(f"{name}_lane")
    result = ValueRef.temporary(name)
    return [
        _op_emit(
            descriptor=move,
            operands={"input": source},
            results={"dst": lane},
            result_types={"dst": DescriptorResultType()},
        ),
        _op_emit(
            descriptor=broadcast,
            operands={"value": lane},
            results={"dst": result},
            result_types={"dst": DescriptorResultType()},
        ),
    ], result


def _iota_scale_i8_emits(
    seed: ValueRef,
    *,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[list[EmitDescriptorOp], ValueRef]:
    emits, step_words = _iota_splat_emits(
        ValueRef.operand("step"),
        element_bit_width=16,
        vector_bit_width=128,
        name="step_words",
        descriptor_lookup=descriptor_lookup,
    )
    move_imm = descriptor_lookup("x86.scalar.movimm.gpr32")
    mask_bits = ValueRef.temporary("byte_mask_bits")
    emits.append(
        EmitDescriptorOp(
            descriptor=move_imm,
            results={"dst": mask_bits},
            result_types={"dst": _I32},
            immediates={"imm32": 255},
            form=DescriptorEmitForm.CONST,
        )
    )
    mask_emits, byte_mask = _iota_splat_emits(
        mask_bits,
        element_bit_width=16,
        vector_bit_width=128,
        name="byte_mask",
        descriptor_lookup=descriptor_lookup,
    )
    emits.extend(mask_emits)
    extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
    shift = descriptor_lookup("x86.avx2.vpsrldq.xmm")
    widen = descriptor_lookup("x86.avx2.vpmovzxbw.xmm.xmm")
    multiply = descriptor_lookup("x86.avx2.vpmullw.xmm")
    and_ = descriptor_lookup("x86.avx2.vpand.xmm")
    pack = descriptor_lookup("x86.avx2.vpackuswb.xmm")
    packed_halves: list[ValueRef] = []
    half_count = vector_bit_width // 128
    for half_index in range(half_count):
        half = seed
        if vector_bit_width == 256:
            half = ValueRef.temporary(f"seed_half{half_index}")
            emits.append(
                _op_emit(
                    descriptor=extract_half,
                    operands={"source": seed},
                    results={"dst": half},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"lane": half_index},
                )
            )
        high_bytes = ValueRef.temporary(f"seed_half{half_index}_high_bytes")
        low_words = ValueRef.temporary(f"seed_half{half_index}_low_words")
        high_words = ValueRef.temporary(f"seed_half{half_index}_high_words")
        low_products = ValueRef.temporary(f"seed_half{half_index}_low_products")
        high_products = ValueRef.temporary(f"seed_half{half_index}_high_products")
        low_bytes = ValueRef.temporary(f"seed_half{half_index}_low_bytes")
        high_bytes_masked = ValueRef.temporary(
            f"seed_half{half_index}_high_bytes_masked"
        )
        packed = ValueRef.temporary(f"scaled_half{half_index}")
        emits.extend(
            (
                _op_emit(
                    descriptor=shift,
                    operands={"source": half},
                    results={"dst": high_bytes},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"bytes": 8},
                ),
                _op_emit(
                    descriptor=widen,
                    operands={"source": half},
                    results={"dst": low_words},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=widen,
                    operands={"source": high_bytes},
                    results={"dst": high_words},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=multiply,
                    operands={"lhs": low_words, "rhs": step_words},
                    results={"dst": low_products},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=multiply,
                    operands={"lhs": high_words, "rhs": step_words},
                    results={"dst": high_products},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=and_,
                    operands={"lhs": low_products, "rhs": byte_mask},
                    results={"dst": low_bytes},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=and_,
                    operands={"lhs": high_products, "rhs": byte_mask},
                    results={"dst": high_bytes_masked},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=pack,
                    operands={"lhs": low_bytes, "rhs": high_bytes_masked},
                    results={"dst": packed},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
        packed_halves.append(packed)
    if vector_bit_width == 128:
        return emits, packed_halves[0]
    zero = descriptor_lookup("x86.avx2.vxorps.zero.ymm")
    insert_half = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
    empty = ValueRef.temporary("empty_offsets")
    with_low = ValueRef.temporary("offsets_with_low")
    result = ValueRef.temporary("scaled_offsets")
    emits.extend(
        (
            _op_emit(
                descriptor=zero,
                results={"dst": empty},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=insert_half,
                operands={"dest": empty, "value": packed_halves[0]},
                results={"dst": with_low},
                result_types={"dst": DescriptorResultType()},
                immediates={"lane": 0},
            ),
            _op_emit(
                descriptor=insert_half,
                operands={"dest": with_low, "value": packed_halves[1]},
                results={"dst": result},
                result_types={"dst": DescriptorResultType()},
                immediates={"lane": 1},
            ),
        )
    )
    return emits, result


def _iota_scale_i64_emits(
    seed: ValueRef,
    *,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[list[EmitDescriptorOp], ValueRef]:
    emits, steps = _iota_splat_emits(
        ValueRef.operand("step"),
        element_bit_width=64,
        vector_bit_width=vector_bit_width,
        name="steps",
        descriptor_lookup=descriptor_lookup,
    )
    zero_descriptor = descriptor_lookup(
        f"x86.avx2.vxorps.zero.{_REGISTER_SUFFIXES[vector_bit_width]}"
    )
    subtract = descriptor_lookup(
        f"x86.avx2.vpsubq.{_REGISTER_SUFFIXES[vector_bit_width]}"
    )
    and_ = descriptor_lookup(f"x86.avx2.vpand.{_REGISTER_SUFFIXES[vector_bit_width]}")
    add = descriptor_lookup(f"x86.avx2.vpaddq.{_REGISTER_SUFFIXES[vector_bit_width]}")
    zero = ValueRef.temporary("zero_offsets")
    low_mask = ValueRef.temporary("low_lane_mask")
    if vector_bit_width == 128:
        emits.extend(
            (
                _op_emit(
                    descriptor=zero_descriptor,
                    results={"dst": zero},
                    result_types={"dst": DescriptorResultType()},
                ),
                _op_emit(
                    descriptor=subtract,
                    operands={"lhs": zero, "rhs": seed},
                    results={"dst": low_mask},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
        result = ValueRef.temporary("scaled_offsets")
        emits.append(
            _op_emit(
                descriptor=and_,
                operands={"lhs": steps, "rhs": low_mask},
                results={"dst": result},
                result_types={"dst": DescriptorResultType()},
            )
        )
        return emits, result

    move_imm = descriptor_lookup("x86.scalar.movimm.gpr64")
    one = ValueRef.temporary("one")
    emits.append(
        EmitDescriptorOp(
            descriptor=move_imm,
            results={"dst": one},
            result_types={"dst": _I64},
            immediates={"imm64": 1},
            form=DescriptorEmitForm.CONST,
        )
    )
    one_emits, ones = _iota_splat_emits(
        one,
        element_bit_width=64,
        vector_bit_width=256,
        name="ones",
        descriptor_lookup=descriptor_lookup,
    )
    emits.extend(one_emits)
    shift = descriptor_lookup("x86.avx2.vpsrlq.ymm")
    low_bits = ValueRef.temporary("low_lane_bits")
    high_bits = ValueRef.temporary("high_lane_bits")
    high_mask = ValueRef.temporary("high_lane_mask")
    low_offsets = ValueRef.temporary("low_offsets")
    doubled_steps = ValueRef.temporary("doubled_steps")
    high_offsets = ValueRef.temporary("high_offsets")
    result = ValueRef.temporary("scaled_offsets")
    emits.extend(
        (
            _op_emit(
                descriptor=zero_descriptor,
                results={"dst": zero},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=and_,
                operands={"lhs": seed, "rhs": ones},
                results={"dst": low_bits},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=subtract,
                operands={"lhs": zero, "rhs": low_bits},
                results={"dst": low_mask},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=shift,
                operands={"source": seed},
                results={"dst": high_bits},
                result_types={"dst": DescriptorResultType()},
                immediates={"shift": 1},
            ),
            _op_emit(
                descriptor=subtract,
                operands={"lhs": zero, "rhs": high_bits},
                results={"dst": high_mask},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=and_,
                operands={"lhs": steps, "rhs": low_mask},
                results={"dst": low_offsets},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=add,
                operands={"lhs": steps, "rhs": steps},
                results={"dst": doubled_steps},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=and_,
                operands={"lhs": doubled_steps, "rhs": high_mask},
                results={"dst": high_offsets},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=add,
                operands={"lhs": low_offsets, "rhs": high_offsets},
                results={"dst": result},
                result_types={"dst": DescriptorResultType()},
            ),
        )
    )
    return emits, result


def _unique_emit_descriptors(
    emits: Iterable[EmitDescriptorOp],
) -> tuple[Descriptor, ...]:
    descriptors: dict[str, Descriptor] = {}
    for emit in emits:
        descriptors.setdefault(emit.descriptor.key, emit.descriptor)
    return tuple(descriptors.values())


def _integer_iota_rule(
    element: VectorElement,
    vector_bit_width: int,
    *,
    unit_step: bool,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    emits, offsets = _iota_seed_emits(
        element.bit_width, vector_bit_width, descriptor_lookup
    )
    if not unit_step:
        if element.bit_width == 8:
            scale_emits, offsets = _iota_scale_i8_emits(
                offsets,
                vector_bit_width=vector_bit_width,
                descriptor_lookup=descriptor_lookup,
            )
            emits.extend(scale_emits)
        elif element.bit_width in (16, 32):
            step_emits, steps = _iota_splat_emits(
                ValueRef.operand("step"),
                element_bit_width=element.bit_width,
                vector_bit_width=vector_bit_width,
                name="steps",
                descriptor_lookup=descriptor_lookup,
            )
            emits.extend(step_emits)
            multiply = descriptor_lookup(
                f"x86.avx2.vpmull{'w' if element.bit_width == 16 else 'd'}."
                f"{_REGISTER_SUFFIXES[vector_bit_width]}"
            )
            scaled_offsets = ValueRef.temporary("scaled_offsets")
            emits.append(
                _op_emit(
                    descriptor=multiply,
                    operands={"lhs": offsets, "rhs": steps},
                    results={"dst": scaled_offsets},
                    result_types={"dst": DescriptorResultType()},
                )
            )
            offsets = scaled_offsets
        else:
            scale_emits, offsets = _iota_scale_i64_emits(
                offsets,
                vector_bit_width=vector_bit_width,
                descriptor_lookup=descriptor_lookup,
            )
            emits.extend(scale_emits)
    base_emits, bases = _iota_splat_emits(
        ValueRef.operand("base"),
        element_bit_width=element.bit_width,
        vector_bit_width=vector_bit_width,
        name="bases",
        descriptor_lookup=descriptor_lookup,
    )
    emits.extend(base_emits)
    suffix = {8: "b", 16: "w", 32: "d", 64: "q"}[element.bit_width]
    add = descriptor_lookup(
        f"x86.avx2.vpadd{suffix}.{_REGISTER_SUFFIXES[vector_bit_width]}"
    )
    emits.append(
        _op_emit(
            descriptor=add,
            operands={"lhs": bases, "rhs": offsets},
            results={"dst": ValueRef.result("result")},
        )
    )
    descriptors = _unique_emit_descriptors(emits)
    return DescriptorRule(
        source_op=vector.vector_iota,
        descriptor=add,
        guards=(
            Guard.value_type("base", Scalar(element.name)),
            Guard.value_type("step", Scalar(element.name)),
            Guard.value_type("result", _full_vector_type(element, vector_bit_width)),
            *((Guard.value_i64_range("step", 1, 1),) if unit_step else ()),
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in descriptors
                if descriptor != add
            ),
        ),
        emit=tuple(emits),
        priority=1 if unit_step else 0,
    )


def avx2_vector_construction_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[ContractCase, ...]:
    return (
        *_vector_zero_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx2",
            vector_bit_widths=AVX2_VECTOR_BIT_WIDTHS,
        ),
        *_uniform_vector_constant_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx2",
            vector_bit_widths=AVX2_VECTOR_BIT_WIDTHS,
            broadcast_source=_IntegerBroadcastSource.XMM_LOW_LANE,
        ),
        *_vector_splat_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx2",
            integer_vector_bit_widths=AVX2_VECTOR_BIT_WIDTHS,
            float_vector_bit_widths=AVX2_VECTOR_BIT_WIDTHS,
            broadcast_source=_IntegerBroadcastSource.XMM_LOW_LANE,
        ),
        *_vector_bitcast_alias_rules(AVX2_VECTOR_BIT_WIDTHS),
        *(
            _integer_iota_rule(
                element,
                vector_bit_width,
                unit_step=unit_step,
                descriptor_lookup=descriptor_lookup,
            )
            for element in INTEGER_ELEMENTS
            for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
            for unit_step in (True, False)
        ),
    )


def avx512_vector_construction_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[ContractCase, ...]:
    return (
        *_vector_zero_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx512",
            vector_bit_widths=AVX512_VECTOR_BIT_WIDTHS,
        ),
        *_uniform_vector_constant_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx512",
            vector_bit_widths=AVX512_DIRECT_BROADCAST_VECTOR_BIT_WIDTHS,
            broadcast_source=_IntegerBroadcastSource.GPR,
            priority=1,
        ),
        *_vector_splat_rules(
            descriptor_lookup,
            descriptor_key_prefix="x86.avx512",
            integer_vector_bit_widths=AVX512_DIRECT_BROADCAST_VECTOR_BIT_WIDTHS,
            float_vector_bit_widths=AVX512_VECTOR_BIT_WIDTHS,
            broadcast_source=_IntegerBroadcastSource.GPR,
            priority=1,
        ),
        *_vector_bitcast_alias_rules(AVX512_VECTOR_BIT_WIDTHS),
    )
