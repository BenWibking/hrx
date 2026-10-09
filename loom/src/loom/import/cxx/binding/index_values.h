// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef LOOM_IMPORT_CXX_BINDING_INDEX_VALUES_H_
#define LOOM_IMPORT_CXX_BINDING_INDEX_VALUES_H_

#include <cxx/types_fwd.h>

#include <cstdint>
#include <string_view>

#include "loom/import/cxx/source/source.h"
#include "loom/import/cxx/value/representation.h"
#include "loom/import/cxx/value/types.h"

namespace loom::cxx_import {

// Requires one non-Boolean source integer and returns whether its C++
// interpretation is unsigned.
bool require_integral(cxx::TranslationUnit& unit, Diagnostics& diagnostics,
                      Types& types, const cxx::Type* type, cxx::AST* owner,
                      std::string_view role);

// Requires an admitted record with exactly |count| flattened integer
// components. The returned mask records unsigned source interpretations in
// transport order so later emission can preserve C++ widening semantics.
uint16_t require_integral_record(cxx::TranslationUnit& unit,
                                 Diagnostics& diagnostics, Types& types,
                                 const cxx::Type* type, size_t count,
                                 cxx::AST* owner, std::string_view role);

// Returns whether one flattened component was admitted as unsigned.
bool is_unsigned_component(uint32_t mask, size_t index);

// Converts one admitted integer value to High index. Unsigned values first
// widen to i64 so narrow high-bit values cannot sign-extend through index.cast.
loom_value_id_t cast_index(Value value, bool unsigned_source,
                           loom_builder_t* builder,
                           loom_location_id_t location);

}  // namespace loom::cxx_import

#endif  // LOOM_IMPORT_CXX_BINDING_INDEX_VALUES_H_
