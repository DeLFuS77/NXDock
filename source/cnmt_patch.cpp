// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "cnmt_patch.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace nxdock {
namespace {

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
uint16_t Get16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

} // namespace

bool PatchCnmt(std::vector<uint8_t>& s, const CnmtLayers& l, const uint8_t old_id[16], const uint8_t new_id[16], const uint8_t new_hash[32],
               uint64_t new_size, const HashFn& hash, uint8_t master[32]) {
    if (l.block_size == 0 || l.pfs0_offset + l.pfs0_size > s.size() || l.hash_offset + l.hash_size > s.size()) return false;
    uint8_t* pfs = s.data() + l.pfs0_offset;
    if (l.pfs0_size < 0x10 || std::memcmp(pfs, "PFS0", 4) != 0) return false;
    const uint32_t files = Get32(pfs + 4), strings = Get32(pfs + 8);
    const uint64_t head = 0x10 + uint64_t(files) * 0x18 + strings;
    if (head > l.pfs0_size) return false;
    // the .cnmt file
    uint64_t at = 0, size = 0;
    for (uint32_t i = 0; i < files; i++) {
        const uint8_t* e = pfs + 0x10 + i * 0x18;
        const uint32_t name_at = Get32(e + 0x10);
        if (0x10 + uint64_t(files) * 0x18 + name_at >= head) return false;
        const std::string name(reinterpret_cast<const char*>(pfs + 0x10 + files * 0x18 + name_at));
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".cnmt") == 0) {
            at = head + Get64(e);
            size = Get64(e + 8);
        }
    }
    if (size < 0x20 || at + size > l.pfs0_size) return false;
    uint8_t* cnmt = pfs + at;
    const uint16_t ext = Get16(cnmt + 0x0E), count = Get16(cnmt + 0x10);
    if (0x20 + uint64_t(ext) + uint64_t(count) * 0x38 > size) return false;
    // the record of the content: hash (0x20), then id (0x10) and size (5 bytes)
    bool found = false;
    for (uint16_t i = 0; i < count; i++) {
        uint8_t* r = cnmt + 0x20 + ext + i * 0x38;
        if (std::memcmp(r + 0x20, old_id, 16) != 0) continue;
        std::memcpy(r, new_hash, 32);
        std::memcpy(r + 0x20, new_id, 16);
        const uint32_t low = uint32_t(new_size);
        std::memcpy(r + 0x30, &low, 4);
        r[0x34] = uint8_t(new_size >> 32);
        found = true;
    }
    if (!found) return false;
    // the hash table again, and its master hash
    const uint64_t blocks = (l.pfs0_size + l.block_size - 1) / l.block_size;
    if (blocks * 32 > l.hash_size) return false;
    for (uint64_t b = 0; b < blocks; b++) {
        const uint64_t from = l.pfs0_offset + b * l.block_size;
        const uint64_t n = std::min<uint64_t>(l.block_size, l.pfs0_offset + l.pfs0_size - from);
        hash(s.data() + from, size_t(n), s.data() + l.hash_offset + b * 32);
    }
    hash(s.data() + l.hash_offset, size_t(l.hash_size), master);
    return true;
}

} // namespace nxdock
