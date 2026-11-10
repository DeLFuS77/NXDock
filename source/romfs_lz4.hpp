// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The compressed storage of an NCA section (firmware 12 and later), written as Nintendo writes it
// (24A, "Compress games"). Seen in games Nintendo ships compressed:
//  - only the data level of the RomFS (the last level of its IVFC) is compressed; the hash levels
//    stay before it, and they hash the data as it reads decompressed, so they do not change;
//  - the stored data level is: the first 0x200 bytes as they are (an entry of kind "none" whose
//    stored size is 0xFFFFFFFF), then blocks of at most 64 KiB, each LZ4 or as it is, at places
//    aligned to 0x10, then the table, right after the last block (0x10 aligned);
//  - the table is a bucket tree of 0x4000-byte nodes: a first node with how many sets and where
//    each starts, then one node per set of up to 682 entries of 0x18 bytes: the offset of the
//    block in the decompressed data, its place after the start of the stored data level, its
//    kind (0 none, 3 LZ4), the level it was made at and its stored size;
//  - the FS header keeps the place and size of the table (after the start of the data level) and
//    its BKTR header at 0x178; the size of the data level becomes what is stored (blocks + table).
// Atmosphère: fssystem_compressed_storage.hpp, fssystem_bucket_tree.cpp.
//
// Plain C++: tested on the computer.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace nxdock::lz4pack {

constexpr uint32_t kBlock = 0x10000;     // the blocks, as Nintendo's
constexpr uint32_t kHead = 0x200;        // the start, kept as it is
constexpr uint32_t kNode = 0x4000;       // a node of the table
constexpr uint32_t kPerSet = (kNode - 0x10) / 0x18; // 682
constexpr uint8_t kNone = 0, kLz4 = 3;
constexpr uint32_t kNoneSize = 0xFFFFFFFF;

struct Entry {
    uint64_t virt = 0, phys = 0;
    uint8_t type = kNone;
    int8_t level = 0;
    uint32_t size = kNoneSize; // stored bytes of an LZ4 block
};

struct Packed {
    std::vector<Entry> entries;
    uint64_t virt_size = 0;
    uint64_t stored = 0;      // stored bytes of the blocks, the last one included
    uint64_t table_offset = 0; // after the start of the data level: stored, aligned to 0x10
};

using ReadFn = std::function<bool(uint64_t offset, uint8_t* buf, size_t size)>;     // the plain data level
using WriteFn = std::function<bool(uint64_t offset, const uint8_t* data, size_t size)>; // its stored form, in order
using StepFn = std::function<bool(uint64_t done)>;                                    // false: stop

// Packs a data level of virt_size bytes. Blocks that do not get at least min_gain bytes smaller
// stay as they are; next to each other they make one entry. Padding between stored blocks is
// written as zeros, so the stored form is written in one go, from 0 to stored.
bool Pack(uint64_t virt_size, int level, uint32_t min_gain, const ReadFn& read, const WriteFn& write, const StepFn& step, Packed& out);

// The table of the entries, as the FS reads it; its size is a whole number of nodes.
std::vector<uint8_t> BuildTable(const std::vector<Entry>& entries, uint64_t virt_end);
// And read back (for the checks): false when it is not a table of count entries.
bool ParseTable(const uint8_t* table, size_t size, uint32_t count, std::vector<Entry>& out, uint64_t& virt_end);

// The plain bytes of [offset, offset + size) from the stored form: what the FS gives.
bool Unpack(const std::vector<Entry>& entries, uint64_t virt_end, const ReadFn& stored, uint64_t offset, uint8_t* out, size_t size);

// What packing would save, from a sample of the blocks (one in every `every`): the share of the
// bytes saved, from 0 to 1.
double EstimateGain(uint64_t virt_size, int level, uint32_t every, const ReadFn& read);

} // namespace nxdock::lz4pack
