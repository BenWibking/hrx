# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Independent raw-bit oracles for native and residual transposition."""

import struct
import sys
from pathlib import Path

SPECIAL_BITS = (
    0x0000,
    0x8000,
    0x0001,
    0x8001,
    0x007F,
    0x807F,
    0x0080,
    0x8080,
    0x3F80,
    0xBF80,
    0x3FC0,
    0xBFC0,
    0x7F7F,
    0xFF7F,
    0x7F80,
    0xFF80,
    0x7F81,
    0xFF81,
    0x7FC1,
    0xFFC1,
    0x3E80,
    0xBE80,
    0x4000,
    0xC000,
    0x1234,
    0x5678,
    0x9ABC,
    0xDEF0,
    0x5555,
    0xAAAA,
    0x2468,
    0x1357,
)


def transpose(values, row_count, column_count):
    assert len(values) == row_count * column_count
    result = [0] * len(values)
    for row in range(row_count):
        for column in range(column_count):
            result[row_count * column + row] = values[column_count * row + column]
    return result


def make_words(changed):
    # The odd multiplier permutes all 65,536 bit patterns for the changed case.
    exhaustive = [
        (position * 4051 + 0x9E37) & 0xFFFF if changed else position
        for position in range(65536)
    ]
    assert len(set(exhaustive)) == 65536
    special = tuple(reversed(SPECIAL_BITS)) if changed else SPECIAL_BITS
    words = list(exhaustive)
    for rotation in range(len(special)):
        words.extend(special[(lane + rotation) % len(special)] for lane in range(64))
    assert len(words) == 1056 * 64
    return words


def main():
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    head_guard = bytes([0xD3]) * 64
    tail_guard = bytes([0xA5]) * 64
    for name, changed in (("original", False), ("changed", True)):
        words = make_words(changed)
        expected = []
        for start in range(0, len(words), 64):
            packet = words[start : start + 64]
            result = transpose(packet, 8, 8)
            # Check the scalar coordinate oracle independently of its loops.
            assert result == [packet[(lane % 8) * 8 + lane // 8] for lane in range(64)]
            assert transpose(result, 8, 8) == packet
            expected.extend(result)
        source = struct.pack(f"<{len(words)}H", *words)
        result = struct.pack(f"<{len(expected)}H", *expected)
        (directory / f"{name}-input.bin").write_bytes(head_guard + source + tail_guard)
        (directory / f"{name}-expected.bin").write_bytes(
            head_guard + result + tail_guard
        )
    (directory / "output.bin").write_bytes(
        head_guard + bytes([0x5A]) * (1056 * 128) + tail_guard
    )

    residual_records = []
    residual_expected = []
    for bit in range(7):
        record = [0x81 + bit if lane & (1 << bit) else 0 for lane in range(128)]
        ordinary = transpose(record, 8, 16)
        predicate = transpose([int(value != 0) for value in record], 8, 16)
        assert transpose(ordinary, 16, 8) == record
        residual_records.extend(record)
        residual_expected.extend((*ordinary, *predicate))
    (directory / "residual-input.bin").write_bytes(
        head_guard + bytes(residual_records) + tail_guard
    )
    (directory / "residual-expected.bin").write_bytes(
        head_guard + bytes(residual_expected) + tail_guard
    )
    (directory / "residual-output.bin").write_bytes(
        head_guard + bytes([0x5A]) * len(residual_expected) + tail_guard
    )


if __name__ == "__main__":
    main()
