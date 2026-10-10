# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Attributes sampled program counters to compile report code rows.

A compile report requested with code attribution carries one
`code_attribution_rows` entry per emitted native instruction, keyed by its
byte offset within the function symbol. Joining those rows with the code
object's function symbols maps each sampled code-object offset to the
operation, source location, wait reason and scratch data that produced the
instruction it landed on.
"""

from __future__ import annotations

import bisect
import collections
import csv
import json
import re
import struct
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path

from loom.reporting.compile_report import CompileReportDocument, CompileReportError

CodeRow = Mapping[str, object]


def _source_text(file: object, line: object, column: object | None) -> str:
    text = f"{str(file).rsplit('/', 1)[-1]}:{line}"
    return text if column is None else f"{text}:{column}"


def _row_source(row: CodeRow, *, column: bool) -> str:
    if "source_line" not in row:
        return "<unknown>"
    return _source_text(
        row["source_file"], row["source_line"], row["source_column"] if column else None
    )


def _scratch_source(row: CodeRow) -> str:
    """Names the data a scratch access touches by storage and source location.

    Spill slot ordinals are reused across spill batches, so spills are named by
    their unique storage reservation rather than by slot.
    """
    if "scratch_access" not in row:
        return ""
    where = (
        _source_text(row["scratch_source_file"], row["scratch_source_line"], None)
        if "scratch_source_line" in row
        else "<unknown>"
    )
    storage = str(row.get("scratch_storage") or "<unresolved>")
    what = "spill" if "spill_slot" in row else "storage"
    return f"{what} {storage} of {where}"


GROUP_KEYS: dict[str, Callable[[CodeRow], str]] = {
    "source": lambda row: _row_source(row, column=True),
    "line": lambda row: _row_source(row, column=False),
    "kind": lambda row: str(row.get("kind", "")),
    "wait": lambda row: str(row.get("wait_reason", "")),
    "producer": lambda row: str(row.get("wait_producer", "")),
    "block": lambda row: f"{row.get('function', '')}:{row.get('block_index', '')}",
    "descriptor": lambda row: str(
        row.get("descriptor_key") or row.get("operation", "")
    ),
    "function": lambda row: str(row.get("function", "")),
    "scratch": lambda row: str(row.get("scratch_access", "")),
    "storage": lambda row: str(row.get("scratch_storage", "")),
    "scratch_source": _scratch_source,
}

DEFAULT_GROUP_BY = ("line", "wait", "producer")


def code_attribution_rows(document: CompileReportDocument) -> tuple[CodeRow, ...]:
    """Returns the report's code attribution rows or raises when absent."""
    rows = document.report.get("code_attribution_rows")
    if not isinstance(rows, list):
        raise CompileReportError(
            f"{document.source} has no code_attribution_rows; compile with "
            "--compile-report=details --compile-report-code-attribution"
        )
    return tuple(rows)


def read_function_symbols(path: Path) -> dict[str, tuple[int, int]]:
    """Returns {name: (address, size)} for the STT_FUNC symbols of an ELF64 file."""
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise CompileReportError(f"cannot read {path}: {exc}") from exc
    if data[:4] != b"\x7fELF" or len(data) < 64 or data[4] != 2:
        raise CompileReportError(f"{path} is not an ELF64 code object")
    endian = "<" if data[5] == 1 else ">"
    (section_offset,) = struct.unpack_from(endian + "Q", data, 0x28)
    section_entry_size, section_count = struct.unpack_from(endian + "HH", data, 0x3A)
    sections = [
        struct.unpack_from(
            endian + "IIQQQQIIQQ", data, section_offset + i * section_entry_size
        )
        for i in range(section_count)
    ]
    symbols: dict[str, tuple[int, int]] = {}
    for _, kind, _, _, offset, size, link, _, _, entry_size in sections:
        if kind != 2 or entry_size == 0:  # SHT_SYMTAB
            continue
        string_offset = sections[link][4]
        for index in range(size // entry_size):
            name_offset, info, _, _, value, symbol_size = struct.unpack_from(
                endian + "IBBHQQ", data, offset + index * entry_size
            )
            if info & 0xF != 2:  # STT_FUNC
                continue
            start = string_offset + name_offset
            symbols[data[start : data.index(b"\0", start)].decode()] = (
                value,
                symbol_size,
            )
    return symbols


@dataclass(frozen=True)
class _FunctionRows:
    start: int
    end: int
    offsets: list[int]
    rows: list[CodeRow]


class CodeAttribution:
    """Maps code-object offsets to the code attribution rows covering them."""

    def __init__(
        self, rows: Iterable[CodeRow], symbols: Mapping[str, tuple[int, int]]
    ) -> None:
        rows_by_function: dict[str, list[CodeRow]] = collections.defaultdict(list)
        for row in rows:
            rows_by_function[str(row["function"])].append(row)
        missing = sorted(set(rows_by_function) - set(symbols))
        if missing:
            raise CompileReportError(
                f"report functions missing from the code object: {missing}"
            )
        self.functions: list[_FunctionRows] = []
        for name, function_rows in rows_by_function.items():
            address, size = symbols[name]
            function_rows.sort(key=lambda row: int(row["symbol_offset"]))
            self.functions.append(
                _FunctionRows(
                    start=address,
                    end=address + size,
                    offsets=[
                        address + int(row["symbol_offset"]) for row in function_rows
                    ],
                    rows=function_rows,
                )
            )
        self.functions.sort(key=lambda function: function.start)
        self._starts = [function.start for function in self.functions]

    @property
    def function_names(self) -> frozenset[str]:
        return frozenset(str(f.rows[0]["function"]) for f in self.functions)

    def lookup(self, address: int) -> tuple[int, CodeRow] | None:
        """Returns (instruction address, row) for the instruction at |address|."""
        index = bisect.bisect_right(self._starts, address) - 1
        if index < 0:
            return None
        function = self.functions[index]
        if address >= function.end:
            return None
        row_index = bisect.bisect_right(function.offsets, address) - 1
        if row_index < 0:
            return None
        row = function.rows[row_index]
        start = function.offsets[row_index]
        if address >= start + int(row["byte_size"]):
            return None
        return start, row


def read_rocprof_samples(
    path: Path, function_names: Iterable[str], kernel: str | None = None
) -> collections.Counter[int]:
    """Counts host-trap PC samples from the code object holding the functions.

    Code objects are selected by exact kernel-symbol name, so profiles that also
    ran another implementation of the same kernel (such as a HIP reference) do
    not mix their samples into this code object's offsets.
    """
    try:
        with path.open("r", encoding="utf-8") as file:
            tool = json.load(file)["rocprofiler-sdk-tool"][0]
    except (OSError, json.JSONDecodeError, KeyError, IndexError) as exc:
        raise CompileReportError(
            f"{path} is not a rocprofiler-sdk result: {exc}"
        ) from exc
    names = frozenset(function_names)
    code_objects = set()
    for symbol in tool["kernel_symbols"]:
        name = symbol["kernel_name"].removesuffix(".kd")
        if (kernel in name) if kernel is not None else (name in names):
            code_objects.add(symbol["code_object_id"])
    if not code_objects:
        raise CompileReportError(
            f"no profiled kernel in {path} matches the report functions {sorted(names)}"
        )
    if len(code_objects) > 1:
        raise CompileReportError(
            f"several code objects in {path} match: {sorted(code_objects)}; "
            "select one with --kernel"
        )
    counts: collections.Counter[int] = collections.Counter()
    for sample in tool["buffer_records"].get("pc_sample_host_trap", []):
        pc = sample["record"]["pc"]
        if pc["code_object_id"] in code_objects:
            counts[int(pc["code_object_offset"])] += 1
    return counts


def read_csv_samples(path: Path) -> collections.Counter[int]:
    """Reads `code_object_offset[,count]` lines; `#` starts a comment line."""
    counts: collections.Counter[int] = collections.Counter()
    try:
        with path.open(newline="", encoding="utf-8") as file:
            for record in csv.reader(file):
                if not record or record[0].lstrip().startswith("#"):
                    continue
                counts[int(record[0], 0)] += int(record[1]) if len(record) > 1 else 1
    except (OSError, ValueError) as exc:
        raise CompileReportError(f"cannot read samples from {path}: {exc}") from exc
    return counts


def build_attribution_profile(
    attribution: CodeAttribution,
    counts: Mapping[int, int],
    group_by: Iterable[str] = DEFAULT_GROUP_BY,
) -> dict[str, object]:
    """Aggregates sample counts by the requested row keys."""
    keys = tuple(group_by)
    unknown = [key for key in keys if key not in GROUP_KEYS]
    if unknown:
        raise CompileReportError(
            f"unknown group keys {unknown}; expected some of {sorted(GROUP_KEYS)}"
        )
    groups: collections.Counter[tuple[str, ...]] = collections.Counter()
    total = 0
    unattributed = 0
    for address, count in counts.items():
        total += count
        found = attribution.lookup(address)
        if found is None:
            unattributed += count
            continue
        _, row = found
        groups[tuple(GROUP_KEYS[key](row) for key in keys)] += count
    return {
        "total_samples": total,
        "unattributed_samples": unattributed,
        "group_by": list(keys),
        "groups": [
            {"key": list(key), "samples": samples}
            for key, samples in sorted(
                groups.items(), key=lambda item: (-item[1], item[0])
            )
        ],
    }


def format_attribution_profile_text(
    profile: Mapping[str, object], *, top: int = 30
) -> str:
    total = int(profile["total_samples"])
    attributed = total - int(profile["unattributed_samples"])
    lines = [
        f"samples {total}, attributed {attributed} ({attributed / max(total, 1):.1%})",
        f"{'share':>7}  {'samples':>8}  " + "  ".join(profile["group_by"]),
    ]
    for group in list(profile["groups"])[:top]:
        samples = int(group["samples"])
        lines.append(
            f"{samples / max(total, 1):7.2%}  {samples:8d}  "
            + "  ".join(value or "-" for value in group["key"])
        )
    return "\n".join(lines) + "\n"


_DISASSEMBLY_ADDRESS = re.compile(r"^\s+(.*?)\s*//\s*([0-9A-Fa-f]{12}):")


def verify_disassembly(
    attribution: CodeAttribution, disassembly: Iterable[str]
) -> dict[str, object]:
    """Checks rows against `llvm-objdump -d` instruction addresses.

    Every disassembled instruction must start a row and every row must start a
    disassembled instruction.
    """
    checked = 0
    mismatches: list[str] = []
    for line in disassembly:
        match = _DISASSEMBLY_ADDRESS.search(line)
        if not match:
            continue
        address = int(match.group(2), 16)
        checked += 1
        found = attribution.lookup(address)
        if found is None or found[0] != address:
            mismatches.append(f"{address:#x}: {match.group(1)}")
    rows = [row for function in attribution.functions for row in function.rows]
    scratch = [row for row in rows if row.get("scratch_access")]
    return {
        "instructions": checked,
        "rows": len(rows),
        "mismatches": mismatches,
        "scratch_rows": len(scratch),
        "scratch_rows_resolved": sum(
            1 for row in scratch if row.get("scratch_storage")
        ),
        "spill_rows": sum(1 for row in scratch if "spill_slot" in row),
        "ok": not mismatches and checked == len(rows),
    }


def format_disassembly_verification_text(result: Mapping[str, object]) -> str:
    lines = [
        f"checked {result['instructions']} instructions against "
        f"{result['rows']} rows: {len(result['mismatches'])} mismatched",
        f"scratch access rows (own or waited-on): {result['scratch_rows']}, "
        f"storage resolved: {result['scratch_rows_resolved']}, "
        f"spill storage: {result['spill_rows']}",
    ]
    lines.extend(f"  no row starts at {item}" for item in result["mismatches"][:10])
    return "\n".join(lines) + "\n"
