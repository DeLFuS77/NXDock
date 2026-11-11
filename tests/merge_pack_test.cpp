// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The data level of a merged game, packed by merge_pack (25C): runs the update reads more than
// once stored once, blocks the same as earlier ones stored once, Nintendo's own LZ4 of a block
// kept when it is the smaller, the same bytes on 1 and on 3 threads, and everything read back
// through the table as it was. Also the layout of the IVFC and the header of the merged NCA
// (merge_nca), against what Nintendo writes.
#include "merge_nca.hpp"
#include "merge_pack.hpp"
#include "lz4nintendo.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

extern "C" unsigned char* SHA256(const unsigned char* d, size_t n, unsigned char* md);

using namespace nxdock;
using Bytes = std::vector<uint8_t>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main() {
    const lz4pack::HashFn sha = [](const uint8_t* d, size_t n, uint8_t out[32]) { SHA256(d, n, out); };
    std::mt19937_64 rng(5);
    // a base game and what an update reads from it: runs of odd sizes, some read twice or three
    // times from the same place, and new ones of its own
    Bytes base(6u << 20);
    {
        const char* words[] = {"wheel ", "glider ", "item ", "cup ", "lap ", "star "};
        size_t i = 0;
        while (i < base.size()) {
            const char* w = words[rng() % 6];
            for (size_t k = 0; w[k] && i < base.size(); k++) base[i++] = uint8_t(w[k]);
            if (rng() % 9 == 0 && i < base.size()) base[i++] = uint8_t(rng());
        }
    }
    std::vector<mergepack::IndirectRun> ind;
    Bytes data; // the data level the game reads (the patched section from offset 0)
    std::vector<std::pair<uint64_t, uint64_t>> places;
    for (int k = 0; k < 40; k++) places.push_back({rng() % (base.size() - 300000), 1 + rng() % 250000});
    for (int k = 0; k < 160; k++) {
        if (rng() % 3 == 0) {
            // new bytes of the update
            const uint64_t n = 1 + rng() % 90000;
            ind.push_back({data.size(), 0, 1});
            for (uint64_t j = 0; j < n; j++) data.push_back(uint8_t(rng() % 7 ? 'a' + rng() % 3 : rng()));
        } else {
            const auto& pl = places[rng() % places.size()];
            ind.push_back({data.size(), pl.first, 0});
            data.insert(data.end(), base.begin() + pl.first, base.begin() + pl.first + pl.second);
        }
    }
    // a whole block of 64 KiB twice, both where a block of the data level starts (0x200 on, as
    // Nintendo cuts them): found again by its hash when the level is packed as one run
    {
        Bytes block(0x10000);
        for (auto& b : block) b = uint8_t('A' + rng() % 20);
        ind.push_back({data.size(), 0, 1});
        while ((data.size() - 0x200) % 0x10000) data.push_back('x');
        data.insert(data.end(), block.begin(), block.end());
        data.insert(data.end(), block.begin(), block.end());
    }
    const uint64_t size = data.size();
    const auto runs = mergepack::RunsFromIndirect(ind, size, 0, size);
    uint64_t covered = 0, again = 0;
    bool follow = true;
    for (const auto& r : runs) {
        follow &= r.virt == covered;
        covered += r.size;
        if (r.same_as >= 0) {
            again += r.size;
            follow &= !std::memcmp(data.data() + r.virt, data.data() + r.same_as, size_t(r.size));
        }
    }
    CHECK(follow && covered == size && again > 0);
    // a data level with hash levels before it: runs clipped to it
    {
        const auto clipped = mergepack::RunsFromIndirect(ind, size, 0x8000, size - 0x8000);
        uint64_t c = 0;
        for (const auto& r : clipped) c += r.size;
        CHECK(!clipped.empty() && clipped[0].virt == 0 && c == size - 0x8000);
    }

    const lz4pack::ReadFn read = [&](uint64_t o, uint8_t* b, size_t n) {
        if (o + n > data.size()) return false;
        std::memcpy(b, data.data() + o, n);
        return true;
    };
    auto pack = [&](const std::vector<mergepack::Run>& r, unsigned threads, const mergepack::OldBlockFn& old, Bytes& stored, lz4pack::Packed& p,
                    mergepack::Stats& st) {
        stored.clear();
        return mergepack::Pack(size, r, 9, 64, read,
                               [&](uint64_t o, const uint8_t* d, size_t n) {
                                   if (o != stored.size()) return false;
                                   stored.insert(stored.end(), d, d + n);
                                   return true;
                               },
                               nullptr, sha, old, p, st, threads);
    };
    auto back = [&](const lz4pack::Packed& p, const Bytes& stored) {
        Bytes out(size);
        const bool ok = lz4pack::Unpack(p.entries, size, [&](uint64_t o, uint8_t* b, size_t n) {
            if (o + n > stored.size()) return false;
            std::memcpy(b, stored.data() + o, n);
            return true;
        }, 0, out.data(), out.size());
        return ok && out == data;
    };

    // 1. stored once: smaller than packed plainly, and read back the same
    Bytes s_plain, s_once, s_once3;
    lz4pack::Packed p_plain, p_once, p_once3;
    mergepack::Stats st_plain, st_once, st_once3;
    CHECK(lz4pack::Pack(size, 9, 64, read, [&](uint64_t, const uint8_t* d, size_t n) { s_plain.insert(s_plain.end(), d, d + n); return true; }, nullptr,
                        p_plain));
    CHECK(pack(runs, 1, nullptr, s_once, p_once, st_once));
    CHECK(back(p_once, s_once));
    CHECK(st_once.runs_again == again);
    CHECK(p_once.table_offset < p_plain.table_offset);
    // the same bytes on 3 threads
    CHECK(pack(runs, 3, nullptr, s_once3, p_once3, st_once3));
    CHECK(s_once3 == s_once && p_once3.entries.size() == p_once.entries.size() && back(p_once3, s_once3));
    // entries of a run stored once point back: places before the end of what was stored then
    int back_refs = 0;
    for (size_t i = 1; i < p_once.entries.size(); i++) {
        if (p_once.entries[i].phys < p_once.entries[i - 1].phys) back_refs++;
    }
    CHECK(back_refs > 0);
    // LZ4 blocks start on 16 bytes, and none stores more than 64 KiB
    bool aligned = true;
    for (const auto& e : p_once.entries) aligned &= e.type != lz4pack::kLz4 || (e.phys % 16 == 0 && e.size <= lz4pack::kBlock);
    CHECK(aligned);

    // 2. one run: blocks the same as earlier ones still stored once
    Bytes s_one;
    lz4pack::Packed p_one;
    mergepack::Stats st_one;
    CHECK(pack({}, 2, nullptr, s_one, p_one, st_one) && back(p_one, s_one) && st_one.runs_again == 0 && st_one.blocks_again >= 0x10000);

    // 2b. parts of the same place of the base, not the same start or size: the blocks inside are
    // cut on the 64 KiB of the base, so they are the same and stored once
    {
        Bytes d2;
        std::vector<mergepack::IndirectRun> i2;
        const uint64_t spans[][2] = {{0x23456, 700000}, {0x23456 + 150000, 600000}, {0x23456 + 333333, 300000}};
        for (int k = 0; k < 3; k++) {
            i2.push_back({d2.size(), 0, 1});
            for (int j = 0; j < 777 + k; j++) d2.push_back(uint8_t(rng()));
            i2.push_back({d2.size(), spans[k][0], 0});
            d2.insert(d2.end(), base.begin() + spans[k][0], base.begin() + spans[k][0] + spans[k][1]);
        }
        const auto r2 = mergepack::RunsFromIndirect(i2, d2.size(), 0, d2.size());
        Bytes s2;
        lz4pack::Packed p2;
        mergepack::Stats st2;
        CHECK(mergepack::Pack(d2.size(), r2, 9, 64,
                              [&](uint64_t o, uint8_t* b, size_t n) {
                                  if (o + n > d2.size()) return false;
                                  std::memcpy(b, d2.data() + o, n);
                                  return true;
                              },
                              [&](uint64_t o, const uint8_t* d, size_t n) {
                                  if (o != s2.size()) return false;
                                  s2.insert(s2.end(), d, d + n);
                                  return true;
                              },
                              nullptr, sha, nullptr, p2, st2, 2));
        Bytes o2(d2.size());
        CHECK(lz4pack::Unpack(p2.entries, d2.size(), [&](uint64_t o, uint8_t* b, size_t n) {
            if (o + n > s2.size()) return false;
            std::memcpy(b, s2.data() + o, n);
            return true;
        }, 0, o2.data(), o2.size()) && o2 == d2);
        // the second and third read lie (nearly) inside the first: the 64 KiB of the base wholly
        // in both (7 and 3 of them) stored once; cut from the start of each run, none would be
        CHECK(st2.blocks_again == 10 * 0x10000);
    }

    // 3. Nintendo's LZ4 of a block, when smaller, is kept as it is
    {
        std::vector<uint64_t> asked;
        Bytes s_old;
        lz4pack::Packed p_old;
        mergepack::Stats st_old;
        const mergepack::OldBlockFn old = [&](uint64_t v, uint32_t n, Bytes& out) {
            asked.push_back(v);
            if (v % 3) return false;
            // an LZ4 of the block smaller than any level makes: a run of literals is not, so the
            // fast level 0 stands for "Nintendo's" and the level 16 for the one made here
            Bytes in(data.begin() + v, data.begin() + v + n), c(nx_lz4_bound(n)), work(NX_LZ4_WORK_SIZE);
            const int64_t k = nx_lz4_compress(in.data(), n, c.data(), c.size(), 16, work.data());
            if (k <= 0) return false;
            out.assign(c.begin(), c.begin() + k);
            return true;
        };
        CHECK(pack({}, 1, old, s_old, p_old, st_old) && back(p_old, s_old) && !asked.empty());
        CHECK(st_old.old_kept > 0);
    }

    // 4. the IVFC as Nintendo lays it out (the layout of a game Nintendo compressed: 510080332 bytes
    // decompressed, levels of 16 KiB, the data level at 1064960)
    {
        const auto lv = mergenca::IvfcLayout(510080332);
        CHECK(lv.size() == 6 && lv[0].offset == 0 && lv[0].size == 0x4000 && lv[3].offset == 0xC000 && lv[3].size == 0x4000);
        CHECK(lv[4].offset == 0x10000 && lv[4].size == 999424 && lv[5].offset == 1064960);
    }
    // 5. the header of the merged NCA: the RomFS of the update made a plain one, the sections after
    // it moved, the hash of its FS header in the NCA header
    {
        Bytes h(0xC00, 0);
        std::memcpy(h.data() + 0x200, "NCA3", 4);
        auto put32 = [&](size_t at, uint32_t v) { std::memcpy(h.data() + at, &v, 4); };
        // ExeFS at 0x4000..0x10000, the RomFS of the update at 0x10000..0x50000, a logo after it
        put32(0x240, 0x4000 / 0x200);
        put32(0x244, 0x10000 / 0x200);
        put32(0x250, 0x10000 / 0x200);
        put32(0x254, 0x50000 / 0x200);
        put32(0x260, 0x50000 / 0x200);
        put32(0x264, 0x54000 / 0x200);
        uint8_t* ufsh = h.data() + 0x600;
        ufsh[2] = 0;
        ufsh[3] = 3;
        ufsh[4] = 4;
        std::memset(ufsh + 0x100, 0x11, 0x40);
        ufsh[0x140] = 0x77;
        const auto lv = mergenca::IvfcLayout(1 << 20);
        uint8_t master[32] = {1, 2, 3};
        const auto fsh = mergenca::RomfsHeader(ufsh, lv, master, 0x1000, 0x8000, 9);
        CHECK(fsh[4] == 3 && fsh[0x140] == 0x77 && fsh[0x100] == 0 && fsh[0x13F] == 0 && fsh[0x188] == 'B' && fsh[0x8 + 0xC] == 7);
        mergenca::Plan plan;
        CHECK(mergenca::PlanNca(h.data(), 1, 0x9000, fsh, sha, plan));
        CHECK(plan.sections.size() == 3 && plan.sections[1].romfs && plan.sections[1].start == 0x10000 && plan.sections[1].end == 0x1C000);
        CHECK(plan.sections[2].start == 0x1C000 && plan.sections[2].end == 0x20000 && plan.size == 0x20000);
        uint8_t d[32];
        SHA256(fsh.data(), 0x200, d);
        uint64_t nsize;
        std::memcpy(&nsize, plan.header.data() + 0x208, 8);
        CHECK(!std::memcmp(plan.header.data() + 0x2A0, d, 32) && !std::memcmp(plan.header.data() + 0x600, fsh.data(), 0x200) && nsize == 0x20000);
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("merge_pack: all passed (%.1f MB: %.1f MB packed plainly, %.1f MB with what is held again stored once)\n", size / 1e6,
                     (p_plain.table_offset) / 1e6, (p_once.table_offset) / 1e6);
    return failures ? 1 : 0;
}
