# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""x86 AVX512 source-to-low contract fragment."""

from __future__ import annotations

from collections.abc import Mapping, Sequence

from loom.dialect.vector import ALL_VECTOR_OPS
from loom.dialect.vector import defs as vector
from loom.dsl import Op
from loom.target.arch.x86.contracts.floating_reduction import (
    ordered_float_reduction_emit_chain,
    reassociated_float_reduction_emit_chain,
)
from loom.target.arch.x86.contracts.memory import x86_vector_memory_rules
from loom.target.arch.x86.contracts.vector_arithmetic import (
    avx512_vector_arithmetic_rules,
)
from loom.target.arch.x86.contracts.vector_construction import (
    avx512_vector_construction_rules,
)
from loom.target.arch.x86.descriptors import X86_AVX512_CORE_DESCRIPTOR_SET
from loom.target.arch.x86.vector_families import FLOAT_ELEMENTS
from loom.target.contracts import (
    AttrProject,
    ContractCase,
    ContractFragment,
    DescriptorEmitForm,
    DescriptorRule,
    DirectDescriptorCase,
    EmitDescriptorOp,
    Guard,
    GuardDiagnostic,
    Scalar,
    TypePattern,
    ValueRef,
    Vector,
    binary_descriptor_rules,
    descriptor_by_key,
)
from loom.target.low_descriptors import Descriptor

_F32 = Scalar("f32")
_V4I1 = Vector("i1", lanes=4)
_V4I32 = Vector("i32", lanes=4)
_V4F32 = Vector("f32", lanes=4)
_V16I1 = Vector("i1", lanes=16)
_V16I32 = Vector("i32", lanes=16)
_V16F32 = Vector("f32", lanes=16)

_SOURCE_MEMORY_DIAGNOSTIC = GuardDiagnostic(
    subject_role="source-memory",
    subject_name="x86-avx512",
    constraint_key="x86.avx512.source_memory",
)


def _descriptor(key: str) -> Descriptor:
    return descriptor_by_key(X86_AVX512_CORE_DESCRIPTOR_SET, key)


def _op_emit(
    *,
    descriptor: Descriptor,
    operands: dict[str, ValueRef] | None = None,
    results: dict[str, ValueRef] | None = None,
    result_types: dict[str, TypePattern] | None = None,
    immediates: Mapping[str, AttrProject | int] | None = None,
) -> EmitDescriptorOp:
    return EmitDescriptorOp(
        descriptor=descriptor,
        operands={} if operands is None else operands,
        results={} if results is None else results,
        result_types=result_types,
        immediates={} if immediates is None else immediates,
        form=DescriptorEmitForm.OP,
    )


def _select_rule(
    condition_type: TypePattern,
    value_type: TypePattern,
    descriptor_key: str,
) -> DescriptorRule:
    descriptor = _descriptor(descriptor_key)
    return DescriptorRule(
        source_op=vector.vector_select,
        descriptor=descriptor,
        guards=(
            Guard.value_type("condition", condition_type),
            Guard.value_type("true_value", value_type),
            Guard.value_type("false_value", value_type),
            Guard.value_type("result", value_type),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={
                    "mask": ValueRef.operand("condition"),
                    "true_value": ValueRef.operand("true_value"),
                    "false_value": ValueRef.operand("false_value"),
                },
                results={"dst": ValueRef.result("result")},
            ),
        ),
    )


def _compare_rule(
    source_op: Op,
    predicates: Sequence[str] | None,
    predicate_immediates: Mapping[str, int],
    operand_type: TypePattern,
    result_type: TypePattern,
    descriptor_key: str,
) -> DescriptorRule:
    descriptor = _descriptor(descriptor_key)
    return DescriptorRule(
        source_op=source_op,
        descriptor=descriptor,
        guards=(
            *(
                ()
                if predicates is None
                else (Guard.enum_attr_in("predicate", predicates),)
            ),
            Guard.value_type("lhs", operand_type),
            Guard.value_type("rhs", operand_type),
            Guard.value_type("result", result_type),
        ),
        emit=(
            _op_emit(
                descriptor=descriptor,
                operands={
                    "lhs": ValueRef.operand("lhs"),
                    "rhs": ValueRef.operand("rhs"),
                },
                results={"dst": ValueRef.result("result")},
                immediates={
                    "predicate": AttrProject.enum_remap(
                        "predicate",
                        predicate_immediates,
                    )
                },
            ),
        ),
    )


_INTEGER_COMPARE_IMMEDIATES = {
    "eq": 0,
    "ne": 4,
    "slt": 1,
    "sle": 2,
    "sgt": 6,
    "sge": 5,
    "ult": 1,
    "ule": 2,
    "ugt": 6,
    "uge": 5,
}

_SIGNED_INTEGER_COMPARE_PREDICATES = ("eq", "ne", "slt", "sle", "sgt", "sge")
_UNSIGNED_INTEGER_COMPARE_PREDICATES = ("ult", "ule", "ugt", "uge")

# Quiet AVX predicates implement the ordered/unordered result while avoiding
# signaling-NaN exceptions as an implementation artifact.
_FLOAT_COMPARE_IMMEDIATES = {
    "oeq": 0,
    "ogt": 30,
    "oge": 29,
    "olt": 17,
    "ole": 18,
    "one": 12,
    "ord": 7,
    "ueq": 8,
    "ugt": 22,
    "uge": 21,
    "ult": 25,
    "ule": 26,
    "une": 4,
    "uno": 3,
}


def _memory_rules() -> tuple[DescriptorRule, ...]:
    return x86_vector_memory_rules(
        _descriptor,
        descriptor_key_prefix="x86.avx512",
        vector_bit_widths=(512,),
        diagnostic=_SOURCE_MEMORY_DIAGNOSTIC,
    )


def _f32x16_extract_emit_chain(extract: Descriptor) -> tuple[EmitDescriptorOp, ...]:
    return tuple(
        _op_emit(
            descriptor=extract,
            operands={"source": ValueRef.operand("input")},
            results={"dst": ValueRef.temporary(f"q{lane}")},
            result_types={"dst": _V4F32},
            immediates={"lane": lane},
        )
        for lane in range(4)
    )


def _reduce_f32x16_ordered_rule() -> DescriptorRule:
    extract = _descriptor("x86.avx512.vextractf32x4.xmm.zmm")
    addss = _descriptor("x86.avx2.vaddss.xmm")
    vpermilps = _descriptor("x86.avx2.vpermilps.xmm")
    return DescriptorRule(
        source_op=vector.vector_reduce,
        descriptor=addss,
        guards=(
            Guard.enum_attr_equals("kind", "addf"),
            Guard.instance_flags_has_none("fastmath", "reassoc"),
            Guard.value_type("input", _V16F32),
            Guard.value_type("init", _F32),
            Guard.value_type("result", _F32),
            Guard.descriptor_available(extract),
            Guard.descriptor_available(vpermilps),
            Guard.descriptor_available(addss),
        ),
        emit=(
            *_f32x16_extract_emit_chain(extract),
            *ordered_float_reduction_emit_chain(
                tuple(ValueRef.temporary(f"q{lane}") for lane in range(4)),
                FLOAT_ELEMENTS[0],
                "addf",
                _descriptor,
                temporary_prefix="ordered_",
            ),
        ),
    )


def _reduce_f32x16_reassociated_rule() -> DescriptorRule:
    extract = _descriptor("x86.avx512.vextractf32x4.xmm.zmm")
    addps = _descriptor("x86.avx2.vaddps.xmm")
    addss = _descriptor("x86.avx2.vaddss.xmm")
    vpermilps = _descriptor("x86.avx2.vpermilps.xmm")

    quarter_sum_emits = (
        _op_emit(
            descriptor=addps,
            operands={
                "lhs": ValueRef.temporary("q0"),
                "rhs": ValueRef.temporary("q1"),
            },
            results={"dst": ValueRef.temporary("sum01")},
            result_types={"dst": _V4F32},
        ),
        _op_emit(
            descriptor=addps,
            operands={
                "lhs": ValueRef.temporary("q2"),
                "rhs": ValueRef.temporary("q3"),
            },
            results={"dst": ValueRef.temporary("sum23")},
            result_types={"dst": _V4F32},
        ),
        _op_emit(
            descriptor=addps,
            operands={
                "lhs": ValueRef.temporary("sum01"),
                "rhs": ValueRef.temporary("sum23"),
            },
            results={"dst": ValueRef.temporary("xmm_sum")},
            result_types={"dst": _V4F32},
        ),
    )
    return DescriptorRule(
        source_op=vector.vector_reduce,
        descriptor=addss,
        guards=(
            Guard.enum_attr_equals("kind", "addf"),
            Guard.instance_flags_has_all("fastmath", "reassoc"),
            Guard.value_type("input", _V16F32),
            Guard.value_type("init", _F32),
            Guard.value_type("result", _F32),
            Guard.descriptor_available(extract),
            Guard.descriptor_available(vpermilps),
            Guard.descriptor_available(addps),
            Guard.descriptor_available(addss),
        ),
        emit=(
            *_f32x16_extract_emit_chain(extract),
            *quarter_sum_emits,
            *reassociated_float_reduction_emit_chain(
                ValueRef.temporary("xmm_sum"),
                FLOAT_ELEMENTS[0],
                "addf",
                _descriptor,
                temporary_prefix="horizontal_",
            ),
        ),
    )


def _cases() -> Sequence[ContractCase]:
    return (
        *avx512_vector_construction_rules(_descriptor),
        _select_rule(_V16I1, _V16I32, "x86.avx512.vpblendmd.zmm"),
        _select_rule(_V16I1, _V16F32, "x86.avx512.vblendmps.zmm"),
        _select_rule(_V4I1, _V4I32, "x86.avx512.vpblendmd.xmm"),
        _select_rule(_V4I1, _V4F32, "x86.avx512.vblendmps.xmm"),
        _compare_rule(
            vector.vector_cmpi,
            _SIGNED_INTEGER_COMPARE_PREDICATES,
            _INTEGER_COMPARE_IMMEDIATES,
            _V16I32,
            _V16I1,
            "x86.avx512.vpcmpd.zmm",
        ),
        _compare_rule(
            vector.vector_cmpi,
            _UNSIGNED_INTEGER_COMPARE_PREDICATES,
            _INTEGER_COMPARE_IMMEDIATES,
            _V16I32,
            _V16I1,
            "x86.avx512.vpcmpud.zmm",
        ),
        _compare_rule(
            vector.vector_cmpi,
            _SIGNED_INTEGER_COMPARE_PREDICATES,
            _INTEGER_COMPARE_IMMEDIATES,
            _V4I32,
            _V4I1,
            "x86.avx512.vpcmpd.xmm",
        ),
        _compare_rule(
            vector.vector_cmpi,
            _UNSIGNED_INTEGER_COMPARE_PREDICATES,
            _INTEGER_COMPARE_IMMEDIATES,
            _V4I32,
            _V4I1,
            "x86.avx512.vpcmpud.xmm",
        ),
        _compare_rule(
            vector.vector_cmpf,
            None,
            _FLOAT_COMPARE_IMMEDIATES,
            _V16F32,
            _V16I1,
            "x86.avx512.vcmpps.zmm",
        ),
        _compare_rule(
            vector.vector_cmpf,
            None,
            _FLOAT_COMPARE_IMMEDIATES,
            _V4F32,
            _V4I1,
            "x86.avx512.vcmpps.xmm",
        ),
        *avx512_vector_arithmetic_rules(_descriptor),
        *binary_descriptor_rules(
            tuple(
                DirectDescriptorCase(
                    source_op, _descriptor(descriptor_key), type_pattern
                )
                for source_op, type_pattern, descriptor_key in (
                    (vector.vector_andi, _V16I1, "x86.avx512.kandq"),
                    (vector.vector_ori, _V16I1, "x86.avx512.korq"),
                    (vector.vector_xori, _V16I1, "x86.avx512.kxorq"),
                )
            ),
            form=DescriptorEmitForm.OP,
        ),
        *_memory_rules(),
        _reduce_f32x16_ordered_rule(),
        _reduce_f32x16_reassociated_rule(),
    )


X86_AVX512_CONTRACT_DIALECT_OPS = {
    "vector": ALL_VECTOR_OPS,
}

X86_AVX512_CONTRACT_FRAGMENT = ContractFragment(
    name="x86.avx512",
    descriptor_set=X86_AVX512_CORE_DESCRIPTOR_SET,
    public_header="loom/target/arch/x86/contracts/avx512.h",
    cases=_cases(),
)
