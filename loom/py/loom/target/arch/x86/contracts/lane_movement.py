# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AVX2 scalar lane movement contract rules."""

from __future__ import annotations

from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.vector_families import (
    AVX2_LANE_FAMILIES,
    AVX2_VECTOR_BIT_WIDTHS,
)
from loom.target.contracts import (
    AttrProject,
    ContractCase,
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    ValueRef,
    Vector,
)
from loom.target.low_descriptors import Descriptor

_F32 = Scalar("f32")
_F64 = Scalar("f64")
_I64 = Scalar("i64")
_V2F64 = Vector("f64", lanes=2)


def _lane_descriptor_key(mnemonic: str, element_bit_width: int) -> str:
    if mnemonic.startswith("vpextr"):
        return f"x86.avx2.{mnemonic}.gpr{max(32, element_bit_width)}.xmm"
    return f"x86.avx2.{mnemonic}.xmm"


def _lane_extract_rule(
    *,
    element_names: tuple[str, ...],
    element_bit_width: int,
    vector_bit_width: int,
    descriptor_key: str,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lane_count = vector_bit_width // element_bit_width
    half_lane_count = 128 // element_bit_width
    source_type = Vector(element_names, lanes=lane_count)
    result_type = Scalar(element_names)
    descriptor = descriptor_lookup(descriptor_key)
    emits: list[EmitDescriptorOp] = []
    dependencies: list[Descriptor] = []
    source = ValueRef.operand("source")
    lane: AttrProject = AttrProject.i64_array_element("static_indices", element=0)
    if vector_bit_width == 256:
        extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
        dependencies.append(extract_half)
        source = ValueRef.temporary("half")
        lane = AttrProject.i64_array_element_remainder(
            "static_indices", element=0, divisor=half_lane_count
        )
        emits.append(
            _op_emit(
                descriptor=extract_half,
                operands={"source": ValueRef.operand("source")},
                results={"dst": source},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=half_lane_count
                    )
                },
            )
        )
    emits.append(
        _op_emit(
            descriptor=descriptor,
            operands={"source": source},
            results={"dst": ValueRef.result("result")},
            immediates={"lane": lane},
        )
    )
    return DescriptorRule(
        source_op=vector.vector_extract,
        descriptor=descriptor,
        guards=(
            Guard.value_type("source", source_type),
            Guard.value_type("result", result_type),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, 0, lane_count - 1),
            *(Guard.descriptor_available(value) for value in dependencies),
        ),
        emit=tuple(emits),
    )


def _lane_insert_rule(
    *,
    element_names: tuple[str, ...],
    element_bit_width: int,
    vector_bit_width: int,
    descriptor_key: str,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lane_count = vector_bit_width // element_bit_width
    half_lane_count = 128 // element_bit_width
    value_type = Scalar(element_names)
    vector_type = Vector(element_names, lanes=lane_count)
    descriptor = descriptor_lookup(descriptor_key)
    emits: list[EmitDescriptorOp] = []
    dependencies: list[Descriptor] = []
    dest = ValueRef.operand("dest")
    lane: AttrProject = AttrProject.i64_array_element("static_indices", element=0)
    if vector_bit_width == 256:
        extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
        dependencies.append(extract_half)
        dest = ValueRef.temporary("half")
        lane = AttrProject.i64_array_element_remainder(
            "static_indices", element=0, divisor=half_lane_count
        )
        emits.append(
            _op_emit(
                descriptor=extract_half,
                operands={"source": ValueRef.operand("dest")},
                results={"dst": dest},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=half_lane_count
                    )
                },
            )
        )
    inserted = (
        ValueRef.result("result")
        if vector_bit_width == 128
        else ValueRef.temporary("inserted_half")
    )
    emits.append(
        _op_emit(
            descriptor=descriptor,
            operands={"dest": dest, "value": ValueRef.operand("value")},
            results={"dst": inserted},
            result_types=(
                None if vector_bit_width == 128 else {"dst": DescriptorResultType()}
            ),
            immediates={"lane": lane},
        )
    )
    primary_descriptor = descriptor
    if vector_bit_width == 256:
        insert_half = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
        dependencies.append(insert_half)
        primary_descriptor = insert_half
        emits.append(
            _op_emit(
                descriptor=insert_half,
                operands={
                    "dest": ValueRef.operand("dest"),
                    "value": inserted,
                },
                results={"dst": ValueRef.result("result")},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=half_lane_count
                    )
                },
            )
        )
    return DescriptorRule(
        source_op=vector.vector_insert,
        descriptor=primary_descriptor,
        guards=(
            Guard.value_type("value", value_type),
            Guard.value_type("dest", vector_type),
            Guard.value_type("result", vector_type),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, 0, lane_count - 1),
            *(Guard.descriptor_available(value) for value in dependencies),
        ),
        emit=tuple(emits),
    )


def _lane_movement_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    rules: list[DescriptorRule] = []
    for row in AVX2_LANE_FAMILIES:
        extract_key = _lane_descriptor_key(row.extract_mnemonic, row.element_bit_width)
        insert_key = _lane_descriptor_key(row.insert_mnemonic, row.element_bit_width)
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS:
            rules.extend(
                (
                    _lane_extract_rule(
                        element_names=row.element_names,
                        element_bit_width=row.element_bit_width,
                        vector_bit_width=vector_bit_width,
                        descriptor_key=extract_key,
                        descriptor_lookup=descriptor_lookup,
                    ),
                    _lane_insert_rule(
                        element_names=row.element_names,
                        element_bit_width=row.element_bit_width,
                        vector_bit_width=vector_bit_width,
                        descriptor_key=insert_key,
                        descriptor_lookup=descriptor_lookup,
                    ),
                )
            )
    return tuple(rules)


def _insert_f64_rule(lane: int, descriptor_lookup: _DescriptorLookup) -> DescriptorRule:
    descriptor = descriptor_lookup("x86.avx2.vshufpd.xmm")
    # Each output lane comes from a different input. The scalar occupies the
    # low lane, so insertion selects its input order and the retained dest lane.
    return DescriptorRule(
        source_op=vector.vector_insert,
        descriptor=descriptor,
        guards=(
            Guard.value_type("value", _F64),
            Guard.value_type("dest", _V2F64),
            Guard.value_type("result", _V2F64),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, lane, lane),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={
                    "lhs": ValueRef.operand("value" if lane == 0 else "dest"),
                    "rhs": ValueRef.operand("dest" if lane == 0 else "value"),
                },
                results={"dst": ValueRef.result("result")},
                immediates={"control": 2 if lane == 0 else 0},
            ),
        ),
    )


def _floating_extract_rule(
    element_name: str,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    element_bit_width = 32 if element_name == "f32" else 64
    lane_count = vector_bit_width // element_bit_width
    half_lane_count = 128 // element_bit_width
    source_type = Vector(element_name, lanes=lane_count)
    result_type = Scalar(element_name)
    permute = descriptor_lookup(
        "x86.avx2.vpermilps.xmm" if element_name == "f32" else "x86.avx2.vpermilpd.xmm"
    )
    emits: list[EmitDescriptorOp] = []
    dependencies: list[Descriptor] = []
    source = ValueRef.operand("source")
    lane: AttrProject = AttrProject.i64_array_element("static_indices", element=0)
    if vector_bit_width == 256:
        extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
        dependencies.append(extract_half)
        source = ValueRef.temporary("half")
        lane = AttrProject.i64_array_element_remainder(
            "static_indices", element=0, divisor=half_lane_count
        )
        emits.append(
            _op_emit(
                descriptor=extract_half,
                operands={"source": ValueRef.operand("source")},
                results={"dst": source},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=half_lane_count
                    )
                },
            )
        )
    emits.append(
        _op_emit(
            descriptor=permute,
            operands={"source": source},
            results={"dst": ValueRef.result("result")},
            immediates={"control": lane},
        )
    )
    return DescriptorRule(
        source_op=vector.vector_extract,
        descriptor=permute,
        guards=(
            Guard.value_type("source", source_type),
            Guard.value_type("result", result_type),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, 0, lane_count - 1),
            *(Guard.descriptor_available(value) for value in dependencies),
        ),
        emit=tuple(emits),
    )


def _insert_f32_rule(
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lane_count = vector_bit_width // 32
    insert = descriptor_lookup("x86.avx2.vinsertps.xmm")
    emits: list[EmitDescriptorOp] = []
    dependencies: list[Descriptor] = []
    dest = ValueRef.operand("dest")
    lane: AttrProject = AttrProject.i64_array_element(
        "static_indices", element=0, target_bit_offset=4
    )
    if vector_bit_width == 256:
        extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
        dependencies.append(extract_half)
        dest = ValueRef.temporary("half")
        lane = AttrProject.i64_array_element_remainder(
            "static_indices",
            element=0,
            divisor=4,
            target_bit_offset=4,
        )
        emits.append(
            _op_emit(
                descriptor=extract_half,
                operands={"source": ValueRef.operand("dest")},
                results={"dst": dest},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=4
                    )
                },
            )
        )
    inserted = (
        ValueRef.result("result")
        if vector_bit_width == 128
        else ValueRef.temporary("inserted_half")
    )
    emits.append(
        _op_emit(
            descriptor=insert,
            operands={"dest": dest, "value": ValueRef.operand("value")},
            results={"dst": inserted},
            result_types=(
                None if vector_bit_width == 128 else {"dst": DescriptorResultType()}
            ),
            immediates={"control": lane},
        )
    )
    primary_descriptor = insert
    if vector_bit_width == 256:
        insert_half = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
        dependencies.append(insert_half)
        primary_descriptor = insert_half
        emits.append(
            _op_emit(
                descriptor=insert_half,
                operands={
                    "dest": ValueRef.operand("dest"),
                    "value": inserted,
                },
                results={"dst": ValueRef.result("result")},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=4
                    )
                },
            )
        )
    vector_type = Vector("f32", lanes=lane_count)
    return DescriptorRule(
        source_op=vector.vector_insert,
        descriptor=primary_descriptor,
        guards=(
            Guard.value_type("value", _F32),
            Guard.value_type("dest", vector_type),
            Guard.value_type("result", vector_type),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, 0, lane_count - 1),
            *(Guard.descriptor_available(value) for value in dependencies),
        ),
        emit=tuple(emits),
    )


def _insert_f64x4_rule(descriptor_lookup: _DescriptorLookup) -> DescriptorRule:
    extract_half = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
    move_bits = descriptor_lookup("x86.avx2.vmovq.gpr64.xmm")
    insert_lane = descriptor_lookup("x86.avx2.vpinsrq.xmm")
    insert_half = descriptor_lookup("x86.avx2.vinsertf128.ymm.xmm")
    return DescriptorRule(
        source_op=vector.vector_insert,
        descriptor=insert_half,
        guards=(
            Guard.value_type("value", _F64),
            Guard.value_type("dest", Vector("f64", lanes=4)),
            Guard.value_type("result", Vector("f64", lanes=4)),
            Guard.operand_segment_count("indices", 0),
            Guard.i64_array_count("static_indices", 1),
            Guard.i64_array_element_range("static_indices", 0, 0, 3),
            Guard.descriptor_available(extract_half),
            Guard.descriptor_available(move_bits),
            Guard.descriptor_available(insert_lane),
        ),
        emit=(
            _op_emit(
                descriptor=extract_half,
                operands={"source": ValueRef.operand("dest")},
                results={"dst": ValueRef.temporary("half")},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=2
                    )
                },
            ),
            _op_emit(
                descriptor=move_bits,
                operands={"input": ValueRef.operand("value")},
                results={"dst": ValueRef.temporary("value_bits")},
                result_types={"dst": _I64},
            ),
            _op_emit(
                descriptor=insert_lane,
                operands={
                    "dest": ValueRef.temporary("half"),
                    "value": ValueRef.temporary("value_bits"),
                },
                results={"dst": ValueRef.temporary("inserted_half")},
                result_types={"dst": DescriptorResultType()},
                immediates={
                    "lane": AttrProject.i64_array_element_remainder(
                        "static_indices", element=0, divisor=2
                    )
                },
            ),
            _op_emit(
                descriptor=insert_half,
                operands={
                    "dest": ValueRef.operand("dest"),
                    "value": ValueRef.temporary("inserted_half"),
                },
                results={"dst": ValueRef.result("result")},
                immediates={
                    "lane": AttrProject.i64_array_element_quotient(
                        "static_indices", element=0, divisor=2
                    )
                },
            ),
        ),
    )


def avx2_lane_movement_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[ContractCase, ...]:
    return (
        *_lane_movement_rules(descriptor_lookup),
        *(
            _floating_extract_rule(element_name, vector_bit_width, descriptor_lookup)
            for element_name in ("f32", "f64")
            for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        ),
        *(
            _insert_f32_rule(vector_bit_width, descriptor_lookup)
            for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        ),
        _insert_f64_rule(0, descriptor_lookup),
        _insert_f64_rule(1, descriptor_lookup),
        _insert_f64x4_rule(descriptor_lookup),
    )
