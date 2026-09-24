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
const expected = Uint8Array.from({length: 65536}, (_, i) => (i * 37 + (i >>> 8) + 13) & 255);
const words = new DataView(expected.buffer);
expected.forEach((value, i) => exports.write_byte(0, i, value));
const checkMemory = label => assert.deepEqual(
    Uint8Array.from({length: expected.length}, (_, i) => exports.read_byte(0, i)),
    expected, label);

// A zero root exposes wrapping a negative dynamic part before adding a memory
// immediate. The last root places the final word exactly at the memory end.
for (const root of [0, 4, 4096, 65500]) {
  for (let position = -4; position <= 4; ++position) {
    const address = root + (position + 4) * 4;
    for (const replacement of [0, 0x12345678, 0x80000000, 0xFFFFFFFF]) {
      for (const [name, argument] of [
        ['exchange_biased_word', position],
        ['exchange_large_bias_word', position - 1073741816],
        ...(position >= 0 ? [['exchange_nonnegative_word', position]] : []),
      ]) {
        const previous = words.getInt32(address, true);
        assert.equal(exports[name](root, argument, replacement), previous,
                     `${name}: root=${root}, position=${position}`);
        words.setUint32(address, replacement, true);
        checkMemory(name);
      }
    }
  }
}

// Distinct roots, overlapping views and both exact-end transfer directions
// exercise vector addressing without exposing v128 in the JavaScript ABI.
for (const [source, destination] of [
  [0, 256], [256, 0], [4096, 4100], [4100, 4096],
  [65488, 8192], [8192, 65488],
]) {
  for (let position = -4; position <= 4; ++position) {
    const offset = (position + 4) * 4;
    const values = expected.slice(source + offset, source + offset + 16);
    exports.copy_biased_vector(source, destination, position);
    expected.set(values, destination + offset);
    checkMemory(`copy_biased_vector: ${source} -> ${destination}, ${position}`);
  }
}
