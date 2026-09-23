// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "loom/target/arch/amd/xdna/aie2p/emit/xdna_product.h"

#include <array>
#include <cstring>
#include <vector>

#include "iree/io/vec_stream.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"

namespace loom {
namespace {

class XdnaProductTest : public ::testing::Test {
 protected:
  void SetUp() override {
    iree_arena_block_pool_initialize(4096, iree_allocator_system(), &pool_);
    iree_arena_initialize(&pool_, &arena_);
    IREE_ASSERT_OK(iree_io_vec_stream_create(
        IREE_IO_STREAM_MODE_READABLE | IREE_IO_STREAM_MODE_WRITABLE |
            IREE_IO_STREAM_MODE_SEEKABLE,
        4096, iree_allocator_system(), &stream_));
  }

  void TearDown() override {
    iree_io_stream_release(stream_);
    iree_arena_deinitialize(&arena_);
    iree_arena_block_pool_deinitialize(&pool_);
  }

  // Backing blocks for temporary native emission data.
  iree_arena_block_pool_t pool_;
  // Scratch storage used only while constructing the product.
  iree_arena_allocator_t arena_;
  // Growable native ELF output.
  iree_io_stream_t* stream_ = nullptr;
};

TEST_F(XdnaProductTest, PreservesRequirementsIndependentOfWorkerTopology) {
  // Service-only entries initialize a lock without loading a compute program.
  // Their partition width and external contracts remain required even though
  // no worker or payload-channel graph exists at this boundary.
  loom_aie2p_program_record_t commands[2] = {};
  commands[0].type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_WRITE32;
  commands[0].value.register_write32 = {0x0221F000, 1};
  commands[1].type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_WRITE32;
  commands[1].value.register_write32 = {0x0E21F000, 1};
  const loom_aie2p_array_program_t programs[] = {
      {nullptr, 0, &commands[0], 1, nullptr, 0},
      {nullptr, 0, &commands[1], 1, nullptr, 0},
  };
  const iree_xdna_elf_binding_record_t bindings[] = {
      {
          /*.kind=*/IREE_XDNA_ELF_BINDING_KIND_BUFFER,
          /*.address_space=*/IREE_XDNA_ELF_BINDING_ADDRESS_SPACE_GLOBAL,
          /*.access=*/IREE_XDNA_ELF_BINDING_ACCESS_READ,
          /*.usage=*/IREE_XDNA_ELF_BINDING_USAGE_DEVICE_VISIBLE,
          /*.minimum_byte_length=*/4096 + 256,
          /*.minimum_alignment=*/256,
          /*.minimum_byte_offset=*/256,
          /*.maximum_byte_offset=*/1024,
      },
      {
          /*.kind=*/IREE_XDNA_ELF_BINDING_KIND_BUFFER,
          /*.address_space=*/IREE_XDNA_ELF_BINDING_ADDRESS_SPACE_HOST,
          /*.access=*/IREE_XDNA_ELF_BINDING_ACCESS_READ |
              IREE_XDNA_ELF_BINDING_ACCESS_WRITE,
          /*.usage=*/IREE_XDNA_ELF_BINDING_USAGE_DEVICE_VISIBLE |
              IREE_XDNA_ELF_BINDING_USAGE_COHERENT,
          /*.minimum_byte_length=*/65536 + 64,
          /*.minimum_alignment=*/64,
          /*.minimum_byte_offset=*/0,
          /*.maximum_byte_offset=*/UINT64_MAX,
      },
  };
  const loom_aie2p_xdna_entry_t entries[] = {
      {IREE_SV("ingress"), 2, &bindings[0], 1, &programs[0], nullptr, 0},
      {IREE_SV("egress"), 8, &bindings[1], 1, &programs[1], nullptr, 0},
  };
  const auto profile_key = IREE_SV("amd.xdna.strix_halo.17f0_11");
  const loom_aie2p_xdna_product_t product = {
      loom_xdna_device_profile_lookup(profile_key), entries,
      IREE_ARRAYSIZE(entries)};
  IREE_ASSERT_OK(loom_aie2p_xdna_product_write(&product, stream_, &arena_));
  std::vector<uint8_t> bytes(iree_io_stream_length(stream_));
  IREE_ASSERT_OK(iree_io_stream_seek(stream_, IREE_IO_STREAM_SEEK_SET, 0));
  IREE_ASSERT_OK(
      iree_io_stream_read(stream_, bytes.size(), bytes.data(), nullptr));
  iree_arena_reset(&arena_);

  // ELF32's first program header describes the native metadata. Decode its
  // public wire records independently of the compiler's scratch allocation.
  ASSERT_GE(bytes.size(), 52u);
  const uint32_t program_header_offset =
      iree_unaligned_load_le_u32(bytes.data() + 28);
  ASSERT_LE(uint64_t(program_header_offset) + 32, bytes.size());
  const auto* program_header = bytes.data() + program_header_offset;
  ASSERT_EQ(iree_unaligned_load_le_u32(program_header),
            IREE_XDNA_ELF_PROGRAM_TYPE_METADATA);
  const uint32_t metadata_offset =
      iree_unaligned_load_le_u32(program_header + 4);
  const uint32_t metadata_length =
      iree_unaligned_load_le_u32(program_header + 16);
  ASSERT_LE(uint64_t(metadata_offset) + metadata_length, bytes.size());
  ASSERT_GE(metadata_length, IREE_XDNA_ELF_HEADER_RECORD_SIZE);
  const auto* metadata = bytes.data() + metadata_offset;
  const auto header = iree_xdna_elf_decode_header(metadata);
  EXPECT_EQ(header.column_count, 8u);
  ASSERT_EQ(header.allocation_count, 2u);
  ASSERT_EQ(header.allocation_use_count, 2u);
  ASSERT_EQ(header.entry_count, 2u);
  ASSERT_EQ(header.binding_count, 2u);
  const uint32_t entry_offset =
      IREE_XDNA_ELF_HEADER_RECORD_SIZE +
      header.allocation_count * IREE_XDNA_ELF_ALLOCATION_RECORD_SIZE +
      header.allocation_use_count * sizeof(uint32_t);
  const uint32_t binding_offset =
      entry_offset + header.entry_count * IREE_XDNA_ELF_ENTRY_RECORD_SIZE;
  ASSERT_LE(
      binding_offset + header.binding_count * IREE_XDNA_ELF_BINDING_RECORD_SIZE,
      metadata_length);
  for (uint32_t i = 0; i < IREE_ARRAYSIZE(entries); ++i) {
    const auto entry = iree_xdna_elf_decode_entry(
        metadata + entry_offset + i * IREE_XDNA_ELF_ENTRY_RECORD_SIZE);
    ASSERT_EQ(entry.first_binding, i);
    EXPECT_EQ(entry.binding_count, 1u);
    const auto binding = iree_xdna_elf_decode_binding(
        metadata + binding_offset +
        entry.first_binding * IREE_XDNA_ELF_BINDING_RECORD_SIZE);
    EXPECT_EQ(binding.kind, bindings[i].kind);
    EXPECT_EQ(binding.address_space, bindings[i].address_space);
    EXPECT_EQ(binding.access, bindings[i].access);
    EXPECT_EQ(binding.usage, bindings[i].usage);
    EXPECT_EQ(binding.minimum_byte_length, bindings[i].minimum_byte_length);
    EXPECT_EQ(binding.minimum_alignment, bindings[i].minimum_alignment);
    EXPECT_EQ(binding.minimum_byte_offset, bindings[i].minimum_byte_offset);
    EXPECT_EQ(binding.maximum_byte_offset, bindings[i].maximum_byte_offset);
  }
}

TEST(Aie2pXdnaProductTest, LoadsInitializedTileSectionsBeforeActivation) {
  const loom_xdna_device_profile_t* profile =
      loom_xdna_device_profile_lookup(IREE_SV("amd.xdna.strix_halo.17f0_11"));
  ASSERT_NE(profile, nullptr);
  const loom_xdna_array_family_t* family =
      loom_xdna_device_profile_array_family(profile);
  ASSERT_NE(family, nullptr);

  uint32_t program_load_base = 0;
  for (uint8_t i = 0; i < family->tile_count; ++i) {
    if (family->tiles[i].kind == LOOM_XDNA_TILE_KIND_COMPUTE) {
      program_load_base = family->tiles[i].memory.program_load_base;
    }
  }
  ASSERT_NE(program_load_base, 0u);

  constexpr loom_xdna_tile_coordinate_t kCoordinate = {1, 2};
  constexpr uint32_t kDataOwnerOffset = 0x4020;
  const std::array<uint8_t, 6> code = {
      0x44, 0x20, 0xc1, 0x20, 0x40, 0x07,
  };
  const std::array<uint8_t, 5> table = {
      0x11, 0x22, 0x33, 0x44, 0x55,
  };
  loom_native_section_t linked_sections[] = {
      {
          /*.name=*/IREE_SV(".text.kernel"),
          /*.storage=*/LOOM_NATIVE_SECTION_STORAGE_CONTENTS,
          /*.access=*/LOOM_NATIVE_SECTION_ACCESS_READ |
              LOOM_NATIVE_SECTION_ACCESS_EXECUTE,
          /*.address=*/0,
          /*.alignment=*/16,
          /*.contents=*/iree_make_const_byte_span(code.data(), code.size()),
      },
      {
          /*.name=*/IREE_SV(".rodata.table"),
          /*.storage=*/LOOM_NATIVE_SECTION_STORAGE_CONTENTS,
          /*.access=*/LOOM_NATIVE_SECTION_ACCESS_READ,
          /*.address=*/0x74020,
          /*.alignment=*/32,
          /*.contents=*/iree_make_const_byte_span(table.data(), table.size()),
      },
      {
          /*.name=*/IREE_SV(".storage.kernel.scratch"),
          /*.storage=*/LOOM_NATIVE_SECTION_STORAGE_RESERVATION,
          /*.access=*/LOOM_NATIVE_SECTION_ACCESS_READ |
              LOOM_NATIVE_SECTION_ACCESS_WRITE,
          /*.address=*/0x70000,
          /*.alignment=*/64,
          /*.contents=*/iree_const_byte_span_empty(),
          /*.reservation_length=*/64,
      },
  };
  const loom_aie2p_linked_section_placement_t linked_placements[] = {
      {
          /*.memory_space=*/LOOM_XDNA_MEMORY_SPACE_PROGRAM,
          /*.owner_offset=*/0,
      },
      {
          /*.memory_space=*/LOOM_XDNA_MEMORY_SPACE_DATA,
          /*.owner_offset=*/kDataOwnerOffset,
      },
      {
          /*.memory_space=*/LOOM_XDNA_MEMORY_SPACE_DATA,
          /*.owner_offset=*/0,
      },
  };
  const loom_aie2p_linked_tile_t linked_tile = {
      /*.assembly=*/
      {
          /*.sections=*/linked_sections,
          /*.section_count=*/IREE_ARRAYSIZE(linked_sections),
      },
      /*.section_placements=*/linked_placements,
      /*.section_placement_count=*/IREE_ARRAYSIZE(linked_placements),
      /*.symbol_layouts=*/nullptr,
      /*.symbol_layout_count=*/0,
      /*.entry_section_index=*/0,
      /*.entry_address=*/0,
  };
  const loom_native_object_symbol_t entry_symbol = {
      /*.name=*/IREE_SV("kernel"),
      /*.section_contribution_index=*/0,
      /*.section_offset=*/0,
      /*.size=*/code.size(),
      /*.binding=*/LOOM_NATIVE_OBJECT_SYMBOL_BINDING_GLOBAL,
      /*.visibility=*/LOOM_NATIVE_OBJECT_SYMBOL_VISIBILITY_DEFAULT,
      /*.kind=*/LOOM_NATIVE_OBJECT_SYMBOL_KIND_FUNCTION,
  };
  loom_aie2p_leaf_contribution_t contribution = {};
  contribution.object.symbols = &entry_symbol;
  contribution.object.symbol_count = 1;
  contribution.realization.entry_symbol_index = 0;

  const loom_aie2p_xdna_tile_t tile = {
      /*.coordinate=*/kCoordinate,
      /*.contribution=*/&contribution,
      /*.linked_tile=*/&linked_tile,
  };
  constexpr uint32_t kActivationAddress = 0x01234000;
  constexpr uint32_t kActivationValue = 0x0000CAFE;
  loom_aie2p_program_record_t records[2] = {};
  records[0].type = LOOM_AIE2P_PROGRAM_RECORD_TILE_PROGRAM_LOAD;
  records[0].value.tile_program_load.tile_program_index = 0;
  records[1].type = LOOM_AIE2P_PROGRAM_RECORD_REGISTER_WRITE32;
  records[1].value.register_write32.address = kActivationAddress;
  records[1].value.register_write32.value = kActivationValue;
  loom_aie2p_array_program_t array_program = {};
  array_program.array_records = records;
  array_program.array_record_count = IREE_ARRAYSIZE(records);
  array_program.tile_program_count = 1;

  const loom_aie2p_xdna_entry_t entry = {
      /*.name=*/IREE_SV("entry"),
      /*.column_count=*/kCoordinate.column + 1,
      /*.bindings=*/nullptr,
      /*.binding_count=*/0,
      /*.array_program=*/&array_program,
      /*.tiles=*/&tile,
      /*.tile_count=*/1,
  };
  const loom_aie2p_xdna_product_t product = {
      /*.device_profile=*/profile,
      /*.entries=*/&entry,
      /*.entry_count=*/1,
  };

  iree_arena_block_pool_t block_pool;
  iree_arena_block_pool_initialize(4096, iree_allocator_system(), &block_pool);
  iree_arena_allocator_t arena;
  iree_arena_initialize(&block_pool, &arena);
  iree_io_stream_t* stream = nullptr;
  IREE_ASSERT_OK(iree_io_vec_stream_create(
      IREE_IO_STREAM_MODE_READABLE | IREE_IO_STREAM_MODE_WRITABLE |
          IREE_IO_STREAM_MODE_SEEKABLE,
      4096, iree_allocator_system(), &stream));
  IREE_ASSERT_OK(loom_aie2p_xdna_product_write(&product, stream, &arena));

  std::vector<uint8_t> file_bytes(iree_io_stream_length(stream));
  IREE_ASSERT_OK(iree_io_stream_seek(stream, IREE_IO_STREAM_SEEK_SET, 0));
  IREE_ASSERT_OK(iree_io_stream_read(stream, file_bytes.size(),
                                     file_bytes.data(), nullptr));
  ASSERT_GE(file_bytes.size(), 52u);
  const uint32_t program_header_offset =
      iree_unaligned_load_le_u32(file_bytes.data() + 28);
  const uint16_t program_header_size =
      iree_unaligned_load_le_u16(file_bytes.data() + 42);
  const uint16_t program_header_count =
      iree_unaligned_load_le_u16(file_bytes.data() + 44);
  ASSERT_EQ(program_header_size, 32u);
  ASSERT_LE((uint64_t)program_header_offset +
                (uint64_t)program_header_count * program_header_size,
            file_bytes.size());

  const uint8_t* metadata_header = file_bytes.data() + program_header_offset;
  ASSERT_EQ(iree_unaligned_load_le_u32(metadata_header),
            IREE_XDNA_ELF_PROGRAM_TYPE_METADATA);
  const uint32_t metadata_offset =
      iree_unaligned_load_le_u32(metadata_header + 4);
  const uint32_t metadata_size =
      iree_unaligned_load_le_u32(metadata_header + 16);
  ASSERT_LE((uint64_t)metadata_offset + metadata_size, file_bytes.size());
  const uint8_t* metadata = file_bytes.data() + metadata_offset;
  const iree_xdna_elf_header_record_t product_header =
      iree_xdna_elf_decode_header(metadata);
  ASSERT_EQ(product_header.allocation_count, 1u);
  const iree_xdna_elf_allocation_record_t allocation =
      iree_xdna_elf_decode_allocation(metadata +
                                      IREE_XDNA_ELF_HEADER_RECORD_SIZE);

  std::vector<uint8_t> transaction(allocation.byte_length, 0);
  std::vector<uint32_t> load_offsets;
  for (uint32_t i = 0; i < allocation.load_count; ++i) {
    const uint32_t header_index = allocation.first_load + i;
    ASSERT_LT(header_index, program_header_count);
    const uint8_t* header = file_bytes.data() + program_header_offset +
                            (uint64_t)header_index * program_header_size;
    ASSERT_EQ(iree_unaligned_load_le_u32(header),
              IREE_XDNA_ELF_PROGRAM_TYPE_LOAD);
    const uint32_t file_offset = iree_unaligned_load_le_u32(header + 4);
    const uint32_t load_offset = iree_unaligned_load_le_u32(header + 8);
    const uint32_t allocation_ordinal = iree_unaligned_load_le_u32(header + 12);
    const uint32_t file_size = iree_unaligned_load_le_u32(header + 16);
    const uint32_t memory_size = iree_unaligned_load_le_u32(header + 20);
    ASSERT_EQ(allocation_ordinal, 0u);
    ASSERT_LE(file_size, memory_size);
    ASSERT_LE((uint64_t)file_offset + file_size, file_bytes.size());
    ASSERT_LE((uint64_t)load_offset + memory_size, transaction.size());
    std::memcpy(transaction.data() + load_offset,
                file_bytes.data() + file_offset, file_size);
    load_offsets.push_back(load_offset);
  }

  ASSERT_GE(load_offsets.size(), 5u);
  EXPECT_EQ(load_offsets[0], 0u);
  EXPECT_EQ(load_offsets[1], 32u);
  EXPECT_EQ(load_offsets[2], 40u);
  EXPECT_EQ(load_offsets[3], 56u);
  EXPECT_EQ(load_offsets[4], 64u);
  EXPECT_EQ(iree_unaligned_load_le_u32(transaction.data() + 8), 3u);

  const uint32_t coordinate_address =
      ((uint32_t)kCoordinate.column << family->column_shift) |
      ((uint32_t)kCoordinate.row << family->row_shift);
  const uint8_t* code_load = transaction.data() + 16;
  EXPECT_EQ(code_load[0], 1u);
  EXPECT_EQ(iree_unaligned_load_le_u32(code_load + 8),
            coordinate_address | program_load_base);
  EXPECT_EQ(iree_unaligned_load_le_u32(code_load + 12), 24u);
  EXPECT_EQ(0, std::memcmp(transaction.data() + 32, code.data(), code.size()));
  EXPECT_EQ(transaction[38], 0u);
  EXPECT_EQ(transaction[39], 0u);

  const uint8_t* data_load = transaction.data() + 40;
  EXPECT_EQ(data_load[0], 1u);
  EXPECT_EQ(iree_unaligned_load_le_u32(data_load + 8),
            coordinate_address | kDataOwnerOffset);
  EXPECT_EQ(iree_unaligned_load_le_u32(data_load + 12), 24u);
  EXPECT_EQ(0,
            std::memcmp(transaction.data() + 56, table.data(), table.size()));
  EXPECT_EQ(transaction[61], 0u);
  EXPECT_EQ(transaction[62], 0u);
  EXPECT_EQ(transaction[63], 0u);

  const uint8_t* activation = transaction.data() + 64;
  EXPECT_EQ(iree_unaligned_load_le_u32(activation + 8), kActivationAddress);
  EXPECT_EQ(iree_unaligned_load_le_u32(activation + 16), kActivationValue);
  EXPECT_EQ(iree_unaligned_load_le_u32(activation + 20), 24u);

  iree_io_stream_release(stream);
  iree_arena_deinitialize(&arena);
  iree_arena_block_pool_deinitialize(&block_pool);
}

}  // namespace
}  // namespace loom
