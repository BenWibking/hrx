// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdio.h>
#include <string.h>

#include "build_tools/bazel/test/executable_fixture/library.h"

int main(int argc, char** argv) {
  if (argc != 1 &&
      (argc != 2 || strcmp(argv[1], "argument with spaces") != 0)) {
    fprintf(stderr, "expected no arguments or one 'argument with spaces'\n");
    return 2;
  }
  return executable_fixture_value() == 42 ? 0 : 1;
}
