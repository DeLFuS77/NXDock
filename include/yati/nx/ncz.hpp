#pragma once

#include "yati/source/base.hpp"
#include "defines.hpp"

#include <switch.h>
#include <vector>
#include <memory>
#include <zstd.h>

namespace sphaira::ncz {

#define NCZ_SECTION_MAGIC 0x4E544345535A434EUL
// NXDock: the experimental sections of NXPress (NCZSECT2, NCZSECT3), read in nczsect.hpp.
#define NCZ_SECTION2_MAGIC 0x32544345535A434EUL
#define NCZ_SECTION3_MAGIC 0x33544345535A434EUL
// NXDock: more sections than an update ever has means a damaged table.
#define NCZ_MAX_SECTIONS (1UL << 20)
// todo: byteswap this
#define NCZ_BLOCK_MAGIC std::byteswap(0x4E435A424C4F434BUL)

#define NCZ_BLOCK_VERSION (2)
#define NCZ_BLOCK_TYPE (1)

#define NCZ_NORMAL_SIZE    (0x4000)
#define NCZ_SECTION_OFFSET (NCZ_NORMAL_SIZE + sizeof(ncz::Header))

struct Header {
    u64 magic; // NCZ_SECTION_MAGIC
    u64 total_sections;
};

struct BlockHeader {
    u64 magic; // NCZ_BLOCK_MAGIC
    u8 version;
    u8 type;
    u8 padding;
    u8 block_size_exponent;
    u32 total_blocks;
    u64 decompressed_size;

    Result IsValid() const {
        R_UNLESS(magic == NCZ_BLOCK_MAGIC, 9);
        R_UNLESS(version == NCZ_BLOCK_VERSION, Result_YatiInvalidNczBlockVersion);
        R_UNLESS(type == NCZ_BLOCK_TYPE, Result_YatiInvalidNczBlockType);
        R_UNLESS(total_blocks, Result_YatiInvalidNczBlockTotal);
        R_UNLESS(block_size_exponent >= 14 && block_size_exponent <= 32, Result_YatiInvalidNczBlockSizeExponent);
        R_SUCCEED();
    }
};

struct Block {
    u32 size;
};
using Blocks = std::vector<Block>;

struct BlockInfo {
    u64 offset; // compressed offset.
    u64 size; // compressed size.

    auto InRange(u64 off) const -> bool {
        return off < offset + size && off >= offset;
    }
};

struct Section {
    u64 offset;
    u64 size;
    u64 crypto_type;
    u64 padding;
    u8 key[0x10];
    u8 counter[0x10];

    auto InRange(u64 off) const -> bool {
        return off < offset + size && off >= offset;
    }
};
using Sections = std::vector<Section>;

// NXDock: the random-access NczBlockReader is left out; installs read NCZ in order.

} // namespace sphaira::ncz
