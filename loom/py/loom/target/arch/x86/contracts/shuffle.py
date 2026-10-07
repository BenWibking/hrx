# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AVX2 vector shuffle contract families."""

from __future__ import annotations

from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.vector_families import AVX2_SHUFFLE_FAMILIES
from loom.target.contracts import (
    AttrProject,
    DescriptorEmitForm,
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    TypePattern,
    ValueRef,
    Vector,
)

_I64 = Scalar("i64")


def _immediate_shuffle_rule(
    type_pattern: TypePattern,
    descriptor_key: str,
    lane_count: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    if lane_count not in (2, 4):
        raise ValueError("immediate x86 shuffle requires two or four lanes")
    descriptor = descriptor_lookup(descriptor_key)
    return DescriptorRule(
        source_op=vector.vector_shuffle,
        descriptor=descriptor,
        guards=(
            Guard.value_type("source", type_pattern),
            Guard.value_type("result", type_pattern),
            Guard.i64_array_count("source_lanes", lane_count),
            Guard.i64_array_elements_range("source_lanes", 0, lane_count - 1),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={"source": ValueRef.operand("source")},
                results={"dst": ValueRef.result("result")},
                immediates={
                    "control": AttrProject.i64_array_pack_elements(
                        "source_lanes",
                        element=0,
                        count=lane_count,
                        bit_width=lane_count.bit_length() - 1,
                    )
                },
            ),
        ),
        priority=2,
    )


def _mask_half_emits(
    *,
    output_byte_offset: int,
    bytes_per_lane: int,
    source_byte_offset: int,
    name: str,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[tuple[EmitDescriptorOp, ...], ValueRef]:
    move_immediate = descriptor_lookup("x86.scalar.movimm.gpr64")
    move = descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")
    interleave = descriptor_lookup("x86.avx2.vpunpcklqdq.xmm")
    emits: list[EmitDescriptorOp] = []
    qwords: list[ValueRef] = []
    for chunk_ordinal in range(2):
        chunk_name = f"{name}_qword{chunk_ordinal}"
        bits = ValueRef.temporary(f"{chunk_name}_bits")
        value = ValueRef.temporary(chunk_name)
        emits.extend(
            (
                EmitDescriptorOp(
                    descriptor=move_immediate,
                    results={"dst": bits},
                    result_types={"dst": _I64},
                    immediates={
                        "imm64": AttrProject.i64_array_shuffle_mask_chunk(
                            "source_lanes",
                            output_byte_offset=(output_byte_offset + chunk_ordinal * 8),
                            bytes_per_lane=bytes_per_lane,
                            source_byte_offset=source_byte_offset,
                        )
                    },
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
        qwords.append(value)
    mask = ValueRef.temporary(name)
    emits.append(
        _op_emit(
            descriptor=interleave,
            operands={"lhs": qwords[0], "rhs": qwords[1]},
            results={"dst": mask},
            result_types={"dst": DescriptorResultType()},
        )
    )
    return tuple(emits), mask


def _xmm_byte_shuffle_rule(
    *,
    element_names: tuple[str, ...],
    bytes_per_lane: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lane_count = 16 // bytes_per_lane
    type_pattern = Vector(element_names, lanes=lane_count)
    shuffle = descriptor_lookup("x86.avx2.vpshufb.xmm")
    mask_emits, mask = _mask_half_emits(
        output_byte_offset=0,
        bytes_per_lane=bytes_per_lane,
        source_byte_offset=0,
        name="mask",
        descriptor_lookup=descriptor_lookup,
    )
    return DescriptorRule(
        source_op=vector.vector_shuffle,
        descriptor=shuffle,
        guards=(
            Guard.value_type("source", type_pattern),
            Guard.value_type("result", type_pattern),
            Guard.i64_array_count("source_lanes", lane_count),
            Guard.i64_array_elements_range("source_lanes", 0, lane_count - 1),
            Guard.descriptor_available(descriptor_lookup("x86.scalar.movimm.gpr64")),
            Guard.descriptor_available(descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")),
            Guard.descriptor_available(descriptor_lookup("x86.avx2.vpunpcklqdq.xmm")),
        ),
        emit=(
            *mask_emits,
            _op_emit(
                descriptor=shuffle,
                operands={
                    "lhs": ValueRef.operand("source"),
                    "rhs": mask,
                },
                results={"dst": ValueRef.result("result")},
            ),
        ),
        priority=1,
    )


def _ymm_dword_shuffle_rule(
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    move_immediate = descriptor_lookup("x86.scalar.movimm.gpr64")
    move = descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")
    interleave = descriptor_lookup("x86.avx2.vpunpcklqdq.xmm")
    zero = descriptor_lookup("x86.avx2.vxorps.zero.ymm")
    insert = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
    shuffle = descriptor_lookup("x86.avx2.vpermps.ymm")

    emits: list[EmitDescriptorOp] = []
    qwords: list[ValueRef] = []
    for chunk_ordinal in range(4):
        bits = ValueRef.temporary(f"control_qword{chunk_ordinal}_bits")
        value = ValueRef.temporary(f"control_qword{chunk_ordinal}")
        emits.extend(
            (
                EmitDescriptorOp(
                    descriptor=move_immediate,
                    results={"dst": bits},
                    result_types={"dst": _I64},
                    immediates={
                        "imm64": AttrProject.i64_array_pack_elements(
                            "source_lanes",
                            element=chunk_ordinal * 2,
                            count=2,
                            bit_width=32,
                        )
                    },
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
        qwords.append(value)

    halves = []
    for half_ordinal in range(2):
        half = ValueRef.temporary(f"control_half{half_ordinal}")
        emits.append(
            _op_emit(
                descriptor=interleave,
                operands={
                    "lhs": qwords[half_ordinal * 2],
                    "rhs": qwords[half_ordinal * 2 + 1],
                },
                results={"dst": half},
                result_types={"dst": DescriptorResultType()},
            )
        )
        halves.append(half)

    empty = ValueRef.temporary("empty_control")
    with_low = ValueRef.temporary("control_with_low")
    control = ValueRef.temporary("control")
    emits.extend(
        (
            _op_emit(
                descriptor=zero,
                results={"dst": empty},
                result_types={"dst": DescriptorResultType()},
            ),
            _op_emit(
                descriptor=insert,
                operands={"dest": empty, "value": halves[0]},
                results={"dst": with_low},
                result_types={"dst": DescriptorResultType()},
                immediates={"lane": 0},
            ),
            _op_emit(
                descriptor=insert,
                operands={"dest": with_low, "value": halves[1]},
                results={"dst": control},
                result_types={"dst": DescriptorResultType()},
                immediates={"lane": 1},
            ),
            _op_emit(
                descriptor=shuffle,
                operands={"control": control, "source": ValueRef.operand("source")},
                results={"dst": ValueRef.result("result")},
            ),
        )
    )
    type_pattern = Vector(("i32", "f32"), lanes=8)
    return DescriptorRule(
        source_op=vector.vector_shuffle,
        descriptor=shuffle,
        guards=(
            Guard.value_type("source", type_pattern),
            Guard.value_type("result", type_pattern),
            Guard.i64_array_count("source_lanes", 8),
            Guard.i64_array_elements_range("source_lanes", 0, 7),
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in (move_immediate, move, interleave, zero, insert)
            ),
        ),
        emit=tuple(emits),
        priority=2,
    )


def _ymm_byte_shuffle_rule(
    *,
    element_names: tuple[str, ...],
    bytes_per_lane: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lane_count = 32 // bytes_per_lane
    type_pattern = Vector(element_names, lanes=lane_count)
    duplicate_half = descriptor_lookup("x86.avx2.vpermq.ymm")
    shuffle = descriptor_lookup("x86.avx2.vpshufb.ymm")
    combine = descriptor_lookup("x86.avx2.vpor.ymm")
    zero = descriptor_lookup("x86.avx2.vxorps.zero.ymm")
    insert = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
    move_immediate = descriptor_lookup("x86.scalar.movimm.gpr64")
    move = descriptor_lookup("x86.avx2.vmovq.xmm.gpr64")
    interleave = descriptor_lookup("x86.avx2.vpunpcklqdq.xmm")

    emits: list[EmitDescriptorOp] = []
    duplicated_sources = []
    for source_half in range(2):
        source_ref = ValueRef.temporary(f"source_half{source_half}")
        emits.append(
            _op_emit(
                descriptor=duplicate_half,
                operands={"source": ValueRef.operand("source")},
                results={"dst": source_ref},
                result_types={"dst": DescriptorResultType()},
                immediates={"control": 0x44 if source_half == 0 else 0xEE},
            )
        )
        duplicated_sources.append(source_ref)

    empty = ValueRef.temporary("empty")
    emits.append(
        _op_emit(
            descriptor=zero,
            results={"dst": empty},
            result_types={"dst": DescriptorResultType()},
        )
    )
    shuffled_sources = []
    for source_half in range(2):
        mask_halves = []
        for output_half in range(2):
            mask_emits, mask_half = _mask_half_emits(
                output_byte_offset=output_half * 16,
                bytes_per_lane=bytes_per_lane,
                source_byte_offset=source_half * 16,
                name=f"mask{output_half}_{source_half}",
                descriptor_lookup=descriptor_lookup,
            )
            emits.extend(mask_emits)
            mask_halves.append(mask_half)
        with_low = ValueRef.temporary(f"mask{source_half}_with_low")
        full_mask = ValueRef.temporary(f"mask{source_half}")
        shuffled = ValueRef.temporary(f"shuffled{source_half}")
        emits.extend(
            (
                _op_emit(
                    descriptor=insert,
                    operands={"dest": empty, "value": mask_halves[0]},
                    results={"dst": with_low},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"lane": 0},
                ),
                _op_emit(
                    descriptor=insert,
                    operands={"dest": with_low, "value": mask_halves[1]},
                    results={"dst": full_mask},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"lane": 1},
                ),
                _op_emit(
                    descriptor=shuffle,
                    operands={"lhs": duplicated_sources[source_half], "rhs": full_mask},
                    results={"dst": shuffled},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
        shuffled_sources.append(shuffled)
    emits.append(
        _op_emit(
            descriptor=combine,
            operands={"lhs": shuffled_sources[0], "rhs": shuffled_sources[1]},
            results={"dst": ValueRef.result("result")},
        )
    )
    return DescriptorRule(
        source_op=vector.vector_shuffle,
        descriptor=shuffle,
        guards=(
            Guard.value_type("source", type_pattern),
            Guard.value_type("result", type_pattern),
            Guard.i64_array_count("source_lanes", lane_count),
            Guard.i64_array_elements_range("source_lanes", 0, lane_count - 1),
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in (
                    duplicate_half,
                    combine,
                    zero,
                    insert,
                    move_immediate,
                    move,
                    interleave,
                )
            ),
        ),
        emit=tuple(emits),
        priority=1,
    )


def avx2_shuffle_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    byte_lane_rules = tuple(
        rule
        for family in AVX2_SHUFFLE_FAMILIES
        if family.element_bit_width in (8, 16)
        for rule in (
            _xmm_byte_shuffle_rule(
                element_names=family.element_names,
                bytes_per_lane=family.element_bit_width // 8,
                descriptor_lookup=descriptor_lookup,
            ),
            _ymm_byte_shuffle_rule(
                element_names=family.element_names,
                bytes_per_lane=family.element_bit_width // 8,
                descriptor_lookup=descriptor_lookup,
            ),
        )
    )
    return (
        _immediate_shuffle_rule(
            Vector(("i32", "f32"), lanes=4),
            "x86.avx2.vpshufd.xmm",
            4,
            descriptor_lookup,
        ),
        _immediate_shuffle_rule(
            Vector(("i64", "f64"), lanes=2),
            "x86.avx2.vpermilpd.xmm",
            2,
            descriptor_lookup,
        ),
        _immediate_shuffle_rule(
            Vector(("i64", "f64"), lanes=4),
            "x86.avx2.vpermq.ymm",
            4,
            descriptor_lookup,
        ),
        _ymm_dword_shuffle_rule(descriptor_lookup),
        *byte_lane_rules,
    )
