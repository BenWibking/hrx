// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_IMPORT_CXX_BINDING_BUFFER_H_
#define LOOM_IMPORT_CXX_BINDING_BUFFER_H_

#include <cxx/ast_fwd.h>
#include <cxx/attributes.h>
#include <cxx/symbols_fwd.h>

#include <optional>
#include <span>
#include <string_view>

#include "loom/import/cxx/value/storage.h"

namespace loom::cxx_import {

// A typed dynamic allocation. The concrete specialization supplies its object
// type, memory space and minimum alignment; the call supplies a size_t element
// count. The result is the ordinary two-component source pointer projection.
class BufferIntrinsic {
 public:
  static bool supports(std::string_view name);

  // Unknown operations return nullopt. Invalid signatures and selectors
  // diagnose at owner. The translation unit owns the retained source types.
  static std::optional<BufferIntrinsic> resolve(cxx::TranslationUnit& unit,
                                                Diagnostics& diagnostics,
                                                Types& types,
                                                cxx::FunctionSymbol* function,
                                                const cxx::Attribute& attribute,
                                                cxx::AST* owner);

  // Emits the physical extent and fresh root after the count has been
  // evaluated using normal C++ argument rules.
  Value call(std::span<const Value> arguments, Storage& storage,
             cxx::AST* owner) const;

  bool equivalent(const BufferIntrinsic& other) const;

 private:
  BufferIntrinsic(const cxx::Type* element_type,
                  const cxx::Type* element_count_type,
                  loom_value_fact_memory_space_t memory_space,
                  int64_t alignment)
      : element_type_(element_type),
        element_count_type_(element_count_type),
        memory_space_(memory_space),
        alignment_(alignment) {}

  // Source object type named by T and by the result pointee.
  const cxx::Type* element_type_;
  // Canonical source size type accepted by the single runtime operand.
  const cxx::Type* element_count_type_;
  // Allocatable High memory domain selected by the template.
  loom_value_fact_memory_space_t memory_space_;
  // Positive power-of-two minimum byte alignment.
  int64_t alignment_;
};

}  // namespace loom::cxx_import

#endif  // LOOM_IMPORT_CXX_BINDING_BUFFER_H_
