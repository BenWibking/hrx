# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Immutable x86 vector-family rows shared by descriptor and contract generation."""

from __future__ import annotations

from dataclasses import dataclass

AVX2_VECTOR_BIT_WIDTHS = (128, 256)


@dataclass(frozen=True, slots=True)
class VectorElement:
    name: str
    bit_width: int

    def lane_count(self, vector_bit_width: int) -> int:
        if vector_bit_width % self.bit_width:
            raise ValueError(
                f"vector width {vector_bit_width} does not contain whole "
                f"{self.name} lanes"
            )
        return vector_bit_width // self.bit_width


@dataclass(frozen=True, slots=True)
class VectorBinaryFamily:
    source_operation: str
    mnemonic: str
    semantic: str
    element: VectorElement


@dataclass(frozen=True, slots=True)
class VectorLaneFamily:
    """Bit-compatible scalar lane movement shared by source element types."""

    element_bit_width: int
    element_names: tuple[str, ...]
    extract_mnemonic: str
    insert_mnemonic: str


@dataclass(frozen=True, slots=True)
class VectorShuffleFamily:
    """Bit-compatible shuffle strategy shared by source element types."""

    element_bit_width: int
    element_names: tuple[str, ...]


INTEGER_ELEMENTS = (
    VectorElement("i8", 8),
    VectorElement("i16", 16),
    VectorElement("i32", 32),
    VectorElement("i64", 64),
)

FLOAT_ELEMENTS = (
    VectorElement("f32", 32),
    VectorElement("f64", 64),
)

STORAGE_ELEMENTS = (
    VectorElement("f8E4M3", 8),
    VectorElement("f8E5M2", 8),
    VectorElement("f16", 16),
    VectorElement("bf16", 16),
)

AVX2_LANE_FAMILIES = (
    VectorLaneFamily(
        8,
        ("i8", "f8E4M3", "f8E5M2"),
        "vpextrb",
        "vpinsrb",
    ),
    VectorLaneFamily(
        16,
        ("i16", "f16", "bf16"),
        "vpextrw",
        "vpinsrw",
    ),
    VectorLaneFamily(32, ("i32",), "vpextrd", "vpinsrd"),
    VectorLaneFamily(64, ("i64",), "vpextrq", "vpinsrq"),
)

AVX2_SHUFFLE_FAMILIES = (
    VectorShuffleFamily(8, ("i8", "f8E4M3", "f8E5M2")),
    VectorShuffleFamily(16, ("i16", "f16", "bf16")),
    VectorShuffleFamily(32, ("i32", "f32")),
    VectorShuffleFamily(64, ("i64", "f64")),
)

AVX2_INTEGER_BINARY_FAMILIES = (
    *(
        VectorBinaryFamily("addi", f"vpadd{suffix}", "integer.add", element)
        for element, suffix in zip(INTEGER_ELEMENTS, ("b", "w", "d", "q"), strict=True)
    ),
    *(
        VectorBinaryFamily("subi", f"vpsub{suffix}", "integer.sub", element)
        for element, suffix in zip(INTEGER_ELEMENTS, ("b", "w", "d", "q"), strict=True)
    ),
    VectorBinaryFamily("muli", "vpmullw", "integer.mul", INTEGER_ELEMENTS[1]),
    VectorBinaryFamily("muli", "vpmulld", "integer.mul", INTEGER_ELEMENTS[2]),
    *(
        VectorBinaryFamily("minsi", mnemonic, "integer.mins", element)
        for element, mnemonic in zip(
            INTEGER_ELEMENTS[:3], ("vpminsb", "vpminsw", "vpminsd"), strict=True
        )
    ),
    *(
        VectorBinaryFamily("maxsi", mnemonic, "integer.maxs", element)
        for element, mnemonic in zip(
            INTEGER_ELEMENTS[:3], ("vpmaxsb", "vpmaxsw", "vpmaxsd"), strict=True
        )
    ),
    *(
        VectorBinaryFamily("minui", mnemonic, "integer.minu", element)
        for element, mnemonic in zip(
            INTEGER_ELEMENTS[:3], ("vpminub", "vpminuw", "vpminud"), strict=True
        )
    ),
    *(
        VectorBinaryFamily("maxui", mnemonic, "integer.maxu", element)
        for element, mnemonic in zip(
            INTEGER_ELEMENTS[:3], ("vpmaxub", "vpmaxuw", "vpmaxud"), strict=True
        )
    ),
    VectorBinaryFamily("shli", "vpsllvd", "integer.shl", INTEGER_ELEMENTS[2]),
    VectorBinaryFamily("shli", "vpsllvq", "integer.shl", INTEGER_ELEMENTS[3]),
    VectorBinaryFamily("shrsi", "vpsravd", "integer.shrs", INTEGER_ELEMENTS[2]),
    VectorBinaryFamily("shrui", "vpsrlvd", "integer.shru", INTEGER_ELEMENTS[2]),
    VectorBinaryFamily("shrui", "vpsrlvq", "integer.shru", INTEGER_ELEMENTS[3]),
)

AVX2_FLOAT_BINARY_FAMILIES = tuple(
    VectorBinaryFamily(source_operation, f"v{stem}{suffix}", semantic, element)
    for source_operation, stem, semantic in (
        ("addf", "add", "float.add"),
        ("subf", "sub", "float.sub"),
        ("mulf", "mul", "float.mul"),
        ("divf", "div", "float.div"),
    )
    for element, suffix in zip(FLOAT_ELEMENTS, ("ps", "pd"), strict=True)
)

AVX2_SCALAR_FLOAT_BINARY_FAMILIES = tuple(
    VectorBinaryFamily(source_operation, f"v{stem}{suffix}", semantic, element)
    for source_operation, stem, semantic in (
        ("addf", "add", "float.add"),
        ("subf", "sub", "float.sub"),
        ("mulf", "mul", "float.mul"),
        ("divf", "div", "float.div"),
    )
    for element, suffix in zip(FLOAT_ELEMENTS, ("ss", "sd"), strict=True)
)

AVX2_BITWISE_FAMILIES = (
    ("andi", "vpand", "bits.and"),
    ("ori", "vpor", "bits.or"),
    ("xori", "vpxor", "bits.xor"),
)

AVX2_INTEGER_REDUCTION_FAMILIES = (
    *(
        row
        for row in AVX2_INTEGER_BINARY_FAMILIES
        if row.source_operation in ("addi", "muli", "minsi", "maxsi", "minui", "maxui")
    ),
    *(
        VectorBinaryFamily(source_operation, mnemonic, semantic, element)
        for source_operation, mnemonic, semantic in AVX2_BITWISE_FAMILIES
        for element in INTEGER_ELEMENTS
    ),
)

AVX2_INTEGER_COMPARE_MNEMONICS = {
    element.name: (f"vpcmpeq{suffix}", f"vpcmpgt{suffix}")
    for element, suffix in zip(INTEGER_ELEMENTS, ("b", "w", "d", "q"), strict=True)
}

AVX2_FLOAT_COMPARE_MNEMONICS = {
    "f32": "vcmpps",
    "f64": "vcmppd",
}

AVX2_FLOAT_FMA_MNEMONICS = {
    "f32": "vfmadd231ps",
    "f64": "vfmadd231pd",
}

AVX2_SCALAR_FLOAT_FMA_MNEMONICS = {
    "f32": "vfmadd231ss",
    "f64": "vfmadd231sd",
}

AVX2_FLOAT_EXTREMA_OPERATIONS = (
    "minimumf",
    "maximumf",
    "minnumf",
    "maxnumf",
)

AVX2_FLOAT_EXTREMA_MNEMONICS = {
    "minimumf": {"f32": "vminps", "f64": "vminpd"},
    "maximumf": {"f32": "vmaxps", "f64": "vmaxpd"},
    "minnumf": {"f32": "vminps", "f64": "vminpd"},
    "maxnumf": {"f32": "vmaxps", "f64": "vmaxpd"},
}

AVX2_SCALAR_FLOAT_EXTREMA_MNEMONICS = {
    "minimumf": {"f32": "vminss", "f64": "vminsd"},
    "maximumf": {"f32": "vmaxss", "f64": "vmaxsd"},
    "minnumf": {"f32": "vminss", "f64": "vminsd"},
    "maxnumf": {"f32": "vmaxss", "f64": "vmaxsd"},
}

AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS = ("addf", "mulf")

AVX2_FLOAT_REDUCTION_OPERATIONS = (
    *AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS,
    *AVX2_FLOAT_EXTREMA_OPERATIONS,
)

AVX2_PAYLOAD_ELEMENT_NAMES = tuple(
    element.name for element in (*INTEGER_ELEMENTS, *STORAGE_ELEMENTS, *FLOAT_ELEMENTS)
)


def validate_vector_families() -> None:
    keys = [
        (family.source_operation, family.element.name)
        for family in (*AVX2_INTEGER_BINARY_FAMILIES, *AVX2_FLOAT_BINARY_FAMILIES)
    ]
    if len(keys) != len(set(keys)):
        raise ValueError("duplicate AVX2 source-operation and element family")
    if set(AVX2_INTEGER_COMPARE_MNEMONICS) != {
        element.name for element in INTEGER_ELEMENTS
    }:
        raise ValueError(
            "AVX2 integer comparison rows must cover every integer element"
        )
    if set(AVX2_FLOAT_COMPARE_MNEMONICS) != {
        element.name for element in FLOAT_ELEMENTS
    }:
        raise ValueError("AVX2 floating comparison rows must cover every float element")
    if set(AVX2_FLOAT_EXTREMA_MNEMONICS) != set(AVX2_FLOAT_EXTREMA_OPERATIONS):
        raise ValueError("AVX2 packed extrema rows must cover every float semantic")
    if set(AVX2_SCALAR_FLOAT_EXTREMA_MNEMONICS) != set(AVX2_FLOAT_EXTREMA_OPERATIONS):
        raise ValueError("AVX2 scalar extrema rows must cover every float semantic")


validate_vector_families()
