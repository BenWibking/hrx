# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""All independent four-lane Boolean triples with guarded numeric consumers."""

import argparse
from pathlib import Path

from loom.gen.test.kernel_fixture import Arrays, Case


def selection(arrays):
    conditions = []
    true_values = []
    false_values = []
    expected = []
    for item in range(4096):
        selected = []
        for lane in range(4):
            condition = bool(item & (1 << lane))
            true_value = bool(item & (1 << (lane + 4)))
            false_value = bool(item & (1 << (lane + 8)))
            conditions.append(17 + lane if condition else 0)
            true_values.append(21 + lane if true_value else 0)
            false_values.append(25 + lane if false_value else 0)
            selected.append(true_value if condition else false_value)
        expected.extend(0x513579BD if value else -0x1234567 for value in selected)
        expected.extend([-int(selected[0]), int(selected[3])])
        expected.extend([int(selected[2]) if item % 64 < 56 else -123, -123])

    case = Case(arrays, "predicate_select_values", "i32", len(expected))
    for name, values in [
        ("conditions", conditions),
        ("true_input", true_values),
        ("false_input", false_values),
    ]:
        case.array(name, values)
    case.launch(
        "predicate_select",
        "%conditions, %true_input, %false_input, %output",
        "tensor<16384xi32>, tensor<16384xi32>, tensor<16384xi32>, tensor<32768xi32>",
    )
    declaration = "kernel.decl @predicate_select() launch(%conditions: buffer, %true_input: buffer, %false_input: buffer, %output: buffer)\n\n"
    return declaration + case.finish(expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arrays", type=Path, required=True)
    options = parser.parse_args()
    arrays = Arrays(options.arrays, options.output.parent)
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(selection(arrays))


if __name__ == "__main__":
    main()
