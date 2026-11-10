// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The experimental NCZ formats of NXPress ("extra compression"), read as the library app reads
// them (switchfs/nsz.go, lz4rebuild.go):
//
//   - NCZSECT2: the section table of NCZSECTN, where the field at 0x18 of a section, unused
//     before, moves its AES-CTR counter (a multiple of 16, signed, only on an AES-CTR section):
//     a piece of an NCA in sparse storage is encrypted at its place in the whole game.
//   - NCZSECT3: NCZSECT2 whose stream holds some LZ4 blocks of the compressed storage of the NCA
//     decompressed. After the stream: "NCZLZ4RB", the number of blocks (u64), the size of the
//     table (u64) and the table compressed with zstd, 17 bytes a block: its offset in the NCA
//     (u64), its size there (u32), its decompressed size (u32) and its level (s8). The last 16
//     bytes of the NCZ: where "NCZLZ4RB" starts (u64) and "NCZLZ4FT". Each block is made again
//     with Nintendo's compressors (lz4nintendo.h) when the NCA is rebuilt.
#pragma once

#include "yati/nx/ncz.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sphaira::ncz {

// "NCZLZ4RB" and "NCZLZ4FT" read as little endian
constexpr u64 LZ4_TABLE_MAGIC = 0x4252345A4C5A434EULL;
constexpr u64 LZ4_FOOTER_MAGIC = 0x5446345A4C5A434EULL;
constexpr u64 LZ4_FOOTER_SIZE = 16;
constexpr u64 LZ4_TABLE_HEAD_SIZE = 24;
constexpr u64 LZ4_TABLE_ENTRY_SIZE = 17;
// the most blocks a table may hold here: 16 bytes each in memory, 32 MiB at most (a game of
// 64 KiB blocks would need 128 GiB of them)
constexpr u64 LZ4_MAX_BLOCKS = 1ULL << 21;

struct Lz4Block {
    u64 offset;
    u32 size;
    u32 raw_size;
    int8_t level;
};

// Whether magic is one of the section tables that move the counter (NCZSECT2, NCZSECT3).
bool IsShifted(u64 magic);

// Whether magic looks like the section table of a later format ("NCZSECT" and another letter).
bool IsUnknownSectionMagic(u64 magic);

// Checks the sections of an NCZ as the library app does, and places them as it does: the bytes
// between the NCA header and a later first section are a section without encryption, and a first
// section that starts earlier begins where the header ends. For NCZSECTN the field at 0x18 is
// unused and is cleared; for NCZSECT2/3 it must be a multiple of 16, and 0 when not AES-CTR.
Result PrepareSections(u64 magic, std::vector<Section>& sections);

// The AES-CTR counter of the 16 byte block holding the byte at position of the NCA.
void CounterAt(const Section& section, u64 position, u8 counter[0x10]);

// From the last 16 bytes of an NCZSECT3 of ncz_size bytes: where its table starts, which is
// where its stream ends.
Result ParseLz4Footer(const u8 footer[LZ4_FOOTER_SIZE], u64 ncz_size, u64& table_start);

// The table, the bytes from table_start up to the footer: the blocks, checked to follow each
// other after the NCA header.
Result ParseLz4Table(std::span<const u8> table, std::vector<Lz4Block>& blocks);

// Makes a block again from its decompressed bytes; out gets its bytes in the NCA. work holds
// NX_LZ4_WORK_SIZE bytes.
Result RebuildBlock(const Lz4Block& block, std::span<const u8> raw, std::vector<u8>& out, void* work);

// Cuts the decompressed stream of an NCZSECT3 into pieces, in the order of the NCA: bytes as
// they are, or the decompressed bytes of a whole block of the table, to make again.
class Lz4Rebuilder {
public:
    struct Piece {
        std::vector<u8> data;
        // the block of the table, or -1 for bytes as they are
        s64 block = -1;
    };

    // position is the place in the NCA of the first byte of the stream.
    Lz4Rebuilder(std::vector<Lz4Block> blocks, u64 position);

    // Appends the pieces of the next bytes of the stream to out; bytes as they are come in
    // pieces of PASS_PIECE at most.
    void Feed(const u8* data, size_t size, std::vector<Piece>& out);

    // Whether every block was read whole.
    bool Done() const;

    const Lz4Block& Block(s64 index) const { return m_blocks[size_t(index)]; }

    static constexpr size_t PASS_PIECE = 256 * 1024;

private:
    std::vector<Lz4Block> m_blocks;
    size_t m_next{};
    // the place in the NCA of the next byte of the stream outside a block
    u64 m_position{};
    // the block being read, and its bytes so far
    bool m_in_block{};
    std::vector<u8> m_current{};
};

} // namespace sphaira::ncz
