// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// On the computer: an NCZSECT2 and an NCZSECT3 of NXPress come back as the very NCA the library
// app gives of them (tests/fixtures, made by switchfs.OpenInstallStream): four sections, two of
// them moving their AES-CTR counter, LZ4 blocks of every level kept decompressed, one across two
// sections, one of zero bytes and one left as LZ4. The stream is read in pieces of odd sizes, as
// the install reads it, and damaged tables and blocks are refused.
// Usage: nczsect_test <fixtures directory>
#include "yati/nx/nczsect.hpp"
#include "lz4nintendo.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include <zstd.h>

extern "C" void log_write(const char*, ...) {}
extern "C" void log_write_arg(const char*, va_list*) {}

using namespace sphaira;

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

std::vector<u8> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

u64 Fnv(const std::vector<u8>& b) {
    u64 h = 0xcbf29ce484222325ull;
    for (u8 c : b) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

u64 Get64(const u8* p) {
    u64 v;
    std::memcpy(&v, p, 8);
    return v;
}

// AES-128, encryption only, for the counter mode of the sections.
struct Aes128 {
    u8 sbox[256]{};
    u8 rk[176]{};

    static u8 Rotl(u8 x, int s) { return u8((x << s) | (x >> (8 - s))); }
    static u8 Xtime(u8 x) { return u8((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

    explicit Aes128(const u8 key[16]) {
        u8 p = 1, q = 1;
        do {
            p = u8(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            q ^= u8(q << 1);
            q ^= u8(q << 2);
            q ^= u8(q << 4);
            if (q & 0x80) q ^= 0x09;
            sbox[p] = u8(q ^ Rotl(q, 1) ^ Rotl(q, 2) ^ Rotl(q, 3) ^ Rotl(q, 4) ^ 0x63);
        } while (p != 1);
        sbox[0] = 0x63;
        std::memcpy(rk, key, 16);
        u8 rcon = 1;
        for (int i = 16; i < 176; i += 4) {
            u8 t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
            if (i % 16 == 0) {
                const u8 first = t[0];
                t[0] = u8(sbox[t[1]] ^ rcon);
                t[1] = sbox[t[2]];
                t[2] = sbox[t[3]];
                t[3] = sbox[first];
                rcon = Xtime(rcon);
            }
            for (int k = 0; k < 4; k++) rk[i + k] = u8(rk[i - 16 + k] ^ t[k]);
        }
    }

    void Encrypt(const u8 in[16], u8 out[16]) const {
        u8 s[16];
        for (int i = 0; i < 16; i++) s[i] = u8(in[i] ^ rk[i]);
        for (int round = 1; round <= 10; round++) {
            u8 t[16];
            for (int c = 0; c < 4; c++) {
                for (int r = 0; r < 4; r++) t[c * 4 + r] = sbox[s[((c + r) % 4) * 4 + r]];
            }
            if (round < 10) {
                for (int c = 0; c < 4; c++) {
                    u8* col = t + c * 4;
                    const u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                    const u8 all = u8(a0 ^ a1 ^ a2 ^ a3);
                    col[0] = u8(a0 ^ all ^ Xtime(u8(a0 ^ a1)));
                    col[1] = u8(a1 ^ all ^ Xtime(u8(a1 ^ a2)));
                    col[2] = u8(a2 ^ all ^ Xtime(u8(a2 ^ a3)));
                    col[3] = u8(a3 ^ all ^ Xtime(u8(a3 ^ a0)));
                }
            }
            for (int i = 0; i < 16; i++) s[i] = u8(t[i] ^ rk[round * 16 + i]);
        }
        std::memcpy(out, s, 16);
    }
};

// AES-CTR over data from counter, as libnx's context goes on from one call to the next.
void Ctr(const u8 key[16], u8 counter[16], u8* data, size_t size) {
    const Aes128 aes(key);
    u8 stream[16];
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            aes.Encrypt(counter, stream);
            for (int k = 15; k >= 0 && !++counter[k]; k--) {
            }
        }
        data[i] ^= stream[i % 16];
    }
}

// The NCA of an NCZ as the install makes it: the sections, the LZ4 table of an NCZSECT3, the zstd
// stream read in pieces of the sizes given, the blocks made again, the sections encrypted.
Result Decode(const std::vector<u8>& ncz, std::vector<u8>& nca, const std::vector<size_t>& pieces, bool encrypt,
              const std::function<void(std::vector<ncz::Lz4Block>&)>& tamper = {}) {
    nca.clear();
    if (ncz.size() < 0x4010) return 1;
    const u64 magic = Get64(ncz.data() + 0x4000);
    const u64 count = Get64(ncz.data() + 0x4008);
    R_UNLESS(magic == NCZ_SECTION_MAGIC || ncz::IsShifted(magic), Result_NczExperimental);
    R_UNLESS(count && count <= (ncz.size() - 0x4010) / sizeof(ncz::Section), Result_YatiInvalidNczSectionCount);
    std::vector<ncz::Section> sections(count);
    std::memcpy(sections.data(), ncz.data() + 0x4010, count * sizeof(ncz::Section));
    R_TRY(ncz::PrepareSections(magic, sections));

    const u64 stream_begin = 0x4010 + count * sizeof(ncz::Section);
    u64 stream_end = ncz.size();
    std::vector<ncz::Lz4Block> blocks;
    const bool rebuild = magic == NCZ_SECTION3_MAGIC;
    if (rebuild) {
        u64 start;
        R_TRY(ncz::ParseLz4Footer(ncz.data() + ncz.size() - ncz::LZ4_FOOTER_SIZE, ncz.size(), start));
        R_UNLESS(start >= stream_begin, Result_NczBadTable);
        R_TRY(ncz::ParseLz4Table(std::span<const u8>(ncz.data() + start, ncz.size() - ncz::LZ4_FOOTER_SIZE - start), blocks));
        if (tamper) tamper(blocks);
        stream_end = start;
    }
    ncz::Lz4Rebuilder rebuilder(blocks, NCZ_NORMAL_SIZE);
    std::vector<u8> work(NX_LZ4_WORK_SIZE);

    nca.assign(ncz.begin(), ncz.begin() + NCZ_NORMAL_SIZE);
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<u8> out(ZSTD_DStreamOutSize());
    std::vector<ncz::Lz4Rebuilder::Piece> made;
    std::vector<u8> block;
    Result rc = 0;
    size_t turn = 0;
    for (u64 at = stream_begin; at < stream_end && R_SUCCEEDED(rc);) {
        const size_t want = std::min<u64>(pieces[turn++ % pieces.size()], stream_end - at);
        ZSTD_inBuffer input = {ncz.data() + at, want, 0};
        while (input.pos < input.size && R_SUCCEEDED(rc)) {
            // the output in pieces of odd sizes too
            ZSTD_outBuffer output = {out.data(), std::min<size_t>(out.size(), 1 + (turn * 7919) % out.size()), 0};
            const size_t res = ZSTD_decompressStream(dctx, &output, &input);
            if (ZSTD_isError(res)) {
                rc = Result_YatiInvalidNczZstdError;
                break;
            }
            if (!rebuild) {
                nca.insert(nca.end(), out.data(), out.data() + output.pos);
                continue;
            }
            made.clear();
            rebuilder.Feed(out.data(), output.pos, made);
            for (auto& piece : made) {
                if (piece.block < 0) {
                    nca.insert(nca.end(), piece.data.begin(), piece.data.end());
                    continue;
                }
                rc = ncz::RebuildBlock(rebuilder.Block(piece.block), piece.data, block, work.data());
                if (R_FAILED(rc)) break;
                nca.insert(nca.end(), block.begin(), block.end());
            }
        }
        at += want;
    }
    ZSTD_freeDCtx(dctx);
    R_TRY(rc);
    R_UNLESS(rebuilder.Done(), Result_NczLz4Rebuild);

    if (encrypt) {
        for (const auto& section : sections) {
            if (section.crypto_type < 3 || section.offset + section.size > nca.size()) continue;
            u8 counter[16];
            ncz::CounterAt(section, section.offset, counter);
            Ctr(section.key, counter, nca.data() + section.offset, section.size);
        }
    }
    R_SUCCEED();
}

struct Expected {
    u64 size{};
    u64 fnv{};
};

Expected ExpectedOf(const std::string& dir, const std::string& name) {
    std::ifstream f(dir + "/nczsect_expected.txt");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream fields(line);
        std::string file, sha, fnv;
        Expected e;
        fields >> file >> e.size >> sha >> fnv;
        if (file == name) {
            e.fnv = std::stoull(fnv, nullptr, 16);
            return e;
        }
    }
    return {};
}

bool Same(const std::vector<u8>& nca, const Expected& e) {
    return e.size && nca.size() == e.size && Fnv(nca) == e.fnv;
}

std::vector<u8> PackTable(const std::vector<ncz::Lz4Block>& blocks, u64 magic = ncz::LZ4_TABLE_MAGIC) {
    std::vector<u8> entries;
    for (const auto& b : blocks) {
        for (int i = 0; i < 8; i++) entries.push_back(u8(b.offset >> (8 * i)));
        for (int i = 0; i < 4; i++) entries.push_back(u8(b.size >> (8 * i)));
        for (int i = 0; i < 4; i++) entries.push_back(u8(b.raw_size >> (8 * i)));
        entries.push_back(u8(b.level));
    }
    std::vector<u8> packed(ZSTD_compressBound(entries.size()));
    packed.resize(ZSTD_compress(packed.data(), packed.size(), entries.data(), entries.size(), 3));
    std::vector<u8> table(24);
    const u64 head[3] = {magic, u64(blocks.size()), u64(packed.size())};
    std::memcpy(table.data(), head, 24);
    table.insert(table.end(), packed.begin(), packed.end());
    return table;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s fixtures\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    {
        // FIPS-197, appendix C.1
        u8 key[16], in[16], out[16];
        for (int i = 0; i < 16; i++) {
            key[i] = u8(i);
            in[i] = u8(i * 0x11);
        }
        Aes128(key).Encrypt(in, out);
        const u8 want[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
        Check(!std::memcmp(out, want, 16), "the AES of the test is AES");
    }

    const auto v2 = ReadFile(dir + "/nczsect2.ncz");
    const auto v3 = ReadFile(dir + "/nczsect3.ncz");
    const auto want2 = ExpectedOf(dir, "nczsect2.ncz");
    const auto want3 = ExpectedOf(dir, "nczsect3.ncz");
    const auto plain = ExpectedOf(dir, "plain");
    Check(!v2.empty() && !v3.empty() && want2.size && want3.size && plain.size, "the fixtures are there");

    std::vector<u8> nca;
    Check(R_SUCCEEDED(Decode(v2, nca, {1 << 20}, true)) && Same(nca, want2), "NCZSECT2: the NCA of the library app, counters moved");
    Check(R_SUCCEEDED(Decode(v3, nca, {1 << 20}, false)) && Same(nca, plain), "NCZSECT3: every LZ4 block made again byte for byte");
    Check(R_SUCCEEDED(Decode(v3, nca, {1 << 20}, true)) && Same(nca, want3), "NCZSECT3: the NCA of the library app");
    Check(R_SUCCEEDED(Decode(v3, nca, {1, 7, 333, 4096, 65537, 3}, true)) && Same(nca, want3), "NCZSECT3: read in pieces of odd sizes");
    Check(R_SUCCEEDED(Decode(v2, nca, {13, 100000, 2}, true)) && Same(nca, want2), "NCZSECT2: read in pieces of odd sizes");

    // without the moved counters the NCA is another one
    {
        auto plain_counters = v2;
        u64 magic = NCZ_SECTION_MAGIC;
        std::memcpy(plain_counters.data() + 0x4000, &magic, 8);
        Check(R_SUCCEEDED(Decode(plain_counters, nca, {1 << 20}, true)) && !Same(nca, want2), "the counter field is what moves the counter");
    }

    // the blocks of the table, as the library app wrote them
    {
        u64 start = 0;
        std::vector<ncz::Lz4Block> blocks;
        const bool read = R_SUCCEEDED(ncz::ParseLz4Footer(v3.data() + v3.size() - 16, v3.size(), start)) &&
                          R_SUCCEEDED(ncz::ParseLz4Table(std::span<const u8>(v3.data() + start, v3.size() - 16 - start), blocks));
        bool levels = read && blocks.size() == 13;
        if (levels) {
            const int want[] = {0, NX_LZ4_LEVEL_FAST4, 5, 16, 1, 9, 2, 0, 5, NX_LZ4_LEVEL_FAST4, 12, 5, 5};
            for (size_t i = 0; i < blocks.size(); i++) levels = levels && blocks[i].level == want[i];
            levels = levels && blocks[12].raw_size == 0;
        }
        Check(levels, "the LZ4 table: 13 blocks of levels 0, 64 and 1 to 16, one of zero bytes");

        // a block told at another level is not made to its size
        Check(Decode(v3, nca, {1 << 20}, true, [](auto& b) { b[3].level = 5; }) == Result_NczLz4Rebuild, "a block told at another level is refused");
        Check(Decode(v3, nca, {1 << 20}, true, [](auto& b) { b[0].level = 17; }) == Result_NczLz4Rebuild, "a level that is not Nintendo's is refused");
        Check(Decode(v3, nca, {1 << 20}, true, [](auto& b) { b[5].raw_size += 1; }) != 0, "a block of the wrong size is refused");
        // a stream that ends before its last block
        Check(Decode(v3, nca, {1 << 20}, true, [](auto& b) { b.push_back({b.back().offset + 1000000, 10, 10, 0}); }) == Result_NczLz4Rebuild,
              "a stream that ends before its last block is refused");

        // tables built here
        std::vector<u8> t;
        Check(read && R_SUCCEEDED(ncz::ParseLz4Table(PackTable(blocks), blocks)) && blocks.size() == 13, "a table packed again is read");
        auto backwards = blocks;
        std::swap(backwards[1], backwards[2]);
        Check(ncz::ParseLz4Table(PackTable(backwards), blocks) == Result_NczBadTable, "blocks out of order are refused");
        auto empty = backwards;
        empty[0] = {0x4000, 0, 10, 0};
        Check(ncz::ParseLz4Table(PackTable({empty[0]}), blocks) == Result_NczBadTable, "a block of no size in the NCA is refused");
        Check(ncz::ParseLz4Table(PackTable({{0x3FFF, 10, 10, 0}}), blocks) == Result_NczBadTable, "a block in the NCA header is refused");
        Check(ncz::ParseLz4Table(PackTable({{0x4000, 10, (1u << 24) + 1, 0}}), blocks) == Result_NczBadTable, "a block too large is refused");
        Check(ncz::ParseLz4Table(PackTable({}, 0x1234), blocks) == Result_NczBadTable, "a table of another magic is refused");
        t = PackTable({{0x4000, 10, 10, 0}});
        t.pop_back();
        Check(ncz::ParseLz4Table(t, blocks) == Result_NczBadTable, "a table cut short is refused");
        t = PackTable({{0x4000, 10, 10, 0}});
        const u64 many = ncz::LZ4_MAX_BLOCKS + 1;
        std::memcpy(t.data() + 8, &many, 8);
        Check(ncz::ParseLz4Table(t, blocks) == Result_NczBadTable, "a table of too many blocks is refused");
        Check(R_SUCCEEDED(ncz::ParseLz4Table(PackTable({}), blocks)) && blocks.empty(), "a table of no blocks is read");

        u8 footer[16]{};
        Check(ncz::ParseLz4Footer(footer, 1 << 20, start) == Result_NczBadTable, "a footer of another magic is refused");
        u64 values[2] = {0x100, ncz::LZ4_FOOTER_MAGIC};
        std::memcpy(footer, values, 16);
        Check(ncz::ParseLz4Footer(footer, 1 << 20, start) == Result_NczBadTable, "a table that starts in the NCA header is refused");
        values[0] = (1 << 20) - 30;
        std::memcpy(footer, values, 16);
        Check(ncz::ParseLz4Footer(footer, 1 << 20, start) == Result_NczBadTable, "a table that ends past the footer is refused");
    }

    // the sections
    {
        ncz::Section s{};
        s.offset = 0x4000;
        s.size = 0x1000;
        s.crypto_type = 3;
        s.padding = u64(-0x40);
        std::vector<ncz::Section> v{s};
        Check(R_SUCCEEDED(ncz::PrepareSections(NCZ_SECTION2_MAGIC, v)) && v[0].padding == u64(-0x40), "NCZSECT2 keeps a counter moved back");
        u8 counter[16];
        std::memset(v[0].counter, 0xAB, 16);
        ncz::CounterAt(v[0], 0x4100, counter);
        const u8 want[16] = {0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0, 0, 0, 0, 0, 0, 0x04, 0x0C};
        Check(!std::memcmp(counter, want, 16), "the counter of a byte is its position moved, in blocks of 16");
        v = {s};
        Check(R_SUCCEEDED(ncz::PrepareSections(NCZ_SECTION_MAGIC, v)) && v[0].padding == 0, "NCZSECTN does not move the counter");
        v = {s};
        v[0].padding = 8;
        Check(ncz::PrepareSections(NCZ_SECTION3_MAGIC, v) == Result_NczBadTable, "a counter moved by less than a block is refused");
        v = {s};
        v[0].crypto_type = 1;
        Check(ncz::PrepareSections(NCZ_SECTION2_MAGIC, v) == Result_NczBadTable, "a moved counter without encryption is refused");
        v = {s, s};
        v[1].offset = 0x6000;
        Check(ncz::PrepareSections(NCZ_SECTION2_MAGIC, v) == Result_NczBadTable, "sections with a hole between them are refused");
        v = {s};
        v[0].offset = 0x5000;
        Check(R_SUCCEEDED(ncz::PrepareSections(NCZ_SECTION2_MAGIC, v)) && v.size() == 2 && v[0].offset == 0x4000 && v[0].size == 0x1000 &&
                  v[0].crypto_type == 1, "the bytes before a later first section are a section without encryption");
        v = {s};
        v[0].offset = 0x3000;
        v[0].size = 0x2000;
        Check(R_SUCCEEDED(ncz::PrepareSections(NCZ_SECTION2_MAGIC, v)) && v[0].offset == 0x4000 && v[0].size == 0x1000,
              "a first section in the header starts where the header ends");
        const u64 sect4 = 0x34544345535A434EULL; // NCZSECT4
        Check(ncz::IsUnknownSectionMagic(sect4) && !ncz::IsUnknownSectionMagic(NCZ_SECTION_MAGIC) && !ncz::IsUnknownSectionMagic(NCZ_SECTION3_MAGIC) &&
                  !ncz::IsUnknownSectionMagic(0x4B434F4C42345A43ULL), "a later section format is told apart");
    }

    std::printf("%s\n", g_failed ? "FAILED" : "all passed");
    return g_failed ? 1 : 0;
}
