# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Packed integer shift contracts for AMD XDNA AIE2P."""

from __future__ import annotations

from dataclasses import dataclass

from loom.dialect.vector import defs as vector
from loom.dsl import Op
from loom.target.arch.amd.xdna.aie2p.core_descriptors import (
    AIE2P_CORE_DESCRIPTOR_SET,
)
from loom.target.contracts import (
    DescriptorEmitForm,
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    ValueProject,
    ValueRef,
    Vector,
    descriptor_by_key,
)
from loom.target.low_descriptors import Descriptor


@dataclass(frozen=True, slots=True)
class IntegerShiftRuleShape:
    """Logical lane interval realized by one physical i32 shift packet."""

    # Native lane count shifted by the physical instruction sequence.
    native_lane_count: int
    # First logical lane count realized by this rule.
    minimum_lane_count: int
    # Last logical lane count realized by this rule.
    maximum_lane_count: int

    def __post_init__(self) -> None:
        if not (
            self.native_lane_count == 16
            and 1
            <= self.minimum_lane_count
            <= self.maximum_lane_count
            <= self.native_lane_count
        ):
            raise ValueError("integer shift logical lane interval is invalid")

    @property
    def vector_type(self) -> Vector:
        """Source-visible packet type interval."""

        if self.minimum_lane_count == self.maximum_lane_count:
            return Vector("i32", lanes=self.minimum_lane_count)
        return Vector(
            "i32",
            minimum_lanes=self.minimum_lane_count,
            maximum_lanes=self.maximum_lane_count,
        )

    @property
    def report_lane_range(self) -> str:
        """Stable logical lane spelling used by compile reports."""

        if self.minimum_lane_count == self.maximum_lane_count:
            return str(self.minimum_lane_count)
        return f"{self.minimum_lane_count}-{self.maximum_lane_count}"


# Uniform i32 shifts widen through one physical sixteen-lane accumulator
# packet. Lanes beyond a partial logical vector remain unobservable.
INTEGER_SHIFT_RULE_SHAPES = (
    IntegerShiftRuleShape(16, 16, 16),
    IntegerShiftRuleShape(16, 1, 15),
)


def _descriptor(key: str) -> Descriptor:
    return descriptor_by_key(AIE2P_CORE_DESCRIPTOR_SET, key)


def _integer_shift_rule(
    source_op: Op, rule_shape: IntegerShiftRuleShape
) -> DescriptorRule:
    """Shifts uniform i32 packets through one accumulator widening."""

    packet = rule_shape.vector_type
    signedness = "signed" if source_op is vector.vector_shrsi else "unsigned"
    widen = _descriptor(f"amd.xdna.aie2p.widen.2x.x-to-c.{signedness}.configured")
    narrow = _descriptor(f"amd.xdna.aie2p.narrow.2x.c-to-x.{signedness}.configured")
    shift_left = source_op is vector.vector_shli
    distance = ValueProject.exact_i64("rhs")
    return DescriptorRule(
        source_op=source_op,
        descriptor=widen,
        guards=(
            *(Guard.value_type(field, packet) for field in ("lhs", "rhs", "result")),
            Guard.value_exact_i64("rhs"),
            Guard.value_i64_range("rhs", 0, 31),
        ),
        emit=(
            *(
                EmitDescriptorOp(
                    descriptor=_descriptor("amd.xdna.aie2p.constant.i32.shift"),
                    results={"dst": ValueRef.temporary(name)},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"i": amount},
                    form=DescriptorEmitForm.CONST,
                )
                for name, amount in (
                    ("upshift", distance if shift_left else 0),
                    ("downshift", 0 if shift_left else distance),
                )
            ),
            # Widen to i64 before shifting. Unsaturated SRS then selects the
            # low i32 bits; floor rounding preserves arithmetic right shift.
            *(
                EmitDescriptorOp(
                    descriptor=_descriptor(f"amd.xdna.aie2p.state.{name}.immediate"),
                    immediates={"i": value},
                    form=DescriptorEmitForm.OP,
                )
                for name, value in (
                    ("saturation", 0),
                    ("ups-mode", 1),
                    ("srs-mode", 1),
                    ("rounding", 0),
                )
            ),
            EmitDescriptorOp(
                descriptor=widen,
                operands={
                    "src": ValueRef.operand("lhs"),
                    "su": ValueRef.temporary("upshift"),
                },
                results={"dst": ValueRef.temporary("wide")},
                result_types={"dst": DescriptorResultType()},
                form=DescriptorEmitForm.OP,
            ),
            EmitDescriptorOp(
                descriptor=narrow,
                operands={
                    "src": ValueRef.temporary("wide"),
                    "su": ValueRef.temporary("downshift"),
                },
                results={"dst": ValueRef.result("result")},
                form=DescriptorEmitForm.OP,
            ),
        ),
        report_key="native_"
        + source_op.name.removeprefix("vector.")
        + f"_i32x{rule_shape.report_lane_range}_uniform",
    )


AIE2P_INTEGER_SHIFT_RULES = tuple(
    _integer_shift_rule(source_op, rule_shape)
    for source_op in (vector.vector_shli, vector.vector_shrui, vector.vector_shrsi)
    for rule_shape in INTEGER_SHIFT_RULE_SHAPES
)
