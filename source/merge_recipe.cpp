// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "merge_recipe.hpp"

#include "lz4nintendo.h"

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

std::vector<std::pair<uint64_t, uint64_t>> Leftover(const Recipe& r) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    uint64_t at = 0;
    for (const auto& p : r.pieces) {
        if (p.phys > at) out.push_back({at, std::min(p.phys, r.base_size) - at});
        at = std::max(at, p.phys + p.size);
        if (at >= r.base_size) break;
    }
    if (at < r.base_size) out.push_back({at, r.base_size - at});
    return out;
}

namespace {

constexpr char kSpareMagic[8] = {'N', 'X', 'D', 'S', 'P', 'R', '0', '1'};
constexpr uint32_t kSpareBlock = 0x10000;

bool AllZero(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (d[i]) return false;
    }
    return true;
}

} // namespace

bool WriteSpare(const std::vector<std::pair<uint64_t, uint64_t>>& places, const ReadFn& base, const OutFn& out, const StepFn& step,
                uint64_t& written, int level) {
    written = 0;
    std::vector<uint8_t> index;
    std::vector<uint8_t> plain(kSpareBlock), packed(nx_lz4_bound(kSpareBlock)), work(NX_LZ4_WORK_SIZE);
    std::vector<uint8_t> head(kSpareMagic, kSpareMagic + 8);
    head.resize(16, 0);
    if (!out(head.data(), head.size())) return false;
    written = head.size();
    uint64_t count = 0, done = 0;
    for (const auto& [from, size] : places) {
        for (uint64_t o = 0; o < size;) {
            const uint32_t n = uint32_t(std::min<uint64_t>(kSpareBlock, size - o));
            if (!base(from + o, plain.data(), n)) return false;
            uint8_t type = 0;
            uint32_t stored = n;
            const uint8_t* data = plain.data();
            if (AllZero(plain.data(), n)) {
                stored = 0;
            } else {
                const int64_t k = nx_lz4_compress(plain.data(), n, packed.data(), packed.size(), level, work.data());
                if (k > 0 && uint64_t(k) + 64 < n) {
                    type = 3;
                    stored = uint32_t(k);
                    data = packed.data();
                }
            }
            if (stored && !out(data, stored)) return false;
            Put(index, from + o);
            Put(index, written);
            Put(index, uint64_t(n) | uint64_t(stored) << 32);
            index.push_back(type);
            written += stored;
            o += n;
            done += n;
            count++;
            if (step && !step(done)) return false;
        }
    }
    const uint64_t index_at = written;
    std::vector<uint8_t> tail;
    Put(tail, count);
    tail.insert(tail.end(), index.begin(), index.end());
    Put(tail, index_at);
    tail.insert(tail.end(), kSpareMagic, kSpareMagic + 8);
    if (!out(tail.data(), tail.size())) return false;
    written += tail.size();
    return true;
}

uint64_t EstimateSpare(const std::vector<std::pair<uint64_t, uint64_t>>& places, const ReadFn& base, uint32_t every, int level) {
    std::vector<uint8_t> plain(kSpareBlock), packed(nx_lz4_bound(kSpareBlock)), work(NX_LZ4_WORK_SIZE);
    uint64_t total = 0, sampled = 0, stored = 0, k = 0;
    for (const auto& [from, size] : places) {
        for (uint64_t o = 0; o < size; o += kSpareBlock, k++) {
            const uint32_t n = uint32_t(std::min<uint64_t>(kSpareBlock, size - o));
            total += n;
            if (k % std::max<uint32_t>(every, 1)) continue;
            if (!base(from + o, plain.data(), n)) return UINT64_MAX;
            sampled += n;
            if (AllZero(plain.data(), n)) continue;
            const int64_t c = nx_lz4_compress(plain.data(), n, packed.data(), packed.size(), level, work.data());
            stored += c > 0 && uint64_t(c) + 64 < n ? uint64_t(c) : n;
        }
    }
    return sampled ? uint64_t(double(total) * double(stored) / double(sampled)) : 0;
}

bool Spare::Open(ReadFn file, uint64_t size) {
    m_file = std::move(file);
    m_blocks.clear();
    uint8_t foot[16];
    if (size < 16 + 24 || !m_file(size - 16, foot, 16) || std::memcmp(foot + 8, kSpareMagic, 8) != 0) return false;
    const uint64_t index_at = Get(foot);
    uint8_t c[8];
    if (index_at + 8 > size - 16 || !m_file(index_at, c, 8)) return false;
    const uint64_t count = Get(c);
    if (count > (size - 16 - index_at - 8) / 25 || index_at + 8 + count * 25 != size - 16) return false;
    std::vector<uint8_t> index(size_t(count * 25));
    if (!m_file(index_at + 8, index.data(), index.size())) return false;
    for (uint64_t i = 0; i < count; i++) {
        const uint8_t* p = index.data() + i * 25;
        Block b;
        b.phys = Get(p);
        b.at = Get(p + 8);
        const uint64_t sizes = Get(p + 16);
        b.size = uint32_t(sizes);
        b.stored = uint32_t(sizes >> 32);
        b.type = p[24];
        if (b.size == 0 || b.size > kSpareBlock || (b.type != 0 && b.type != 3) || b.at + b.stored > index_at ||
            (i && b.phys < m_blocks.back().phys + m_blocks.back().size)) {
            return false;
        }
        m_blocks.push_back(b);
    }
    return true;
}

uint64_t Spare::Covered() const {
    uint64_t n = 0;
    for (const auto& b : m_blocks) n += b.size;
    return n;
}

bool Spare::Read(uint64_t phys, uint8_t* buf, size_t size) {
    while (size) {
        auto it = std::upper_bound(m_blocks.begin(), m_blocks.end(), phys, [](uint64_t v, const Block& b) { return v < b.phys; });
        if (it == m_blocks.begin()) return false;
        const Block& b = *--it;
        if (phys >= b.phys + b.size) return false;
        const size_t off = size_t(phys - b.phys), n = std::min<size_t>(size, b.size - off);
        if (b.stored == 0) {
            std::memset(buf, 0, n);
        } else if (b.type == 0) {
            if (!m_file(b.at + off, buf, n)) return false;
        } else {
            m_tmp.resize(b.stored + size_t(b.size));
            if (!m_file(b.at, m_tmp.data(), b.stored) || nx_lz4_decompress(m_tmp.data(), b.stored, m_tmp.data() + b.stored, b.size) != 0) return false;
            std::memcpy(buf, m_tmp.data() + b.stored + off, n);
        }
        phys += n;
        buf += n;
        size -= n;
    }
    return true;
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
