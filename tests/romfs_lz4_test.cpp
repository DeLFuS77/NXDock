// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The compressed storage written as in the games Nintendo compresses (24A): a data level packed
// and read back byte for byte through its table, the table laid out as theirs (first node, sets
// of 682, ends), the head kept as it is, places aligned to 0x10, and the estimate from a sample.
#include "romfs_lz4.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nxdock::lz4pack;

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
    if (failures) std::printf("%d failed\n", failures);
    else std::printf("romfs_lz4: all passed (%.1f%% of %llu MB stored)\n", 100.0 * double(p.table_offset) / double(size), (unsigned long long)(size >> 20));
    return failures ? 1 : 0;
}
