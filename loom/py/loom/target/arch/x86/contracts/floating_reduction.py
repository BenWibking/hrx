# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""x86 floating-point reductions and scalar dot products."""

from __future__ import annotations

from collections.abc import Sequence

from loom.dialect.vector import defs as vector
from loom.target.arch.x86.contracts.rule_builders import (
    DescriptorLookup as _DescriptorLookup,
)
from loom.target.arch.x86.contracts.rule_builders import (
    emit_descriptor_op as _op_emit,
)
from loom.target.arch.x86.vector_families import (
    AVX2_FLOAT_EXTREMA_OPERATIONS,
    AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS,
    AVX2_SCALAR_FLOAT_FMA_MNEMONICS,
    AVX2_VECTOR_BIT_WIDTHS,
    FLOAT_ELEMENTS,
    VectorElement,
)
from loom.target.contracts import (
    DescriptorResultType,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    ValueRef,
    Vector,
)
from loom.target.low_descriptors import Descriptor

_FLOAT_SUFFIXES = {"f32": ("ps", "ss"), "f64": ("pd", "sd")}
_OPERATION_STEMS = {
    "addf": "add",
    "mulf": "mul",
    "minimumf": "min",
    "maximumf": "max",
    "minnumf": "min",
    "maxnumf": "max",
}


def _float_descriptor(
    operation: str,
    element: VectorElement,
    *,
    scalar: bool,
    descriptor_lookup: _DescriptorLookup,
) -> Descriptor:
    packed_suffix, scalar_suffix = _FLOAT_SUFFIXES[element.name]
    return descriptor_lookup(
        f"x86.avx2.v{_OPERATION_STEMS[operation]}"
        f"{scalar_suffix if scalar else packed_suffix}.xmm"
    )


def _xmm_chunks(
    operand: str,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> tuple[list[EmitDescriptorOp], tuple[ValueRef, ...], tuple[Descriptor, ...]]:
    if vector_bit_width == 128:
        return [], (ValueRef.operand(operand),), ()
    extract = descriptor_lookup("x86.avx2.vextractf128.xmm.ymm")
    chunks = tuple(ValueRef.temporary(f"{operand}_half{lane}") for lane in range(2))
    emits = [
        _op_emit(
            descriptor=extract,
            operands={"source": ValueRef.operand(operand)},
            results={"dst": chunk},
            result_types={"dst": DescriptorResultType()},
            immediates={"lane": lane},
        )
        for lane, chunk in enumerate(chunks)
    ]
    return emits, chunks, (extract,)


def ordered_float_reduction_emit_chain(
    input_values: Sequence[ValueRef],
    element: VectorElement,
    operation: str,
    descriptor_lookup: _DescriptorLookup,
    *,
    temporary_prefix: str = "",
) -> tuple[EmitDescriptorOp, ...]:
    """Combines XMM chunks in logical lane order starting with `init`."""
    scalar_combine = _float_descriptor(
        operation, element, scalar=True, descriptor_lookup=descriptor_lookup
    )
    shuffle = descriptor_lookup(
        f"x86.avx2.vpermil{'ps' if element.name == 'f32' else 'pd'}.xmm"
    )
    chunk_type = Vector(element.name, lanes=128 // element.bit_width)
    scalar_type = Scalar(element.name)
    chunk_lane_count = 128 // element.bit_width
    lane_count = len(input_values) * chunk_lane_count
    lane_ordinal = 0
    accumulator = ValueRef.operand("init")
    emits: list[EmitDescriptorOp] = []
    for input_value in input_values:
        for lane in range(chunk_lane_count):
            lane_value = input_value
            if lane != 0:
                lane_value = ValueRef.temporary(f"{temporary_prefix}lane{lane_ordinal}")
                emits.append(
                    _op_emit(
                        descriptor=shuffle,
                        operands={"source": input_value},
                        results={"dst": lane_value},
                        result_types={"dst": chunk_type},
                        immediates={"control": lane},
                    )
                )
            lane_ordinal += 1
            next_accumulator = (
                ValueRef.result("result")
                if lane_ordinal == lane_count
                else ValueRef.temporary(f"{temporary_prefix}accumulator{lane_ordinal}")
            )
            emits.append(
                _op_emit(
                    descriptor=scalar_combine,
                    operands={"lhs": accumulator, "rhs": lane_value},
                    results={"dst": next_accumulator},
                    result_types={"dst": scalar_type},
                )
            )
            accumulator = next_accumulator
    return tuple(emits)


def reassociated_float_reduction_emit_chain(
    input_value: ValueRef,
    element: VectorElement,
    operation: str,
    descriptor_lookup: _DescriptorLookup,
    *,
    temporary_prefix: str = "",
) -> tuple[EmitDescriptorOp, ...]:
    """Reduces one XMM value with a balanced packed tree, then combines init."""
    packed_combine = _float_descriptor(
        operation, element, scalar=False, descriptor_lookup=descriptor_lookup
    )
    scalar_combine = _float_descriptor(
        operation, element, scalar=True, descriptor_lookup=descriptor_lookup
    )
    shift = descriptor_lookup("x86.avx2.vpsrldq.xmm")
    scalar_type = Scalar(element.name)
    reduced = input_value
    emits: list[EmitDescriptorOp] = []
    shift_bytes = 8
    ordinal = 0
    while shift_bytes >= element.bit_width // 8:
        shifted = ValueRef.temporary(f"{temporary_prefix}shifted{ordinal}")
        next_reduced = ValueRef.temporary(f"{temporary_prefix}reduced{ordinal}")
        emits.extend(
            (
                _op_emit(
                    descriptor=shift,
                    operands={"source": reduced},
                    results={"dst": shifted},
                    result_types={"dst": DescriptorResultType()},
                    immediates={"bytes": shift_bytes},
                ),
                _op_emit(
                    descriptor=packed_combine,
                    operands={"lhs": reduced, "rhs": shifted},
                    results={"dst": next_reduced},
                    result_types={"dst": DescriptorResultType()},
                ),
            )
        )
        reduced = next_reduced
        ordinal += 1
        shift_bytes //= 2
    emits.append(
        _op_emit(
            descriptor=scalar_combine,
            operands={"lhs": ValueRef.operand("init"), "rhs": reduced},
            results={"dst": ValueRef.result("result")},
            result_types={"dst": scalar_type},
        )
    )
    return tuple(emits)


def _float_reduction_rule(
    element: VectorElement,
    vector_bit_width: int,
    operation: str,
    reassociated: bool,
    descriptor_lookup: _DescriptorLookup,
    *,
    extra_guards: Sequence[Guard] = (),
    priority: int = 0,
) -> DescriptorRule:
    chunk_emits, chunks, chunk_dependencies = _xmm_chunks(
        "input", vector_bit_width, descriptor_lookup
    )
    packed_combine = _float_descriptor(
        operation, element, scalar=False, descriptor_lookup=descriptor_lookup
    )
    scalar_combine = _float_descriptor(
        operation, element, scalar=True, descriptor_lookup=descriptor_lookup
    )
    dependencies = [*chunk_dependencies, scalar_combine]
    emits = list(chunk_emits)
    if reassociated:
        reduced = chunks[0]
        if len(chunks) == 2:
            reduced = ValueRef.temporary("half_sum")
            emits.append(
                _op_emit(
                    descriptor=packed_combine,
                    operands={"lhs": chunks[0], "rhs": chunks[1]},
                    results={"dst": reduced},
                    result_types={"dst": DescriptorResultType()},
                )
            )
        shift = descriptor_lookup("x86.avx2.vpsrldq.xmm")
        dependencies.extend((packed_combine, shift))
        emits.extend(
            reassociated_float_reduction_emit_chain(
                reduced,
                element,
                operation,
                descriptor_lookup,
                temporary_prefix="horizontal_",
            )
        )
    else:
        shuffle = descriptor_lookup(
            f"x86.avx2.vpermil{'ps' if element.name == 'f32' else 'pd'}.xmm"
        )
        dependencies.append(shuffle)
        emits.extend(
            ordered_float_reduction_emit_chain(
                chunks,
                element,
                operation,
                descriptor_lookup,
                temporary_prefix="ordered_",
            )
        )
    scalar_type = Scalar(element.name)
    vector_type = Vector(
        element.name,
        lanes=vector_bit_width // element.bit_width,
    )
    return DescriptorRule(
        source_op=vector.vector_reduce,
        descriptor=scalar_combine,
        guards=(
            Guard.enum_attr_equals("kind", operation),
            (
                Guard.instance_flags_has_all("fastmath", "reassoc")
                if reassociated
                else Guard.instance_flags_has_none("fastmath", "reassoc")
            ),
            Guard.value_type("input", vector_type),
            Guard.value_type("init", scalar_type),
            Guard.value_type("result", scalar_type),
            *extra_guards,
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in dict.fromkeys(dependencies)
            ),
        ),
        emit=tuple(emits),
        priority=priority,
    )


def avx2_float_reduction_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    """Generates ordered and reassociated f32/f64 reduction families."""
    return tuple(
        _float_reduction_rule(
            element,
            vector_bit_width,
            operation,
            reassociated,
            descriptor_lookup,
        )
        for operation in AVX2_PACKED_FLOAT_REDUCTION_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
        for reassociated in (False, True)
    ) + tuple(
        _float_reduction_rule(
            element,
            vector_bit_width,
            operation,
            True,
            descriptor_lookup,
            extra_guards=(
                Guard.instance_flags_has_all("fastmath", "nnan"),
                Guard.instance_flags_has_all("fastmath", "nsz"),
            ),
            priority=1,
        )
        for operation in AVX2_FLOAT_EXTREMA_OPERATIONS
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )


def _float_dot_rule(
    element: VectorElement,
    vector_bit_width: int,
    descriptor_lookup: _DescriptorLookup,
) -> DescriptorRule:
    lhs_emits, lhs_chunks, lhs_dependencies = _xmm_chunks(
        "lhs", vector_bit_width, descriptor_lookup
    )
    rhs_emits, rhs_chunks, rhs_dependencies = _xmm_chunks(
        "rhs", vector_bit_width, descriptor_lookup
    )
    shuffle = descriptor_lookup(
        f"x86.avx2.vpermil{'ps' if element.name == 'f32' else 'pd'}.xmm"
    )
    fma = descriptor_lookup(
        f"x86.avx2.{AVX2_SCALAR_FLOAT_FMA_MNEMONICS[element.name]}.xmm"
    )
    chunk_type = Vector(element.name, lanes=128 // element.bit_width)
    scalar_type = Scalar(element.name)
    chunk_lane_count = 128 // element.bit_width
    lane_count = vector_bit_width // element.bit_width
    accumulator = ValueRef.operand("init")
    emits = [*lhs_emits, *rhs_emits]
    lane_ordinal = 0
    for lhs_chunk, rhs_chunk in zip(lhs_chunks, rhs_chunks, strict=True):
        for lane in range(chunk_lane_count):
            lhs_lane = lhs_chunk
            rhs_lane = rhs_chunk
            if lane != 0:
                lhs_lane = ValueRef.temporary(f"lhs_lane{lane_ordinal}")
                rhs_lane = ValueRef.temporary(f"rhs_lane{lane_ordinal}")
                emits.extend(
                    (
                        _op_emit(
                            descriptor=shuffle,
                            operands={"source": lhs_chunk},
                            results={"dst": lhs_lane},
                            result_types={"dst": chunk_type},
                            immediates={"control": lane},
                        ),
                        _op_emit(
                            descriptor=shuffle,
                            operands={"source": rhs_chunk},
                            results={"dst": rhs_lane},
                            result_types={"dst": chunk_type},
                            immediates={"control": lane},
                        ),
                    )
                )
            next_accumulator = (
                ValueRef.result("result")
                if lane_ordinal + 1 == lane_count
                else ValueRef.temporary(f"accumulator{lane_ordinal}")
            )
            emits.append(
                _op_emit(
                    descriptor=fma,
                    operands={
                        "acc": accumulator,
                        "lhs": lhs_lane,
                        "rhs": rhs_lane,
                    },
                    results={"dst": next_accumulator},
                    result_types={"dst": scalar_type},
                )
            )
            accumulator = next_accumulator
            lane_ordinal += 1
    vector_type = Vector(element.name, lanes=lane_count)
    return DescriptorRule(
        source_op=vector.vector_dotf,
        descriptor=fma,
        guards=(
            Guard.value_type("lhs", vector_type),
            Guard.value_type("rhs", vector_type),
            Guard.value_type("init", scalar_type),
            Guard.value_type("result", scalar_type),
            *(
                Guard.descriptor_available(descriptor)
                for descriptor in dict.fromkeys(
                    (*lhs_dependencies, *rhs_dependencies, shuffle)
                )
            ),
        ),
        emit=tuple(emits),
    )


def avx2_float_dot_rules(
    descriptor_lookup: _DescriptorLookup,
) -> tuple[DescriptorRule, ...]:
    """Generates exact ordered scalar-FMA dots for every AVX2 float shape."""
    return tuple(
        _float_dot_rule(element, vector_bit_width, descriptor_lookup)
        for element in FLOAT_ELEMENTS
        for vector_bit_width in AVX2_VECTOR_BIT_WIDTHS
    )
