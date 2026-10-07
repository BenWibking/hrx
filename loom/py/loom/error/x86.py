# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""X86 domain — X86-owned legality and lowering diagnostics."""

from loom.errors import ErrorDef, ErrorDomain, ErrorParam, ParamKind, Severity

ERR_X86_001 = ErrorDef(
    domain=ErrorDomain.X86,
    code=1,
    severity=Severity.ERROR,
    summary="X86 task entry contract is not satisfied.",
    message="x86 task entry requires {constraint}",
    params=(ErrorParam("constraint", ParamKind.STRING),),
)

ERR_X86_002 = ErrorDef(
    domain=ErrorDomain.X86,
    code=2,
    severity=Severity.ERROR,
    summary="X86 task parameter is not representable.",
    message=(
        "x86 task parameter {parameter_index} of type {actual_type} "
        "requires {constraint}"
    ),
    params=(
        ErrorParam("parameter_index", ParamKind.I64),
        ErrorParam("actual_type", ParamKind.TYPE),
        ErrorParam("constraint", ParamKind.STRING),
    ),
)

ERR_X86_003 = ErrorDef(
    domain=ErrorDomain.X86,
    code=3,
    severity=Severity.ERROR,
    summary="X86 task ABI layout field is invalid.",
    message="x86 task ABI field '{field_name}' requires {constraint}",
    params=(
        ErrorParam("field_name", ParamKind.STRING),
        ErrorParam("constraint", ParamKind.STRING),
    ),
)

ERR_X86_004 = ErrorDef(
    domain=ErrorDomain.X86,
    code=4,
    severity=Severity.ERROR,
    summary="X86 native emission contract is not satisfied.",
    message="x86 native emission requires {constraint}",
    params=(ErrorParam("constraint", ParamKind.STRING),),
)

ERR_X86_005 = ErrorDef(
    domain=ErrorDomain.X86,
    code=5,
    severity=Severity.ERROR,
    summary="An instruction has no native X86 encoding.",
    message="x86 native encoding is unavailable for '{operation_name}'",
    params=(ErrorParam("operation_name", ParamKind.STRING),),
)

ERR_X86_006 = ErrorDef(
    domain=ErrorDomain.X86,
    code=6,
    severity=Severity.ERROR,
    summary="Native exports have the same external name.",
    message="duplicate native export '{symbol_name}'",
    params=(ErrorParam("symbol_name", ParamKind.STRING),),
)

ERR_X86_007 = ErrorDef(
    domain=ErrorDomain.X86,
    code=7,
    severity=Severity.ERROR,
    summary="A native symbol does not satisfy the artifact contract.",
    message="x86 native symbol '{symbol_name}' requires {constraint}",
    params=(
        ErrorParam("symbol_name", ParamKind.STRING),
        ErrorParam("constraint", ParamKind.STRING),
    ),
)

ALL_X86_ERRORS = (
    ERR_X86_001,
    ERR_X86_002,
    ERR_X86_003,
    ERR_X86_004,
    ERR_X86_005,
    ERR_X86_006,
    ERR_X86_007,
)
