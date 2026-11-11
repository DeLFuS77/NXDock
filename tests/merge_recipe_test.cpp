// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The recipe of a merged game (25E, merge_recipe): where the pieces of the base an update read lie
// in the merged RomFS, written and read back, the places a newer update reads that it has not, and
// the base made again from the merged RomFS with the rest fetched. (Against a real game: merged with
// one update, then a newer one merged over the base made again from it.)
#include "merge_recipe.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using namespace nxdock;
using Bytes = std::vector<uint8_t>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main() {
    std::mt19937_64 rng(25);
    // the base section, and the patched section of an update over it: its hash levels (0x8000
    // bytes, from the update), then its data level made of runs of the base and of its own
    Bytes base(4 << 20);
    for (auto& b : base) b = uint8_t(rng());
    const uint64_t data_offset = 0x8000;
    std::vector<patchview::IndirectEntry> ie;
    Bytes patched(data_offset, 0xEE);
    ie.push_back({0, 0, 1});
    // runs of the base, some read twice, one overlapping another, and new bytes between them
    const uint64_t runs[][2] = {{0x10000, 0x30000}, {0x200000, 0x1234}, {0x10000, 0x30000}, {0x20000, 0x40000}, {0x300000, 0x8000}};
    for (const auto& r : runs) {
        ie.push_back({patched.size(), r[0], 0});
        patched.insert(patched.end(), base.begin() + r[0], base.begin() + r[0] + r[1]);
        ie.push_back({patched.size(), 0, 1});
        for (int i = 0; i < 777; i++) patched.push_back(uint8_t(rng()));
    }
    const uint64_t end = patched.size();
    // a run of the base in the hash levels: left out of the recipe
    ie.insert(ie.begin() + 1, {0x4000, 0x3F0000, 0});
    ie.insert(ie.begin() + 2, {0x5000, 0, 1});
    const Bytes romfs(patched.begin() + data_offset, patched.end()); // the merged game's RomFS

    // 1. the recipe: pieces sorted, those inside others left out, places of the RomFS right
    const auto r = recipe::FromIndirect(ie, end, data_offset, end - data_offset, base.size(), 196608);
    CHECK(r.version == 196608 && r.base_size == base.size());
    CHECK(r.pieces.size() == 4); // 0x10000 (read twice: once), 0x20000 (overlapping it), 0x200000, 0x300000
    bool right = true, sorted = true;
    for (size_t i = 0; i < r.pieces.size(); i++) {
        const auto& p = r.pieces[i];
        right &= !std::memcmp(romfs.data() + p.at, base.data() + p.phys, size_t(p.size));
        if (i) sorted &= r.pieces[i - 1].phys <= p.phys;
    }
    CHECK(right && sorted);
    // written and read back
    {
        const Bytes b = recipe::Write(r);
        recipe::Recipe back;
        CHECK(recipe::Read(b, back) && recipe::Write(back) == b && back.pieces.size() == r.pieces.size());
        Bytes bad = b;
        bad[0] ^= 1;
        CHECK(!recipe::Read(bad, back));
        bad = b;
        bad.pop_back();
        CHECK(!recipe::Read(bad, back));
    }

    // 2. a newer update: reads most of the same places, a bit more of one, and a new one
    std::vector<patchview::IndirectEntry> newer = {{0, 0, 1}, {0x8000, 0x10000, 0}, {0x8000 + 0x50000, 0x200000, 0}, {0x8000 + 0x52000, 0x400000, 0},
                                                  {0x8000 + 0x53000, 0, 1}};
    const uint64_t newer_end = 0x8000 + 0x60000;
    // 0x10000..0x60000: the recipe has 0x10000..0x60000 (0x10000+0x30000 and 0x20000+0x40000)
    // 0x200000..0x202000: it has 0x1234 of it; 0x400000..0x401000: none
    const auto missing = recipe::Missing(r, newer, newer_end);
    uint64_t miss = 0;
    for (const auto& m : missing) miss += m.second;
    CHECK(missing.size() == 2 && miss == (0x2000 - 0x1234) + 0x1000);
    CHECK(missing[0].first == 0x201234);

    // 3. the base made again: from the merged RomFS where the recipe has it, fetched elsewhere
    uint64_t fetched = 0;
    recipe::RebuiltBase rebuilt(
        r,
        [&](uint64_t at, uint8_t* b, size_t n) {
            if (at + n > romfs.size()) return false;
            std::memcpy(b, romfs.data() + at, n);
            return true;
        },
        [&](uint64_t o, uint8_t* b, size_t n) {
            fetched += n;
            std::memcpy(b, base.data() + o, n);
            return true;
        });
    Bytes got(0x2000);
    CHECK(rebuilt.Read(0x200000, got.data(), got.size()) && !std::memcmp(got.data(), base.data() + 0x200000, got.size()));
    CHECK(fetched == 0x2000 - 0x1234 && rebuilt.Fetched() == fetched);
    got.resize(0x50000);
    fetched = 0;
    CHECK(rebuilt.Read(0x10000, got.data(), got.size()) && !std::memcmp(got.data(), base.data() + 0x10000, got.size()) && fetched == 0);
    // without the library: what it has reads, what it has not fails
    recipe::RebuiltBase alone(r, [&](uint64_t at, uint8_t* b, size_t n) {
        std::memcpy(b, romfs.data() + at, n);
        return true;
    }, recipe::FetchFn());
    CHECK(alone.Read(0x300000, got.data(), 0x8000) && !alone.Read(0x400000, got.data(), 16) && !alone.Read(base.size() - 8, got.data(), 16));

    // 4. the spare: what the recipe has not, kept compressed; with it the whole base is there
    // (some of the base made compressible, and a stretch of zeros)
    for (size_t i = 0x380000; i < base.size(); i++) base[i] = (i >> 4) & 1 ? uint8_t('a' + (i & 7)) : 0;
    std::memset(base.data() + 0x100000, 0, 0x30000);
    const auto left = recipe::Leftover(r);
    uint64_t left_bytes = 0, covered = 0;
    for (const auto& l : left) left_bytes += l.second;
    for (const auto& p : r.pieces) covered += p.size;
    CHECK(left_bytes < base.size() && left.front().first == 0 && left.back().first + left.back().second == base.size());
    const recipe::ReadFn from_base = [&](uint64_t o, uint8_t* b, size_t n) {
        if (o + n > base.size()) return false;
        std::memcpy(b, base.data() + o, n);
        return true;
    };
    Bytes spare_file;
    uint64_t written = 0;
    CHECK(recipe::WriteSpare(left, from_base, [&](const uint8_t* d, size_t n) { spare_file.insert(spare_file.end(), d, d + n); return true; }, nullptr,
                             written));
    CHECK(written == spare_file.size() && written < left_bytes);
    const uint64_t estimate = recipe::EstimateSpare(left, from_base, 4);
    CHECK(estimate > written / 2 && estimate < written * 2);
    recipe::Spare spare;
    const recipe::ReadFn spare_read = [&](uint64_t o, uint8_t* b, size_t n) {
        if (o + n > spare_file.size()) return false;
        std::memcpy(b, spare_file.data() + o, n);
        return true;
    };
    CHECK(spare.Open(spare_read, spare_file.size()) && spare.Covered() == left_bytes);
    // the base made again from the merged game and the spare alone, no library: all of it
    recipe::RebuiltBase whole(r, [&](uint64_t at, uint8_t* b, size_t n) {
        std::memcpy(b, romfs.data() + at, n);
        return true;
    }, [&](uint64_t o, uint8_t* b, size_t n) { return spare.Read(o, b, n); });
    Bytes all(base.size());
    // (the recipe's pieces of the base as it was before the bytes above were changed)
    bool same = whole.Read(0, all.data(), all.size());
    for (const auto& p : r.pieces) std::memcpy(all.data() + p.phys, base.data() + p.phys, size_t(p.size)); // what the RomFS held then
    CHECK(same && all == base);
    // a damaged spare is refused
    {
        Bytes bad = spare_file;
        bad[bad.size() - 1] ^= 1;
        recipe::Spare s2;
        CHECK(!s2.Open([&](uint64_t o, uint8_t* b, size_t n) {
            if (o + n > bad.size()) return false;
            std::memcpy(b, bad.data() + o, n);
            return true;
        }, bad.size()));
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("merge_recipe: all passed (%zu pieces; a newer update misses %llu bytes; spare of %.1f KB for %.1f KB left)\n", r.pieces.size(),
                     (unsigned long long)miss, written / 1e3, left_bytes / 1e3);
    return failures ? 1 : 0;
}
