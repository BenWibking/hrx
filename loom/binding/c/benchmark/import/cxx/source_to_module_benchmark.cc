// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <memory>
#include <string>
#include <string_view>

#include "benchmark/benchmark.h"
#include "loom/binding/c/benchmark/compile_throughput_benchmark.h"
#include "loom/binding/c/benchmark/import/cxx/q8s32_sources.h"
#include "loomc/import/cxx.h"

namespace loomc::bench {
namespace {

struct CxxImportSource {
  // Optional facade prefix prepended to the shared no-use function.
  const char* prefix;
  // Optional embedded translation unit replacing the shared function.
  const char* source_identifier;
  // Optional embedded header supplied through the public source provider.
  const char* header_identifier;
};

static iree_status_t CreateCxxSource(loomc_string_view_t identifier,
                                     loomc_byte_span_t contents,
                                     SourcePtr* out_source) {
  out_source->reset();
  const loomc_source_options_t options = {
      /*.type=*/LOOMC_STRUCTURE_TYPE_SOURCE_OPTIONS,
      /*.structure_size=*/sizeof(options),
      /*.next=*/nullptr,
      /*.format=*/LOOMC_SOURCE_FORMAT_UNKNOWN,
      /*.identifier=*/identifier,
      /*.contents=*/contents,
      /*.storage=*/LOOMC_SOURCE_STORAGE_COPY,
      /*.release=*/nullptr,
      /*.release_user_data=*/nullptr,
  };
  loomc_source_t* source = nullptr;
  IREE_RETURN_IF_ERROR(
      to_iree_status(loomc_source_create(&options, loom_allocator(), &source)));
  out_source->reset(source);
  return iree_ok_status();
}

class CxxImportScenario final : public CompileScenario {
 public:
  explicit CxxImportScenario(const CxxImportSource& source) : input_(source) {}

  iree_host_size_t job_count() const override { return 1; }

  iree_status_t SetUp(iree_host_size_t worker_count) override {
    IREE_RETURN_IF_ERROR(CompileScenario::SetUp(worker_count));
    if (input_.source_identifier) {
      const auto* sources = loomc_cxx_benchmark_q8s32_sources_create();
      const size_t source_count = loomc_cxx_benchmark_q8s32_sources_size();
      EmbeddedSource source =
          FindEmbeddedSource(sources, source_count, input_.source_identifier);
      IREE_RETURN_IF_ERROR(
          CreateCxxSource(source.identifier, source.contents, &source_));
      EmbeddedSource header =
          FindEmbeddedSource(sources, source_count, input_.header_identifier);
      IREE_RETURN_IF_ERROR(
          CreateCxxSource(header.identifier, header.contents, &header_));
    } else {
      std::string text = std::string(input_.prefix) +
                         "__bf16 scale(__bf16 value) { "
                         "return value * (__bf16)5.0f; }";
      IREE_RETURN_IF_ERROR(CreateCxxSource(
          loomc_make_cstring_view("scale.cpp"),
          loomc_make_byte_span(text.data(), text.size()), &source_));
    }
    return iree_ok_status();
  }

  iree_status_t RunJob(iree_host_size_t worker_ordinal,
                       iree_host_size_t job_ordinal) override {
    (void)job_ordinal;
    loomc_cxx_import_options_t options = {};
    options.type = LOOMC_STRUCTURE_TYPE_CXX_IMPORT_OPTIONS;
    options.structure_size = sizeof(options);
    if (header_) {
      options.source_provider = {ProvideSource, this};
    }
    loomc_module_t* raw_module = nullptr;
    loomc_result_t* raw_result = nullptr;
    auto status = to_iree_status(loomc_module_import_cxx(
        context_.get(), workspace_at(worker_ordinal).get(), source_.get(),
        &options, loom_allocator(), &raw_module, &raw_result));
    ModulePtr module(raw_module);
    ResultPtr result(raw_result);
    IREE_RETURN_IF_ERROR(status);
    return RequireSucceededResult(result.get(), "C++ import");
  }

 private:
  static loomc_status_t ProvideSource(void* user_data, loomc_string_view_t path,
                                      loomc_source_t** out_source) {
    auto& self = *static_cast<CxxImportScenario*>(user_data);
    *out_source = nullptr;
    const auto header_identifier = loomc_source_identifier(self.header_.get());
    // The embedded source table is intentionally flattened. Include lookup
    // still supplies an ordinary generic path, so index it by basename.
    std::string_view candidate(path.data, path.size);
    const size_t separator = candidate.find_last_of('/');
    if (separator != std::string_view::npos) {
      candidate.remove_prefix(separator + 1);
    }
    if (candidate ==
        std::string_view(header_identifier.data, header_identifier.size)) {
      loomc_source_retain(self.header_.get());
      *out_source = self.header_.get();
    }
    return loomc_ok_status();
  }

  // Static source selection chosen by benchmark registration.
  const CxxImportSource& input_;
  // Copied source bytes shared across invocations without cached parse state.
  SourcePtr source_;
  // Optional immutable user header returned by the source provider.
  SourcePtr header_;
};

std::unique_ptr<CompileScenario> CreateCxxImportScenario(
    const ::benchmark::State& state, const void* user_data) {
  (void)state;
  return std::make_unique<CxxImportScenario>(
      *static_cast<const CxxImportSource*>(user_data));
}

void SourceToModule(::benchmark::State& state, const CxxImportSource* source) {
  RunCompileBenchmarkDirect(state, CreateCxxImportScenario, source);
}

constexpr CxxImportSource kNoIncludes = {"", nullptr, nullptr};
constexpr CxxImportSource kStdFloat = {"#include <stdfloat>\n", nullptr,
                                       nullptr};
constexpr CxxImportSource kNumeric = {"#include <loomcxx/numeric.h>\n", nullptr,
                                      nullptr};
constexpr CxxImportSource kVector = {"#include <loomcxx/vector.h>\n", nullptr,
                                     nullptr};
constexpr CxxImportSource kEncodingType = {
    "#include <loomcxx/encoding_type.h>\n", nullptr, nullptr};
constexpr CxxImportSource kEncoding = {"#include <loomcxx/encoding.h>\n",
                                       nullptr, nullptr};
constexpr CxxImportSource kPredicate = {"#include <loomcxx/predicate.h>\n",
                                        nullptr, nullptr};
constexpr CxxImportSource kKernel = {"#include <loomcxx/kernel.h>\n", nullptr,
                                     nullptr};
constexpr CxxImportSource kKernelPredicate = {
    "#include <loomcxx/kernel.h>\n#include <loomcxx/predicate.h>\n", nullptr,
    nullptr};
constexpr CxxImportSource kQ8S32Providers = {
    nullptr, "q8s32_specialization_providers.cxx", "q8s32_specialization.h"};

BENCHMARK_CAPTURE(SourceToModule, NoIncludes, &kNoIncludes)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, StdFloat, &kStdFloat)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Numeric, &kNumeric)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Vector, &kVector)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, EncodingType, &kEncodingType)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Encoding, &kEncoding)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Predicate, &kPredicate)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Kernel, &kKernel)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, KernelPredicate, &kKernelPredicate)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(SourceToModule, Q8S32Providers, &kQ8S32Providers)
    ->Unit(::benchmark::kMicrosecond);

}  // namespace
}  // namespace loomc::bench
