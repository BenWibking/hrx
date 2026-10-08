# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Tests for packed integer shift contracts on AMD XDNA AIE2P."""

from loom.dialect.vector import defs as vector
from loom.target.arch.amd.xdna.aie2p.contracts.integer_shift import (
    AIE2P_INTEGER_SHIFT_RULES,
    INTEGER_SHIFT_RULE_SHAPES,
)
from loom.target.contracts import DescriptorRule, EmitDescriptorOp, Guard


def _rule(report_key: str) -> DescriptorRule:
    rules = [
        rule for rule in AIE2P_INTEGER_SHIFT_RULES if rule.report_key == report_key
    ]
    assert len(rules) == 1
    return rules[0]


def test_native_uniform_i32_shifts_cover_the_logical_carrier() -> None:
    covered_lane_counts: set[int] = set()
    for rule_shape in INTEGER_SHIFT_RULE_SHAPES:
        logical_lane_counts = set(
            range(rule_shape.minimum_lane_count, rule_shape.maximum_lane_count + 1)
        )
        assert covered_lane_counts.isdisjoint(logical_lane_counts)
        covered_lane_counts.update(logical_lane_counts)
    assert covered_lane_counts == set(range(1, 17))

    for source_op in (
        vector.vector_shli,
        vector.vector_shrui,
        vector.vector_shrsi,
    ):
        signedness = "signed" if source_op is vector.vector_shrsi else "unsigned"
        for rule_shape in INTEGER_SHIFT_RULE_SHAPES:
            report_key = (
                "native_"
                + source_op.name.removeprefix("vector.")
                + f"_i32x{rule_shape.report_lane_range}_uniform"
            )
            rule = _rule(report_key)
            assert rule.source_op is source_op
            assert rule.guards == (
                *(
                    Guard.value_type(field, rule_shape.vector_type)
                    for field in ("lhs", "rhs", "result")
                ),
                Guard.value_exact_i64("rhs"),
                Guard.value_i64_range("rhs", 0, 31),
            )
            assert [
                emit.descriptor.key
                for emit in rule.emit
                if isinstance(emit, EmitDescriptorOp)
            ] == [
                "amd.xdna.aie2p.constant.i32.shift",
                "amd.xdna.aie2p.constant.i32.shift",
                "amd.xdna.aie2p.state.saturation.immediate",
                "amd.xdna.aie2p.state.ups-mode.immediate",
                "amd.xdna.aie2p.state.srs-mode.immediate",
                "amd.xdna.aie2p.state.rounding.immediate",
                f"amd.xdna.aie2p.widen.2x.x-to-c.{signedness}.configured",
                f"amd.xdna.aie2p.narrow.2x.c-to-x.{signedness}.configured",
            ]
