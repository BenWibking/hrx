# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""IEEE width conversions with exact host rounding and NaN classification."""

import argparse
import math
import random
import struct
from pathlib import Path

from loom.gen.test.kernel_fixture import Arrays, Case, signed_bits


def float_bits(value, width):
    return int.from_bytes(struct.pack("<f" if width == 32 else "<d", value), "little")


def float_value(bits, width):
    return struct.unpack("<f" if width == 32 else "<d", bits.to_bytes(width // 8, "little"))[0]


def round_f32(value):
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def numeric_inputs(width):
    if width == 32:
        positive = [
            0,
            1,
            2,
            0x3FFFFF,
            0x7FFFFF,
            0x800000,
            0x800001,
            0x3F7FFFFF,
            0x3F800000,
            0x3F800001,
            0x4B7FFFFF,
            0x4B800000,
            0x7F7FFFFF,
            0x7F800000,
            0x12345678,
            0x76543210,
        ]
    else:
        # Halfway values straddle zero, the subnormal/normal boundary, and
        # adjacent even/odd significands. Both signs use the same exact bits.
        half_subnormal = math.ldexp(1.0, -150)
        minimum_normal = math.ldexp(1.0, -126)
        halfway = 1.0 + math.ldexp(1.0, -24)
        positive = [
            float_bits(value, 64)
            for value in (
                0.0,
                math.ldexp(1.0, -1074),
                math.ldexp(1.0, -1022),
                half_subnormal,
                math.nextafter(half_subnormal, math.inf),
                3 * half_subnormal,
                5 * half_subnormal,
                minimum_normal - 2 * half_subnormal,
                minimum_normal - half_subnormal,
                minimum_normal,
                math.nextafter(halfway, 0.0),
                halfway,
                math.nextafter(halfway, math.inf),
                1.0 + 3 * math.ldexp(1.0, -24),
                float_value(0x7F7FFFFF, 32),
                math.ldexp(1.0, 128),
                math.inf,
            )
        ]
    bits = positive + [value | (1 << (width - 1)) for value in positive]
    random_source = random.Random(width)
    fraction_bits = 23 if width == 32 else 52
    exponent_limit = 255 if width == 32 else 2047
    while len(bits) < 64:
        value = random_source.getrandbits(width)
        if (value >> fraction_bits) & exponent_limit != exponent_limit:
            bits.append(value)
    return bits


def conversion_case(arrays, source_width, *, nan):
    result_width = 64 if source_width == 32 else 32
    name = f"scalar_f{source_width}_to_f{result_width}_{'nan' if nan else 'values'}"
    case = Case(arrays, name, f"f{result_width}", 128)
    if nan:
        fraction_bits = 23 if source_width == 32 else 52
        exponent_bits = 8 if source_width == 32 else 11
        exponent = ((1 << exponent_bits) - 1) << fraction_bits
        quiet = 1 << (fraction_bits - 1)
        sign = 1 << (source_width - 1)
        bits = [exponent | quiet, exponent | 1, sign | exponent | quiet | 123, sign | exponent | 1] * 16
        expected = [math.nan] * 128
        uniform = "nan"
    else:
        bits = numeric_inputs(source_width)
        values = [float_value(value, source_width) for value in bits]
        expected = values if source_width == 32 else [round_f32(value) for value in values]
        uniform = "-0.0" if source_width == 32 else repr(1.0 + math.ldexp(1.0, -24))
        expected += [-0.0 if source_width == 32 else 1.0] * 64
    # Integer storage preserves signaling NaNs without a host floating load.
    case.array("bits", [signed_bits(value, source_width) for value in bits], f"i{source_width}")
    case.lines.append(f"  %input = check.tensor.view %bits offset(0) : tensor<64xi{source_width}> -> tensor<64xf{source_width}>")
    case.scalar("uniform", uniform, f"f{source_width}")
    case.launch(f"scalar_f{source_width}_to_f{result_width}", "%input, %uniform, %output", f"tensor<64xf{source_width}>, f{source_width}, tensor<128xf{result_width}>")
    return case.finish(expected, tolerance=0.0 if nan else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arrays", type=Path, required=True)
    options = parser.parse_args()
    arrays = Arrays(options.arrays, options.output.parent)
    declarations = "".join(f"kernel.decl @scalar_f{source}_to_f{result}() launch(%input: buffer, %uniform: f{source}, %output: buffer)\n" for source, result in ((32, 64), (64, 32)))
    cases = [conversion_case(arrays, width, nan=nan) for width in (32, 64) for nan in (False, True)]
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(declarations + "\n" + "\n".join(cases))


if __name__ == "__main__":
    main()
