// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/transforms/vector/combine_patterns.h"

#include "loom/ir/module.h"
#include "loom/ops/scalar/ops.h"
#include "loom/ops/vector/construction.h"
#include "loom/ops/vector/ops.h"
#include "loom/ops/vector/table.h"
#include "loom/rewrite/rewriter.h"

static loom_op_t* loom_vector_combine_defining_op(
    const loom_rewriter_t* rewriter, loom_value_id_t value_id) {
  loom_value_t* value = loom_module_value(rewriter->module, value_id);
  if (loom_value_is_block_arg(value)) {
    return NULL;
  }
  return loom_value_def_op(value);
}

static bool loom_vector_combine_value_is_non_negative(
    const loom_rewriter_t* rewriter, loom_value_id_t value_id) {
  if (rewriter->fact_table == NULL) {
    return false;
  }
  const loom_fact_context_t* context = &rewriter->fact_table->context;
  const loom_value_facts_t facts =
      loom_rewriter_value_facts(rewriter, value_id);
  loom_value_fact_uniform_element_t uniform = {0};
  if (loom_value_facts_query_uniform_element(context, facts, &uniform)) {
    return loom_value_facts_is_non_negative(uniform.element);
  }

  loom_value_fact_small_static_lanes_t lanes = {0};
  if (!loom_value_facts_query_small_static_lanes(context, facts, &lanes)) {
    return false;
  }
  for (iree_host_size_t i = 0; i < lanes.count; ++i) {
    if (!loom_value_facts_is_non_negative(lanes.lanes[i])) {
      return false;
    }
  }
  return true;
}

static loom_value_id_t loom_vector_combine_conversion_input(
    const loom_op_t* op) {
  switch (op->kind) {
    case LOOM_OP_VECTOR_EXTF:
      return loom_vector_extf_input(op);
    case LOOM_OP_VECTOR_FPTRUNC:
      return loom_vector_fptrunc_input(op);
    case LOOM_OP_VECTOR_EXTSI:
      return loom_vector_extsi_input(op);
    case LOOM_OP_VECTOR_EXTUI:
      return loom_vector_extui_input(op);
    case LOOM_OP_VECTOR_TRUNCI:
      return loom_vector_trunci_input(op);
    default:
      IREE_ASSERT_UNREACHABLE("non-conversion vector combine root");
      IREE_BUILTIN_UNREACHABLE();
  }
}

static iree_status_t loom_vector_combine_replace_with_value(
    loom_rewriter_t* rewriter, loom_op_t* op, loom_value_id_t replacement) {
  return loom_rewriter_replace_all_uses_and_erase(rewriter, op, &replacement,
                                                  1);
}

static iree_status_t loom_vector_combine_replace_with_conversion(
    loom_rewriter_t* rewriter, loom_op_t* op, loom_op_kind_t replacement_kind,
    loom_value_id_t input) {
  const loom_type_t input_type =
      loom_module_value_type(rewriter->module, input);
  const loom_type_t result_type =
      loom_module_value_type(rewriter->module, loom_op_const_results(op)[0]);
  if (loom_type_equal(input_type, result_type)) {
    return loom_vector_combine_replace_with_value(rewriter, op, input);
  }

  loom_builder_set_before(&rewriter->builder, op);
  const loom_value_id_t value_checkpoint =
      loom_rewriter_value_checkpoint(rewriter);
  loom_op_t* replacement_op = NULL;
  switch (replacement_kind) {
    case LOOM_OP_VECTOR_EXTF: {
      IREE_RETURN_IF_ERROR(
          loom_vector_extf_build(&rewriter->builder, input, input_type,
                                 result_type, op->location, &replacement_op));
      break;
    }
    case LOOM_OP_VECTOR_FPTRUNC: {
      IREE_RETURN_IF_ERROR(loom_vector_fptrunc_build(
          &rewriter->builder, input, input_type, result_type, op->location,
          &replacement_op));
      break;
    }
    case LOOM_OP_VECTOR_EXTSI: {
      IREE_RETURN_IF_ERROR(
          loom_vector_extsi_build(&rewriter->builder, input, input_type,
                                  result_type, op->location, &replacement_op));
      break;
    }
    case LOOM_OP_VECTOR_EXTUI: {
      IREE_RETURN_IF_ERROR(
          loom_vector_extui_build(&rewriter->builder, input, input_type,
                                  result_type, op->location, &replacement_op));
      break;
    }
    case LOOM_OP_VECTOR_TRUNCI: {
      IREE_RETURN_IF_ERROR(
          loom_vector_trunci_build(&rewriter->builder, input, input_type,
                                   result_type, op->location, &replacement_op));
      break;
    }
    default:
      IREE_ASSERT_UNREACHABLE("unsupported vector conversion replacement");
      IREE_BUILTIN_UNREACHABLE();
  }

  loom_value_id_t replacement = loom_op_const_results(replacement_op)[0];
  IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
      rewriter, op, &replacement, 1, value_checkpoint));
  return loom_vector_combine_replace_with_value(rewriter, op, replacement);
}

static iree_status_t loom_vector_combine_sink_conversion_through_splat(
    loom_rewriter_t* rewriter, loom_op_t* op, loom_op_t* splat_op) {
  const loom_value_id_t scalar = loom_vector_splat_scalar(splat_op);
  const loom_type_t scalar_type =
      loom_module_value_type(rewriter->module, scalar);
  const loom_type_t result_type =
      loom_module_value_type(rewriter->module, loom_op_const_results(op)[0]);
  const loom_type_t scalar_result_type =
      loom_type_scalar(loom_type_element_type(result_type));

  loom_builder_set_before(&rewriter->builder, op);
  const loom_value_id_t value_checkpoint =
      loom_rewriter_value_checkpoint(rewriter);
  loom_op_t* scalar_op = NULL;
  switch (op->kind) {
    case LOOM_OP_VECTOR_EXTF: {
      IREE_RETURN_IF_ERROR(
          loom_scalar_extf_build(&rewriter->builder, scalar, scalar_type,
                                 scalar_result_type, op->location, &scalar_op));
      break;
    }
    case LOOM_OP_VECTOR_FPTRUNC: {
      IREE_RETURN_IF_ERROR(loom_scalar_fptrunc_build(
          &rewriter->builder, scalar, scalar_type, scalar_result_type,
          op->location, &scalar_op));
      break;
    }
    case LOOM_OP_VECTOR_EXTSI: {
      IREE_RETURN_IF_ERROR(loom_scalar_extsi_build(
          &rewriter->builder, scalar, scalar_type, scalar_result_type,
          op->location, &scalar_op));
      break;
    }
    case LOOM_OP_VECTOR_EXTUI: {
      IREE_RETURN_IF_ERROR(loom_scalar_extui_build(
          &rewriter->builder, scalar, scalar_type, scalar_result_type,
          op->location, &scalar_op));
      break;
    }
    case LOOM_OP_VECTOR_TRUNCI: {
      IREE_RETURN_IF_ERROR(loom_scalar_trunci_build(
          &rewriter->builder, scalar, scalar_type, scalar_result_type,
          op->location, &scalar_op));
      break;
    }
    default:
      IREE_ASSERT_UNREACHABLE("unsupported vector splat conversion");
      IREE_BUILTIN_UNREACHABLE();
  }

  loom_op_t* replacement_op = NULL;
  IREE_RETURN_IF_ERROR(loom_vector_splat_build(
      &rewriter->builder, loom_op_const_results(scalar_op)[0], result_type,
      op->location, &replacement_op));
  loom_value_id_t replacement = loom_op_const_results(replacement_op)[0];
  IREE_RETURN_IF_ERROR(loom_rewriter_preserve_result_names_on_new_values(
      rewriter, op, &replacement, 1, value_checkpoint));
  return loom_vector_combine_replace_with_value(rewriter, op, replacement);
}

static bool loom_vector_combine_select_float_chain(
    const loom_module_t* module, const loom_op_t* op,
    const loom_op_t* defining_op, loom_value_id_t* out_input,
    loom_op_kind_t* out_replacement_kind) {
  if (!loom_vector_extf_isa(defining_op) ||
      (!loom_vector_extf_isa(op) && !loom_vector_fptrunc_isa(op))) {
    return false;
  }

  const loom_value_id_t input = loom_vector_extf_input(defining_op);
  const loom_type_t input_type = loom_module_value_type(module, input);
  const loom_type_t result_type =
      loom_module_value_type(module, loom_op_const_results(op)[0]);
  if (loom_type_equal(input_type, result_type)) {
    *out_input = input;
    *out_replacement_kind = LOOM_OP_KIND_UNKNOWN;
    return true;
  }

  const int32_t input_bitwidth =
      loom_scalar_type_bitwidth(loom_type_element_type(input_type));
  const int32_t result_bitwidth =
      loom_scalar_type_bitwidth(loom_type_element_type(result_type));
  if (input_bitwidth == result_bitwidth) {
    // Equal-width formats are distinct representations. There is no vector
    // resize operation that can bypass the intermediate conversion.
    return false;
  }
  *out_input = input;
  *out_replacement_kind = result_bitwidth > input_bitwidth
                              ? LOOM_OP_VECTOR_EXTF
                              : LOOM_OP_VECTOR_FPTRUNC;
  return true;
}

static bool loom_vector_combine_select_integer_chain(
    const loom_rewriter_t* rewriter, const loom_op_t* op,
    const loom_op_t* defining_op, loom_value_id_t outer_input,
    loom_value_id_t* out_input, loom_op_kind_t* out_replacement_kind) {
  loom_value_id_t input = LOOM_VALUE_ID_INVALID;
  loom_op_kind_t extension_kind = LOOM_OP_KIND_UNKNOWN;
  const bool has_signed_extension = loom_vector_extsi_isa(defining_op);
  const bool has_unsigned_extension = loom_vector_extui_isa(defining_op);
  const bool has_truncation = loom_vector_trunci_isa(defining_op);
  if (has_signed_extension) {
    input = loom_vector_extsi_input(defining_op);
    extension_kind = LOOM_OP_VECTOR_EXTSI;
  } else if (has_unsigned_extension) {
    input = loom_vector_extui_input(defining_op);
    extension_kind = LOOM_OP_VECTOR_EXTUI;
  } else if (has_truncation) {
    input = loom_vector_trunci_input(defining_op);
  } else {
    return false;
  }

  if (loom_vector_extsi_isa(op)) {
    if (has_truncation) {
      return false;
    }
  } else if (loom_vector_extui_isa(op)) {
    if (has_truncation) {
      return false;
    }
    if (has_signed_extension &&
        !loom_vector_combine_value_is_non_negative(rewriter, outer_input)) {
      return false;
    }
    extension_kind = LOOM_OP_VECTOR_EXTUI;
  } else if (!loom_vector_trunci_isa(op)) {
    return false;
  }

  const loom_type_t input_type =
      loom_module_value_type(rewriter->module, input);
  const loom_type_t result_type =
      loom_module_value_type(rewriter->module, loom_op_const_results(op)[0]);
  if (loom_type_equal(input_type, result_type)) {
    *out_input = input;
    *out_replacement_kind = LOOM_OP_KIND_UNKNOWN;
    return true;
  }

  const int32_t input_bitwidth =
      loom_scalar_type_bitwidth(loom_type_element_type(input_type));
  const int32_t result_bitwidth =
      loom_scalar_type_bitwidth(loom_type_element_type(result_type));
  *out_input = input;
  *out_replacement_kind =
      result_bitwidth > input_bitwidth ? extension_kind : LOOM_OP_VECTOR_TRUNCI;
  return true;
}

static iree_status_t loom_vector_conversion_chain_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  *out_changed = false;

  const loom_value_id_t outer_input = loom_vector_combine_conversion_input(op);
  loom_op_t* defining_op =
      loom_vector_combine_defining_op(rewriter, outer_input);
  if (defining_op == NULL) {
    return iree_ok_status();
  }
  if (loom_vector_splat_isa(defining_op)) {
    IREE_RETURN_IF_ERROR(loom_vector_combine_sink_conversion_through_splat(
        rewriter, op, defining_op));
    *out_changed = true;
    return iree_ok_status();
  }

  loom_value_id_t input = LOOM_VALUE_ID_INVALID;
  loom_op_kind_t replacement_kind = LOOM_OP_KIND_UNKNOWN;
  const bool matched =
      loom_vector_combine_select_float_chain(rewriter->module, op, defining_op,
                                             &input, &replacement_kind) ||
      loom_vector_combine_select_integer_chain(
          rewriter, op, defining_op, outer_input, &input, &replacement_kind);
  if (!matched) {
    return iree_ok_status();
  }

  if (replacement_kind == LOOM_OP_KIND_UNKNOWN) {
    IREE_RETURN_IF_ERROR(
        loom_vector_combine_replace_with_value(rewriter, op, input));
  } else {
    IREE_RETURN_IF_ERROR(loom_vector_combine_replace_with_conversion(
        rewriter, op, replacement_kind, input));
  }
  *out_changed = true;
  return iree_ok_status();
}

static iree_status_t loom_vector_from_elements_combine_lanes_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_from_elements_combine_lanes(op, rewriter, out_changed);
}

static iree_status_t loom_vector_from_elements_to_table_lookup_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_from_elements_to_table_lookup(op, rewriter, out_changed);
}

static iree_status_t loom_vector_table_lookup_simplify_indices_pattern(
    const loom_rewrite_pattern_t* pattern, void* context, loom_op_t* op,
    loom_rewriter_t* rewriter, bool* out_changed) {
  (void)pattern;
  (void)context;
  return loom_vector_table_lookup_simplify_indices(op, rewriter, out_changed);
}

static const loom_rewrite_pattern_t kVectorSourceCombinePatterns[] = {
    {
        .root_kind = LOOM_OP_VECTOR_EXTF,
        .match_and_rewrite = loom_vector_conversion_chain_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_FPTRUNC,
        .match_and_rewrite = loom_vector_conversion_chain_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_EXTSI,
        .match_and_rewrite = loom_vector_conversion_chain_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_EXTUI,
        .match_and_rewrite = loom_vector_conversion_chain_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_TRUNCI,
        .match_and_rewrite = loom_vector_conversion_chain_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_FROM_ELEMENTS,
        .match_and_rewrite = loom_vector_from_elements_combine_lanes_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_FROM_ELEMENTS,
        .match_and_rewrite = loom_vector_from_elements_to_table_lookup_pattern,
    },
    {
        .root_kind = LOOM_OP_VECTOR_TABLE_LOOKUP,
        .match_and_rewrite = loom_vector_table_lookup_simplify_indices_pattern,
    },
};

const loom_rewrite_pattern_provider_t
    loom_vector_source_combine_pattern_provider = {
        .name = IREE_SVL("vector"),
        .patterns = kVectorSourceCombinePatterns,
        .pattern_count = IREE_ARRAYSIZE(kVectorSourceCombinePatterns),
};
