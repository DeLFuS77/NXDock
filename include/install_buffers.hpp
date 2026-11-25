// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The buffers an NCA goes through while it installs: read, decompressed, written. Each of the
// two rings between the threads holds a few buffers; as an application each one is taken up
// front at the size of a decompressed piece (4 MiB), and opened from the Album, where the whole
// heap is about 260 MiB, the ring holds two and each grows only as far as it is filled (#4).
//
// Plain C++, tested on the computer (tests/install_buffers_test.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace nxdock {

struct RingSlot {
    std::vector<std::uint8_t> buf;
    std::int64_t off{};
};

// A ring of buffers passed by swapping, never copied. `Size` is a power of two; Limit() keeps
// fewer of them in use.
template <std::size_t Size>
class BufferRing {
    static_assert(Size && (Size & (Size - 1)) == 0, "a power of two");

public:
    // At most `n` buffers in the ring (a power of two from 1 to Size; others are rounded down),
    // each `reserve` bytes taken now (0: as it fills). Only those slots are ever used, so no more
    // buffers than that stay in it. Called before the threads start, on an empty ring.
    void Limit(unsigned n, std::size_t reserve) {
        unsigned limit = 1;
        while (limit * 2 <= n && limit * 2 <= unsigned(Size)) limit *= 2;
        m_limit = limit;
        m_r = m_w = 0;
        for (unsigned i = 0; i < Size; i++) {
            std::vector<std::uint8_t>().swap(m_slot[i].buf);
            if (i < m_limit && reserve) m_slot[i].buf.reserve(reserve);
        }
    }

    unsigned Capacity() const { return m_limit; }
    // the indexes go round twice the limit, so a full ring and an empty one differ; the limit is
    // a power of two, so the difference is right when they wrap
    unsigned Used() const { return (m_w - m_r) % (2U * m_limit); }
    unsigned Free() const { return m_limit - Used(); }

    void Push(std::vector<std::uint8_t>& in, std::int64_t off) {
        auto& slot = m_slot[m_w % m_limit];
        slot.off = off;
        std::swap(slot.buf, in);
        m_w = (m_w + 1U) % (2U * m_limit);
    }

    void Pop(std::vector<std::uint8_t>& out, std::int64_t& off) {
        auto& slot = m_slot[m_r % m_limit];
        off = slot.off;
        std::swap(slot.buf, out);
        m_r = (m_r + 1U) % (2U * m_limit);
    }

    // The memory the buffers hold now, for the log and the tests.
    std::size_t Held() const {
        std::size_t n = 0;
        for (const auto& s : m_slot) n += s.buf.capacity();
        return n;
    }

private:
    RingSlot m_slot[Size]{};
    unsigned m_r{}, m_w{};
    unsigned m_limit = unsigned(Size);
};

// Looking for the block header of an NCZ reads a few bytes of its stream; when they are not a
// block header they go first in the next read. Only the bytes read are kept: a stream shorter
// than a block header never gets bytes past its end (#4).
inline void KeepLookedAhead(std::vector<std::uint8_t>& carry, const void* data, std::size_t read) {
    carry.resize(read);
    if (read) std::memcpy(carry.data(), data, read);
}

} // namespace nxdock
