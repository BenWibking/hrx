// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/import/cxx/import.h"

#include <cxx/symbols.h>
#include <cxx/types.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <new>
#include <stdexcept>
#include <string_view>

#include "loom/analysis/callable_effect_inference.h"
#include "loom/error/source.h"
#include "loom/import/cxx/binding/numeric_conversion.h"
#include "loom/import/cxx/source/error.h"
#include "loom/import/cxx/source/source.h"
#include "loom/import/cxx/translation.h"
#include "loom/verify/verify.h"

namespace {

bool resolve_source_contents(void* user_data, const loom_module_t* module,
                             loom_location_id_t location,
                             loom_source_range_t* out_range) {
  loom_source_range_t range;
  if (!loom_source_resolve({}, module, location, &range)) {
    return false;
  }
  auto& contents = *static_cast<loom::cxx_import::SourceContents*>(user_data);
  try {
    const auto source = contents.source(
        std::string_view(range.filename.data, range.filename.size));
    range.provenance = LOOM_SOURCE_PROVENANCE_EXACT_SOURCE;
    range.source = loom::cxx_import::view(source);
    range.start = loom_source_byte_offset(range.source, range.start_line,
                                          range.start_column);
    range.end =
        loom_source_range_byte_offset(&range, range.end_line, range.end_column);
  } catch (const std::logic_error&) {
    return false;
  }
  *out_range = range;
  return true;
}

}  // namespace

iree_status_t loom_cxx_import(iree_string_view_t source,
                              iree_string_view_t filename,
                              loom_context_t* context,
                              iree_arena_block_pool_t* block_pool,
                              const loom_cxx_import_options_t* options,
                              iree_allocator_t host_allocator,
                              loom_module_t** out_module) {
  IREE_ASSERT_ARGUMENT(out_module);
  *out_module = nullptr;
  loom_cxx_import_options_t defaults;
  if (!options) {
    loom_cxx_import_options_initialize(&defaults);
    options = &defaults;
  }
  IREE_RETURN_IF_ERROR(loom_cxx_import_options_validate(options));
  using namespace loom::cxx_import;
  try {
    NumericConversionConstantEvaluator constant_evaluator;
    Source parsed(source, filename, *options, &constant_evaluator);
    // A launched kernel whose lowering is rejected is dropped and the unit
    // retranslated without it, so one unsupported kernel does not discard
    // every other kernel's device code.
    const bool drop_failed =
        options->flags & LOOM_CXX_IMPORT_FLAG_DROP_FAILED_KERNELS;
    std::unordered_set<cxx::FunctionSymbol*> excluded;
    std::unique_ptr<loom_module_t, decltype(&loom_module_free)> module(
        nullptr, loom_module_free);
    for (;;) {
      loom_module_t* raw_module = nullptr;
      check(loom_module_allocate(context, filename, block_pool, nullptr,
                                 host_allocator, &raw_module));
      module.reset(raw_module);
      // Source IDs are module-local; buffer observations so an abandoned
      // attempt's IDs never reach the caller.
      struct Observed {
        loom_source_id_t id;
        std::string filename;
        iree_string_view_t source;
      };
      std::vector<Observed> observed;
      loom_cxx_import_options_t attempt = *options;
      if (drop_failed && options->source_observer.fn) {
        attempt.source_observer = {
            [](void* user_data, loom_source_id_t id, iree_string_view_t filename,
               iree_string_view_t source) -> iree_status_t {
              static_cast<std::vector<Observed>*>(user_data)->push_back(
                  {id, std::string(filename.data, filename.size), source});
              return iree_ok_status();
            },
            &observed};
      }
      try {
        translate(parsed.unit(), parsed.diagnostics(), module.get(), attempt,
                  drop_failed ? &excluded : nullptr);
        if (drop_failed && options->source_observer.fn) {
          for (const auto& entry : observed) {
            check(options->source_observer.fn(options->source_observer.user_data,
                                              entry.id,
                                              iree_make_string_view(
                                                  entry.filename.data(),
                                                  entry.filename.size()),
                                              entry.source));
          }
        }
        break;
      } catch (const KernelRejected& rejected) {
        auto* kernel = static_cast<cxx::FunctionSymbol*>(rejected.kernel);
        if (!excluded.insert(kernel).second || excluded.size() > 1000) {
          throw SourceRejected();
        }
        std::fprintf(stderr, "loom-import-cxx: dropping launched kernel %s\n",
                     cxx::to_string(kernel->type()).c_str());
      }
    }
    check(loom_callable_effect_inference_run(module.get()));
    loom_verify_options_t verification_options = {
        .sink = options->diagnostic_sink,
        .source_resolver = {resolve_source_contents,
                            &parsed.diagnostics().contents()},
    };
    loom_verify_result_t verification = {};
    check(
        loom_verify_module(module.get(), &verification_options, &verification));
    if (verification.error_count) {
      // Verification diagnostics retain the original C/C++ source locations.
      // Treat rejected external source like frontend admission failures: the
      // diagnostic is the result and no partial module escapes the importer.
      return iree_ok_status();
    }
    *out_module = module.release();
    return iree_ok_status();
  } catch (StatusError& error) {
    return error.release();
  } catch (const SourceRejected&) {
    return iree_ok_status();
  } catch (const std::bad_alloc&) {
    return iree_make_status(IREE_STATUS_RESOURCE_EXHAUSTED,
                            "C/C++ frontend allocation failed");
  } catch (const std::exception& error) {
    return iree_make_status(IREE_STATUS_INTERNAL, "C/C++ frontend failed: %s",
                            error.what());
  }
}
