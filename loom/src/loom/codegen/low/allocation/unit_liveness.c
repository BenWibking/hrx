// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/codegen/low/allocation/unit_liveness.h"

#include "loom/codegen/low/allocation/live_range.h"
#include "loom/codegen/low/allocation/storage.h"
#include "loom/ir/module.h"
#include "loom/target/registers.h"

bool loom_low_allocation_unit_liveness_storage_is_ignored(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    loom_value_id_t value_id, const loom_value_id_t* ignored_value_ids,
    uint16_t ignored_value_count) {
  const loom_low_placement_table_t* placement =
      unit_liveness->tied_storage_placement;
  for (uint16_t i = 0; i < ignored_value_count; ++i) {
    if (ignored_value_ids[i] == value_id) {
      return true;
    }
    if (placement != NULL) {
      const loom_value_ordinal_t* roots =
          placement->tied_storage_origins_by_value_ordinal;
      const loom_value_ordinal_t value_ordinal =
          loom_module_value_ordinal_scratch_lookup(placement->module, value_id);
      const loom_value_ordinal_t ignored_ordinal =
          loom_module_value_ordinal_scratch_lookup(placement->module,
                                                   ignored_value_ids[i]);
      if (roots[value_ordinal] == roots[ignored_ordinal]) {
        return true;
      }
    }
  }
  return false;
}

// Clobber rows for one storage location: the half-open slice of the sorted
// clobber table with that (storage_key, location).
static void loom_low_allocation_unit_liveness_location_clobbers(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    uint32_t storage_key, uint32_t location, iree_host_size_t* out_begin,
    iree_host_size_t* out_end) {
  const loom_low_allocation_clobber_t* entries = unit_liveness->clobbers.entries;
  const loom_low_allocation_clobber_t key = {
      .storage_key = storage_key, .location = location, .point = 0};
  iree_host_size_t begin = 0;
  iree_host_size_t end = unit_liveness->clobbers.count;
  while (begin < end) {
    const iree_host_size_t middle = begin + (end - begin) / 2;
    if (loom_low_allocation_clobber_less(&entries[middle], &key)) {
      begin = middle + 1;
    } else {
      end = middle;
    }
  }
  *out_begin = begin;
  end = unit_liveness->clobbers.count;
  iree_host_size_t first = begin;
  while (first < end) {
    const iree_host_size_t middle = first + (end - first) / 2;
    if (entries[middle].storage_key == storage_key &&
        entries[middle].location == location) {
      first = middle + 1;
    } else {
      end = middle;
    }
  }
  *out_end = first;
}

// Whether a clobber of one location's slice lands in [start_point, end_point).
// A permitted definition exactly at start_point does not conflict. |cursor|
// only moves forward, so callers visit windows in ascending start order.
static bool loom_low_allocation_unit_liveness_window_is_clobbered(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    iree_host_size_t* cursor, iree_host_size_t slice_end, uint32_t start_point,
    uint32_t end_point) {
  const loom_low_allocation_clobber_t* entries = unit_liveness->clobbers.entries;
  while (*cursor < slice_end && entries[*cursor].point < start_point) {
    ++*cursor;
  }
  for (iree_host_size_t i = *cursor;
       i < slice_end && entries[i].point < end_point; ++i) {
    if (!entries[i].permits_definition || entries[i].point != start_point) {
      return true;
    }
  }
  return false;
}

bool loom_low_allocation_unit_liveness_clobber_conflicts(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    const loom_low_descriptor_set_t* descriptor_set,
    const loom_low_allocation_assignment_t* candidate) {
  if (unit_liveness->clobbers.count == 0 ||
      candidate->location_kind !=
          LOOM_LOW_ALLOCATION_LOCATION_PHYSICAL_REGISTER) {
    return false;
  }
  const bool is_explicit =
      loom_low_allocation_storage_assignment_uses_explicit_physical_register(
          descriptor_set, candidate);
  const bool uses_candidate_ordinals =
      is_explicit &&
      loom_low_allocation_storage_assignment_uses_physical_candidate_ordinals(
          descriptor_set, candidate);
  if (is_explicit &&
      (!uses_candidate_ordinals || candidate->location_count == 1)) {
    uint32_t physical_register_id = candidate->location_base;
    if (uses_candidate_ordinals) {
      const bool resolved =
          loom_low_allocation_storage_assignment_unit_physical_register(
              descriptor_set, candidate, 0, &physical_register_id);
      IREE_ASSERT_TRUE(resolved);
    }
    const loom_low_physical_register_t* physical_register =
        &descriptor_set->physical_registers[physical_register_id];
    const uint16_t* atomic_units =
        &descriptor_set->physical_register_atomic_units
             [physical_register->atomic_unit_start];
    if (atomic_units[0] >= unit_liveness->clobbers.atomic_unit_end ||
        atomic_units[physical_register->atomic_unit_count - 1] <
            unit_liveness->clobbers.atomic_unit_begin) {
      return false;
    }
  }
  for (uint32_t unit = 0; unit < candidate->location_count; ++unit) {
    const uint32_t start_point =
        loom_low_allocation_live_range_assignment_unit_start_point(
            unit_liveness->start_points, unit_liveness->point_count, candidate,
            unit);
    const uint32_t end_point =
        loom_low_allocation_live_range_assignment_unit_end_point(
            unit_liveness->end_points, unit_liveness->point_count, candidate,
            unit);
    const uint16_t* atomic_units = NULL;
    uint16_t atomic_unit_count = 1;
    uint32_t storage_key = loom_low_reg_class_storage_key(
        descriptor_set, candidate->descriptor_reg_class_id);
    if (is_explicit) {
      uint32_t physical_register_id = 0;
      const bool resolved =
          loom_low_allocation_storage_assignment_unit_physical_register(
              descriptor_set, candidate, unit, &physical_register_id);
      IREE_ASSERT(resolved, "accepted assignment must name its physical units");
      atomic_units = loom_low_descriptor_set_physical_register_atomic_units(
          descriptor_set, physical_register_id, &atomic_unit_count);
      storage_key = 0;
    }
    for (uint16_t atomic_unit = 0; atomic_unit < atomic_unit_count;
         ++atomic_unit) {
      const uint32_t location = atomic_units != NULL
                                    ? atomic_units[atomic_unit]
                                    : candidate->location_base + unit;
      // Most locations have no clobbers; find this location's slice once and
      // walk it alongside the candidate's ascending segments.
      iree_host_size_t cursor = 0;
      iree_host_size_t slice_end = 0;
      loom_low_allocation_unit_liveness_location_clobbers(
          unit_liveness, storage_key, location, &cursor, &slice_end);
      if (cursor == slice_end) {
        continue;
      }
      if (candidate->liveness_segments.count == 0) {
        if (loom_low_allocation_unit_liveness_window_is_clobbered(
                unit_liveness, &cursor, slice_end, start_point, end_point)) {
          return true;
        }
        continue;
      }
      for (uint32_t segment_index = 0;
           segment_index < candidate->liveness_segments.count;
           ++segment_index) {
        const loom_liveness_segment_t* segment =
            &unit_liveness->storage_segments
                 .entries[candidate->liveness_segments.start + segment_index];
        if (loom_low_allocation_unit_liveness_window_is_clobbered(
                unit_liveness, &cursor, slice_end,
                iree_max(start_point, segment->start_point),
                iree_min(end_point, segment->end_point))) {
          return true;
        }
      }
    }
  }
  return false;
}

uint32_t loom_low_allocation_unit_liveness_point_start_for_value_ordinal(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    const loom_liveness_analysis_t* liveness,
    loom_value_ordinal_t value_ordinal) {
  IREE_ASSERT_ARGUMENT(unit_liveness);
  IREE_ASSERT_ARGUMENT(liveness);
  IREE_ASSERT_LT(value_ordinal, liveness->value_count);
  return unit_liveness->values[value_ordinal].unit_point_start;
}

const uint32_t*
loom_low_allocation_unit_liveness_start_points_for_value_ordinal(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    const loom_liveness_analysis_t* liveness,
    loom_value_ordinal_t value_ordinal) {
  const uint32_t start =
      loom_low_allocation_unit_liveness_point_start_for_value_ordinal(
          unit_liveness, liveness, value_ordinal);
  return start == UINT32_MAX ? NULL : &unit_liveness->start_points[start];
}

loom_liveness_segment_range_t
loom_low_allocation_unit_liveness_storage_segment_range_for_value_ordinal(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    const loom_liveness_analysis_t* liveness,
    loom_value_ordinal_t value_ordinal) {
  IREE_ASSERT_ARGUMENT(unit_liveness);
  IREE_ASSERT_ARGUMENT(liveness);
  IREE_ASSERT_LT(value_ordinal, liveness->value_count);
  if (iree_bitmap_test(unit_liveness->values_with_incomplete_storage_segments,
                       value_ordinal)) {
    return unit_liveness->storage_segments.tied_sources != NULL
               ? unit_liveness->storage_segments.tied_sources[value_ordinal]
               : (loom_liveness_segment_range_t){0};
  }
  return loom_liveness_segment_range_for_value_ordinal(liveness, value_ordinal);
}

bool loom_low_allocation_unit_liveness_storage_component_live_at_point(
    const loom_low_allocation_unit_liveness_t* unit_liveness,
    const loom_liveness_analysis_t* liveness,
    const loom_low_placement_table_t* placement,
    loom_value_ordinal_t value_ordinal, uint32_t unit_offset,
    uint32_t unit_count, uint32_t program_point) {
  const loom_liveness_interval_t* value_interval =
      loom_liveness_interval_for_value_ordinal(liveness, value_ordinal);
  if (value_interval == NULL || unit_count == 0) {
    return false;
  }
  IREE_ASSERT_LE(unit_offset, value_interval->unit_count);
  IREE_ASSERT_LE(unit_count, value_interval->unit_count - unit_offset);
  const loom_value_ordinal_t storage_ordinal =
      placement->tied_storage_origins_by_value_ordinal == NULL
          ? value_ordinal
          : placement->tied_storage_origins_by_value_ordinal[value_ordinal];
  const loom_liveness_segment_range_t segments =
      loom_low_allocation_unit_liveness_storage_segment_range_for_value_ordinal(
          unit_liveness, liveness, storage_ordinal);
  const bool storage_live_at_point =
      segments.count == 0
          ? unit_liveness->values[storage_ordinal].acquisition_start_point <=
                program_point
          : loom_liveness_segment_range_contains(
                unit_liveness->storage_segments.entries, segments,
                program_point);
  if (!storage_live_at_point) {
    return false;
  }

  const uint32_t point_start =
      loom_low_allocation_unit_liveness_point_start_for_value_ordinal(
          unit_liveness, liveness, storage_ordinal);
  IREE_ASSERT_NE(point_start, UINT32_MAX);
  for (uint32_t i = 0; i < unit_count; ++i) {
    if (unit_liveness->end_points[point_start + unit_offset + i] >
        program_point) {
      return true;
    }
  }
  return false;
}
