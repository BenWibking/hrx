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
const write = (base, bytes) => bytes.forEach((value, i) => exports.write_byte(base, i, value));
const read = (base, length) => Uint8Array.from({length}, (_, i) => exports.read_byte(base, i));
const truePayload = 0x513579BD;
const falsePayload = -0x1234567;

// Every four-lane Boolean pattern reaches both vector and scalar consumers.
for (let conditions = 0; conditions < 16; ++conditions) {
  for (let trueValues = 0; trueValues < 16; ++trueValues) {
    for (let falseValues = 0; falseValues < 16; ++falseValues) {
      const inputs = [conditions, trueValues, falseValues].map((bits, input) => {
        const bytes = new Uint8Array(48).fill(0x73 + input);
        const view = new DataView(bytes.buffer);
        for (let lane = 0; lane < 4; ++lane) {
          view.setInt32(16 + lane * 4, bits & (1 << lane) ? 17 + input * 4 + lane : 0, true);
        }
        write(256 + input * 64, bytes);
        return bytes;
      });
      const expected = new Uint8Array(48).fill(0xA5);
      write(512, expected);
      const selected = Array.from({length: 4}, (_, lane) => {
        const bit = 1 << lane;
        return Boolean((conditions & bit ? trueValues : falseValues) & bit);
      });
      const output = new DataView(expected.buffer);
      selected.forEach((value, lane) => output.setInt32(
        16 + lane * 4, value ? truePayload : falsePayload, true));
      const context = `conditions=${conditions}, true=${trueValues}, false=${falseValues}`;
      assert.deepEqual(exports.predicate_select_values(272, 336, 400, 528, 0,
                                                       truePayload, falsePayload),
                       [selected[0] ? -1 : 0, Number(selected[3])], context);
      assert.deepEqual(read(512, expected.length), expected, context);
      inputs.forEach((bytes, input) => assert.deepEqual(
        read(256 + input * 64, bytes.length), bytes, context));
    }
  }
}
