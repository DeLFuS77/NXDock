// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The compressed storage written as in the games Nintendo compresses (24A): a data level packed
// and read back byte for byte through its table, the table laid out as theirs (first node, sets
// of 682, ends), the head kept as it is, places aligned to 0x10, and the estimate from a sample.
#include "romfs_lz4.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace nxdock::lz4pack;

// SHA-256 of the libcrypto in the container (its headers are not there)
extern "C" unsigned char* SHA256(const unsigned char* d, size_t n, unsigned char* md);

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(int argc, char** argv) {
    // 90 MB and a bit: zeros (as the gap in one of the games), text, and noise that does not shrink
    const uint64_t size = 90ull * 1024 * 1024 + 12345;
    std::vector<uint8_t> plain(size, 0);
    std::mt19937_64 rng(7);
    for (uint64_t i = 0; i < size; i++) {
        const uint64_t mb = i >> 20;
        if (mb % 5 == 1) plain[i] = uint8_t("the quick brown fox jumps over the lazy dog "[i % 44]);
        else if (mb % 5 == 3) plain[i] = uint8_t(rng());
    }
    std::vector<uint8_t> stored;
    Packed p;
    uint64_t steps = 0;
    const bool ok = Pack(size, 0, 16,
        [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, plain.data() + o, n); return true; },
        [&](uint64_t o, const uint8_t* d, size_t n) {
            if (o != stored.size()) return false; // in order, in one go
            stored.insert(stored.end(), d, d + n);
            return true;
        },
        [&](uint64_t) { steps++; return true; }, p);
    CHECK(ok);
    CHECK(stored.size() == p.table_offset && p.table_offset % 16 == 0 && p.stored <= p.table_offset);
    CHECK(p.stored < size / 2); // zeros and text shrink a lot
    // as Nintendo's: the head first, as it is, of 0x200 bytes
    CHECK(p.entries.size() > kPerSet);
    CHECK(p.entries[0].virt == 0 && p.entries[0].phys == 0 && p.entries[0].type == kNone && p.entries[0].size == kNoneSize);
    CHECK(p.entries[1].virt == kHead);
    int lz4 = 0, none = 0, misaligned = 0, too_big = 0;
    for (size_t i = 0; i < p.entries.size(); i++) {
        const auto& e = p.entries[i];
        (e.type == kLz4 ? lz4 : none)++;
        if (e.phys % 16) misaligned++;
        const uint64_t end = i + 1 < p.entries.size() ? p.entries[i + 1].virt : size;
        if (e.type == kLz4 && end - e.virt > kBlock) too_big++;
    }
    CHECK(lz4 > 0 && none > 0 && misaligned == 0 && too_big == 0);
    // the noise is kept as it is, in few entries (joined)
    CHECK(none < 20);

    // the table, as the FS reads it
    const auto table = BuildTable(p.entries, size);
    const size_t sets = (p.entries.size() + kPerSet - 1) / kPerSet;
    CHECK(table.size() == (1 + sets) * kNode);
    uint32_t count;
    std::memcpy(&count, table.data() + 4, 4);
    CHECK(count == sets);
    uint32_t set1_count;
    std::memcpy(&set1_count, table.data() + kNode + 4, 4);
    CHECK(set1_count == kPerSet);
    uint64_t set1_end;
    std::memcpy(&set1_end, table.data() + kNode + 8, 8);
    CHECK(p.entries.size() > kPerSet && set1_end == p.entries[kPerSet].virt);
    std::vector<Entry> back;
    uint64_t end = 0;
    CHECK(ParseTable(table.data(), table.size(), uint32_t(p.entries.size()), back, end) && end == size && back.size() == p.entries.size());
    CHECK(!ParseTable(table.data(), table.size(), uint32_t(p.entries.size()) + 1, back, end));
    if (argc > 1) {
        // for a check with another reader: the table, then its count of entries in a file beside
        if (FILE* f = std::fopen(argv[1], "wb")) {
            std::fwrite(table.data(), 1, table.size(), f);
            std::fclose(f);
        }
        if (FILE* f = std::fopen((std::string(argv[1]) + ".count").c_str(), "w")) {
            std::fprintf(f, "%zu %llu\n", p.entries.size(), (unsigned long long)size);
            std::fclose(f);
        }
    }

    // read back, whole and in odd pieces across blocks
    const ReadFn from = [&](uint64_t o, uint8_t* b, size_t n) {
        if (o + n > stored.size()) return false;
        std::memcpy(b, stored.data() + o, n);
        return true;
    };
    std::vector<uint8_t> again(size);
    CHECK(Unpack(back, end, from, 0, again.data(), again.size()) && again == plain);
    int wrong = 0;
    for (uint64_t o = 0; o + 100000 < size; o += 977777) {
        std::vector<uint8_t> piece(70001);
        if (!Unpack(back, end, from, o, piece.data(), piece.size()) || std::memcmp(piece.data(), plain.data() + o, piece.size())) wrong++;
    }
    CHECK(wrong == 0);

    // the estimate from one block in 8 is near the truth
    const double gain = EstimateGain(size, 0, 8, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, plain.data() + o, n); return true; });
    const double truth = 1.0 - double(p.table_offset + table.size()) / double(size);
    CHECK(gain > truth - 0.08 && gain < truth + 0.08);
    // a stop half way
    Packed q;
    CHECK(!Pack(size, 0, 16, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, plain.data() + o, n); return true; },
                [&](uint64_t, const uint8_t*, size_t) { return true; }, [&](uint64_t done) { return done < size / 3; }, q));
    // 25A: compressed on 3 threads, the same bytes and the same table
    {
        std::vector<uint8_t> stored3;
        Packed p3;
        CHECK(Pack(size, 9, 16, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, plain.data() + o, n); return true; },
                   [&](uint64_t o, const uint8_t* d, size_t n) {
                       if (o != stored3.size()) return false;
                       stored3.insert(stored3.end(), d, d + n);
                       return true;
                   }, nullptr, p3, 3));
        std::vector<uint8_t> stored1;
        Packed p1;
        CHECK(Pack(size, 9, 16, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, plain.data() + o, n); return true; },
                   [&](uint64_t, const uint8_t* d, size_t n) { stored1.insert(stored1.end(), d, d + n); return true; }, nullptr, p1, 1));
        CHECK(stored3 == stored1 && p3.entries.size() == p1.entries.size() && p3.table_offset == p1.table_offset);
        // the HC level 9 stores less than the fast level 0 on text
        CHECK(p1.table_offset < p.table_offset);
        CHECK(ShareAtLevel(p1.entries, 9) == 1.0 && ShareAtLevel(p.entries, 0) == 1.0);

        // packed again at 9, what the fast level 0 packed: on words in random order (more like
        // game data than the text above, which every level packs alike) something is saved, and
        // the estimate from one block in 4 is near what packing really saves
        {
            const char* words[] = {"sword ", "shield ", "dungeon ", "lava ", "boss ", "coin ", "level ", "menu ", "save ", "music ", "texture ", "player "};
            std::vector<uint8_t> text;
            std::mt19937_64 r(3);
            while (text.size() < 8u * 1024 * 1024) {
                const char* w = words[r() % 12];
                text.insert(text.end(), w, w + std::strlen(w));
                if (r() % 7 == 0) text.push_back(uint8_t(r()));
            }
            const uint64_t tsize = text.size();
            std::vector<uint8_t> s0, s9;
            Packed q0, q9;
            CHECK(Pack(tsize, 0, 64, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, text.data() + o, n); return true; },
                       [&](uint64_t, const uint8_t* d, size_t n) { s0.insert(s0.end(), d, d + n); return true; }, nullptr, q0, 3));
            CHECK(Pack(tsize, 9, 64, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, text.data() + o, n); return true; },
                       [&](uint64_t, const uint8_t* d, size_t n) { s9.insert(s9.end(), d, d + n); return true; }, nullptr, q9, 3));
            const double est = EstimateRepack(q0.entries, tsize, 9, 4, [&](uint64_t o, uint8_t* b, size_t n) {
                if (o + n > s0.size()) return false;
                std::memcpy(b, s0.data() + o, n);
                return true;
            });
            const double real = 1.0 - double(q9.table_offset) / double(q0.table_offset);
            CHECK(real > 0.05 && est > real - 0.03 && est < real + 0.03);
        }
        // nothing to gain on what is level 9 already
        const ReadFn from1 = [&](uint64_t o, uint8_t* b, size_t n) {
            if (o + n > stored1.size()) return false;
            std::memcpy(b, stored1.data() + o, n);
            return true;
        };
        CHECK(EstimateRepack(p1.entries, size, 9, 4, from1) < 0.001);
        // a data level packed by Nintendo, read back through its table and packed again: the
        // same plain bytes come out of the new one
        std::vector<uint8_t> again9;
        Packed p9;
        CHECK(Pack(size, 9, 64, [&](uint64_t o, uint8_t* b, size_t n) { return Unpack(back, end, from, o, b, n); },
                   [&](uint64_t, const uint8_t* d, size_t n) { again9.insert(again9.end(), d, d + n); return true; }, nullptr, p9, 2));
        std::vector<uint8_t> plain9(size);
        CHECK(Unpack(p9.entries, size, [&](uint64_t o, uint8_t* b, size_t n) {
                  if (o + n > again9.size()) return false;
                  std::memcpy(b, again9.data() + o, n);
                  return true;
              }, 0, plain9.data(), plain9.size()) && plain9 == plain);
    }

    // a zeros entry reads as zeros
    {
        std::vector<Entry> z = {{0, 0, kNone, 0, kNoneSize}, {0x200, 0, kZeros, 0, 0}};
        std::vector<uint8_t> head(0x200, 0xAB);
        std::vector<uint8_t> got(0x10200, 0xFF);
        CHECK(Unpack(z, 0x10200, [&](uint64_t o, uint8_t* b, size_t n) {
                  if (o + n > head.size()) return false;
                  std::memcpy(b, head.data() + o, n);
                  return true;
              }, 0, got.data(), got.size()));
        CHECK(got[0] == 0xAB && got[0x1FF] == 0xAB && got[0x200] == 0 && got[0x101FF] == 0);
    }

    // 25A: the IVFC made again, checked level by level as the FS does: each block of a level that
    // holds hashes (or data), padded with zeros, hashes to its entry in the level above, the
    // entries for the zeros after them stay zero, and level 0 hashes to the master hash. (That it
    // gives Nintendo's levels byte for byte was checked against two games.)
    {
        const HashFn sha = [](const uint8_t* d, size_t n, uint8_t out[32]) { SHA256(d, n, out); };
        std::vector<uint8_t> level_data(stored.begin(), stored.end());
        level_data.insert(level_data.end(), table.begin(), table.end());
        BlockHasher h(0x4000, sha);
        // fed in odd pieces
        for (size_t o = 0; o < level_data.size();) {
            const size_t n = std::min<size_t>(level_data.size() - o, 1 + (o * 7919) % 100000);
            h.Feed(level_data.data() + o, n);
            o += n;
        }
        CHECK(h.Fed() == level_data.size());
        auto hashes = h.Finish();
        CHECK(hashes.size() == (level_data.size() + 0x3FFF) / 0x4000 * 32);
        // the layout of a game: 5 hash levels of 16 KiB blocks, then the data level; the level
        // above the data kept the size it had for the data decompressed
        std::vector<IvfcLevel> lv = {{0, 0, 14}, {0x4000, 0, 14}, {0x8000, 0, 14}, {0xC000, 0, 14}, {0x10000, 0x40000, 14}, {0x400000, level_data.size(), 14}};
        std::vector<std::vector<uint8_t>> levels;
        uint8_t master[32];
        CHECK(BuildIvfc(hashes, lv, sha, levels, master));
        CHECK(levels.size() == 5 && lv[4].size == 0x40000 && lv[3].size == 0x4000 && lv[0].size == 0x4000);
        std::vector<const std::vector<uint8_t>*> all;
        for (auto& l : levels) all.push_back(&l);
        all.push_back(&level_data);
        std::vector<size_t> used(all.size());
        used.back() = level_data.size();
        for (size_t i = all.size() - 1; i-- > 0;) used[i] = (used[i + 1] + 0x3FFF) / 0x4000 * 32;
        bool good = true;
        for (size_t i = 0; i < all.size(); i++) {
            const auto& cur = *all[i];
            if (i + 1 < all.size()) {
                good &= cur.size() == lv[i].size;
                for (size_t o = used[i]; o < cur.size(); o++) good &= cur[o] == 0;
            }
            for (size_t b = 0; b * 0x4000 < used[i]; b++) {
                std::vector<uint8_t> block(0x4000, 0);
                std::memcpy(block.data(), cur.data() + b * 0x4000, std::min<size_t>(0x4000, used[i] - b * 0x4000));
                uint8_t d[32];
                SHA256(block.data(), block.size(), d);
                if (i == 0 && b > 0) good = false;
                const uint8_t* want = i == 0 ? master : all[i - 1]->data() + b * 32;
                good &= !std::memcmp(d, want, 32);
            }
        }
        CHECK(good);
        // a level that would not fit before the next one is refused
        std::vector<IvfcLevel> tight = lv;
        tight[4].offset = 0x3FFF00;
        tight[4].size = 0;
        CHECK(!BuildIvfc(hashes, tight, sha, levels, master));
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("romfs_lz4: all passed (%.1f%% of %llu MB stored)\n", 100.0 * double(p.table_offset) / double(size), (unsigned long long)(size >> 20));
    return failures ? 1 : 0;
}
