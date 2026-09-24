# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""All four-lane Boolean pairs with vector, scalar, and divergent consumers."""

import argparse
from pathlib import Path

from loom.gen.test.kernel_fixture import Arrays, Case


def bitwise(arrays):
    lhs_values = []
    rhs_values = []
    expected = []
    for item in range(256):
        lhs = [bool(item & (1 << lane)) for lane in range(4)]
        rhs = [bool(item & (1 << (lane + 4))) for lane in range(4)]
        lhs_values.extend(17 + lane if value else 0 for lane, value in enumerate(lhs))
        rhs_values.extend(21 + lane if value else 0 for lane, value in enumerate(rhs))
        for result in (
            [a and b for a, b in zip(lhs, rhs, strict=True)],
            [a or b for a, b in zip(lhs, rhs, strict=True)],
            [a != b for a, b in zip(lhs, rhs, strict=True)],
        ):
            expected.extend(0x513579BD if value else -0x1234567 for value in result)
            expected.extend([-int(result[0]), int(result[3])])
            expected.extend([int(result[2]) if item % 64 < 56 else -123, -123])

    case = Case(arrays, "predicate_bitwise_values", "i32", len(expected))
    case.array("lhs", lhs_values)
    case.array("rhs", rhs_values)
    case.launch(
        "predicate_bitwise",
        "%lhs, %rhs, %output",
        "tensor<1024xi32>, tensor<1024xi32>, tensor<6144xi32>",
    )
    declaration = "kernel.decl @predicate_bitwise() launch(%lhs_input: buffer, %rhs_input: buffer, %output: buffer)\n\n"
    return declaration + case.finish(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arrays", type=Path, required=True)
    options = parser.parse_args()
    arrays = Arrays(options.arrays, options.output.parent)
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(bitwise(arrays))


if __name__ == "__main__":
    main()
