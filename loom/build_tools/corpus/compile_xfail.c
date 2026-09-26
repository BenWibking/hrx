// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iree/base/api.h"
#include "iree/base/tooling/flags.h"
#include "iree/io/file_contents.h"
#include "loom/build_tools/corpus/diagnostic_report.h"
#include "loom/target/tool/process.h"

IREE_FLAG(string, compiler, "", "Exact filesystem path to loom-compile.");
IREE_FLAG_NAMED(string, expected_diagnostic, "expected-diagnostic", "",
                "Required compiler diagnostic identity as DOMAIN/NNN.");
IREE_FLAG_NAMED(string, stamp_output, "stamp-output", "",
                "Output stamp written only after the expected failure.");

static bool loom_corpus_argument_is_flag(iree_string_view_t argument,
                                         iree_string_view_t flag) {
  return iree_string_view_equal(argument, flag) ||
         (argument.size > flag.size && argument.data[flag.size] == '=' &&
          iree_string_view_starts_with(argument, flag));
}

static iree_status_t loom_corpus_validate_compiler_arguments(int argument_count,
                                                             char** arguments) {
  if (argument_count == 0) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "loom-compile arguments are required");
  }
  const iree_string_view_t owned_flags[] = {
      IREE_SV("--output"),
      IREE_SV("--compile-report"),
      IREE_SV("--compile-report-output"),
  };
  for (int i = 0; i < argument_count; ++i) {
    const iree_string_view_t argument = iree_make_cstring_view(arguments[i]);
    for (iree_host_size_t j = 0; j < IREE_ARRAYSIZE(owned_flags); ++j) {
      if (loom_corpus_argument_is_flag(argument, owned_flags[j])) {
        return iree_make_status(
            IREE_STATUS_INVALID_ARGUMENT,
            "corpus diagnostic probe owns compiler flag '%.*s'",
            (int)owned_flags[j].size, owned_flags[j].data);
      }
    }
  }
  return iree_ok_status();
}

static iree_status_t loom_corpus_make_output_argument(
    iree_string_view_t output_path, iree_allocator_t allocator,
    char** out_argument) {
  *out_argument = NULL;
  const iree_string_view_t prefix = IREE_SV("--output=");
  iree_host_size_t argument_length = 0;
  iree_host_size_t allocation_size = 0;
  if (!iree_host_size_checked_add(prefix.size, output_path.size,
                                  &argument_length) ||
      !iree_host_size_checked_add(argument_length, 1, &allocation_size)) {
    return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                            "compiler output argument is too long");
  }
  char* argument = NULL;
  IREE_RETURN_IF_ERROR(
      iree_allocator_malloc(allocator, allocation_size, (void**)&argument));
  memcpy(argument, prefix.data, prefix.size);
  memcpy(argument + prefix.size, output_path.data, output_path.size);
  argument[argument_length] = '\0';
  *out_argument = argument;
  return iree_ok_status();
}

static void loom_corpus_print_compiler_stderr(
    const loom_tool_output_t* compiler_stderr) {
  if (compiler_stderr->length == 0) {
    return;
  }
  fputs("loom-compile stderr:\n", stderr);
  fwrite(compiler_stderr->data, 1, compiler_stderr->length, stderr);
  if (compiler_stderr->data[compiler_stderr->length - 1] != '\n') {
    fputc('\n', stderr);
  }
}

static iree_status_t loom_corpus_compile_xfail(int argc, char** argv) {
  const iree_string_view_t compiler_path =
      iree_make_cstring_view(FLAG_compiler);
  const iree_string_view_t expected_diagnostic_text =
      iree_make_cstring_view(FLAG_expected_diagnostic);
  const iree_string_view_t stamp_output_path =
      iree_make_cstring_view(FLAG_stamp_output);
  if (iree_string_view_is_empty(compiler_path)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "--compiler is required");
  }
  if (iree_string_view_is_empty(expected_diagnostic_text)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "--expected-diagnostic is required");
  }
  if (iree_string_view_is_empty(stamp_output_path)) {
    return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                            "--stamp-output is required");
  }
  IREE_RETURN_IF_ERROR(
      loom_corpus_validate_compiler_arguments(argc - 1, argv + 1));

  loom_corpus_diagnostic_id_t expected_diagnostic = {0};
  IREE_RETURN_IF_ERROR(loom_corpus_diagnostic_id_parse(expected_diagnostic_text,
                                                       &expected_diagnostic));

  iree_allocator_t allocator = iree_allocator_system();
  loom_tool_temp_file_t artifact_file = {0};
  iree_status_t status = loom_tool_temp_file_initialize(
      IREE_SV("loom-corpus-xfail"), &artifact_file);
  const bool artifact_file_initialized = iree_status_is_ok(status);
  char* output_argument = NULL;
  if (iree_status_is_ok(status)) {
    status = loom_corpus_make_output_argument(
        loom_tool_temp_file_path(&artifact_file), allocator, &output_argument);
  }

  const iree_host_size_t compiler_argument_count = (iree_host_size_t)(argc - 1);
  iree_host_size_t total_argument_count = 0;
  iree_host_size_t argument_storage_size = 0;
  if (iree_status_is_ok(status) &&
      (!iree_host_size_checked_add(compiler_argument_count, 3,
                                   &total_argument_count) ||
       !iree_host_size_checked_mul(total_argument_count,
                                   sizeof(iree_string_view_t),
                                   &argument_storage_size))) {
    status = iree_make_status(IREE_STATUS_OUT_OF_RANGE,
                              "too many compiler arguments");
  }
  iree_string_view_t* compiler_arguments = NULL;
  if (iree_status_is_ok(status)) {
    status = iree_allocator_malloc(allocator, argument_storage_size,
                                   (void**)&compiler_arguments);
  }
  if (iree_status_is_ok(status)) {
    for (iree_host_size_t i = 0; i < compiler_argument_count; ++i) {
      compiler_arguments[i] = iree_make_cstring_view(argv[i + 1]);
    }
    compiler_arguments[compiler_argument_count] =
        IREE_SV("--compile-report=details");
    compiler_arguments[compiler_argument_count + 1] =
        IREE_SV("--compile-report-output=-");
    compiler_arguments[compiler_argument_count + 2] =
        iree_make_cstring_view(output_argument);
  }

  loom_tool_process_result_t result = {0};
  if (iree_status_is_ok(status)) {
    status = loom_tool_process_run(compiler_path, /*search_path=*/false,
                                   compiler_arguments, total_argument_count,
                                   allocator, &result);
  }
  if (iree_status_is_ok(status)) {
    const iree_string_view_t compile_report_json = iree_make_string_view(
        result.stdout_bytes.data, result.stdout_bytes.length);
    status = loom_corpus_compile_report_expect_diagnostic(
        result.exit_code, compile_report_json, expected_diagnostic);
    if (!iree_status_is_ok(status)) {
      loom_corpus_print_compiler_stderr(&result.stderr_bytes);
    }
  }
  if (iree_status_is_ok(status)) {
    status = iree_io_file_contents_write(
        stamp_output_path,
        iree_make_const_byte_span(expected_diagnostic_text.data,
                                  expected_diagnostic_text.size),
        allocator);
  }

  loom_tool_process_result_deinitialize(&result, allocator);
  iree_allocator_free(allocator, compiler_arguments);
  iree_allocator_free(allocator, output_argument);
  if (artifact_file_initialized) {
    status = iree_status_join(status,
                              loom_tool_temp_file_deinitialize(&artifact_file));
  }
  return status;
}

int main(int argc, char** argv) {
  iree_flags_set_usage(
      "loom-corpus-compile-xfail",
      "Runs one Loom corpus root and succeeds only for its declared compiler "
      "diagnostic. Remaining arguments are passed to loom-compile.");
  iree_flags_parse_checked(IREE_FLAGS_PARSE_MODE_UNDEFINED_OK, &argc, &argv);
  iree_status_t status = loom_corpus_compile_xfail(argc, argv);
  if (iree_status_is_ok(status)) {
    return EXIT_SUCCESS;
  }
  iree_status_fprint(stderr, status);
  iree_status_free(status);
  return EXIT_FAILURE;
}
