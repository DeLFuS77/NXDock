// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The rings of buffers between the install threads, and the bytes read while looking for the
// block header of an NCZ (#4).
#include "install_buffers.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// The read thread of yati, as it goes for a solid NCZ: after the header part it reads as many
// bytes as a block header to look for one; when they are not one they go first in the next read.
// `stream` bytes follow; the reads are of `piece` bytes. True when every byte reached the decompressor.
static bool ReadSolid(std::size_t stream, std::size_t piece) {
    constexpr std::size_t kHeader = 24; // the size of ncz::BlockHeader
    std::vector<std::uint8_t> data(stream);
    for (std::size_t i = 0; i < stream; i++) data[i] = std::uint8_t(i * 7 + 1);
    std::size_t offset = 0;
    const auto read = [&](void* out, std::size_t want) {
        const std::size_t n = std::min(want, stream - offset);
        if (n) std::memcpy(out, data.data() + offset, n);
        offset += n;
        return n;
    };
    std::vector<std::uint8_t> got, carry;
    std::uint8_t header[kHeader] = {};
    KeepLookedAhead(carry, header, read(header, kHeader)); // not a block header
    while (offset < stream) {
        std::vector<std::uint8_t> buf = carry;
        const std::size_t before = buf.size();
        carry.clear();
        buf.resize(before + piece);
        const std::size_t n = read(buf.data() + before, piece);
        buf.resize(before + n);
        got.insert(got.end(), buf.begin(), buf.end());
    }
    // what the loop left: the stream ended inside the look ahead
    got.insert(got.end(), carry.begin(), carry.end());
    return got == data;
}

int main() {
    {
        // as an application: four buffers of 4 MiB taken now
        BufferRing<4> r;
        r.Limit(4, 4u << 20);
        CHECK(r.Capacity() == 4 && r.Free() == 4 && r.Held() == 4 * (4u << 20));
    }
    {
        // as an applet: two, taken as they fill, and no more than two ever held
        BufferRing<4> r;
        r.Limit(2, 0);
        CHECK(r.Capacity() == 2 && r.Free() == 2 && r.Held() == 0);
        std::int64_t off = 0;
        int wrong = 0;
        for (int round = 0; round < 50; round++) {
            for (int k = 0; k < 2; k++) {
                std::vector<std::uint8_t> b(1000 + size_t(k), std::uint8_t(round));
                r.Push(b, round * 10 + k);
            }
            if (r.Free() != 0 || r.Used() != 2) wrong++;
            for (int k = 0; k < 2; k++) {
                std::vector<std::uint8_t> out;
                r.Pop(out, off);
                if (off != round * 10 + k || out.size() != 1000 + size_t(k) || out[0] != std::uint8_t(round)) wrong++;
            }
            if (r.Used() != 0) wrong++;
        }
        CHECK(wrong == 0);
        // the buffers given back are the empty ones the consumer had: what stays is small
        CHECK(r.Held() <= 2 * 1001);
    }
    {
        // a limit that is not a power of two is rounded down; 0 is 1
        BufferRing<4> r;
        r.Limit(3, 0);
        CHECK(r.Capacity() == 2);
        r.Limit(0, 0);
        CHECK(r.Capacity() == 1 && r.Free() == 1);
        r.Limit(9, 0);
        CHECK(r.Capacity() == 4);
    }
    {
        // a ring left alone is as before: four
        BufferRing<4> r;
        CHECK(r.Capacity() == 4 && r.Free() == 4);
    }
    {
        // only the bytes read are kept, never the whole header
        std::uint8_t header[24];
        for (int i = 0; i < 24; i++) header[i] = std::uint8_t(i + 1);
        std::vector<std::uint8_t> carry;
        int wrong = 0;
        for (std::size_t n = 0; n <= 24; n++) {
            KeepLookedAhead(carry, header, n);
            if (carry.size() != n || (n && std::memcmp(carry.data(), header, n) != 0)) wrong++;
        }
        CHECK(wrong == 0);
    }
    {
        // a solid stream of every length up to past a block header, read in pieces: every byte
        // reaches the decompressor, and none past its end
        int wrong = 0;
        for (std::size_t stream = 0; stream <= 80; stream++) {
            for (std::size_t piece : {1u, 7u, 24u, 1024u}) {
                if (!ReadSolid(stream, piece)) wrong++;
            }
        }
        CHECK(wrong == 0);
    }
    std::printf(failures ? "%d failed\n" : "install_buffers: all passed\n", failures);
    return failures ? 1 : 0;
}
