# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from dataclasses import replace

import pytest

from loom.target.arch.amdgpu.descriptors.api import (
    _AMDGPU_CORE_DESCRIPTOR_SET_BUILDER_FLAG_BUFFER_LOAD_SGPR_CAPTURE,
    _AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS,
    _with_instruction_classes,
    _with_storage_lease_rows,
)
from loom.target.arch.amdgpu.descriptors.common import (
    _COUNTER_VMEM_LOAD,
    _SCHEDULE_VMEM_LOAD,
)
from loom.target.arch.amdgpu.descriptors.contracts import (
    _amdgpu_contract_descriptor_from_overlay,
)
from loom.target.low_descriptors import (
    DescriptorSet,
    InstructionClass,
    StorageLeaseAttachment,
    StorageLeaseFlag,
    StorageLeaseKind,
)

_CAPTURE_TARGETS = ("rdna3", "gfx11_generic", "rdna3_5")


def _descriptor_set(target: str) -> DescriptorSet:
    builder = _AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS[target]
    return _with_instruction_classes(
        replace(
            builder.base,
            descriptors=(
                *(
                    _amdgpu_contract_descriptor_from_overlay(overlay)
                    for overlay in builder.overlay_rows()
                ),
                *builder.extra_descriptors,
                *builder.base.descriptors,
            ),
        )
    )


def test_buffer_scalar_capture_target_contracts() -> None:
    assert {
        target
        for target, builder in _AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS.items()
        if builder.flags
        & _AMDGPU_CORE_DESCRIPTOR_SET_BUILDER_FLAG_BUFFER_LOAD_SGPR_CAPTURE
    } == set(_CAPTURE_TARGETS)


@pytest.mark.parametrize("target", _CAPTURE_TARGETS)
def test_buffer_scalar_capture_preserves_all_result_leases(target: str) -> None:
    descriptor_set = _with_storage_lease_rows(
        _descriptor_set(target),
        builder_flags=_AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS[target].flags,
    )
    descriptors = {
        descriptor.key: descriptor for descriptor in descriptor_set.descriptors
    }
    families = (
        *(
            (name, units, ("", "_off_zero", "_vaddr_offset"))
            for name, units in (
                ("dword", 1),
                ("b64", 2),
                ("b96", 3),
                ("b128", 4),
                ("u8", 1),
                ("i8", 1),
                ("u16", 1),
                ("i16", 1),
            )
        ),
        ("b16_d16", 1, ("", "_vaddr_offset")),
        ("b16_d16_hi", 1, ("", "_vaddr_offset")),
    )
    for family, units, suffixes in families:
        for suffix in suffixes:
            descriptor = descriptors[f"amdgpu.buffer_load_{family}{suffix}"]
            assert len(descriptor.storage_leases) == 1, descriptor.key
            lease = descriptor.storage_leases[0]
            assert lease.kind is StorageLeaseKind.RESULT_WRITE
            assert lease.attachment is StorageLeaseAttachment.RESULT
            assert lease.attachment_index == 0
            assert lease.unit_offset == 0
            assert lease.unit_count == units
            assert lease.release_class_id == _COUNTER_VMEM_LOAD
            assert lease.flags == (
                StorageLeaseFlag.STARTS_AT_ISSUE,
                StorageLeaseFlag.RELEASE_BEFORE_BOUNDARY,
                StorageLeaseFlag.RELEASE_FOR_PRESSURE,
            )


@pytest.mark.parametrize("target", tuple(_AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS))
def test_buffer_scalar_capture_changes_only_qualified_source_leases(
    target: str,
) -> None:
    descriptor_set = _descriptor_set(target)
    flags = _AMDGPU_CORE_DESCRIPTOR_SET_BUILDERS[target].flags
    original = _with_storage_lease_rows(
        descriptor_set,
        builder_flags=(
            flags & ~_AMDGPU_CORE_DESCRIPTOR_SET_BUILDER_FLAG_BUFFER_LOAD_SGPR_CAPTURE
        ),
    )
    captured = _with_storage_lease_rows(descriptor_set, builder_flags=flags)
    changed_count = 0
    for before, after in zip(original.descriptors, captured.descriptors, strict=True):
        if (
            target in _CAPTURE_TARGETS
            and InstructionClass.BUFFER_LOAD in before.instruction_classes
            and before.schedule_class == _SCHEDULE_VMEM_LOAD
        ):
            expected_leases = tuple(
                lease
                for lease in before.storage_leases
                if not (
                    lease.kind is StorageLeaseKind.SOURCE_READ
                    and lease.release_class_id == _COUNTER_VMEM_LOAD
                )
            )
            assert before.storage_leases != expected_leases, before.key
            assert after == replace(before, storage_leases=expected_leases), before.key
            changed_count += 1
        else:
            # Includes global/flat loads, LDSDMA, atomics, stores, SMEM,
            # tensor and XCNT contracts, and every unaffected target family.
            assert after == before, before.key
    assert changed_count == (28 if target in _CAPTURE_TARGETS else 0)
