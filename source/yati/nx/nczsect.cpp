// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NCZSECT2 and NCZSECT3 of NXPress, as the library app reads them; see nczsect.hpp.
#include "yati/nx/nczsect.hpp"
#include "lz4nintendo.h"

#include <algorithm>
#include <cstring>
#include <zstd.h>

namespace sphaira::ncz {
namespace {

u64 Get64(const u8* p) {
    u64 v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

u32 Get32(const u8* p) {
    u32 v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

constexpr u64 SECTION_MAGIC_MASK = 0x00FFFFFFFFFFFFFFULL;
constexpr u64 MAX_RAW_SIZE = 1ULL << 24;

} // namespace

bool IsShifted(u64 magic) {
    return magic == NCZ_SECTION2_MAGIC || magic == NCZ_SECTION3_MAGIC;
}

bool IsUnknownSectionMagic(u64 magic) {
    return (magic & SECTION_MAGIC_MASK) == (NCZ_SECTION_MAGIC & SECTION_MAGIC_MASK) && magic != NCZ_SECTION_MAGIC && !IsShifted(magic);
}

Result PrepareSections(u64 magic, std::vector<Section>& sections) {
    R_UNLESS(!sections.empty(), Result_YatiInvalidNczSectionCount);
    const bool shifted = IsShifted(magic);
    for (auto& section : sections) {
        if (!shifted) {
            section.padding = 0;
            continue;
        }
        const auto shift = s64(section.padding);
        const bool ctr = section.crypto_type == 3 || section.crypto_type == 4;
        R_UNLESS(shift % 16 == 0 && (shift == 0 || ctr), Result_NczBadTable);
    }

    // like nsz: the bytes between the copied header and a first section that starts later are
    // stored without encryption, and a first section that starts earlier continues where the
    // copied header ends
    if (sections[0].offset > NCZ_NORMAL_SIZE) {
        Section gap{};
        gap.offset = NCZ_NORMAL_SIZE;
        gap.size = sections[0].offset - NCZ_NORMAL_SIZE;
        gap.crypto_type = 1;
        sections.insert(sections.begin(), gap);
    } else if (sections[0].offset < NCZ_NORMAL_SIZE) {
        const u64 skip = NCZ_NORMAL_SIZE - sections[0].offset;
        R_UNLESS(skip < sections[0].size, Result_NczBadTable);
        sections[0].offset += skip;
        sections[0].size -= skip;
    }

    // the new formats are checked as the library app checks them: the sections follow each
    // other from the end of the copied header
    if (shifted) {
        u64 next = NCZ_NORMAL_SIZE;
        for (const auto& section : sections) {
            R_UNLESS(section.offset == next && section.offset + section.size >= section.offset, Result_NczBadTable);
            next = section.offset + section.size;
        }
    }
    R_SUCCEED();
}

void CounterAt(const Section& section, u64 position, u8 counter[0x10]) {
    std::memcpy(counter, section.counter, 0x8);
    const u64 block = u64(s64(position) + s64(section.padding)) >> 4;
    for (int i = 0; i < 8; i++) {
        counter[0x8 + i] = u8(block >> (56 - 8 * i));
    }
}

Result ParseLz4Footer(const u8 footer[LZ4_FOOTER_SIZE], u64 ncz_size, u64& table_start) {
    R_UNLESS(ncz_size >= LZ4_FOOTER_SIZE + LZ4_TABLE_HEAD_SIZE, Result_NczBadTable);
    const u64 start = Get64(footer);
    R_UNLESS(Get64(footer + 8) == LZ4_FOOTER_MAGIC, Result_NczBadTable);
    R_UNLESS(start >= NCZ_NORMAL_SIZE && start <= ncz_size - LZ4_FOOTER_SIZE - LZ4_TABLE_HEAD_SIZE, Result_NczBadTable);
    table_start = start;
    R_SUCCEED();
}

Result ParseLz4Table(std::span<const u8> table, std::vector<Lz4Block>& blocks) {
    blocks.clear();
    R_UNLESS(table.size() >= LZ4_TABLE_HEAD_SIZE, Result_NczBadTable);
    R_UNLESS(Get64(table.data()) == LZ4_TABLE_MAGIC, Result_NczBadTable);
    const u64 count = Get64(table.data() + 8);
    const u64 packed_size = Get64(table.data() + 16);
    R_UNLESS(packed_size == table.size() - LZ4_TABLE_HEAD_SIZE, Result_NczBadTable);
    if (count > LZ4_MAX_BLOCKS) {
        log_write("[NCZ] an LZ4 table of %llu blocks is more than this console keeps\n", (unsigned long long)count);
        R_THROW(Result_NczBadTable);
    }

    std::vector<u8> entries;
    try {
        entries.resize(size_t(count * LZ4_TABLE_ENTRY_SIZE) + 1);
        blocks.resize(size_t(count));
    } catch (...) {
        blocks.clear();
        R_THROW(Result_OutOfMemory);
    }
    const size_t made = ZSTD_decompress(entries.data(), size_t(count * LZ4_TABLE_ENTRY_SIZE),
                                        table.data() + LZ4_TABLE_HEAD_SIZE, size_t(packed_size));
    if (ZSTD_isError(made) || made != count * LZ4_TABLE_ENTRY_SIZE) {
        blocks.clear();
        R_THROW(Result_NczBadTable);
    }

    u64 next = NCZ_NORMAL_SIZE;
    for (size_t i = 0; i < blocks.size(); i++) {
        const u8* e = entries.data() + i * LZ4_TABLE_ENTRY_SIZE;
        auto& block = blocks[i];
        block.offset = Get64(e);
        block.size = Get32(e + 8);
        block.raw_size = Get32(e + 12);
        block.level = int8_t(e[16]);
        R_UNLESS(block.offset >= next && block.raw_size <= MAX_RAW_SIZE && block.size != 0, Result_NczBadTable);
        next = block.offset + block.size;
        R_UNLESS(next > block.offset, Result_NczBadTable);
    }
    R_SUCCEED();
}

Result RebuildBlock(const Lz4Block& block, std::span<const u8> raw, std::vector<u8>& out, void* work) {
    R_UNLESS(raw.size() == block.raw_size, Result_NczLz4Rebuild);
    if (!nx_lz4_level_known(block.level)) {
        log_write("[NCZ] the LZ4 block at 0x%llx is of level %d, not one of Nintendo's\n", (unsigned long long)block.offset, int(block.level));
        R_THROW(Result_NczLz4Rebuild);
    }
    out.resize(std::max<size_t>(nx_lz4_bound(raw.size()), block.size));
    const int64_t made = nx_lz4_compress(raw.data(), raw.size(), out.data(), out.size(), block.level, work);
    if (made != int64_t(block.size)) {
        log_write("[NCZ] the LZ4 block at 0x%llx is made to %lld bytes, not %u\n", (unsigned long long)block.offset, (long long)made, block.size);
        R_THROW(Result_NczLz4Rebuild);
    }
    out.resize(block.size);
    R_SUCCEED();
}

Lz4Rebuilder::Lz4Rebuilder(std::vector<Lz4Block> blocks, u64 position)
: m_blocks{std::move(blocks)}, m_position{position} {
}

void Lz4Rebuilder::Feed(const u8* data, size_t size, std::vector<Piece>& out) {
    while (size || (!m_in_block && m_next < m_blocks.size() && m_blocks[m_next].offset == m_position && !m_blocks[m_next].raw_size)) {
        if (m_in_block) {
            const auto& block = m_blocks[m_next - 1];
            const size_t take = std::min<size_t>(size, block.raw_size - m_current.size());
            m_current.insert(m_current.end(), data, data + take);
            data += take;
            size -= take;
            if (m_current.size() == block.raw_size) {
                out.push_back(Piece{std::move(m_current), s64(m_next - 1)});
                m_current = {};
                m_in_block = false;
                m_position += block.size;
            }
            continue;
        }
        if (m_next < m_blocks.size() && m_blocks[m_next].offset == m_position) {
            const auto& block = m_blocks[m_next++];
            if (!block.raw_size) {
                out.push_back(Piece{{}, s64(m_next - 1)});
                m_position += block.size;
                continue;
            }
            m_in_block = true;
            m_current.clear();
            m_current.reserve(block.raw_size);
            continue;
        }
        // the bytes up to the next block, or all of them
        size_t pass = size;
        if (m_next < m_blocks.size()) {
            pass = size_t(std::min<u64>(pass, m_blocks[m_next].offset - m_position));
        }
        while (pass) {
            if (out.empty() || out.back().block >= 0 || out.back().data.size() >= PASS_PIECE) {
                out.push_back(Piece{});
                out.back().data.reserve(std::min(pass, PASS_PIECE));
            }
            auto& piece = out.back().data;
            const size_t take = std::min(pass, PASS_PIECE - piece.size());
            piece.insert(piece.end(), data, data + take);
            data += take;
            size -= take;
            pass -= take;
            m_position += take;
        }
    }
}

bool Lz4Rebuilder::Done() const {
    return !m_in_block && m_next == m_blocks.size();
}

} // namespace sphaira::ncz
