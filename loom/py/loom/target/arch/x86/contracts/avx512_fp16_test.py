# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from loom.dialect.scalar import arithmetic as scalar_arithmetic
from loom.dialect.scalar import comparison as scalar_comparison
from loom.dialect.scalar import conversion as scalar_conversion
from loom.dialect.scalar import math as scalar_math
from loom.dialect.scf import defs as scf
from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.avx512_fp16 import (
    X86_AVX512_FP16_CONTRACT_FRAGMENT,
)
from loom.target.arch.x86.descriptors import (
    X86_AVX512_FEATURES_DESCRIPTOR_SET,
    X86_AVX512_FP16_DESCRIPTOR_SET,
)
from loom.target.arch.x86.vector_families import (
    AVX512_FP16_FLOAT_BINARY_FAMILIES,
    AVX512_FP16_FLOAT_FMA_MNEMONIC,
    AVX512_FP16_SCALAR_FLOAT_BINARY_FAMILIES,
    AVX512_FP16_SCALAR_FLOAT_FMA_MNEMONIC,
)
from loom.target.contracts import DescriptorRule, GuardKind, Scalar, Vector

_SCALAR_BINARY_OPS = {
    scalar_arithmetic.scalar_addf: "addf",
    scalar_arithmetic.scalar_subf: "subf",
    scalar_arithmetic.scalar_mulf: "mulf",
    scalar_arithmetic.scalar_divf: "divf",
}
_VECTOR_BINARY_OPS = {
    vector.vector_addf: "addf",
    vector.vector_subf: "subf",
    vector.vector_mulf: "mulf",
    vector.vector_divf: "divf",
}
_REGISTER_SUFFIXES = {128: "xmm", 256: "ymm", 512: "zmm"}


def _rules_for(source_op) -> tuple[DescriptorRule, ...]:
    return tuple(
        case
        for case in X86_AVX512_FP16_CONTRACT_FRAGMENT.cases
        if isinstance(case, DescriptorRule) and case.source_op is source_op
    )


def _value_type(rule: DescriptorRule, field: str):
    return next(
        guard.type_pattern
        for guard in rule.guards
        if guard.kind == GuardKind.VALUE_TYPE and guard.field == field
    )


def _register_class(rule: DescriptorRule, field: str) -> str | None:
    return next(
        (
            guard.register_class
            for guard in rule.guards
            if guard.kind == GuardKind.LOW_VALUE_REGISTER_CLASS and guard.field == field
        ),
        None,
    )


def test_feature_view_contains_the_complete_fp16_overlay() -> None:
    feature_keys = {
        descriptor.key for descriptor in X86_AVX512_FEATURES_DESCRIPTOR_SET.descriptors
    }
    assert {
        descriptor.key for descriptor in X86_AVX512_FP16_DESCRIPTOR_SET.descriptors
    } <= feature_keys


def test_fp16_rules_override_the_base_scalar_carrier_rules() -> None:
    assert all(
        isinstance(case, DescriptorRule) and case.priority == 1
        for case in X86_AVX512_FP16_CONTRACT_FRAGMENT.cases
    )


def test_scalar_arithmetic_covers_the_native_fp16_family() -> None:
    expected_binary = {
        (
            getattr(scalar_arithmetic, f"scalar_{row.source_operation}"),
            f"x86.avx512_fp16.{row.mnemonic}.xmm",
        )
        for row in AVX512_FP16_SCALAR_FLOAT_BINARY_FAMILIES
    }
    actual_binary = {
        (rule.source_op, rule.descriptor.key)
        for source_op in _SCALAR_BINARY_OPS
        for rule in _rules_for(source_op)
    }
    assert actual_binary == expected_binary
    assert all(
        _value_type(rule, "result") == Scalar("f16")
        for source_op in _SCALAR_BINARY_OPS
        for rule in _rules_for(source_op)
    )

    fma_rules = _rules_for(scalar_math.scalar_fmaf)
    assert {
        (rule.descriptor.key, _value_type(rule, "result")) for rule in fma_rules
    } == {
        (
            f"x86.avx512_fp16.{AVX512_FP16_SCALAR_FLOAT_FMA_MNEMONIC}.xmm",
            Scalar("f16"),
        )
    }


def test_vector_arithmetic_covers_every_native_fp16_width() -> None:
    expected_binary = {
        (
            getattr(vector, f"vector_{row.source_operation}"),
            f"x86.avx512_fp16.{row.mnemonic}.{_REGISTER_SUFFIXES[bit_width]}",
            Vector("f16", lanes=bit_width // 16),
        )
        for row in AVX512_FP16_FLOAT_BINARY_FAMILIES
        for bit_width in _REGISTER_SUFFIXES
    }
    actual_binary = {
        (rule.source_op, rule.descriptor.key, _value_type(rule, "result"))
        for source_op in _VECTOR_BINARY_OPS
        for rule in _rules_for(source_op)
    }
    assert actual_binary == expected_binary

    assert {
        (rule.descriptor.key, _value_type(rule, "result"))
        for rule in _rules_for(vector.vector_fmaf)
    } == {
        (
            "x86.avx512_fp16."
            f"{AVX512_FP16_FLOAT_FMA_MNEMONIC}.{_REGISTER_SUFFIXES[bit_width]}",
            Vector("f16", lanes=bit_width // 16),
        )
        for bit_width in _REGISTER_SUFFIXES
    }


def test_comparisons_cover_scalar_and_every_vector_representation() -> None:
    scalar_rules = _rules_for(scalar_comparison.scalar_cmpf)
    assert len(scalar_rules) == 1
    assert _value_type(scalar_rules[0], "lhs") == Scalar("f16")
    assert _value_type(scalar_rules[0], "result") == Scalar("i1")

    assert {
        (
            _value_type(rule, "lhs"),
            _value_type(rule, "result"),
            _register_class(rule, "result"),
        )
        for rule in _rules_for(vector.vector_cmpf)
    } == {
        (Vector("f16", lanes=8), Vector("i1", lanes=8), "x86.k"),
        (Vector("f16", lanes=16), Vector("i1", lanes=16), "x86.k"),
        (Vector("f16", lanes=32), Vector("i1", lanes=32), "x86.k"),
        (Vector("f16", lanes=32), Vector("i1", lanes=32), "x86.ymm"),
    }


def test_scalar_carrier_rules_preserve_the_existing_fp16_surface() -> None:
    assert {
        (_value_type(rule, "input"), _value_type(rule, "result"))
        for rule in _rules_for(scalar_conversion.scalar_bitcast)
    } == {
        (Scalar("f16"), Scalar("i16")),
        (Scalar("i16"), Scalar("f16")),
    }
    assert {_value_type(rule, "result") for rule in _rules_for(scf.scf_select)} == {
        Scalar("f16")
    }
    assert {
        _value_type(rule, "result")
        for rule in _rules_for(scalar_conversion.scalar_constant)
    } == {Scalar("f16")}
    assert {
        _value_type(rule, "result") for rule in _rules_for(vector.vector_splat)
    } == {
        Vector("f16", lanes=8),
        Vector("f16", lanes=16),
        Vector("f16", lanes=32),
    }


def test_conversions_cover_every_representable_native_width() -> None:
    scalar_pairs = {
        source_op: {
            (_value_type(rule, "input"), _value_type(rule, "result"))
            for rule in _rules_for(source_op)
        }
        for source_op in (
            scalar_conversion.scalar_extf,
            scalar_conversion.scalar_fptrunc,
        )
    }
    assert scalar_pairs == {
        scalar_conversion.scalar_extf: {(Scalar("f16"), Scalar("f32"))},
        scalar_conversion.scalar_fptrunc: {(Scalar("f32"), Scalar("f16"))},
    }

    vector_pairs = {
        source_op: {
            (_value_type(rule, "input"), _value_type(rule, "result"))
            for rule in _rules_for(source_op)
        }
        for source_op in (vector.vector_extf, vector.vector_fptrunc)
    }
    assert vector_pairs == {
        vector.vector_extf: {
            (Vector("f16", lanes=8), Vector("f32", lanes=8)),
            (Vector("f16", lanes=16), Vector("f32", lanes=16)),
        },
        vector.vector_fptrunc: {
            (Vector("f32", lanes=8), Vector("f16", lanes=8)),
            (Vector("f32", lanes=16), Vector("f16", lanes=16)),
        },
    }
