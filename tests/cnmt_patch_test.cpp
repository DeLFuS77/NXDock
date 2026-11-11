// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The CNMT of a game with its program changed (24A), on a section made up here: the record gets
// the new id, hash and size, the other records stay, and the hash table and master hash match
// the new bytes.
#include "cnmt_patch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// a stand-in for SHA-256: the layout is what is tested
static void Hash(const uint8_t* d, size_t n, uint8_t out[32]) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 1099511628211ull;
    for (int i = 0; i < 32; i++) out[i] = uint8_t(h >> ((i % 8) * 8)) ^ uint8_t(i);
}

int main() {
    // the .cnmt: header (0x20), an extended header of 0x10, three records
    std::vector<uint8_t> cnmt(0x20 + 0x10 + 3 * 0x38, 0);
    const uint16_t ext = 0x10, count = 3;
    std::memcpy(cnmt.data() + 0x0E, &ext, 2);
    std::memcpy(cnmt.data() + 0x10, &count, 2);
    for (int i = 0; i < 3; i++) {
        uint8_t* r = cnmt.data() + 0x30 + i * 0x38;
        std::memset(r, 0x10 + i, 32);       // hash
        std::memset(r + 0x20, 0xA0 + i, 16); // id
        r[0x30] = 0x34;                      // size
        r[0x36] = uint8_t(i);                // type
    }
    // the PFS0 with it
    const std::string name = "Application_0100000000010000.cnmt";
    std::vector<uint8_t> strings(name.begin(), name.end());
    strings.resize(0x30, 0);
    std::vector<uint8_t> pfs(0x10 + 0x18 + strings.size(), 0);
    std::memcpy(pfs.data(), "PFS0", 4);
    const uint32_t files = 1, ssize = uint32_t(strings.size());
    std::memcpy(pfs.data() + 4, &files, 4);
    std::memcpy(pfs.data() + 8, &ssize, 4);
    const uint64_t fsize = cnmt.size();
    std::memcpy(pfs.data() + 0x18, &fsize, 8);
    std::copy(strings.begin(), strings.end(), pfs.begin() + 0x28);
    pfs.insert(pfs.end(), cnmt.begin(), cnmt.end());
    // the section: hash table of 0x200, then the PFS0, blocks of 0x40 (several, the last short)
    CnmtLayers l;
    l.block_size = 0x40;
    l.hash_offset = 0;
    l.hash_size = 0x200;
    l.pfs0_offset = 0x200;
    l.pfs0_size = pfs.size();
    std::vector<uint8_t> section(0x200);
    section.insert(section.end(), pfs.begin(), pfs.end());
    section.resize(section.size() + 0x30, 0); // padding after

    uint8_t old_id[16], new_id[16], new_hash[32], master[32];
    std::memset(old_id, 0xA1, 16);
    std::memset(new_id, 0xEE, 16);
    std::memset(new_hash, 0x77, 32);
    const uint64_t new_size = 0x1'2345'6789ull;
    CHECK(PatchCnmt(section, l, old_id, new_id, new_hash, new_size, Hash, master));
    const uint8_t* rec = section.data() + 0x200 + 0x28 + 0x30 + 0x30 + 0x38;
    CHECK(rec[0] == 0x77 && rec[31] == 0x77 && rec[0x20] == 0xEE && rec[0x2F] == 0xEE);
    uint32_t low;
    std::memcpy(&low, rec + 0x30, 4);
    CHECK(low == 0x23456789 && rec[0x34] == 0x01 && rec[0x36] == 1);
    const uint8_t* other = rec - 0x38;
    CHECK(other[0] == 0x10 && other[0x20] == 0xA0); // the others stay
    // the hash table matches the new bytes, the last block short
    int wrong = 0;
    const uint64_t blocks = (l.pfs0_size + 0x3F) / 0x40;
    for (uint64_t b = 0; b < blocks; b++) {
        uint8_t h[32];
        const uint64_t n = std::min<uint64_t>(0x40, l.pfs0_size - b * 0x40);
        Hash(section.data() + 0x200 + b * 0x40, n, h);
        if (std::memcmp(h, section.data() + b * 32, 32)) wrong++;
    }
    CHECK(wrong == 0);
    uint8_t m[32];
    Hash(section.data(), 0x200, m);
    CHECK(std::memcmp(m, master, 32) == 0);
    // an id that is not there, and a section that is not a PFS0
    uint8_t missing[16];
    std::memset(missing, 0x55, 16);
    CHECK(!PatchCnmt(section, l, missing, new_id, new_hash, new_size, Hash, master));
    std::vector<uint8_t> junk(section.size(), 0);
    CHECK(!PatchCnmt(junk, l, old_id, new_id, new_hash, new_size, Hash, master));
    if (failures) std::printf("%d failed\n", failures);
    else std::printf("cnmt_patch: all passed\n");
    return failures ? 1 : 0;
}
