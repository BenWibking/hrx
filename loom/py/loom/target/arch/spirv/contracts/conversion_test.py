# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.target.arch.spirv.contracts.logical_core import (
    SPIRV_LOGICAL_CORE_CONTRACT_FRAGMENT,
)
from loom.target.arch.spirv.ordinary_vector_conversion import (
    NATIVE_VECTOR_SCALAR_CONVERSIONS,
)
from loom.target.contracts import (
    DescriptorResultType,
    DescriptorRule,
    GuardKind,
    ValueRef,
)


def test_numeric_source_rules_cover_native_shapes_and_scalar_singletons() -> None:
    operations = {
        f"vector.{conversion.source_op_key}"
        for conversion in NATIVE_VECTOR_SCALAR_CONVERSIONS
    }
    actual = {}
    for rule in SPIRV_LOGICAL_CORE_CONTRACT_FRAGMENT.cases:
        if (
            not isinstance(rule, DescriptorRule)
            or rule.source_op.name not in operations
        ):
            continue
        patterns = {
            guard.field: guard.type_pattern
            for guard in rule.guards
            if guard.kind is GuardKind.VALUE_TYPE
        }
        source = patterns["input"]
        result = patterns["result"]
        assert source.kind == result.kind == "vector"
        assert source.lanes == result.lanes
        key = (rule.source_op.name, source.element, result.element, source.lanes)
        assert key not in actual
        actual[key] = rule

    expected = {
        (
            f"vector.{row.source_op_key}",
            row.source_type.source_type,
            row.result_type.source_type,
            lane_count,
        ): row
        for row in NATIVE_VECTOR_SCALAR_CONVERSIONS
        for lane_count in (1, 2, 3, 4)
    }
    assert len(expected) == 288
    assert actual.keys() == expected.keys()

    for key, row in expected.items():
        lane_count = key[3]
        prefix = "" if lane_count == 1 else f"v{lane_count}"
        descriptor_key = (
            f"spirv.op_{row.descriptor_suffix}."
            f"{prefix}{row.source_type.suffix}.{prefix}{row.result_type.suffix}"
        )
        rule = actual[key]
        assert rule.descriptor.key == descriptor_key
        assert rule.emit[-1].results == {"dst": ValueRef.result("result")}
        needs_input_view = row.source_op_key in ("extui", "uitofp")
        needs_result_view = row.source_op_key in ("extui", "fptoui")
        assert len(rule.emit) == 1 + needs_input_view + needs_result_view
        conversion = rule.emit[int(needs_input_view)]
        assert conversion.descriptor == rule.descriptor
        if needs_input_view:
            assert conversion.operands == {
                "input": ValueRef.temporary("unsigned_input")
            }
            assert rule.emit[0].result_types == {"dst": DescriptorResultType()}
        else:
            assert conversion.operands == {"input": ValueRef.operand("input")}
        if needs_result_view:
            assert conversion.results == {"dst": ValueRef.temporary("unsigned_result")}
            assert conversion.result_types == {"dst": DescriptorResultType()}
            result_pattern = rule.emit[-1].result_types["dst"]
            assert result_pattern.kind == "vector"
            assert result_pattern.element == row.result_type.source_type
            assert result_pattern.lanes == lane_count
        availability = tuple(
            guard.descriptor.key
            for guard in rule.guards
            if guard.kind is GuardKind.DESCRIPTOR_AVAILABLE
        )
        expected_availability = (descriptor_key,) if row.feature_bits else ()
        assert availability == expected_availability
