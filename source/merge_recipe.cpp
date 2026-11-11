// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "merge_recipe.hpp"

#include <algorithm>
#include <cstring>

namespace nxdock::recipe {

namespace {

constexpr char kMagic[8] = {'N', 'X', 'D', 'R', 'C', 'P', '0', '1'};

void Put(std::vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; i++) o.push_back(uint8_t(v >> (8 * i)));
}
uint64_t Get(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= uint64_t(p[i]) << (8 * i);
    return v;
}

// The first piece that ends after `phys` (pieces sorted by phys, possibly overlapping).
size_t FirstAfter(const std::vector<Piece>& pieces, uint64_t phys) {
    // pieces are sorted by start; one that starts before phys may still cover it
    size_t i = size_t(std::upper_bound(pieces.begin(), pieces.end(), phys, [](uint64_t v, const Piece& p) { return v < p.phys; }) - pieces.begin());
    while (i > 0 && pieces[i - 1].phys + pieces[i - 1].size > phys) i--;
    return i;
}

// The piece covering phys (the one reaching furthest), or none.
const Piece* Covering(const std::vector<Piece>& pieces, uint64_t phys) {
    const Piece* best = nullptr;
    for (size_t i = FirstAfter(pieces, phys); i < pieces.size() && pieces[i].phys <= phys; i++) {
        const Piece& p = pieces[i];
        if (p.phys + p.size > phys && (!best || p.phys + p.size > best->phys + best->size)) best = &p;
    }
    return best;
}

// The start of the next piece after phys (UINT64_MAX when none).
uint64_t NextStart(const std::vector<Piece>& pieces, uint64_t phys) {
    const auto it = std::upper_bound(pieces.begin(), pieces.end(), phys, [](uint64_t v, const Piece& p) { return v < p.phys; });
    return it == pieces.end() ? UINT64_MAX : it->phys;
}

} // namespace

Recipe FromIndirect(const std::vector<patchview::IndirectEntry>& entries, uint64_t end, uint64_t data_offset, uint64_t data_size,
                    uint64_t base_size, uint32_t version) {
    Recipe r;
    r.version = version;
    r.base_size = base_size;
    const uint64_t lo = data_offset, hi = data_offset + data_size;
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].storage != 0) continue;
        const uint64_t v = entries[i].virt, next = i + 1 < entries.size() ? entries[i + 1].virt : end;
        const uint64_t a = std::max(v, lo), b = std::min(next, hi);
        if (a >= b) continue;
        r.pieces.push_back({entries[i].phys + (a - v), b - a, a - lo});
    }
    std::sort(r.pieces.begin(), r.pieces.end(), [](const Piece& x, const Piece& y) { return x.phys < y.phys || (x.phys == y.phys && x.size > y.size); });
    // a piece wholly inside the one before it adds nothing
    std::vector<Piece> kept;
    uint64_t reach = 0;
    for (const auto& p : r.pieces) {
        if (!kept.empty() && p.phys + p.size <= reach) continue;
        kept.push_back(p);
        reach = std::max(reach, p.phys + p.size);
    }
    r.pieces = std::move(kept);
    return r;
}

std::vector<uint8_t> Write(const Recipe& r) {
    std::vector<uint8_t> o(kMagic, kMagic + 8);
    Put(o, r.version);
    Put(o, r.base_size);
    Put(o, r.pieces.size());
    for (const auto& p : r.pieces) {
        Put(o, p.phys);
        Put(o, p.size);
        Put(o, p.at);
    }
    return o;
}

bool Read(const std::vector<uint8_t>& b, Recipe& r) {
    if (b.size() < 32 || std::memcmp(b.data(), kMagic, 8) != 0) return false;
    r = Recipe{};
    r.version = uint32_t(Get(b.data() + 8));
    r.base_size = Get(b.data() + 16);
    const uint64_t n = Get(b.data() + 24);
    if (n > (b.size() - 32) / 24 || b.size() != 32 + n * 24) return false;
    r.pieces.resize(size_t(n));
    for (uint64_t i = 0; i < n; i++) {
        const uint8_t* p = b.data() + 32 + i * 24;
        r.pieces[size_t(i)] = {Get(p), Get(p + 8), Get(p + 16)};
        if (i && r.pieces[size_t(i)].phys < r.pieces[size_t(i - 1)].phys) return false;
    }
    return true;
}

std::vector<std::pair<uint64_t, uint64_t>> Missing(const Recipe& r, const std::vector<patchview::IndirectEntry>& entries, uint64_t end) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].storage != 0) continue;
        const uint64_t v = entries[i].virt, next = i + 1 < entries.size() ? entries[i + 1].virt : end;
        uint64_t a = entries[i].phys;
        const uint64_t b = a + (next - v);
        while (a < b) {
            if (const Piece* p = Covering(r.pieces, a)) {
                a = std::min(b, p->phys + p->size);
                continue;
            }
            const uint64_t to = std::min(b, NextStart(r.pieces, a));
            if (!out.empty() && out.back().first + out.back().second == a) out.back().second += to - a;
            else out.push_back({a, to - a});
            a = to;
        }
    }
    std::sort(out.begin(), out.end());
    // joined where they meet or overlap
    std::vector<std::pair<uint64_t, uint64_t>> joined;
    for (const auto& m : out) {
        if (!joined.empty() && m.first <= joined.back().first + joined.back().second) {
            const uint64_t e = std::max(joined.back().first + joined.back().second, m.first + m.second);
            joined.back().second = e - joined.back().first;
        } else {
            joined.push_back(m);
        }
    }
    return joined;
}

RebuiltBase::RebuiltBase(Recipe r, std::function<bool(uint64_t, uint8_t*, size_t)> merged, FetchFn fetch)
    : m_r(std::move(r)), m_merged(std::move(merged)), m_fetch(std::move(fetch)) {}

bool RebuiltBase::Read(uint64_t offset, uint8_t* buf, size_t size) {
    if (offset > m_r.base_size || size > m_r.base_size - offset) return false;
    while (size) {
        if (const Piece* p = Covering(m_r.pieces, offset)) {
            const size_t n = size_t(std::min<uint64_t>(size, p->phys + p->size - offset));
            if (!m_merged(p->at + (offset - p->phys), buf, n)) return false;
            offset += n;
            buf += n;
            size -= n;
            continue;
        }
        const size_t n = size_t(std::min<uint64_t>(size, NextStart(m_r.pieces, offset) - offset));
        if (!m_fetch || !m_fetch(offset, buf, n)) return false;
        m_fetched += n;
        offset += n;
        buf += n;
        size -= n;
    }
    return true;
}

} // namespace nxdock::recipe
