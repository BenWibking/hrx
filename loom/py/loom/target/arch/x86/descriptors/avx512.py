# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AVX512 descriptor rows and view metadata."""

from __future__ import annotations

from pathlib import Path

from loom.target.arch.x86.vector_families import (
    AVX512_BITWISE_FAMILIES,
    AVX512_DIRECT_BROADCAST_VECTOR_BIT_WIDTHS,
    AVX512_FLOAT_BINARY_FAMILIES,
    AVX512_FLOAT_FMA_MNEMONICS,
    AVX512_INTEGER_BINARY_FAMILIES,
    FLOAT_ELEMENTS,
)
from loom.target.low_descriptors import (
    Descriptor,
    DescriptorFlag,
    DescriptorSet,
    EnumDomain,
    EnumValue,
    InstructionClass,
    IssueUse,
    LatencyKind,
    ModelQuality,
    RegClass,
    RegClassFlag,
    Resource,
    ResourceKind,
    ScheduleClass,
    ScheduleClassFlag,
    SpillSlotSpace,
)

from .avx2 import X86_AVX2_DESCRIPTORS
from .common import (
    _ADDRESS_SCALE_ENUM,
    _DESTRUCTIVE_ACCUMULATOR_CONSTRAINTS,
    _LANE_I32X4_IMMEDIATE,
    _REG_K,
    _REG_XMM,
    _REG_YMM,
    _REG_ZMM,
    _RESOURCE_ADDRESS,
    _RESOURCE_CONTROL,
    _RESOURCE_DOT,
    _RESOURCE_LOAD,
    _RESOURCE_MASK,
    _RESOURCE_SCALAR,
    _RESOURCE_STORE,
    _RESOURCE_VECTOR,
    _SCHEDULE_ADDRESS,
    _SCHEDULE_CONTROL,
    _SCHEDULE_MASK,
    _SCHEDULE_MEMORY_LOAD_GPR32,
    _SCHEDULE_MEMORY_LOAD_GPR64,
    _SCHEDULE_MEMORY_LOAD_XMM,
    _SCHEDULE_MEMORY_LOAD_YMM,
    _SCHEDULE_MEMORY_LOAD_ZMM,
    _SCHEDULE_MEMORY_STORE_GPR32,
    _SCHEDULE_MEMORY_STORE_GPR64,
    _SCHEDULE_MEMORY_STORE_XMM,
    _SCHEDULE_MEMORY_STORE_YMM,
    _SCHEDULE_MEMORY_STORE_ZMM,
    _SCHEDULE_SCALAR,
    _SCHEDULE_VECTOR_COMPARE_XMM,
    _SCHEDULE_VECTOR_COMPARE_YMM,
    _SCHEDULE_VECTOR_COMPARE_ZMM,
    _SCHEDULE_VECTOR_DOT_ZMM,
    _SCHEDULE_VECTOR_F32_XMM,
    _SCHEDULE_VECTOR_F32_YMM,
    _SCHEDULE_VECTOR_F32_ZMM,
    _SCHEDULE_VECTOR_FMA_F32_XMM,
    _SCHEDULE_VECTOR_FMA_F32_YMM,
    _SCHEDULE_VECTOR_FMA_F32_ZMM,
    _SCHEDULE_VECTOR_I32_XMM,
    _SCHEDULE_VECTOR_I32_YMM,
    _SCHEDULE_VECTOR_I32_ZMM,
    _asm,
    _gpr32_operand,
    _gpr64_operand,
    _k_operand,
    _k_result,
    _vector_f32_binary_descriptor,
    _vector_fma_descriptor,
    _vector_i32_binary_descriptor,
    _vector_i32_schedule_class,
    _vector_lane_units,
    _vector_mask_compare_descriptor,
    _vector_mask_select_descriptor,
    _vector_splat_descriptor,
    _vector_zero_descriptor,
    _xmm_operand,
    _xmm_result,
    _zmm_mask_select_descriptor,
    _zmm_operand,
    _zmm_result,
)
from .memory import memory_descriptors
from .scalar import X86_SCALAR_DESCRIPTOR_SET

_REGISTER_SUFFIXES = {128: "xmm", 256: "ymm", 512: "zmm"}


def _direct_broadcast_asm_mnemonic(
    mnemonic: str,
    element_bit_width: int,
    vector_bit_width: int,
) -> str | None:
    if vector_bit_width == 256 or (
        vector_bit_width == 128 and element_bit_width in (8, 16)
    ):
        return f"avx512.{mnemonic}.{_REGISTER_SUFFIXES[vector_bit_width]}"
    return None


X86_AVX512_CORE_DESCRIPTOR_SET = DescriptorSet(
    key="x86.avx512.core",
    target_key="x86",
    feature_key="x86.avx512.v1",
    c_header_path=Path(
        "loom/src/loom/target/arch/x86/descriptors/avx512_descriptors.h"
    ),
    c_source_path=Path("loom/src/loom/target/arch/x86/avx512_descriptors.c"),
    header_guard="LOOM_TARGET_ARCH_X86_AVX512_DESCRIPTORS_H_",
    public_header="loom/target/arch/x86/descriptors/avx512_descriptors.h",
    function_name="loom_x86_avx512_core_descriptor_set",
    c_table_prefix="X86Avx512Core",
    c_enum_prefix="X86_AVX512_CORE",
    generator_version=1,
    supports_native_scheduling=True,
    physical_registers=X86_SCALAR_DESCRIPTOR_SET.physical_registers,
    reg_classes=(
        *X86_SCALAR_DESCRIPTOR_SET.reg_classes,
        RegClass(
            _REG_XMM,
            128,
            SpillSlotSpace.STACK,
            flags=(RegClassFlag.PHYSICAL,),
            allocatable_count=32,
            alias_set_id=1,
        ),
        RegClass(
            _REG_YMM,
            256,
            SpillSlotSpace.STACK,
            flags=(RegClassFlag.PHYSICAL,),
            allocatable_count=32,
            alias_set_id=1,
        ),
        RegClass(
            _REG_ZMM,
            512,
            SpillSlotSpace.STACK,
            flags=(RegClassFlag.PHYSICAL,),
            allocatable_count=32,
            alias_set_id=1,
        ),
        RegClass(
            _REG_K,
            64,
            SpillSlotSpace.STACK,
            flags=(RegClassFlag.PHYSICAL,),
            allocatable_count=8,
        ),
    ),
    resources=(
        Resource(_RESOURCE_SCALAR, capacity_per_cycle=1, kind=ResourceKind.SCALAR_ALU),
        Resource(
            _RESOURCE_VECTOR,
            capacity_per_cycle=4,
            kind=ResourceKind.VECTOR_ALU,
            contention_group_id=1,
        ),
        Resource(
            _RESOURCE_DOT,
            capacity_per_cycle=4,
            kind=ResourceKind.VECTOR_ALU,
            contention_group_id=1,
        ),
        Resource(_RESOURCE_MASK, capacity_per_cycle=1, kind=ResourceKind.SCALAR_ALU),
        Resource(
            _RESOURCE_LOAD,
            capacity_per_cycle=4,
            kind=ResourceKind.LOAD,
            contention_group_id=2,
        ),
        Resource(
            _RESOURCE_STORE,
            capacity_per_cycle=4,
            kind=ResourceKind.STORE,
            contention_group_id=3,
        ),
        Resource(
            _RESOURCE_ADDRESS,
            capacity_per_cycle=1,
            kind=ResourceKind.ADDRESS,
            contention_group_id=4,
        ),
        Resource(_RESOURCE_CONTROL, capacity_per_cycle=1, kind=ResourceKind.CONTROL),
    ),
    schedule_classes=(
        ScheduleClass(
            _SCHEDULE_SCALAR,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_SCALAR, cycles=1, units=1),),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_LOAD_GPR32,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(IssueUse(_RESOURCE_LOAD, cycles=1, units=1),),
            flags=(ScheduleClassFlag.MAY_LOAD,),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_LOAD_GPR64,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(IssueUse(_RESOURCE_LOAD, cycles=1, units=1),),
            flags=(ScheduleClassFlag.MAY_LOAD,),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_STORE_GPR32,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_STORE, cycles=1, units=1),),
            flags=(ScheduleClassFlag.MAY_STORE,),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_STORE_GPR64,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_STORE, cycles=1, units=1),),
            flags=(ScheduleClassFlag.MAY_STORE,),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_I32_XMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(128)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_I32_YMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(256)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_I32_ZMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(512)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_F32_XMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(128)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_F32_YMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(256)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_F32_ZMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(512)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_FMA_F32_XMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(128)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_FMA_F32_YMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(256)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_FMA_F32_ZMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(512)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_COMPARE_XMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(128)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_COMPARE_YMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(256)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_COMPARE_ZMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_VECTOR, cycles=1, units=_vector_lane_units(512)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_VECTOR_DOT_ZMM,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_DOT, cycles=1, units=_vector_lane_units(512)),
            ),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_MASK,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_MASK, cycles=1, units=1),),
            model_quality=ModelQuality.ESTIMATED,
        ),
        ScheduleClass(
            _SCHEDULE_ADDRESS,
            latency_kind=LatencyKind.ESTIMATE,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),),
            model_quality=ModelQuality.ESTIMATED,
            instruction_classes=(InstructionClass.SCALAR_ALU,),
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_LOAD_ZMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_LOAD, cycles=1, units=_vector_lane_units(512)),
            ),
            flags=(ScheduleClassFlag.MAY_LOAD,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_LOAD_XMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_LOAD, cycles=1, units=_vector_lane_units(128)),
            ),
            flags=(ScheduleClassFlag.MAY_LOAD,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_LOAD_YMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=4,
            minimum_issue_separation_cycles=4,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_LOAD, cycles=1, units=_vector_lane_units(256)),
            ),
            flags=(ScheduleClassFlag.MAY_LOAD,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_STORE_ZMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_STORE, cycles=1, units=_vector_lane_units(512)),
            ),
            flags=(ScheduleClassFlag.MAY_STORE,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_STORE_XMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_STORE, cycles=1, units=_vector_lane_units(128)),
            ),
            flags=(ScheduleClassFlag.MAY_STORE,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_MEMORY_STORE_YMM,
            latency_kind=LatencyKind.VARIABLE,
            latency_cycles=1,
            issue_uses=(
                IssueUse(_RESOURCE_ADDRESS, cycles=1, units=1),
                IssueUse(_RESOURCE_STORE, cycles=1, units=_vector_lane_units(256)),
            ),
            flags=(ScheduleClassFlag.MAY_STORE,),
            model_quality=ModelQuality.FALLBACK,
        ),
        ScheduleClass(
            _SCHEDULE_CONTROL,
            latency_kind=LatencyKind.EXACT,
            latency_cycles=1,
            issue_uses=(IssueUse(_RESOURCE_CONTROL, cycles=1, units=1),),
            flags=(ScheduleClassFlag.CONTROL,),
            model_quality=ModelQuality.EXACT,
        ),
    ),
    enum_domains=(
        EnumDomain(
            _ADDRESS_SCALE_ENUM,
            (
                EnumValue("1", 1),
                EnumValue("2", 2),
                EnumValue("4", 4),
                EnumValue("8", 8),
            ),
        ),
    ),
    descriptors=(
        *X86_AVX2_DESCRIPTORS,
        Descriptor(
            key="x86.avx512.vextractf32x4.xmm.zmm",
            mnemonic="vextractf32x4",
            semantic_tag="float.extract.f32x16.quarter",
            operands=(_xmm_result(), _zmm_operand("source")),
            immediates=(_LANE_I32X4_IMMEDIATE,),
            asm_forms=_asm(
                mnemonic="vextractf32x4.xmm",
                results=("dst",),
                operands=("source",),
                immediates=("lane",),
            ),
            schedule_class=_SCHEDULE_VECTOR_F32_ZMM,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=128,
            key="x86.avx512.vpcmpd.xmm",
            mnemonic="vpcmpd",
            semantic_tag="integer.cmp.signed.i32x4",
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=128,
            key="x86.avx512.vpcmpud.xmm",
            mnemonic="vpcmpud",
            semantic_tag="integer.cmp.unsigned.i32x4",
        ),
        _vector_mask_select_descriptor(
            vector_bit_width=128,
            key="x86.avx512.vpblendmd.xmm",
            mnemonic="vpblendmd",
            semantic_tag="integer.select.i32x4",
            schedule_class=_SCHEDULE_VECTOR_I32_XMM,
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=128,
            key="x86.avx512.vcmpps.xmm",
            mnemonic="vcmpps",
            semantic_tag="float.cmp.f32x4",
        ),
        _vector_mask_select_descriptor(
            vector_bit_width=128,
            key="x86.avx512.vblendmps.xmm",
            mnemonic="vblendmps",
            semantic_tag="float.select.f32x4",
            schedule_class=_SCHEDULE_VECTOR_F32_XMM,
        ),
        *(
            _vector_splat_descriptor(
                vector_bit_width=vector_bit_width,
                key=(f"x86.avx512.{mnemonic}.{_REGISTER_SUFFIXES[vector_bit_width]}"),
                mnemonic=mnemonic,
                semantic_tag=(f"bits.splat.{element_bit_width}.v{vector_bit_width}"),
                operand=(
                    _gpr64_operand("value")
                    if element_bit_width == 64
                    else _gpr32_operand("value")
                ),
                schedule_class=_vector_i32_schedule_class(vector_bit_width),
                asm_mnemonic=_direct_broadcast_asm_mnemonic(
                    mnemonic,
                    element_bit_width,
                    vector_bit_width,
                ),
            )
            for element_bit_width, mnemonic in (
                (8, "vpbroadcastb"),
                (16, "vpbroadcastw"),
                (32, "vpbroadcastd"),
                (64, "vpbroadcastq"),
            )
            for vector_bit_width in AVX512_DIRECT_BROADCAST_VECTOR_BIT_WIDTHS
        ),
        *(
            _vector_splat_descriptor(
                vector_bit_width=512,
                key=f"x86.avx512.{mnemonic}.zmm",
                mnemonic=mnemonic,
                semantic_tag=(f"float.splat.{element.name}x{element.lane_count(512)}"),
                operand=_xmm_operand("value"),
                schedule_class=_SCHEDULE_VECTOR_F32_ZMM,
            )
            for element, mnemonic in zip(
                FLOAT_ELEMENTS,
                ("vbroadcastss", "vbroadcastsd"),
                strict=True,
            )
        ),
        _vector_zero_descriptor(
            vector_bit_width=512,
            key="x86.avx512.vxorps.zero.zmm",
        ),
        *(
            _vector_i32_binary_descriptor(
                vector_bit_width=512,
                key=f"x86.avx512.{family.mnemonic}.zmm",
                mnemonic=family.mnemonic,
                semantic_tag=(
                    f"{family.semantic}.{family.element.name}x"
                    f"{family.element.lane_count(512)}"
                ),
            )
            for family in AVX512_INTEGER_BINARY_FAMILIES
        ),
        *(
            _vector_i32_binary_descriptor(
                vector_bit_width=512,
                key=f"x86.avx512.{mnemonic}.zmm",
                mnemonic=mnemonic,
                semantic_tag=f"{semantic}.v512",
            )
            for _, mnemonic, semantic in AVX512_BITWISE_FAMILIES
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=512,
            key="x86.avx512.vpcmpd.zmm",
            mnemonic="vpcmpd",
            semantic_tag="integer.cmp.signed.i32x16",
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=512,
            key="x86.avx512.vpcmpud.zmm",
            mnemonic="vpcmpud",
            semantic_tag="integer.cmp.unsigned.i32x16",
        ),
        _zmm_mask_select_descriptor(
            key="x86.avx512.vpblendmd.zmm",
            mnemonic="vpblendmd",
            semantic_tag="integer.select.i32x16",
            schedule_class=_SCHEDULE_VECTOR_I32_ZMM,
        ),
        *(
            _vector_f32_binary_descriptor(
                vector_bit_width=512,
                key=f"x86.avx512.{family.mnemonic}.zmm",
                mnemonic=family.mnemonic,
                semantic_tag=(
                    f"{family.semantic}.{family.element.name}x"
                    f"{family.element.lane_count(512)}"
                ),
            )
            for family in AVX512_FLOAT_BINARY_FAMILIES
        ),
        *(
            _vector_fma_descriptor(
                vector_bit_width=512,
                key=f"x86.avx512.{AVX512_FLOAT_FMA_MNEMONICS[element.name]}.zmm",
                mnemonic=AVX512_FLOAT_FMA_MNEMONICS[element.name],
                semantic_tag=(f"float.fma.{element.name}x{element.lane_count(512)}"),
            )
            for element in FLOAT_ELEMENTS
        ),
        _vector_mask_compare_descriptor(
            vector_bit_width=512,
            key="x86.avx512.vcmpps.zmm",
            mnemonic="vcmpps",
            semantic_tag="float.cmp.f32x16",
        ),
        _zmm_mask_select_descriptor(
            key="x86.avx512.vblendmps.zmm",
            mnemonic="vblendmps",
            semantic_tag="float.select.f32x16",
            schedule_class=_SCHEDULE_VECTOR_F32_ZMM,
        ),
        *memory_descriptors(
            key_prefix="x86.avx512",
            load_mnemonic="vmovdqu32",
            store_mnemonic="vmovdqu32",
            register_class=_REG_ZMM,
            register_suffix="zmm",
            semantic_type="v512",
            width_bits=512,
            load_schedule_class=_SCHEDULE_MEMORY_LOAD_ZMM,
            store_schedule_class=_SCHEDULE_MEMORY_STORE_ZMM,
            assembly_suffix="",
        ),
        Descriptor(
            key="x86.avx512.vpdpbusd.zmm",
            mnemonic="vpdpbusd",
            semantic_tag="dot.u8s8.i32x16",
            operands=(
                _zmm_result(),
                _zmm_operand("acc"),
                _zmm_operand("lhs"),
                _zmm_operand("rhs"),
            ),
            constraints=_DESTRUCTIVE_ACCUMULATOR_CONSTRAINTS,
            asm_forms=_asm(results=("dst",), operands=("acc", "lhs", "rhs")),
            schedule_class=_SCHEDULE_VECTOR_DOT_ZMM,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
        Descriptor(
            key="x86.avx512.vdpbf16ps.zmm",
            mnemonic="vdpbf16ps",
            semantic_tag="dot.bf16.f32x16",
            operands=(
                _zmm_result(),
                _zmm_operand("acc"),
                _zmm_operand("lhs"),
                _zmm_operand("rhs"),
            ),
            constraints=_DESTRUCTIVE_ACCUMULATOR_CONSTRAINTS,
            asm_forms=_asm(results=("dst",), operands=("acc", "lhs", "rhs")),
            schedule_class=_SCHEDULE_VECTOR_DOT_ZMM,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
        Descriptor(
            key="x86.avx512.kandq",
            mnemonic="kandq",
            semantic_tag="mask.and.i64",
            operands=(_k_result(), _k_operand("lhs"), _k_operand("rhs")),
            asm_forms=_asm(results=("dst",), operands=("lhs", "rhs")),
            schedule_class=_SCHEDULE_MASK,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
        Descriptor(
            key="x86.avx512.korq",
            mnemonic="korq",
            semantic_tag="mask.or.i64",
            operands=(_k_result(), _k_operand("lhs"), _k_operand("rhs")),
            asm_forms=_asm(results=("dst",), operands=("lhs", "rhs")),
            schedule_class=_SCHEDULE_MASK,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
        Descriptor(
            key="x86.avx512.kxorq",
            mnemonic="kxorq",
            semantic_tag="mask.xor.i64",
            operands=(_k_result(), _k_operand("lhs"), _k_operand("rhs")),
            asm_forms=_asm(results=("dst",), operands=("lhs", "rhs")),
            schedule_class=_SCHEDULE_MASK,
            flags=(DescriptorFlag.DEAD_REMOVABLE,),
        ),
    ),
)
