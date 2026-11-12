// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/import/cxx/import.h"

#include <cxx/symbols.h>
#include <cxx/types.h>

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
#include "loom/import/cxx/unit.h"
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

struct loom_cxx_unit_t {
  // Constant semantics borrowed by the translation unit; outlives |source|.
  loom::cxx_import::NumericConversionConstantEvaluator constant_evaluator;
  // The preprocessed and checked translation unit.
  std::unique_ptr<loom::cxx_import::Source> source;
  // Usage counts reported by loom::cxx_import::unit_statistics().
  loom::cxx_import::UnitStatistics statistics;
  // Host launch facts of the most recent import, reported by
  // loom::cxx_import::unit_kernel_launches().
  std::vector<loom::cxx_import::KernelLaunch> kernel_launches;
};

namespace {

// Converts importer exceptions to statuses at the public API boundary.
template <typename Body>
iree_status_t guard(Body&& body) {
  using namespace loom::cxx_import;
  try {
    return body();
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

// Translates and verifies a parsed unit. Throws like translate().
iree_status_t import_parsed(loom_cxx_unit_t* unit, iree_string_view_t filename,
                            loom_context_t* context,
                            iree_arena_block_pool_t* block_pool,
                            const loom_cxx_import_options_t* options,
                            iree_allocator_t host_allocator,
                            loom_module_t** out_module) {
  using namespace loom::cxx_import;
  Source& parsed = *unit->source;
  unit->statistics.dropped_kernel_count = 0;
  unit->kernel_launches.clear();
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
                drop_failed ? &excluded : nullptr, &unit->kernel_launches);
      if (drop_failed && options->source_observer.fn) {
        for (const auto& entry : observed) {
          check(options->source_observer.fn(
              options->source_observer.user_data, entry.id,
              iree_make_string_view(entry.filename.data(),
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
      // The kernel's own diagnostic, a warning through the sink, reports it.
      ++unit->statistics.dropped_kernel_count;
    }
  }
  check(loom_callable_effect_inference_run(module.get()));
  loom_verify_options_t verification_options = {
      .sink = options->diagnostic_sink,
      .source_resolver = {resolve_source_contents,
                          &parsed.diagnostics().contents()},
  };
  loom_verify_result_t verification = {};
  check(loom_verify_module(module.get(), &verification_options, &verification));
  ++unit->statistics.import_count;
  if (verification.error_count) {
    // Verification diagnostics retain the original C/C++ source locations.
    // Treat rejected external source like frontend admission failures: the
    // diagnostic is the result and no partial module escapes the importer.
    return iree_ok_status();
  }
  *out_module = module.release();
  return iree_ok_status();
}

}  // namespace

iree_status_t loom_cxx_unit_parse(iree_string_view_t source,
                                  iree_string_view_t filename,
                                  const loom_cxx_import_options_t* options,
                                  iree_allocator_t host_allocator,
                                  loom_cxx_unit_t** out_unit) {
  IREE_ASSERT_ARGUMENT(out_unit);
  *out_unit = nullptr;
  loom_cxx_import_options_t defaults;
  if (!options) {
    loom_cxx_import_options_initialize(&defaults);
    options = &defaults;
  }
  IREE_RETURN_IF_ERROR(loom_cxx_import_options_validate(options));
  return guard([&]() -> iree_status_t {
    auto unit = std::make_unique<loom_cxx_unit_t>();
    unit->source = std::make_unique<loom::cxx_import::Source>(
        source, filename, *options, &unit->constant_evaluator);
    *out_unit = unit.release();
    return iree_ok_status();
  });
}

void loom_cxx_unit_free(loom_cxx_unit_t* unit) { delete unit; }

void loom_cxx_unit_set_diagnostic_sink(loom_cxx_unit_t* unit,
                                       loom_diagnostic_sink_t sink) {
  IREE_ASSERT_ARGUMENT(unit);
  unit->source->diagnostics().set_sink(sink);
}

iree_status_t loom_cxx_import_unit(loom_cxx_unit_t* unit,
                                   loom_context_t* context,
                                   iree_arena_block_pool_t* block_pool,
                                   const loom_cxx_import_options_t* options,
                                   iree_allocator_t host_allocator,
                                   loom_module_t** out_module) {
  IREE_ASSERT_ARGUMENT(unit);
  IREE_ASSERT_ARGUMENT(out_module);
  *out_module = nullptr;
  loom_cxx_import_options_t defaults;
  if (!options) {
    loom_cxx_import_options_initialize(&defaults);
    options = &defaults;
  }
  IREE_RETURN_IF_ERROR(loom_cxx_import_options_validate(options));
  unit->source->diagnostics().set_sink(options->diagnostic_sink);
  const auto filename = unit->source->contents().primary_filename();
  return guard([&]() {
    return import_parsed(unit, loom::cxx_import::view(filename), context,
                         block_pool, options, host_allocator, out_module);
  });
}

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
  return guard([&]() {
    loom_cxx_unit_t unit;
    unit.source = std::make_unique<loom::cxx_import::Source>(
        source, filename, *options, &unit.constant_evaluator);
    return import_parsed(&unit, filename, context, block_pool, options,
                         host_allocator, out_module);
  });
}

namespace loom::cxx_import {

cxx::TranslationUnit& unit_translation_unit(loom_cxx_unit_t* unit) {
  return unit->source->unit();
}

const cxx::Toolchain& unit_toolchain(const loom_cxx_unit_t* unit) {
  return unit->source->toolchain();
}

bool unit_has_error(const loom_cxx_unit_t* unit) {
  return unit->source->diagnostics().has_error();
}

UnitStatistics unit_statistics(const loom_cxx_unit_t* unit) {
  return unit->statistics;
}

const std::vector<KernelLaunch>& unit_kernel_launches(
    const loom_cxx_unit_t* unit) {
  return unit->kernel_launches;
}

}  // namespace loom::cxx_import
