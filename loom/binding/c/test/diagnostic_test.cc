// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "src/diagnostic.h"

#include <cstring>
#include <string>

#include "iree/testing/gtest.h"
#include "loom/error/error_defs.h"
#include "test/util.h"

namespace {
using loomc::testing::HandlePtr;
using ResultPtr = HandlePtr<loomc_result_t, loomc_result_release>;
using SourcePtr = HandlePtr<loomc_source_t, loomc_source_release>;

ResultPtr Capture(const loom_source_range_t& range,
                  const loomc_source_t* source = nullptr) {
  loomc_result_t* result = nullptr;
  LOOMC_EXPECT_OK(loomc_result_create(LOOMC_RESULT_STATE_FAILED,
                                      loomc_allocator_system(), &result));
  loom_diagnostic_param_t param = loom_param_string(IREE_SV("x"));
  loom_diagnostic_t diagnostic = {};
  diagnostic.error = loom_error_def_lookup(LOOM_ERROR_DOMAIN_PARSE, 1);
  diagnostic.severity = LOOM_DIAGNOSTIC_ERROR;
  diagnostic.emitter = LOOM_EMITTER_PARSER;
  diagnostic.params = &param;
  diagnostic.param_count = 1;
  diagnostic.source_location = range;
  LOOMC_EXPECT_OK(
      loomc_result_add_loom_diagnostic(result, source, &diagnostic));
  return ResultPtr(result);
}

SourcePtr CreateSource(const char* identifier, const char* contents) {
  loomc_source_options_t options = {};
  options.identifier = loomc_make_cstring_view(identifier);
  options.contents = loomc_make_byte_span(contents, strlen(contents));
  options.storage = LOOMC_SOURCE_STORAGE_COPY;
  loomc_source_t* source = nullptr;
  LOOMC_EXPECT_OK(
      loomc_source_create(&options, loomc_allocator_system(), &source));
  return SourcePtr(source);
}

TEST(DiagnosticTest, OwnsRecordedIdentityWithoutText) {
  std::string filename = "virtual/kernel.cxx";
  loom_source_range_t range = {};
  range.provenance = LOOM_SOURCE_PROVENANCE_UNAVAILABLE_SOURCE;
  range.filename = iree_make_string_view(filename.data(), filename.size());
  range.start_line = 7;
  range.start_column = 12;
  range.end_line = 7;
  range.end_column = 25;
  auto result = Capture(range);
  filename.assign("released");
  ASSERT_EQ(loomc_result_diagnostic_count(result.get()), 1u);
  const auto& stored = loomc_result_diagnostic_at(result.get(), 0)->range;
  ASSERT_NE(stored.source, nullptr);
  auto identifier = loomc_source_identifier(stored.source);
  EXPECT_EQ(std::string(identifier.data, identifier.size),
            "virtual/kernel.cxx");
  EXPECT_EQ(stored.start_line, 7u);
  EXPECT_EQ(stored.start_column, 12u);
  EXPECT_EQ(stored.end_line, 7u);
  EXPECT_EQ(stored.end_column, 25u);
  EXPECT_EQ(stored.start, 0u);
  EXPECT_EQ(stored.end, 0u);
  EXPECT_EQ(loomc_source_contents(stored.source).data_length, 0u);
}

TEST(DiagnosticTest, InputIdentityAloneCannotSupplyOriginalText) {
  for (const char* input_name : {"input.loombc", "header.h"}) {
    for (const char* original : {"", "original"}) {
      SCOPED_TRACE(input_name);
      SCOPED_TRACE(original);
      auto source = CreateSource(input_name, "different contents");
      std::string text = original;
      loom_source_range_t range = {};
      range.filename = IREE_SV("header.h");
      range.source = iree_make_string_view(text.data(), text.size());
      range.provenance = text.empty()
                             ? LOOM_SOURCE_PROVENANCE_UNAVAILABLE_SOURCE
                             : LOOM_SOURCE_PROVENANCE_EXACT_SOURCE;
      range.start_line = range.start_column = range.end_line = 1;
      range.end_column = 9;
      range.end = text.size();
      auto result = Capture(range, source.get());
      source.reset();
      text.assign("released");
      ASSERT_EQ(loomc_result_diagnostic_count(result.get()), 1u);
      const auto& stored = loomc_result_diagnostic_at(result.get(), 0)->range;
      ASSERT_NE(stored.source, nullptr);
      auto identifier = loomc_source_identifier(stored.source);
      EXPECT_EQ(std::string(identifier.data, identifier.size), "header.h");
      auto contents = loomc_source_contents(stored.source);
      EXPECT_EQ(contents.data_length, strlen(original));
      if (contents.data_length) {
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(contents.data),
                              contents.data_length),
                  original);
      }
    }
  }
}

TEST(DiagnosticTest, RetainsMatchingSourceOwner) {
  auto source = CreateSource("source.loom", "original");
  auto contents = loomc_source_contents(source.get());
  loom_source_range_t range = {};
  range.provenance = LOOM_SOURCE_PROVENANCE_EXACT_SOURCE;
  range.filename = IREE_SV("source.loom");
  range.source = iree_make_string_view(
      reinterpret_cast<const char*>(contents.data), contents.data_length);
  range.start_line = range.start_column = range.end_line = 1;
  range.end_column = 9;
  range.end = contents.data_length;
  auto result = Capture(range, source.get());
  ASSERT_EQ(loomc_result_diagnostic_count(result.get()), 1u);
  const auto& stored = loomc_result_diagnostic_at(result.get(), 0)->range;
  EXPECT_EQ(stored.source, source.get());
  source.reset();
  EXPECT_EQ(loomc_source_contents(stored.source).data, contents.data);
}

}  // namespace
