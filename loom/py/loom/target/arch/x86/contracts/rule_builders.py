# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Shared builders for x86 source-to-low contract rules."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Mapping

from loom.target.arch.x86.vector_families import VectorElement
from loom.target.contracts import (
    AttrProject,
    DescriptorEmitForm,
    DescriptorResultType,
    EmitDescriptorOp,
    Guard,
    TypePattern,
    ValueRef,
    Vector,
)
from loom.target.low_descriptors import Descriptor

DescriptorLookup = Callable[[str], Descriptor]


def emit_descriptor_op(
    *,
    descriptor: Descriptor,
    operands: Mapping[str, ValueRef] | None = None,
    results: Mapping[str, ValueRef] | None = None,
    result_types: Mapping[str, TypePattern | DescriptorResultType] | None = None,
    immediates: Mapping[str, AttrProject | int] | None = None,
) -> EmitDescriptorOp:
    return EmitDescriptorOp(
        descriptor=descriptor,
        operands={} if operands is None else dict(operands),
        results={} if results is None else dict(results),
        result_types=None if result_types is None else dict(result_types),
        immediates={} if immediates is None else dict(immediates),
        form=DescriptorEmitForm.OP,
    )


def value_type_guards(
    fields: Iterable[str],
    type_pattern: TypePattern,
) -> tuple[Guard, ...]:
    return tuple(Guard.value_type(field, type_pattern) for field in fields)


def full_vector_type(element: VectorElement, vector_bit_width: int) -> TypePattern:
    return Vector(element.name, lanes=element.lane_count(vector_bit_width))
