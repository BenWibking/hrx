# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""GLSL.std.450 extended math instructions exposed by the SPIR-V target."""

from __future__ import annotations

from dataclasses import dataclass

from loom.target.arch.spirv.ordinary_vector import (
    ORDINARY_VECTOR_COMPONENT_TYPES,
    ORDINARY_VECTOR_TYPES,
    OrdinaryVectorInstructionType,
)


@dataclass(frozen=True, slots=True)
class ExtendedMathOperation:
    """One source math operation and its GLSL.std.450 instruction."""

    source_op_key: str
    instruction_name: str
    instruction_c_enum: str


@dataclass(frozen=True, slots=True)
class ExtendedMathInstruction:
    """One typed descriptor and packet emitted through OpExtInst."""

    operation: ExtendedMathOperation
    value_type: OrdinaryVectorInstructionType

    @property
    def descriptor_key(self) -> str:
        return (
            "spirv.op_ext_inst.glsl_std_450."
            f"{self.operation.source_op_key}.{self.value_type.suffix}"
        )

    @property
    def mnemonic(self) -> str:
        return (
            "OpExtInst.GLSL.std.450."
            f"{self.operation.instruction_name}.{self.value_type.suffix}"
        )


EXTENDED_MATH_OPERATIONS = (
    ExtendedMathOperation(
        source_op_key="expf",
        instruction_name="Exp",
        instruction_c_enum="LOOM_SPIRV_GLSL_STD_450_EXP",
    ),
    ExtendedMathOperation(
        source_op_key="logf",
        instruction_name="Log",
        instruction_c_enum="LOOM_SPIRV_GLSL_STD_450_LOG",
    ),
    ExtendedMathOperation(
        source_op_key="exp2f",
        instruction_name="Exp2",
        instruction_c_enum="LOOM_SPIRV_GLSL_STD_450_EXP2",
    ),
    ExtendedMathOperation(
        source_op_key="log2f",
        instruction_name="Log2",
        instruction_c_enum="LOOM_SPIRV_GLSL_STD_450_LOG2",
    ),
)

_F32_COMPONENT_TYPE = next(
    component_type
    for component_type in ORDINARY_VECTOR_COMPONENT_TYPES
    if component_type.source_types == ("f32",)
)

_F32_VALUE_TYPES = (
    _F32_COMPONENT_TYPE,
    *(
        vector_type
        for vector_type in ORDINARY_VECTOR_TYPES
        if vector_type.component_type == _F32_COMPONENT_TYPE
    ),
)

EXTENDED_MATH_INSTRUCTIONS = tuple(
    ExtendedMathInstruction(operation=operation, value_type=value_type)
    for operation in EXTENDED_MATH_OPERATIONS
    for value_type in _F32_VALUE_TYPES
)
