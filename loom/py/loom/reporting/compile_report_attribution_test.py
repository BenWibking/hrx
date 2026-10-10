# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from __future__ import annotations

import json
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest

from loom.reporting.compile_report import CompileReportError
from loom.reporting.compile_report_attribution import (
    CodeAttribution,
    build_attribution_profile,
    code_attribution_rows,
    read_csv_samples,
    read_function_symbols,
    read_rocprof_samples,
    verify_disassembly,
)


def write_code_object(path: Path, functions: dict[str, tuple[int, int]]) -> None:
    """Writes a minimal little-endian ELF64 file with STT_FUNC symbols."""
    strtab = b"\0"
    name_offsets = {}
    for name in functions:
        name_offsets[name] = len(strtab)
        strtab += name.encode() + b"\0"
    symbols = b"\0" * 24
    for name, (address, size) in functions.items():
        symbols += struct.pack("<IBBHQQ", name_offsets[name], 0x12, 0, 1, address, size)
    header_size = 64
    symtab_offset = header_size
    strtab_offset = symtab_offset + len(symbols)
    section_offset = strtab_offset + len(strtab)
    sections = b"\0" * 64
    sections += struct.pack(
        "<IIQQQQIIQQ", 0, 2, 0, 0, symtab_offset, len(symbols), 2, 1, 8, 24
    )
    sections += struct.pack(
        "<IIQQQQIIQQ", 0, 3, 0, 0, strtab_offset, len(strtab), 0, 0, 1, 0
    )
    header = bytearray(header_size)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<Q", header, 0x28, section_offset)
    struct.pack_into("<HH", header, 0x3A, 64, 3)
    path.write_bytes(bytes(header) + symbols + strtab + sections)


ROWS = [
    {
        "function": "kernel",
        "symbol_offset": 0,
        "byte_size": 8,
        "kind": "packet",
        "source_file": "/src/kernel.cpp",
        "source_line": 10,
        "source_column": 3,
    },
    {
        "function": "kernel",
        "symbol_offset": 8,
        "byte_size": 4,
        "kind": "wait",
        "source_file": "/src/kernel.cpp",
        "source_line": 11,
        "source_column": 5,
        "wait_reason": "amdgpu.ssa_use",
        "wait_producer": "amdgpu.scratch_load_b32_vaddr",
        "scratch_access": "load",
        "scratch_storage": "kernel_spill_storage_7",
        "scratch_source_file": "/src/kernel.cpp",
        "scratch_source_line": 4,
        "spill_value": "bound",
        "spill_slot": 3,
    },
    {
        "function": "kernel",
        "symbol_offset": 12,
        "byte_size": 4,
        "kind": "wait",
        "source_file": "/src/kernel.cpp",
        "source_line": 11,
        "source_column": 5,
        "wait_reason": "amdgpu.ssa_use",
        "wait_producer": "amdgpu.scratch_load_b32_vaddr",
        "scratch_access": "load",
        "scratch_storage": "kernel_spill_storage_9",
        "scratch_source_file": "/src/kernel.cpp",
        "scratch_source_line": 6,
        "spill_value": "other",
        "spill_slot": 3,
    },
    {
        "function": "kernel",
        "symbol_offset": 16,
        "byte_size": 4,
        "kind": "wait",
        "source_file": "/src/kernel.cpp",
        "source_line": 12,
        "source_column": 1,
        "wait_reason": "amdgpu.ssa_use",
        "wait_producer": "amdgpu.scratch_load_b64_vaddr",
        "scratch_access": "load",
        "scratch_storage": "matrix",
        "scratch_source_file": "/src/kernel.cpp",
        "scratch_source_line": 2,
    },
]


def _attribution(tmp_path: Path) -> CodeAttribution:
    code_object = tmp_path / "kernel.hsaco"
    write_code_object(code_object, {"kernel": (0x1000, 20), "helper": (0x2000, 4)})
    return CodeAttribution(ROWS, read_function_symbols(code_object))


def test_lookup_finds_the_instruction_containing_an_address(tmp_path: Path) -> None:
    attribution = _attribution(tmp_path)
    assert attribution.lookup(0x1000)[1]["kind"] == "packet"
    # An address inside an eight-byte instruction maps to its start.
    assert attribution.lookup(0x1004) == (0x1000, ROWS[0])
    assert attribution.lookup(0x1008)[1]["spill_value"] == "bound"
    assert attribution.lookup(0x0FFC) is None
    assert attribution.lookup(0x1014) is None


def test_missing_rows_or_symbols_are_errors(tmp_path: Path) -> None:
    with pytest.raises(CompileReportError, match="code-attribution"):
        code_attribution_rows(SimpleNamespace(report={}, source="r.json"))
    code_object = tmp_path / "other.hsaco"
    write_code_object(code_object, {"other": (0x1000, 4)})
    with pytest.raises(CompileReportError, match="missing from the code object"):
        CodeAttribution(ROWS, read_function_symbols(code_object))


def test_profile_groups_by_storage_not_slot(tmp_path: Path) -> None:
    attribution = _attribution(tmp_path)
    # Both spill rows share slot 3 but hold different values.
    counts = {0x1008: 5, 0x100C: 3, 0x1010: 2, 0x1000: 1, 0x3000: 4}
    profile = build_attribution_profile(attribution, counts, ["scratch_source"])
    assert profile["total_samples"] == 15
    assert profile["unattributed_samples"] == 4
    assert profile["groups"] == [
        {"key": ["spill kernel_spill_storage_7 of kernel.cpp:4"], "samples": 5},
        {"key": ["spill kernel_spill_storage_9 of kernel.cpp:6"], "samples": 3},
        {"key": ["storage matrix of kernel.cpp:2"], "samples": 2},
        {"key": [""], "samples": 1},
    ]


def test_profile_rejects_unknown_keys(tmp_path: Path) -> None:
    attribution = _attribution(tmp_path)
    with pytest.raises(CompileReportError, match="unknown group keys"):
        build_attribution_profile(attribution, {}, ["slot"])


def _write_rocprof(path: Path) -> None:
    path.write_text(
        json.dumps(
            {
                "rocprofiler-sdk-tool": [
                    {
                        "kernel_symbols": [
                            {"code_object_id": 2, "kernel_name": "kernel.kd"},
                            {
                                "code_object_id": 4,
                                "kernel_name": "_ZN12_GLOBAL__N_16kernelEv.kd",
                            },
                        ],
                        "buffer_records": {
                            "pc_sample_host_trap": [
                                {
                                    "record": {
                                        "pc": {
                                            "code_object_id": 2,
                                            "code_object_offset": 0x1008,
                                        }
                                    }
                                },
                                {
                                    "record": {
                                        "pc": {
                                            "code_object_id": 2,
                                            "code_object_offset": 0x1008,
                                        }
                                    }
                                },
                                {
                                    "record": {
                                        "pc": {
                                            "code_object_id": 4,
                                            "code_object_offset": 0x1008,
                                        }
                                    }
                                },
                            ]
                        },
                    }
                ]
            }
        )
    )


def test_rocprof_samples_select_the_exact_kernel(tmp_path: Path) -> None:
    profile = tmp_path / "results.json"
    _write_rocprof(profile)
    # A reference implementation of the same kernel is not mixed in.
    assert read_rocprof_samples(profile, ["kernel"]) == {0x1008: 2}
    with pytest.raises(CompileReportError, match="several code objects"):
        read_rocprof_samples(profile, ["kernel"], kernel="kernel")
    with pytest.raises(CompileReportError, match="no profiled kernel"):
        read_rocprof_samples(profile, ["absent"])


def test_csv_samples(tmp_path: Path) -> None:
    samples = tmp_path / "samples.csv"
    samples.write_text("# offset,count\n0x1008,3\n4104\n0x1008,2\n")
    assert read_csv_samples(samples) == {0x1008: 6}


def test_verify_disassembly(tmp_path: Path) -> None:
    attribution = _attribution(tmp_path)
    lines = [
        "\tv_add_f64 v[0:1], v[2:3], v[4:5] // 000000001000: D2800000 00000000",
        "\ts_waitcnt vmcnt(0)// 000000001008: BF8C0F70",
        "\ts_waitcnt vmcnt(0) // 00000000100C: BF8C0F70",
        "\ts_waitcnt vmcnt(0) // 000000001010: BF8C0F70",
    ]
    result = verify_disassembly(attribution, lines)
    assert result["ok"]
    assert result["scratch_rows"] == 3
    assert result["spill_rows"] == 2
    bad = verify_disassembly(
        attribution, [*lines[:3], lines[3].replace("1010", "1012")]
    )
    assert not bad["ok"]
    assert bad["mismatches"] == ["0x1012: s_waitcnt vmcnt(0)"]
