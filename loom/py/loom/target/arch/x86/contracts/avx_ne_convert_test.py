# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from collections import Counter

from loom.dialect.scalar import conversion as scalar_conversion
from loom.dialect.vector import defs as vector
from loom.dialect.view import defs as view
from loom.target.arch.x86.contracts.avx_ne_convert import (
    X86_AVX_NE_CONVERT_CONTRACT_DIALECT_OPS,
    X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT,
)
from loom.target.contracts import (
    DescriptorRule,
    EmitDescriptorOp,
    GuardKind,
    SourceMemoryOperation,
    SourceNodeRelation,
    SourceValueKind,
    compile_lower_rule_set,
)

_EXPECTED_REPORT_PREFIXES = {
    **{
        f"native_memory_broadcast_{element}_to_f32x{lanes}": (
            f"x86.avx_ne_convert.{mnemonic}."
        )
        for element, mnemonic in (
            ("bf16", "vbcstnebf162ps"),
            ("f16", "vbcstnesh2ps"),
        )
        for lanes in (4, 8)
    },
    **{
        f"native_memory_{selection}_{element}x{lanes * 2}_to_f32x{lanes}": (
            f"x86.avx_ne_convert.{mnemonic}."
        )
        for (element, selection), mnemonic in (
            (("bf16", "even"), "vcvtneebf162ps"),
            (("f16", "even"), "vcvtneeph2ps"),
            (("bf16", "odd"), "vcvtneobf162ps"),
            (("f16", "odd"), "vcvtneoph2ps"),
        )
        for lanes in (4, 8)
    },
}


def _memory_emit(rule: DescriptorRule) -> EmitDescriptorOp:
    emits = tuple(
        emit
        for emit in rule.emit
        if isinstance(emit, EmitDescriptorOp)
        and emit.descriptor.key.startswith("x86.avx_ne_convert.")
    )
    assert len(emits) == 1
    return emits[0]


def test_fragment_compiles_every_authored_rule() -> None:
    compiled = compile_lower_rule_set(
        X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT,
        dialect_ops=X86_AVX_NE_CONVERT_CONTRACT_DIALECT_OPS,
    )

    assert len(compiled.rules) == len(X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT.cases)


def test_rules_cover_every_exact_memory_conversion_shape() -> None:
    rules = X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT.cases
    report_counts = Counter(rule.report_key for rule in rules)
    assert set(report_counts) == set(_EXPECTED_REPORT_PREFIXES)
    assert set(report_counts.values()) == {9}
    assert {rule.source_op for rule in rules} == {view.view_load, vector.vector_load}
    assert all(rule.priority == 1 for rule in rules)
    for rule in rules:
        assert _memory_emit(rule).descriptor.key.startswith(
            _EXPECTED_REPORT_PREFIXES[rule.report_key]
        )


def test_broadcast_rules_join_load_extend_and_splat() -> None:
    rules = tuple(
        rule
        for rule in X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT.cases
        if rule.source_op is view.view_load
    )

    assert len(rules) == 36
    for rule in rules:
        assert tuple(node.source_op for node in rule.source_nodes) == (
            scalar_conversion.scalar_extf,
            vector.vector_splat,
        )
        assert tuple(node.relation for node in rule.source_nodes) == (
            SourceNodeRelation.ADJACENT_UNIQUE_USER,
            SourceNodeRelation.ADJACENT_UNIQUE_USER,
        )
        assert rule.source_nodes[1].parent == "extend"
        memory_emit = _memory_emit(rule)
        assert memory_emit.results["dst"].source_node == "splat"
        assert memory_emit.source_memory is not None
        assert memory_emit.source_memory.operation is SourceMemoryOperation.LOAD
        assert memory_emit.source_memory.element_byte_count == 2
        assert memory_emit.source_memory.vector_lane_count == 1


def test_even_odd_rules_join_load_deinterleave_and_extend() -> None:
    rules = tuple(
        rule
        for rule in X86_AVX_NE_CONVERT_CONTRACT_FRAGMENT.cases
        if rule.source_op is vector.vector_load
    )

    assert len(rules) == 72
    for rule in rules:
        deinterleave, extend = rule.source_nodes
        assert deinterleave.source_op is vector.vector_deinterleave
        assert extend.source_op is vector.vector_extf
        assert extend.parent == "deinterleave"
        selected_result = extend.parent_value.field
        assert selected_result in ("even", "odd")
        dead_results = {
            guard.field
            for guard in deinterleave.guards
            if guard.kind is GuardKind.VALUE_NO_USES
        }
        assert dead_results == {"odd" if selected_result == "even" else "even"}
        memory_emit = _memory_emit(rule)
        assert memory_emit.results["dst"].source_node == "extend"
        assert memory_emit.results["dst"].kind is SourceValueKind.RESULT
        assert memory_emit.source_memory is not None
        assert memory_emit.source_memory.operation is SourceMemoryOperation.LOAD
        assert memory_emit.source_memory.element_byte_count == 2
        assert memory_emit.source_memory.vector_lane_count in (8, 16)
