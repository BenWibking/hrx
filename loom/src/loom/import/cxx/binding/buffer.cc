// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/import/cxx/binding/buffer.h"

#include <cxx/const_value.h>
#include <cxx/control.h>
#include <cxx/names.h>
#include <cxx/symbols.h>
#include <cxx/type_traits.h>
#include <cxx/types.h>

#include <cstdint>
#include <variant>

#include "loom/import/cxx/source/error.h"

namespace loom::cxx_import {

bool BufferIntrinsic::supports(std::string_view name) {
  return name == "buffer.alloca";
}

std::optional<BufferIntrinsic> BufferIntrinsic::resolve(
    cxx::TranslationUnit& unit, Diagnostics& diagnostics, Types& types,
    cxx::FunctionSymbol* function, const cxx::Attribute& attribute,
    cxx::AST* owner) {
  if (!supports(attribute.arguments[0]->name())) {
    return std::nullopt;
  }
  if (attribute.arguments.size() != 1) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca accepts no operation selectors");
  }

  auto* signature = cxx::type_cast<cxx::FunctionType>(function->type());
  auto parameters = signature->parameterTypes();
  auto* result = cxx::type_cast<cxx::PointerType>(
      types.unqualified(signature->returnType()));
  if (signature->isVariadic() || parameters.size() != 1 || !result) {
    diagnostics.reject(unit, owner, "buffer.alloca requires T*(size_t)");
  }
  auto* size_type = unit.control()->getSizeType();
  if (!unit.typeTraits().is_same(types.unqualified(parameters[0]), size_type)) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca element count must be size_t");
  }

  auto arguments = function->templateArguments();
  if (arguments.size() != 3) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca requires element type, memory space, "
                       "and alignment template arguments");
  }
  auto* element_type = cxx::template_argument_as_type(arguments[0]);
  if (!element_type ||
      !unit.typeTraits().is_same(element_type, result->elementType())) {
    diagnostics.reject(
        unit, owner,
        "buffer.alloca element type must match its result pointer");
  }

  auto space_value = cxx::template_argument_value(arguments[1]);
  auto* space =
      space_value ? std::get_if<cxx::ConstInt>(&*space_value) : nullptr;
  if (!space || space->isNegative() || space->toUWide() > UINT8_MAX) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca memory space must be a constant enum");
  }
  auto memory_space =
      static_cast<loom_value_fact_memory_space_t>(space->toUIntMax());
  switch (memory_space) {
    case LOOM_VALUE_FACT_MEMORY_SPACE_GLOBAL:
    case LOOM_VALUE_FACT_MEMORY_SPACE_WORKGROUP:
    case LOOM_VALUE_FACT_MEMORY_SPACE_PRIVATE:
    case LOOM_VALUE_FACT_MEMORY_SPACE_HOST:
    case LOOM_VALUE_FACT_MEMORY_SPACE_GENERIC:
      break;
    default:
      diagnostics.reject(
          unit, owner,
          "buffer.alloca requires an allocatable memory space: global, "
          "workgroup, private, host, or generic");
  }

  auto alignment_value = cxx::template_argument_value(arguments[2]);
  auto* alignment =
      alignment_value ? std::get_if<cxx::ConstInt>(&*alignment_value) : nullptr;
  if (!alignment || alignment->isNegative() ||
      alignment->toUWide() > INT64_MAX) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca alignment must be a positive "
                       "power-of-two byte count");
  }
  auto alignment_bytes = static_cast<uint64_t>(alignment->toUIntMax());
  if (alignment_bytes == 0 || (alignment_bytes & (alignment_bytes - 1)) != 0) {
    diagnostics.reject(unit, owner,
                       "buffer.alloca alignment must be a positive "
                       "power-of-two byte count");
  }

  return BufferIntrinsic(element_type, parameters[0], memory_space,
                         static_cast<int64_t>(alignment_bytes));
}

Value BufferIntrinsic::call(std::span<const Value> arguments, Storage& storage,
                            cxx::AST* owner) const {
  return Value(storage.allocate_elements(element_type_, arguments[0].ssa(),
                                         element_count_type_, memory_space_,
                                         alignment_, owner));
}

bool BufferIntrinsic::equivalent(const BufferIntrinsic& other) const {
  return element_type_ == other.element_type_ &&
         element_count_type_ == other.element_count_type_ &&
         memory_space_ == other.memory_space_ && alignment_ == other.alignment_;
}

}  // namespace loom::cxx_import
