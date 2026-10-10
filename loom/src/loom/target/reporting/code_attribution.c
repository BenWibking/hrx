// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/reporting/code_attribution.h"

#include "loom/target/reporting/report.h"
#include "loom/target/reporting/row_list.h"

iree_status_t loom_target_compile_report_record_code_attribution_row(
    struct loom_target_compile_report_t* report,
    const loom_target_compile_report_code_attribution_row_t* row) {
  if (!loom_target_compile_report_wants_details(
          report, LOOM_TARGET_COMPILE_REPORT_DETAIL_CODE_ATTRIBUTION_ROWS)) {
    return iree_ok_status();
  }
  report->detail_flags |=
      LOOM_TARGET_COMPILE_REPORT_DETAIL_CODE_ATTRIBUTION_ROWS;
  return loom_target_compile_report_row_list_append(
      &report->code_attribution_rows, sizeof(*row), report->allocator, row);
}
