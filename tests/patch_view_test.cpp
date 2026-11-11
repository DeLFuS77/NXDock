// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The RomFS of a game with its update, read through patch_view (25B), on a game made up here: a
// RomFS with its IVFC and compressed storage, cut into runs, some in the update and some in the
// base game, the base as it is and sparse, the update in subsections of several generations. A
// made-up cipher stands for AES-CTR: its stream depends on the key and the whole counter, so a
// wrong counter anywhere gives wrong bytes. A damaged byte of the base game is caught by the IVFC
// of the update. (Against real games, with AES: four games with
// their updates and one with three; every block checked.)
#include "patch_view.hpp"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

extern "C" unsigned char* SHA256(const unsigned char* d, size_t n, unsigned char* md);

using namespace nxdock;
using Bytes = std::vector<uint8_t>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

namespace {

uint64_t Mix(uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
uint64_t Be64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}
// the made-up cipher: a stream of 16 bytes per block from the key and the counter
void FakeCtr(const uint8_t key[16], const uint8_t ctr[16], uint8_t* data, size_t n) {
    uint64_t k0, k1, hi = Be64(ctr), lo = Be64(ctr + 8);
    std::memcpy(&k0, key, 8);
    std::memcpy(&k1, key + 8, 8);
    for (size_t i = 0; i < n; i += 16) {
        const uint64_t a = Mix(k0 ^ Mix(hi * 31 + 7) ^ Mix(k1 ^ lo)), b = Mix(a ^ k1);
        uint8_t s[16];
        std::memcpy(s, &a, 8);
        std::memcpy(s + 8, &b, 8);
        for (size_t j = 0; j < 16 && i + j < n; j++) data[i + j] ^= s[j];
        if (++lo == 0) hi++;
    }
}
void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }

void CounterAt(const uint8_t* fsh, uint64_t at, uint8_t ctr[16]) {
    for (int i = 0; i < 8; i++) ctr[i] = fsh[0x147 - i];
    for (int i = 0; i < 8; i++) ctr[8 + i] = uint8_t((at >> 4) >> (8 * (7 - i)));
}
void Generation(uint8_t ctr[16], uint32_t g) {
    for (int i = 0; i < 4; i++) ctr[4 + i] = uint8_t(g >> (8 * (3 - i)));
}
// encrypts bytes at their place in the NCA (a multiple of 16)
void Encrypt(Bytes& nca, uint64_t at, size_t n, const uint8_t* key, const uint8_t* fsh, int64_t generation = -1, uint64_t counter_at = UINT64_MAX) {
    uint8_t ctr[16];
    CounterAt(fsh, counter_at == UINT64_MAX ? at : counter_at, ctr);
    if (generation >= 0) Generation(ctr, uint32_t(generation));
    FakeCtr(key, ctr, nca.data() + at, n);
}

// a bucket tree of entries (each entry_size bytes, the first 8 its virtual offset)
Bytes Bucket(const std::vector<Bytes>& entries, size_t entry_size, uint64_t end) {
    const size_t per = (0x4000 - 0x10) / entry_size, sets = (entries.size() + per - 1) / per;
    Bytes t((1 + sets) * 0x4000, 0);
    Put32(t.data() + 4, uint32_t(sets));
    Put64(t.data() + 8, end);
    for (size_t s = 0; s < sets; s++) {
        std::memcpy(t.data() + 0x10 + s * 8, entries[s * per].data(), 8);
        uint8_t* node = t.data() + (1 + s) * 0x4000;
        const size_t first = s * per, count = std::min(per, entries.size() - first);
        Put32(node, uint32_t(s));
        Put32(node + 4, uint32_t(count));
        uint64_t set_end = end;
        if (first + count < entries.size()) std::memcpy(&set_end, entries[first + count].data(), 8);
        Put64(node + 8, set_end);
        for (size_t k = 0; k < count; k++) std::memcpy(node + 0x10 + k * entry_size, entries[first + k].data(), entry_size);
    }
    return t;
}

patchview::NcaSection Section(const Bytes& nca, const uint8_t* key, const Bytes& fsh, uint64_t start, uint64_t size) {
    patchview::NcaSection s;
    s.raw = [&nca](uint64_t o, uint8_t* b, size_t n) {
        if (o > nca.size() || n > nca.size() - o) return false;
        std::memcpy(b, nca.data() + o, n);
        return true;
    };
    s.ctr = FakeCtr;
    std::memcpy(s.key.data(), key, 16);
    std::memcpy(s.fsh.data(), fsh.data(), 0x200);
    s.start = start;
    s.size = size;
    return s;
}

} // namespace

int main() {
    std::mt19937_64 rng(11);
    const lz4pack::HashFn sha = [](const uint8_t* d, size_t n, uint8_t out[32]) { SHA256(d, n, out); };
    // 1. the RomFS the game reads: words, compressed, with its IVFC as Nintendo lays it out
    Bytes romfs;
    const char* words[] = {"kart ", "shell ", "track ", "drift ", "boost ", "coin "};
    while (romfs.size() < 3u * 1024 * 1024 + 777) {
        const char* w = words[rng() % 6];
        romfs.insert(romfs.end(), w, w + std::strlen(w));
        if (rng() % 5 == 0) romfs.push_back(uint8_t(rng()));
    }
    Bytes data_level;
    lz4pack::Packed packed;
    CHECK(lz4pack::Pack(romfs.size(), 0, 64, [&](uint64_t o, uint8_t* b, size_t n) { std::memcpy(b, romfs.data() + o, n); return true; },
                        [&](uint64_t, const uint8_t* d, size_t n) { data_level.insert(data_level.end(), d, d + n); return true; }, nullptr, packed));
    const Bytes table = lz4pack::BuildTable(packed.entries, romfs.size());
    data_level.insert(data_level.end(), table.begin(), table.end());
    lz4pack::BlockHasher hasher(0x4000, sha);
    hasher.Feed(data_level.data(), data_level.size());
    std::vector<lz4pack::IvfcLevel> lv = {{0, 0, 14}, {0x4000, 0, 14}, {0x8000, 0, 14}, {0xC000, 0, 14}, {0x10000, 0x10000, 14}, {0x20000, data_level.size(), 14}};
    std::vector<Bytes> levels;
    uint8_t master[32];
    CHECK(lz4pack::BuildIvfc(hasher.Finish(), lv, sha, levels, master));
    Bytes patched(0x20000 + ((data_level.size() + 15) & ~size_t(15)), 0);
    for (size_t i = 0; i < levels.size(); i++) std::memcpy(patched.data() + lv[i].offset, levels[i].data(), levels[i].size());
    std::memcpy(patched.data() + 0x20000, data_level.data(), data_level.size());

    // 2. cut into runs of odd sizes (multiples of 16), each from the base game or the update
    Bytes base_content(64 * 1024, 0), update_data;
    for (auto& b : base_content) b = uint8_t(rng());
    std::vector<patchview::IndirectEntry> runs;
    std::vector<std::pair<uint64_t, uint64_t>> base_used; // places of the base the update reads
    for (uint64_t at = 0; at < patched.size();) {
        const uint64_t n = std::min<uint64_t>(patched.size() - at, 16 * (1 + rng() % 9000));
        const bool from_base = rng() % 2;
        if (from_base) {
            const uint64_t phys = base_content.size();
            base_content.insert(base_content.end(), patched.begin() + at, patched.begin() + at + n);
            // filler the update does not read, between the pieces it reads
            for (int k = 0; k < 4096; k++) base_content.push_back(uint8_t(rng()));
            runs.push_back({at, phys, 0});
            base_used.push_back({phys, phys + n});
        } else {
            runs.push_back({at, update_data.size(), 1});
            update_data.insert(update_data.end(), patched.begin() + at, patched.begin() + at + n);
        }
        at += n;
    }
    base_content.resize((base_content.size() + 0x3FFF) & ~size_t(0x3FFF));
    update_data.resize((update_data.size() + 0x3FFF) & ~size_t(0x3FFF));

    const uint8_t base_key[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint8_t upd_key[16] = {9, 9, 9, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    const uint64_t start = 0xC00 + 0x3400; // where the sections start in their NCAs

    // 3. the base game as it is: AES-CTR at its place
    Bytes base_fsh(0x200, 0);
    base_fsh[2] = 0;
    base_fsh[3] = 3;
    base_fsh[4] = 3;
    Put64(base_fsh.data() + 0x140, 0x0000000100000000ull | 0x1234);
    Bytes base_nca(start + base_content.size(), 0);
    std::memcpy(base_nca.data() + start, base_content.data(), base_content.size());
    Encrypt(base_nca, start, base_content.size(), base_key, base_fsh.data());

    // 4. the same base game sparse: only the pieces the update reads, one after the other, each
    // encrypted at its place in the whole section, then the table with the sparse generation
    Bytes sparse_fsh = base_fsh;
    const uint64_t physical = start + 0x4000; // where the stored pieces start in the NCA
    Bytes stored;
    std::vector<Bytes> sparse_entries;
    uint64_t virt = 0;
    std::vector<std::pair<uint64_t, uint64_t>> pieces; // stored at, virtual
    for (auto [a, b] : base_used) {
        if (a > virt) {
            Bytes e(0x14, 0);
            Put64(e.data(), virt);
            Put32(e.data() + 16, 1); // zeros
            sparse_entries.push_back(e);
        }
        Bytes e(0x14, 0);
        Put64(e.data(), a);
        Put64(e.data() + 8, stored.size());
        sparse_entries.push_back(e);
        pieces.push_back({stored.size(), a});
        stored.insert(stored.end(), base_content.begin() + a, base_content.begin() + b);
        virt = b;
    }
    if (virt < base_content.size()) {
        Bytes e(0x14, 0);
        Put64(e.data(), virt);
        Put32(e.data() + 16, 1);
        sparse_entries.push_back(e);
    }
    stored.resize((stored.size() + 15) & ~size_t(15));
    const Bytes sparse_table = Bucket(sparse_entries, 0x14, base_content.size());
    Bytes sparse_nca(physical + stored.size() + sparse_table.size(), 0);
    std::memcpy(sparse_nca.data() + physical, stored.data(), stored.size());
    for (size_t i = 0; i < pieces.size(); i++) {
        const uint64_t at = physical + pieces[i].first, v = start + pieces[i].second;
        const uint64_t n = (i + 1 < pieces.size() ? pieces[i + 1].first : stored.size()) - pieces[i].first;
        Encrypt(sparse_nca, at, n, base_key, sparse_fsh.data(), -1, v);
    }
    const uint64_t table_at = physical + stored.size();
    std::memcpy(sparse_nca.data() + table_at, sparse_table.data(), sparse_table.size());
    Put64(sparse_fsh.data() + 0x148, stored.size());
    Put64(sparse_fsh.data() + 0x150, sparse_table.size());
    Put32(sparse_fsh.data() + 0x158, 0x52544B42);
    Put32(sparse_fsh.data() + 0x160, uint32_t(sparse_entries.size()));
    Put64(sparse_fsh.data() + 0x168, physical);
    sparse_fsh[0x170] = 3; // generation
    Encrypt(sparse_nca, table_at, sparse_table.size(), base_key, sparse_fsh.data(), 3 << 16);

    // 5. the update: its data in three subsections of other generations, then the indirect
    // table and the subsection table with the counter of the section
    Bytes upd_fsh(0x200, 0);
    upd_fsh[2] = 0;
    upd_fsh[3] = 3;
    upd_fsh[4] = 4;
    Put64(upd_fsh.data() + 0x140, 0x0000000700000000ull | 0x77);
    std::vector<Bytes> ind;
    for (auto& r : runs) {
        Bytes e(0x14, 0);
        Put64(e.data(), r.virt);
        Put64(e.data() + 8, r.phys);
        Put32(e.data() + 16, uint32_t(r.storage));
        ind.push_back(e);
    }
    const Bytes ind_table = Bucket(ind, 0x14, patched.size());
    const uint64_t third = (update_data.size() / 3) & ~uint64_t(15);
    const uint64_t subs[3] = {0, third, 2 * third};
    const uint32_t gens[3] = {11, 22, 33};
    std::vector<Bytes> ex;
    for (int i = 0; i < 3; i++) {
        Bytes e(0x10, 0);
        Put64(e.data(), subs[i]);
        Put32(e.data() + 12, gens[i]);
        ex.push_back(e);
    }
    const Bytes ex_table = Bucket(ex, 0x10, update_data.size());
    const uint64_t ind_at = update_data.size(), ex_at = ind_at + ind_table.size();
    const uint64_t upd_size = ex_at + ex_table.size();
    Bytes upd_nca(start + upd_size, 0);
    std::memcpy(upd_nca.data() + start, update_data.data(), update_data.size());
    std::memcpy(upd_nca.data() + start + ind_at, ind_table.data(), ind_table.size());
    std::memcpy(upd_nca.data() + start + ex_at, ex_table.data(), ex_table.size());
    for (int i = 0; i < 3; i++) {
        const uint64_t a = subs[i], b = i < 2 ? subs[i + 1] : update_data.size();
        Encrypt(upd_nca, start + a, b - a, upd_key, upd_fsh.data(), gens[i]);
    }
    Encrypt(upd_nca, start + ind_at, upd_size - ind_at, upd_key, upd_fsh.data());
    Put64(upd_fsh.data() + 0x100, ind_at);
    Put64(upd_fsh.data() + 0x108, ind_table.size());
    Put32(upd_fsh.data() + 0x110, 0x52544B42);
    Put32(upd_fsh.data() + 0x118, uint32_t(ind.size()));
    Put64(upd_fsh.data() + 0x120, ex_at);
    Put64(upd_fsh.data() + 0x128, ex_table.size());
    Put32(upd_fsh.data() + 0x130, 0x52544B42);
    Put32(upd_fsh.data() + 0x138, 3);
    // its IVFC and compressed storage
    uint8_t* h = upd_fsh.data() + 8;
    Put32(h, 0x43465649);
    Put32(h + 4, 0x20000);
    Put32(h + 8, 0x20);
    Put32(h + 0xC, 7);
    for (size_t i = 0; i < lv.size(); i++) {
        Put64(h + 0x10 + i * 0x18, lv[i].offset);
        Put64(h + 0x10 + i * 0x18 + 8, lv[i].size);
        Put32(h + 0x10 + i * 0x18 + 0x10, lv[i].log2);
    }
    std::memcpy(h + 0xC0, master, 32);
    Put64(upd_fsh.data() + 0x178, packed.table_offset);
    Put64(upd_fsh.data() + 0x180, table.size());
    Put32(upd_fsh.data() + 0x188, 0x52544B42);
    Put32(upd_fsh.data() + 0x190, uint32_t(packed.entries.size()));

    CHECK(!patchview::IsSparse(base_fsh.data()) && patchview::IsSparse(sparse_fsh.data()) && patchview::IsPatch(upd_fsh.data()));

    // 6. read back, over each base game: whole and in odd pieces, the bytes the game reads
    for (int sparse = 0; sparse < 2; sparse++) {
        std::string error;
        const Bytes& bn = sparse ? sparse_nca : base_nca;
        auto base = patchview::OpenBase(Section(bn, base_key, sparse ? sparse_fsh : base_fsh, start, base_content.size()), error);
        CHECK(base != nullptr);
        if (!base) continue;
        auto pv = patchview::OpenPatched(Section(upd_nca, upd_key, upd_fsh, start, upd_size), base.get(), error);
        CHECK(pv != nullptr);
        if (!pv) {
            std::printf("  %s\n", error.c_str());
            continue;
        }
        std::vector<patchview::IndirectEntry> entries;
        uint64_t end = 0;
        CHECK(patchview::IndirectEntries(*pv, entries, end) && entries.size() == runs.size() && end == patched.size());
        patchview::Romfs r;
        CHECK(r.Open(pv.get(), upd_fsh.data(), sha, error) && r.Compressed() && r.Size() == romfs.size());
        Bytes all(romfs.size());
        CHECK(r.Read(0, all.data(), all.size()) && all == romfs && r.Damaged() == 0);
        int wrong = 0;
        for (uint64_t o = 0; o + 70001 < romfs.size(); o += 333331) {
            Bytes piece(70001);
            if (!r.Read(o, piece.data(), piece.size()) || std::memcmp(piece.data(), romfs.data() + o, piece.size())) wrong++;
        }
        CHECK(wrong == 0);
    }

    // 7. a byte of the base game the update reads, damaged: caught by the IVFC of the update
    {
        Bytes bad = base_nca;
        bad[start + base_used[base_used.size() / 2].first + 5] ^= 0x40;
        std::string error;
        auto base = patchview::OpenBase(Section(bad, base_key, base_fsh, start, base_content.size()), error);
        auto pv = patchview::OpenPatched(Section(upd_nca, upd_key, upd_fsh, start, upd_size), base.get(), error);
        patchview::Romfs r;
        bool read = r.Open(pv.get(), upd_fsh.data(), sha, error);
        if (read) {
            Bytes all(romfs.size());
            read = r.Read(0, all.data(), all.size());
        }
        CHECK(!read && r.Damaged() > 0);
    }
    // 8. an update over no base game is refused, as is a base section that is not one
    {
        std::string error;
        CHECK(patchview::OpenPatched(Section(upd_nca, upd_key, upd_fsh, start, upd_size), nullptr, error) == nullptr && error == "no base game");
        CHECK(patchview::OpenPatched(Section(base_nca, base_key, base_fsh, start, base_content.size()), nullptr, error) == nullptr);
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("patch_view: all passed (%zu runs, %.1f MB)\n", runs.size(), romfs.size() / 1e6);
    return failures ? 1 : 0;
}
