// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "sdcheck.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nxdock {
namespace {

uint64_t Mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

// Where a block of the free space lives: its file and its place in it.
struct Place {
    int file;
    uint64_t off;
};
Place PlaceOf(uint64_t block) {
    const uint64_t per = kSdFile / kSdBlock;
    return {int(block / per), (block % per) * kSdBlock};
}

// When a block holds the pattern of a later block, the card wrapped round: the distance between
// the two is what it really holds (or a multiple of it). 0 when it is not so.
uint64_t WrapDistance(const unsigned char* back, uint64_t seed, uint64_t own) {
    uint64_t idx = 0, s = 0;
    std::memcpy(&idx, back, 8);
    std::memcpy(&s, back + 8, 8);
    if (s != seed || idx <= own) return 0;
    return (idx - own) * kSdBlock;
}

} // namespace

void SdPattern(uint64_t block, uint64_t seed, unsigned char* out, size_t size) {
    uint64_t x = Mix(block * 0x9E3779B97F4A7C15ull ^ seed) | 1;
    size_t i = 0;
    // the place itself first, readable in a dump of the card
    for (; i + 8 <= size && i < 16; i += 8) {
        const uint64_t v = i == 0 ? block : seed;
        std::memcpy(out + i, &v, 8);
    }
    for (; i + 8 <= size; i += 8) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        std::memcpy(out + i, &x, 8);
    }
    for (; i < size; i++) out[i] = (unsigned char)(x >> (8 * (i & 7)));
}

SdCheckResult RunSdCheck(SdCheckIo& io, bool quick, uint64_t seed, SdCheckProgress& progress) {
    SdCheckResult r;
    const double t0 = io.Now();
    progress.phase = 1;
    progress.done = 0;
    progress.write_mb = 0;
    progress.read_mb = 0;
    const auto finish = [&](SdCheckResult::Kind kind) {
        progress.phase = 3;
        io.End();
        r.kind = kind;
        r.seconds = io.Now() - t0;
        progress.phase = 4;
        return r;
    };
    if (!io.Begin()) return finish(SdCheckResult::Kind::Failed);
    const long long free_b = io.Free();
    const long long total_b = io.Total();
    if (free_b < 0) return finish(SdCheckResult::Kind::Failed);
    if (uint64_t(free_b) < kSdMargin + 64 * kSdBlock) return finish(SdCheckResult::Kind::TooFull);
    const uint64_t usable = (uint64_t(free_b) - kSdMargin) / kSdBlock * kSdBlock;
    const uint64_t used_before = total_b > free_b ? uint64_t(total_b - free_b) : 0;
    const uint64_t per = kSdFile / kSdBlock;
    const int files = int((usable + kSdFile - 1) / kSdFile);
    const auto file_blocks = [&](int f) { return std::min(per, (usable - uint64_t(f) * kSdFile) / kSdBlock); };

    // the blocks this check writes, in order
    std::vector<uint64_t> blocks;
    for (int f = 0; f < files; f++) {
        const uint64_t n = (quick && f > 0) ? 1 : file_blocks(f);
        for (uint64_t i = 0; i < n; i++) blocks.push_back(uint64_t(f) * per + i);
    }
    progress.total = uint64_t(blocks.size()) * 2 * kSdBlock;

    std::vector<unsigned char> buf(kSdBlock), back(kSdBlock);
    std::vector<char> failed(blocks.size(), 0);
    size_t written = 0; // blocks written
    // the speed: every block on a full check, the first file on a quick one
    double write_time = 0, read_time = 0;
    uint64_t write_bytes = 0, read_bytes = 0;
    bool wrapped = false;
    uint64_t wrap = 0; // the shortest distance a block was found again at
    // the start of the card, read again: on a card that wraps round, it is gone once the writing
    // reaches the end of what the card really holds
    const auto Wrapped = [&](int f) {
        if (failed[0]) return false;
        SdPattern(blocks[0], seed, buf.data(), kSdBlock);
        if (io.Read(0, 0, back.data(), kSdBlock) && std::memcmp(buf.data(), back.data(), kSdBlock) == 0) return false;
        wrapped = true;
        const uint64_t d = WrapDistance(back.data(), seed, blocks[0]);
        r.real = d ? d : used_before + uint64_t(f) * kSdFile;
        return true;
    };
    int bad_writes_in_a_row = 0;

    // 1. writing, file by file
    size_t k = 0;
    for (int f = 0; f < files && !wrapped; f++) {
        if (progress.stop) return finish(SdCheckResult::Kind::Stopped);
        if (!io.Create(f, file_blocks(f) * kSdBlock)) {
            if (f == 0) return finish(SdCheckResult::Kind::Failed);
            break; // the card takes no more: what was written is read back
        }
        const bool timed = !quick || f == 0;
        for (; k < blocks.size() && PlaceOf(blocks[k]).file == f; k++) {
            if (progress.stop) {
                io.Close(f);
                return finish(SdCheckResult::Kind::Stopped);
            }
            SdPattern(blocks[k], seed, buf.data(), kSdBlock);
            const double a = io.Now();
            const bool ok = io.Write(f, PlaceOf(blocks[k]).off, buf.data(), kSdBlock);
            const double b = io.Now();
            if (timed) {
                write_time += b - a;
                write_bytes += kSdBlock;
                if (write_time > 0.2) progress.write_mb = double(write_bytes) / write_time / 1048576.0;
            }
            if (!ok) {
                failed[k] = 1;
                if (++bad_writes_in_a_row >= 64) break; // the card stopped taking anything
            } else {
                bad_writes_in_a_row = 0;
            }
            written = k + 1;
            progress.done += kSdBlock;
            // every 64 MB, the start of the card read again (17A)
            if (k > 0 && (k + 1) % 64 == 0 && Wrapped(f)) break;
        }
        io.Close(f);
        if (wrapped || bad_writes_in_a_row >= 64) break;
        if (f > 0 && Wrapped(f)) break;
    }
    if (wrapped) {
        r.checked = uint64_t(written) * kSdBlock;
        r.write_mb = write_time > 0 ? double(write_bytes) / write_time / 1048576.0 : 0;
        return finish(SdCheckResult::Kind::Fake);
    }

    // 2. reading every block back
    progress.phase = 2;
    for (size_t i = 0; i < written; i++) {
        if (progress.stop) return finish(SdCheckResult::Kind::Stopped);
        const Place p = PlaceOf(blocks[i]);
        const double a = io.Now();
        const bool ok = io.Read(p.file, p.off, back.data(), kSdBlock);
        const double b = io.Now();
        if (!quick || p.file == 0) {
            read_time += b - a;
            read_bytes += kSdBlock;
            if (read_time > 0.2) progress.read_mb = double(read_bytes) / read_time / 1048576.0;
        }
        SdPattern(blocks[i], seed, buf.data(), kSdBlock);
        if (!ok || std::memcmp(buf.data(), back.data(), kSdBlock) != 0) {
            failed[i] = 1;
            if (ok) {
                const uint64_t d = WrapDistance(back.data(), seed, blocks[i]);
                if (d && (!wrap || d < wrap)) wrap = d;
            }
        }
        progress.done += kSdBlock;
    }

    r.write_mb = write_time > 0 ? double(write_bytes) / write_time / 1048576.0 : 0;
    r.read_mb = read_time > 0 ? double(read_bytes) / read_time / 1048576.0 : 0;
    size_t bad = 0, first = written;
    for (size_t i = 0; i < written; i++) {
        if (failed[i]) {
            bad++;
            first = std::min(first, i);
        }
    }
    r.checked = uint64_t(written - bad) * kSdBlock;
    if (bad && wrap) {
        // blocks found where earlier ones should be: a card that wraps round
        r.real = wrap;
        return finish(SdCheckResult::Kind::Fake);
    }
    if (bad == 0) {
        // a card that took less than its free space says is not good either
        if (written < blocks.size()) {
            r.real = used_before + blocks[written - 1] * kSdBlock + kSdBlock;
            return finish(SdCheckResult::Kind::Fake);
        }
        return finish(SdCheckResult::Kind::Good);
    }
    // everything from one place on failing: the card ends there, whatever it says
    const bool tail = first > 0 && bad == written - first;
    if (tail) {
        r.real = used_before + blocks[first] * kSdBlock;
        return finish(SdCheckResult::Kind::Fake);
    }
    r.bad = uint64_t(bad) * kSdBlock;
    r.first_bad = blocks[first] * kSdBlock;
    return finish(SdCheckResult::Kind::Bad);
}

double SdCheckEstimate(uint64_t free_bytes, bool quick, double write_mb, double read_mb) {
    if (free_bytes <= kSdMargin) return 0;
    const double w = write_mb > 0.5 ? write_mb : 25, rd = read_mb > 0.5 ? read_mb : 70;
    const double usable = double(free_bytes - kSdMargin) / 1048576.0; // MB
    if (!quick) return usable / w + usable / rd;
    const double first = std::min(usable, 1024.0);
    const double files = usable / 1024.0;
    // a megabyte per file, and the time to make each file
    return first / w + first / rd + files * (1 / w + 1 / rd + 0.05);
}

} // namespace nxdock
