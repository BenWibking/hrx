// SPDX-License-Identifier: BSD-3-Clause
// Prints the CellRecord layout of a kernel, built natively (non-Loom branch of
// support.h). compare_rocm is built once from the seed kernel and packs cells
// with that layout, so a candidate whose layout differs is rejected before it
// reaches the GPU.
#include "reproducer.cpp"

#include <cstddef>
#include <cstdio>

int main() {
    using chemistry::CellRecord;
    std::printf("CellRecord size=%zu align=%zu\n", sizeof(CellRecord), alignof(CellRecord));
    std::printf("current offset=%zu size=%zu\n", offsetof(CellRecord, current), sizeof(CellRecord::current));
    std::printf("time offset=%zu\n", offsetof(CellRecord, time));
    std::printf("density_driver offset=%zu\n", offsetof(CellRecord, density_driver));
    std::printf("completed_steps offset=%zu\n", offsetof(CellRecord, completed_steps));
    std::printf("stats offset=%zu size=%zu\n", offsetof(CellRecord, stats), sizeof(CellRecord::stats));
}
