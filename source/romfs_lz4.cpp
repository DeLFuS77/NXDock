// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "romfs_lz4.hpp"

#include "lz4nintendo.h"

#include <algorithm>
#include <cstring>

namespace nxdock::lz4pack {
namespace {

void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }
uint32_t Get32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint64_t Get64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
uint64_t Align16(uint64_t v) { return (v + 15) & ~uint64_t(15); }

} // namespace

bool Pack(uint64_t virt_size, int level, uint32_t min_gain, const ReadFn& read, const WriteFn& write, const StepFn& step, Packed& out) {
    out = Packed{};
    out.virt_size = virt_size;
    std::vector<uint8_t> plain(kBlock), packed(nx_lz4_bound(kBlock)), work(NX_LZ4_WORK_SIZE);
    static const uint8_t zeros[16] = {};
    uint64_t at = 0; // stored bytes written
    const auto pad_to = [&](uint64_t to) {
        while (at < to) {
            const size_t n = size_t(std::min<uint64_t>(sizeof(zeros), to - at));
            if (!write(at, zeros, n)) return false;
            at += n;
        }
        return true;
    };
    uint64_t virt = 0;
    while (virt < virt_size) {
        // the head stays as it is, then blocks of 64 KiB
        const uint32_t n = uint32_t(std::min<uint64_t>(virt == 0 ? kHead : kBlock, virt_size - virt));
        if (!read(virt, plain.data(), n)) return false;
        const int64_t c = virt == 0 ? -1 : nx_lz4_compress(plain.data(), n, packed.data(), packed.size(), level, work.data());
        const bool lz4 = c > 0 && uint64_t(c) + min_gain <= n;
        if (lz4) {
            if (!pad_to(Align16(at))) return false;
            out.entries.push_back({virt, at, kLz4, int8_t(level), uint32_t(c)});
            if (!write(at, packed.data(), size_t(c))) return false;
            at += uint64_t(c);
        } else {
            // as it is: joined to a stored entry right before it
            const bool join = !out.entries.empty() && out.entries.back().type == kNone && virt != kHead;
            if (!join) {
                if (!pad_to(Align16(at))) return false;
                out.entries.push_back({virt, at, kNone, 0, kNoneSize});
            }
            if (!write(at, plain.data(), n)) return false;
            at += n;
        }
        virt += n;
        if (step && !step(virt)) return false;
    }
    out.stored = at;
    out.table_offset = Align16(at);
    return pad_to(out.table_offset);
}

std::vector<uint8_t> BuildTable(const std::vector<Entry>& entries, uint64_t virt_end) {
    const size_t sets = (entries.size() + kPerSet - 1) / kPerSet;
    std::vector<uint8_t> t((1 + sets) * kNode, 0);
    // the first node: how many sets, where the tree ends, and where each set starts
    Put32(t.data() + 0, 0);
    Put32(t.data() + 4, uint32_t(sets));
    Put64(t.data() + 8, virt_end);
    for (size_t s = 0; s < sets; s++) Put64(t.data() + 0x10 + s * 8, entries[s * kPerSet].virt);
    for (size_t s = 0; s < sets; s++) {
        uint8_t* node = t.data() + (1 + s) * kNode;
        const size_t first = s * kPerSet, count = std::min<size_t>(kPerSet, entries.size() - first);
        const uint64_t end = first + count < entries.size() ? entries[first + count].virt : virt_end;
        Put32(node + 0, uint32_t(s));
        Put32(node + 4, uint32_t(count));
        Put64(node + 8, end);
        for (size_t k = 0; k < count; k++) {
            const Entry& e = entries[first + k];
            uint8_t* p = node + 0x10 + k * 0x18;
            Put64(p, e.virt);
            Put64(p + 8, e.phys);
            p[16] = e.type;
            p[17] = uint8_t(e.level);
            Put32(p + 20, e.size);
        }
    }
    return t;
}

bool ParseTable(const uint8_t* table, size_t size, uint32_t count, std::vector<Entry>& out, uint64_t& virt_end) {
    out.clear();
    if (count == 0 || size < kNode) return false;
    const size_t sets = (count + kPerSet - 1) / kPerSet;
    if (size / kNode < 1 + sets || Get32(table + 4) != sets) return false;
    virt_end = Get64(table + 8);
    for (size_t s = 0; s < sets; s++) {
        const uint8_t* node = table + (1 + s) * kNode;
        const uint32_t n = Get32(node + 4);
        if (Get32(node) != s || n > kPerSet) return false;
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t* p = node + 0x10 + k * 0x18;
            Entry e;
            e.virt = Get64(p);
            e.phys = Get64(p + 8);
            e.type = p[16];
            e.level = int8_t(p[17]);
            e.size = Get32(p + 20);
            if (k == 0 && Get64(table + 0x10 + s * 8) != e.virt) return false;
            out.push_back(e);
        }
    }
    return out.size() == count;
}

bool Unpack(const std::vector<Entry>& entries, uint64_t virt_end, const ReadFn& stored, uint64_t offset, uint8_t* out, size_t size) {
    std::vector<uint8_t> packed, plain;
    while (size > 0) {
        // the entry holding offset
        auto it = std::upper_bound(entries.begin(), entries.end(), offset, [](uint64_t o, const Entry& e) { return o < e.virt; });
        if (it == entries.begin()) return false;
        const Entry& e = *(it - 1);
        const uint64_t end = it == entries.end() ? virt_end : it->virt;
        if (offset >= end) return false;
        const size_t n = size_t(std::min<uint64_t>(size, end - offset));
        if (e.type == kNone) {
            if (!stored(e.phys + (offset - e.virt), out, n)) return false;
        } else if (e.type == kLz4) {
            packed.resize(e.size);
            plain.resize(size_t(end - e.virt));
            if (!stored(e.phys, packed.data(), packed.size()) || nx_lz4_decompress(packed.data(), packed.size(), plain.data(), plain.size()) != 0) return false;
            std::memcpy(out, plain.data() + (offset - e.virt), n);
        } else {
            return false;
        }
        out += n;
        offset += n;
        size -= n;
    }
    return true;
}

double EstimateGain(uint64_t virt_size, int level, uint32_t every, const ReadFn& read) {
    std::vector<uint8_t> plain(kBlock), packed(nx_lz4_bound(kBlock)), work(NX_LZ4_WORK_SIZE);
    uint64_t in = 0, kept = 0;
    const uint64_t blocks = (virt_size + kBlock - 1) / kBlock;
    for (uint64_t b = 0; b < blocks; b += std::max<uint32_t>(1, every)) {
        const uint64_t virt = b * kBlock;
        const uint32_t n = uint32_t(std::min<uint64_t>(kBlock, virt_size - virt));
        if (!read(virt, plain.data(), n)) return 0;
        const int64_t c = nx_lz4_compress(plain.data(), n, packed.data(), packed.size(), level, work.data());
        in += n;
        kept += c > 0 && uint64_t(c) < n ? uint64_t(Align16(uint64_t(c))) : n;
    }
    return in ? 1.0 - double(kept) / double(in) : 0;
}

} // namespace nxdock::lz4pack
