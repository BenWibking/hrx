# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Typed native VEX/EVEX facts for x86 descriptor generation."""

from __future__ import annotations

from dataclasses import dataclass, replace
from enum import Enum, IntEnum

from loom.target.low_descriptors import Descriptor, ImmediateKind, OperandRole


class VectorEncodingPrefix(Enum):
    VEX = "vex"
    EVEX = "evex"


class VectorOpcodeMap(IntEnum):
    MAP_0F = 1
    MAP_0F38 = 2
    MAP_0F3A = 3


class VectorRegisterSelector(IntEnum):
    RESULT = 0
    INPUT_0 = 1
    INPUT_1 = 2
    INPUT_2 = 3
    NONE = 4
    FIXED_2 = 5
    FIXED_3 = 6
    FIXED_4 = 7
    FIXED_6 = 8


class VectorEncodingBehavior(IntEnum):
    REGISTERS = 0
    IMMEDIATE = 1
    BLEND_MASK = 2
    EVEX_MASK = 3
    LOAD = 4
    STORE = 5
    RIP_LOAD = 6


VECTOR_ENCODING_FORMAT_MARKER = 1 << 15
_SIMD_REG_CLASSES = frozenset(("x86.xmm", "x86.ymm", "x86.zmm"))
_SIMD_WIDTHS = {"x86.xmm": 128, "x86.ymm": 256, "x86.zmm": 512}


def vector_encoding_recipe(
    reg: VectorRegisterSelector,
    middle: VectorRegisterSelector,
    rm: VectorRegisterSelector,
    behavior: VectorEncodingBehavior = VectorEncodingBehavior.REGISTERS,
    *,
    full_vector_tuple: bool = False,
) -> int:
    """Packs the direct operand selectors consumed by the native encoder."""
    if full_vector_tuple:
        if (
            behavior
            not in (
                VectorEncodingBehavior.LOAD,
                VectorEncodingBehavior.STORE,
            )
            or middle > VectorRegisterSelector.FIXED_4
        ):
            raise ValueError("invalid full-vector memory tuple recipe")
        middle = int(middle) | 8
    return (
        VECTOR_ENCODING_FORMAT_MARKER
        | int(reg)
        | (int(middle) << 4)
        | (int(rm) << 8)
        | (int(behavior) << 12)
    )


def validate_vector_encoding_recipe(encoding_format_id: int) -> None:
    """Validates the packed recipe shared by Python generation and C emission."""
    if not encoding_format_id & VECTOR_ENCODING_FORMAT_MARKER:
        raise ValueError("vector instruction recipe is missing its marker")
    try:
        behavior = VectorEncodingBehavior((encoding_format_id >> 12) & 7)
    except ValueError as error:
        raise ValueError("invalid vector encoding behavior") from error
    selectors = [
        encoding_format_id & 15,
        (encoding_format_id >> 4) & 15,
        (encoding_format_id >> 8) & 15,
    ]
    if behavior in (VectorEncodingBehavior.LOAD, VectorEncodingBehavior.STORE):
        selectors[1] &= 7
    try:
        for selector in selectors:
            VectorRegisterSelector(selector)
    except ValueError as error:
        raise ValueError("invalid vector register selector") from error


@dataclass(frozen=True, slots=True)
class VectorEncoding:
    prefix: VectorEncodingPrefix
    opcode_map: VectorOpcodeMap
    mandatory_prefix: int
    w: int
    opcode: int
    vector_bit_widths: tuple[int, ...] = ()
    fixed_vector_length: int | None = None

    def __post_init__(self) -> None:
        if self.fixed_vector_length is not None:
            if self.vector_bit_widths:
                raise ValueError("fixed vector encoding cannot declare widths")
            self.encoding_id(None)
            return
        if not self.vector_bit_widths or len(self.vector_bit_widths) != len(
            set(self.vector_bit_widths)
        ):
            raise ValueError("vector encoding requires unique supported widths")
        for vector_bit_width in self.vector_bit_widths:
            self.encoding_id(vector_bit_width)

    def supports(self, vector_bit_width: int | None) -> bool:
        return (
            vector_bit_width is None
            if self.fixed_vector_length is not None
            else vector_bit_width in self.vector_bit_widths
        )

    def encoding_id(self, vector_bit_width: int | None) -> int:
        if not 0 <= self.mandatory_prefix <= 3 or self.w not in (0, 1):
            raise ValueError("invalid VEX/EVEX prefix field")
        if not 0 <= self.opcode <= 0xFF:
            raise ValueError("invalid VEX/EVEX opcode")
        if self.fixed_vector_length is not None:
            if vector_bit_width is not None or not 0 <= self.fixed_vector_length <= 2:
                raise ValueError("fixed vector length conflicts with descriptor width")
            vector_length = self.fixed_vector_length
        else:
            vector_lengths = {
                VectorEncodingPrefix.VEX: {128: 0, 256: 1},
                VectorEncodingPrefix.EVEX: {128: 0, 256: 1, 512: 2},
            }[self.prefix]
            if (
                vector_bit_width not in self.vector_bit_widths
                or vector_bit_width not in vector_lengths
            ):
                raise ValueError(
                    f"{self.prefix.value} encoding does not support "
                    f"{vector_bit_width}-bit vectors"
                )
            vector_length = vector_lengths[vector_bit_width]
        return (
            self.opcode
            | (int(self.opcode_map) << 8)
            | (self.mandatory_prefix << 10)
            | (self.w << 12)
            | ((self.prefix == VectorEncodingPrefix.EVEX) << 13)
            | (vector_length << 14)
        )


@dataclass(frozen=True, slots=True)
class VectorOperandShape:
    role: OperandRole
    register_classes: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class VectorImmediateShape:
    kind: ImmediateKind
    bit_width: int


@dataclass(frozen=True, slots=True)
class VectorMachineInstruction:
    descriptor_mnemonic: str
    encoding_mnemonic: str
    encoding_format_id: int
    operands: tuple[VectorOperandShape, ...]
    immediates: tuple[VectorImmediateShape, ...]
    encodings: tuple[VectorEncoding, ...]

    def __post_init__(self) -> None:
        validate_vector_encoding_recipe(self.encoding_format_id)
        if not self.encodings:
            raise ValueError("vector instruction requires encoding variants")
        seen: set[tuple[VectorEncodingPrefix, int | None]] = set()
        for encoding in self.encodings:
            widths = encoding.vector_bit_widths or (None,)
            for vector_bit_width in widths:
                key = (encoding.prefix, vector_bit_width)
                if key in seen:
                    raise ValueError("vector instruction has overlapping variants")
                seen.add(key)

    def bind(self, descriptor: Descriptor, prefix: VectorEncodingPrefix) -> Descriptor:
        """Validates and binds native facts to one concrete descriptor row."""
        if descriptor.mnemonic != self.descriptor_mnemonic:
            raise ValueError(
                f"{descriptor.key}: expected mnemonic {self.descriptor_mnemonic}"
            )
        operand_shapes = vector_descriptor_operand_shapes(descriptor)
        if operand_shapes != self.operands:
            raise ValueError(f"{descriptor.key}: native operand shape mismatch")
        immediate_shapes = vector_descriptor_immediate_shapes(descriptor)
        if immediate_shapes != self.immediates:
            raise ValueError(f"{descriptor.key}: native immediate shape mismatch")
        vector_bit_width = vector_descriptor_bit_width(descriptor)
        variants = tuple(
            item
            for item in self.encodings
            if item.prefix == prefix and item.supports(vector_bit_width)
        )
        if len(variants) != 1:
            raise ValueError(
                f"{descriptor.key}: expected one {prefix.value} native encoding, "
                f"found {len(variants)}"
            )
        return replace(
            descriptor,
            encoding_format_id=self.encoding_format_id,
            encoding_id=variants[0].encoding_id(vector_bit_width),
        )


def vector_descriptor_operand_shapes(
    descriptor: Descriptor,
) -> tuple[VectorOperandShape, ...]:
    return tuple(
        VectorOperandShape(
            operand.role,
            tuple(
                sorted(
                    "x86.simd"
                    if alternative.reg_class in _SIMD_REG_CLASSES
                    else str(alternative.reg_class)
                    for alternative in operand.reg_alts
                )
            ),
        )
        for operand in descriptor.operands
    )


def vector_descriptor_immediate_shapes(
    descriptor: Descriptor,
) -> tuple[VectorImmediateShape, ...]:
    return tuple(
        VectorImmediateShape(immediate.kind, immediate.bit_width)
        for immediate in descriptor.immediates
    )


def vector_descriptor_bit_width(descriptor: Descriptor) -> int | None:
    widths = {
        _SIMD_WIDTHS[alternative.reg_class]
        for operand in descriptor.operands
        for alternative in operand.reg_alts
        if alternative.reg_class in _SIMD_REG_CLASSES
    }
    return max(widths) if widths else None
