// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

const binary = readFileSync(process.argv[2]);
assert.ok(WebAssembly.validate(binary));
const {instance: {exports}} = await WebAssembly.instantiate(binary);

function checkWord(word) {
  const unsigned = BigInt(word >>> 0);
  const signed = unsigned < 2147483648n ? unsigned : unsigned - 4294967296n;
  assert.equal(exports.widen_signed_word(word), signed, `signed ${word}`);
  assert.equal(exports.widen_unsigned_word(word), unsigned, `unsigned ${word}`);

  const base = 0x123456789ABCDEFn;
  assert.equal(exports.add_signed_word(base, word), base + signed,
               `displacement ${word}`);
  const high = (word ^ 0xA5A5A5A5) >>> 0;
  assert.equal(exports.assemble_wide_word(word, high),
               BigInt.asIntN(64, BigInt(high) * 4294967296n + unsigned),
               `packed ${word}`);
}

// Every half-word appears in each position, including both signed halves of
// the input domain and all-ones upper bits.
for (let half = 0; half < 65536; ++half) {
  for (const word of [half, half * 65536, half + 0x80000000, half + 0xFFFF0000]) {
    checkWord(word);
  }
}

// Wide record coordinates preserve unsigned word interpretation and discard
// the original counter's upper bits before multiplying.
for (const word of [0, 1, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF]) {
  for (const base of [0n, 1n << 40n, -(1n << 63n), (1n << 63n) - 1n]) {
    for (const stride of [0n, 1n, 12n, (1n << 32n) + 3n]) {
      const expected = BigInt.asIntN(64, base + BigInt(word) * stride);
      assert.equal(exports.unsigned_record_address(base, word, stride), expected);
      for (const upper of [0n, 0xA5A5A5A500000000n, 0xFFFFFFFF00000000n]) {
        const counter = BigInt.asIntN(64, upper + BigInt(word));
        assert.equal(exports.wrapped_record_address(base, counter, stride), expected);
      }
    }
  }
}

// The signed displacement addresses both sides of the origin. The last root
// puts the final word exactly at the end of the initial linear-memory page.
for (const root of [0, 4, 4096, 65500]) {
  for (let position = -4; position <= 4; ++position) {
    for (const replacement of [0, -2147483648, 2147483647, -1]) {
      const expected = new Uint8Array(36).fill(0xA7);
      const words = new DataView(expected.buffer);
      const address = 16 + position * 4;
      words.setInt32(address, -123456789, true);
      expected.forEach((value, i) => exports.write_byte(root, i, value));
      assert.equal(exports.exchange_signed_position(root, position, replacement),
                   -123456789, `load ${root}, ${position}`);
      words.setInt32(address, replacement, true);
      const actual = Uint8Array.from({length: expected.length},
                                    (_, i) => exports.read_byte(root, i));
      assert.deepEqual(actual, expected, `store ${root}, ${position}`);
    }
  }
}
