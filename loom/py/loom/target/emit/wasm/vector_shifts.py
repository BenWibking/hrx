# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""SIMD shifts consuming one retained scalar count instead of a vector splat."""

from collections.abc import Iterable

from loom.dialect.vector import defs as vector
from loom.target.arch.wasm.descriptors import WASM_CORE_SIMD128_DESCRIPTOR_SET
from loom.target.contracts import (
    DescriptorEmitForm,
    DescriptorRule,
    EmitDescriptorOp,
    Guard,
    Scalar,
    ValueProject,
    ValueRef,
    Vector,
    descriptor_by_key,
)


def uniform_shift_rules() -> Iterable[DescriptorRule]:
    constant = descriptor_by_key(WASM_CORE_SIMD128_DESCRIPTOR_SET, "wasm.i32.const")
    wrap = descriptor_by_key(WASM_CORE_SIMD128_DESCRIPTOR_SET, "wasm.i32.wrap_i64")
    for bit_count in (8, 16, 32, 64):
        scalar_type = Scalar(f"i{bit_count}")
        vector_type = Vector(f"i{bit_count}", lanes=128 // bit_count)
        origin = ValueRef.uniform_element_origin_operand("rhs")
        count = ValueRef.temporary("count")
        runtime_setup = (
            (
                EmitDescriptorOp(
                    descriptor=wrap,
                    operands={"input": origin},
                    results={"dst": count},
                    result_types={"dst": Scalar("i32")},
                ),
            )
            if bit_count == 64
            else ()
        )
        for source_op, operation in (
            (vector.vector_shli, "shl"),
            (vector.vector_shrsi, "shr_s"),
            (vector.vector_shrui, "shr_u"),
        ):
            descriptor = descriptor_by_key(
                WASM_CORE_SIMD128_DESCRIPTOR_SET,
                f"wasm.i{bit_count}x{128 // bit_count}.{operation}",
            )
            for count_guard, count_value, setup in (
                (
                    Guard.value_exact_i64("rhs"),
                    count,
                    (
                        EmitDescriptorOp(
                            descriptor=constant,
                            results={"dst": count},
                            result_types={"dst": Scalar("i32")},
                            immediates={"i32_value": ValueProject.exact_i64("rhs")},
                            form=DescriptorEmitForm.CONST,
                        ),
                    ),
                ),
                (
                    Guard.uniform_element_origin_type("rhs", scalar_type),
                    count if runtime_setup else origin,
                    runtime_setup,
                ),
            ):
                yield DescriptorRule(
                    source_op=source_op,
                    descriptor=descriptor,
                    guards=(
                        # Reject varying counts before testing packet shapes.
                        count_guard,
                        *(
                            Guard.value_type(field, vector_type)
                            for field in ("lhs", "rhs", "result")
                        ),
                        # SIMD masks counts to the element width, while narrow
                        # scalar legalization uses an I32 carrier. Restrict the
                        # native selection to their common valid count domain.
                        Guard.value_i64_range("rhs", 0, bit_count - 1),
                    ),
                    emit=(
                        *setup,
                        EmitDescriptorOp(
                            descriptor=descriptor,
                            operands={
                                "value": ValueRef.operand("lhs"),
                                "count": count_value,
                            },
                            results={"dst": ValueRef.result("result")},
                        ),
                    ),
                )
