# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.target.arch.x86.descriptors import X86_AVX2_DESCRIPTOR_SET
from loom.target.arch.x86.vector_families import (
    AVX2_FLOAT_BINARY_FAMILIES,
    AVX2_FLOAT_COMPARE_MNEMONICS,
    AVX2_FLOAT_FMA_MNEMONICS,
    AVX2_INTEGER_BINARY_FAMILIES,
    AVX2_INTEGER_COMPARE_MNEMONICS,
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    INTEGER_ELEMENTS,
)


def test_avx2_direct_integer_matrix_matches_isa_families() -> None:
    cells = {
        (family.source_operation, family.element.name)
        for family in AVX2_INTEGER_BINARY_FAMILIES
    }
    assert cells == {
        *(
            (operation, element)
            for operation in ("addi", "subi")
            for element in ("i8", "i16", "i32", "i64")
        ),
        *(
            (operation, element)
            for operation in ("minsi", "maxsi", "minui", "maxui")
            for element in ("i8", "i16", "i32")
        ),
        ("muli", "i16"),
        ("muli", "i32"),
        ("shli", "i32"),
        ("shli", "i64"),
        ("shrsi", "i32"),
        ("shrui", "i32"),
        ("shrui", "i64"),
    }


def test_avx2_float_and_compare_matrices_cover_every_native_element() -> None:
    assert {
        (family.source_operation, family.element.name)
        for family in AVX2_FLOAT_BINARY_FAMILIES
    } == {
        (operation, element)
        for operation in ("addf", "subf", "mulf", "divf")
        for element in ("f32", "f64")
    }
    assert set(AVX2_INTEGER_COMPARE_MNEMONICS) == {
        element.name for element in INTEGER_ELEMENTS
    }
    assert set(AVX2_FLOAT_COMPARE_MNEMONICS) == {
        element.name for element in FLOAT_ELEMENTS
    }
    assert set(AVX2_FLOAT_FMA_MNEMONICS) == {element.name for element in FLOAT_ELEMENTS}


def test_avx2_family_rows_materialize_both_register_widths() -> None:
    descriptor_keys = {
        descriptor.key for descriptor in X86_AVX2_DESCRIPTOR_SET.descriptors
    }
    suffixes = {128: "xmm", 256: "ymm"}
    family_mnemonics = {
        family.mnemonic
        for family in (*AVX2_INTEGER_BINARY_FAMILIES, *AVX2_FLOAT_BINARY_FAMILIES)
    }
    family_mnemonics.update(
        mnemonic
        for pair in AVX2_INTEGER_COMPARE_MNEMONICS.values()
        for mnemonic in pair
    )
    family_mnemonics.update(AVX2_FLOAT_COMPARE_MNEMONICS.values())
    family_mnemonics.update(AVX2_FLOAT_FMA_MNEMONICS.values())
    assert {
        f"x86.avx2.{mnemonic}.{suffixes[vector_bit_width]}"
        for mnemonic in family_mnemonics
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    } <= descriptor_keys
