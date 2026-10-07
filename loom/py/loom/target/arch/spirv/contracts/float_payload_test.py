# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.dialect.scalar import conversion
from loom.dialect.scf import defs as scf
from loom.target.arch.spirv.contracts.bfloat import bfloat_carrier_memory_rules
from loom.target.arch.spirv.contracts.logical_core import (
    SPIRV_LOGICAL_CORE_CONTRACT_FRAGMENT,
)
from loom.target.contracts import DescriptorRule, GuardKind, ValueProjectKind


def _scalar_rules(source_op, field):
    rules = {}
    for rule in SPIRV_LOGICAL_CORE_CONTRACT_FRAGMENT.cases:
        if not isinstance(rule, DescriptorRule) or rule.source_op is not source_op:
            continue
        for guard in rule.guards:
            if guard.kind is not GuardKind.VALUE_TYPE or guard.field != field:
                continue
            pattern = guard.type_pattern
            if pattern.kind != "scalar":
                continue
            for element in pattern.elements:
                rules.setdefault(element, []).append(rule)
    return rules


def test_float_constants_and_selection_cover_native_and_payload_types() -> None:
    constants = _scalar_rules(conversion.scalar_constant, "result")
    selects = _scalar_rules(scf.scf_select, "result")
    expected = {
        "f8E4M3": ("i8",),
        "f8E5M2": ("i8",),
        "f16": ("f16",),
        "bf16": ("bf16", "i32"),
        "f32": ("f32",),
        "f64": ("f64",),
    }
    for source_type, carriers in expected.items():
        # Native rows must precede software rows when both are applicable.
        assert tuple(rule.descriptor.key for rule in selects[source_type]) == tuple(
            f"spirv.op_select.{carrier}" for carrier in carriers
        )
        assert len(constants[source_type]) == len(carriers)
        for rule in constants[source_type]:
            immediate = next(iter(rule.emit[0].immediates.values()))
            assert immediate.kind is ValueProjectKind.FLOAT_BITS


def test_bfloat_memory_uses_existing_bit_preserving_storage_conversions() -> None:
    rules = bfloat_carrier_memory_rules()
    assert len(rules) == 6  # Global and both Workgroup coordinate forms, R/W.
    seen = set()
    for rule in rules:
        memory = next(step for step in rule.emit if step.source_memory is not None)
        constraint = memory.source_memory
        address = memory.source_memory_address_materializer
        seen.add(
            (rule.source_op.name, constraint.memory_spaces, address.coordinate_type)
        )
        assert constraint.element_byte_count == 2
        assert constraint.minimum_alignment == 2
        assert memory.descriptor.key.endswith(".i16")
        # No floating conversion can quiet a NaN or alter its payload.
        assert not any("f_convert" in step.descriptor.key for step in rule.emit)
        if rule.source_op.name == "view.load":
            assert tuple(step.descriptor.key for step in rule.emit[1:]) == (
                "spirv.op_bitcast.i16.u16",
                "spirv.op_u_convert.u16.u32",
                "spirv.op_bitcast.u32.i32",
            )
        else:
            assert rule.emit[0].descriptor.key == "spirv.op_s_convert.i32.i16"
    assert len(seen) == 6
