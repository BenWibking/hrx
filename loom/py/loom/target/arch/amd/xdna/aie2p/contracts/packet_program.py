# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""AIE2P lane-wise descriptor programs over one vector packet."""

from __future__ import annotations

from typing import Literal

from loom.target.arch.amd.xdna.aie2p.core_descriptors import (
    AIE2P_CORE_DESCRIPTOR_SET,
)
from loom.target.contracts import (
    ContractEmit,
    DescriptorEmitForm,
    DescriptorResultType,
    EmitDescriptorOp,
    ValueProject,
    ValueRef,
    descriptor_by_key,
)
from loom.target.low_descriptors import Descriptor

IntegerSignedness = Literal["signed", "unsigned"]


def _descriptor(key: str) -> Descriptor:
    return descriptor_by_key(AIE2P_CORE_DESCRIPTOR_SET, key)


class PacketProgram:
    """Builds lane-wise descriptor programs over one X carrier."""

    def __init__(self, element_bits: int, temporary_prefix: str = "") -> None:
        if element_bits not in (16, 32):
            raise ValueError("packet program element width must be 16 or 32")
        self.emits: list[ContractEmit] = []
        self.element_bits = element_bits
        self.temporary_prefix = temporary_prefix
        # Constants already materialized in the shift-register class.
        self.shift_values: dict[int, ValueRef] = {}
        # Last emitted value of each persistent instruction-state field.
        self.state_values: dict[str, int] = {}

    def temporary(self, name: str) -> ValueRef:
        return ValueRef.temporary(f"{self.temporary_prefix}{name}")

    def constant(
        self,
        name: str,
        value: int | ValueProject,
        *,
        descriptor_key: str | None = None,
    ) -> ValueRef:
        if descriptor_key is None:
            descriptor_key = (
                "amd.xdna.aie2p.constant.i32.short"
                if isinstance(value, int) and -1024 <= value <= 1023
                else "amd.xdna.aie2p.constant.i32"
            )
        result = self.temporary(name)
        self.emits.append(
            EmitDescriptorOp(
                descriptor=_descriptor(descriptor_key),
                results={"dst": result},
                result_types={"dst": DescriptorResultType()},
                immediates={"i": value},
                form=DescriptorEmitForm.CONST,
            )
        )
        return result

    def operation(
        self,
        name: str | None,
        descriptor_key: str,
        result_field: str,
        *,
        immediates: dict[str, int] | None = None,
        copy_operands: tuple[str, ...] = (),
        **operands: ValueRef,
    ) -> ValueRef:
        result = ValueRef.result("result") if name is None else self.temporary(name)
        self.emits.append(
            EmitDescriptorOp(
                descriptor=_descriptor(f"amd.xdna.aie2p.{descriptor_key}"),
                operands=operands,
                results={result_field: result},
                result_types=(
                    None if name is None else {result_field: DescriptorResultType()}
                ),
                immediates={} if immediates is None else immediates,
                form=DescriptorEmitForm.OP,
                copy_operands=copy_operands,
            )
        )
        return result

    def state(self, descriptor_key: str, value: int) -> None:
        """Sets one instruction-state field consumed by later descriptors."""

        if self.state_values.get(descriptor_key) == value:
            return
        self.emits.append(
            EmitDescriptorOp(
                descriptor=_descriptor(
                    f"amd.xdna.aie2p.state.{descriptor_key}.immediate"
                ),
                immediates={"i": value},
                form=DescriptorEmitForm.OP,
            )
        )
        self.state_values[descriptor_key] = value

    def splat(
        self,
        name: str,
        value: int | ValueProject,
        *,
        element_bits: int | None = None,
        descriptor_key: str | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        scalar = self.constant(f"{name}_scalar", value, descriptor_key=descriptor_key)
        return self.operation(
            name,
            f"splat.i{element_bits}x{512 // element_bits}",
            "dst",
            src=scalar,
        )

    def shift(self, value: int) -> ValueRef:
        """Returns one shared shift-register constant for |value|."""

        shift = self.shift_values.get(value)
        if shift is None:
            shift = self.constant(
                f"shift_{value}",
                value,
                descriptor_key="amd.xdna.aie2p.constant.i32.shift",
            )
            self.shift_values[value] = shift
        return shift

    def binary(
        self,
        name: str,
        descriptor_key: str,
        lhs: ValueRef,
        rhs: ValueRef,
    ) -> ValueRef:
        return self.operation(name, descriptor_key, "d", s1=lhs, s2=rhs)

    def _complete_comparison(
        self,
        name: str,
        descriptor_key: str,
        **operands: ValueRef,
    ) -> ValueRef:
        """Completes one low-half comparison into an X-sized predicate."""

        low = self.operation(f"{name}_low", descriptor_key, "cmp", **operands)
        result = self.temporary(name)
        self.emits.append(
            EmitDescriptorOp(
                descriptor=_descriptor("amd.xdna.aie2p.predicate.complete.zero.high32"),
                operands={"storage": low},
                results={"dst": result},
                result_types={"dst": DescriptorResultType()},
                immediates={"i": 0},
                form=DescriptorEmitForm.OP,
            )
        )
        return result

    def compare_zero(
        self,
        name: str,
        value: ValueRef,
        *,
        element_bits: int | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        return self._complete_comparison(
            name,
            f"cmp.eqz.i{element_bits}x{512 // element_bits}.el.low32",
            s2=value,
        )

    def compare_unsigned_less_than(
        self,
        name: str,
        lhs: ValueRef,
        rhs: ValueRef,
        *,
        element_bits: int | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        return self._complete_comparison(
            name,
            f"cmp.lt.unsigned.i{element_bits}x{512 // element_bits}.el.low32",
            s1=lhs,
            s2=rhs,
        )

    def compare_unsigned_greater_equal(
        self,
        name: str,
        lhs: ValueRef,
        rhs: ValueRef,
        *,
        element_bits: int | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        return self._complete_comparison(
            name,
            f"cmp.ge.unsigned.i{element_bits}x{512 // element_bits}.el.low32",
            s1=lhs,
            s2=rhs,
        )

    def compare_signed_less_than(
        self,
        name: str,
        lhs: ValueRef,
        rhs: ValueRef,
        *,
        element_bits: int | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        return self._complete_comparison(
            name,
            f"cmp.lt.signed.i{element_bits}x{512 // element_bits}.el.low32",
            s1=lhs,
            s2=rhs,
        )

    def select(
        self,
        name: str | None,
        true_value: ValueRef,
        false_value: ValueRef,
        condition: ValueRef,
        *,
        element_bits: int | None = None,
    ) -> ValueRef:
        if element_bits is None:
            element_bits = self.element_bits
        return self.operation(
            name,
            f"select.i{element_bits}x{512 // element_bits}.mask64",
            "d",
            s1=false_value,
            s2=true_value,
            sel=condition,
        )


def shift_i32_packet(
    program: PacketProgram,
    name: str,
    source: ValueRef,
    amount: int,
    *,
    signedness: IntegerSignedness = "unsigned",
) -> ValueRef:
    """Shifts one i32 packet by a fixed signed amount without scalar lanes."""

    if not -31 <= amount <= 31:
        raise ValueError("i32 packet shift must fit the native shift interval")
    program.state("saturation", 0)
    program.state("ups-mode", 1)
    program.state("srs-mode", 1)
    program.state("rounding", 0)
    wide = program.operation(
        f"{name}_wide",
        f"widen.2x.x-to-c.{signedness}.configured",
        "dst",
        src=source,
        su=program.shift(max(amount, 0)),
    )
    return program.operation(
        name,
        f"narrow.2x.c-to-x.{signedness}.configured",
        "dst",
        src=wide,
        su=program.shift(max(-amount, 0)),
    )
