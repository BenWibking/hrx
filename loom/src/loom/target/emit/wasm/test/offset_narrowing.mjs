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

const maximum = 0xffffffffn;
const origins = [0n, 4n, 4096n, 0x7fffffffn, 0x80000000n, maximum - 4n, maximum];
const positions = [-2147483648, -1073741824, -1024, -1, 0, 1, 1024, 1073741823,
                   2147483647];
for (const origin of origins) {
  for (const position of positions) {
    const expected = origin + BigInt(position) * 4n;
    if (expected < 0n || expected > maximum) continue;
    assert.equal(BigInt(exports.native_origin(Number(origin), position) >>> 0),
                 expected, `origin ${origin}, position ${position}`);
  }
}

// Exercise bit 31 through payload conversion and widening.
for (const word of [0, 1, 0x3fffffff, 0x7ffffffe, 0x7fffffff]) {
  const expected = BigInt(word) + 0x80000000n;
  const [payload, widened] = exports.unsigned_half_origin(word);
  assert.equal(BigInt(payload >>> 0), expected);
  assert.equal(widened, expected);
}

for (const word of [0, 1, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff]) {
  for (const expected of origins) {
    const origin = expected - BigInt(word);
    const [payload, observed] = exports.observed_origin(word, origin);
    assert.equal(BigInt(payload >>> 0), expected);
    assert.equal(observed, expected);
  }
}

// Deterministic samples retain the valid full numeric result while varying the
// sign of the scaled position independently from the offset's high payload bit.
let state = 0x13579bdf;
for (let i = 0; i < 16384; ++i) {
  state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
  const position = (state % 0x7fffffff) - 0x3fffffff;
  const displacement = BigInt(position) * 4n;
  const minimumOrigin = displacement < 0n ? -displacement : 0n;
  const maximumOrigin = displacement > 0n ? maximum - displacement : maximum;
  state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
  const origin = minimumOrigin + BigInt(state) % (maximumOrigin - minimumOrigin + 1n);
  assert.equal(BigInt(exports.native_origin(Number(origin), position) >>> 0),
               origin + displacement);
}
