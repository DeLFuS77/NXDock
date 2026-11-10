// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Checking the SD card (17A of the visual test of phases 16 to 23), in the way of h2testw: test
// files fill the free space, every megabyte with a pattern of its own place, and are read back
// and deleted. It finds
//  - fake cards, which say they hold more than they do: what goes past their real size wraps
//    round onto the start, or is lost;
//  - parts that fail: megabytes that cannot be read, or come back different;
//  - the real speed of writing and reading.
// Only free space is written; the files go at the end, also when the check is stopped. After
// every file, the first megabyte is read again: on a fake card that wraps, the check stops there,
// before it writes more over the start of the card.
//
// The quick check writes one whole file (the speed) and then one megabyte at the start of every
// file of a gigabyte that fills the rest, so the whole card is reached in a few minutes. The full
// check writes and reads every megabyte of the free space.
//
// The core is plain C++ over SdCheckIo, so it is tested on the computer with fake cards.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nxdock {

class SdCheckIo {
public:
    virtual ~SdCheckIo() = default;
    virtual bool Begin() = 0;                       // the folder of the test files
    virtual long long Free() = 0;                   // free bytes, -1 unknown
    virtual long long Total() = 0;                  // the size the card says, -1 unknown
    virtual bool Create(int file, uint64_t size) = 0; // a file of that size (written or not)
    virtual bool Write(int file, uint64_t off, const void* buf, size_t size) = 0;
    virtual bool Read(int file, uint64_t off, void* buf, size_t size) = 0;
    virtual void Close(int file) = 0;               // done writing it: flushed and closed
    virtual void End() = 0;                         // every test file and the folder deleted
    virtual double Now() = 0;                       // seconds
};

struct SdCheckResult {
    enum class Kind { None, Good, Fake, Bad, TooFull, Failed, Stopped };
    Kind kind = Kind::None;
    uint64_t checked = 0;    // bytes written and read back
    uint64_t real = 0;       // Fake: about how much the card really holds
    uint64_t bad = 0;        // Bad: bytes that failed
    uint64_t first_bad = 0;  // Bad: where the first failing part is, from the start of the free space
    double write_mb = 0, read_mb = 0; // MB/s
    double seconds = 0;
};

struct SdCheckProgress {
    std::atomic<int> phase{0}; // 0 not started, 1 writing, 2 reading, 3 deleting, 4 done
    std::atomic<uint64_t> done{0}, total{0};
    std::atomic<double> write_mb{0}, read_mb{0};
    std::atomic<bool> stop{false};
};

constexpr uint64_t kSdBlock = 1024 * 1024;           // one megabyte, the unit of the pattern
constexpr uint64_t kSdFile = 1024 * kSdBlock;        // one gigabyte per file (FAT32 allows 4)
constexpr uint64_t kSdMargin = 256 * kSdBlock;       // left free, for the system

// The pattern of the block at that place of the free space: what is written there and expected
// back. Two different places never look alike, so a card that wraps is found.
void SdPattern(uint64_t block, uint64_t seed, unsigned char* out, size_t size);

// Runs a check; quick or full. The seed makes every check different (the time, on the console).
SdCheckResult RunSdCheck(SdCheckIo& io, bool quick, uint64_t seed, SdCheckProgress& progress);

// How long a check takes, about, from the free space and the speeds measured (0: not yet).
double SdCheckEstimate(uint64_t free_bytes, bool quick, double write_mb, double read_mb);

} // namespace nxdock
