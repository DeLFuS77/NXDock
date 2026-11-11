// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The check of the SD card against cards made up here: a good one, fake ones that wrap round or
// lose what goes past their real size, one with parts that fail, one too full, and a check that
// is stopped. The card keeps, for each megabyte, which block of the pattern was written there,
// and gives the pattern back when read: no gigabytes are held in memory.
#include "sdcheck.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <vector>

using namespace nxdock;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

constexpr uint64_t MB = kSdBlock, GB = 1024 * MB;

struct FakeCard : SdCheckIo {
    enum class Kind { Good, Wrap, Lose };
    Kind kind = Kind::Good;
    uint64_t says = 8 * GB;   // the size the card says
    uint64_t real = 8 * GB;   // what it holds
    uint64_t used = 512 * MB; // taken by files already
    std::set<uint64_t> broken; // physical megabytes that read back wrong
    std::map<uint64_t, std::pair<uint64_t, uint64_t>> held; // physical MB -> block, seed written
    std::set<int> files;
    double clock = 0;
    bool ended = false;
    int stop_after_writes = -1;
    SdCheckProgress* progress = nullptr;
    int writes = 0;

    uint64_t Physical(int file, uint64_t off) const {
        const uint64_t at = used + uint64_t(file) * kSdFile + off;
        return at / MB;
    }
    bool Begin() override { return true; }
    long long Free() override { return (long long)(says - used); }
    long long Total() override { return (long long)says; }
    bool Create(int file, uint64_t) override {
        files.insert(file);
        return true;
    }
    bool Write(int file, uint64_t off, const void* buf, size_t size) override {
        clock += double(size) / (20.0 * MB);
        if (stop_after_writes >= 0 && ++writes >= stop_after_writes && progress) progress->stop = true;
        uint64_t mb = Physical(file, off);
        if (mb * MB >= real) {
            if (kind == Kind::Lose) return true; // gone, without a word
            if (kind == Kind::Wrap) mb %= real / MB;
        }
        uint64_t block = 0, seed = 0;
        std::memcpy(&block, buf, 8);
        std::memcpy(&seed, (const char*)buf + 8, 8);
        held[mb] = {block, seed};
        return true;
    }
    bool Read(int file, uint64_t off, void* buf, size_t size) override {
        clock += double(size) / (80.0 * MB);
        uint64_t mb = Physical(file, off);
        if (mb * MB >= real) {
            if (kind == Kind::Lose) {
                std::memset(buf, 0, size);
                return true;
            }
            if (kind == Kind::Wrap) mb %= real / MB;
        }
        auto it = held.find(mb);
        if (it == held.end()) {
            std::memset(buf, 0xFF, size);
            return true;
        }
        SdPattern(it->second.first, it->second.second, (unsigned char*)buf, size);
        if (broken.count(mb)) ((unsigned char*)buf)[size / 2] ^= 0x40;
        return true;
    }
    void Close(int) override {}
    void End() override {
        files.clear();
        ended = true;
    }
    double Now() override { return clock; }
};

SdCheckResult Run(FakeCard& card, bool quick) {
    SdCheckProgress progress;
    card.progress = &progress;
    return RunSdCheck(card, quick, 0x1234ABCDull, progress);
}

bool Near(uint64_t a, uint64_t b, uint64_t slack) { return a + slack >= b && b + slack >= a; }

} // namespace

int main() {
    {
        FakeCard card;
        card.says = card.real = 4 * GB;
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::Good, "a good card, full check: good");
        Check(r.checked == (4 * GB - 512 * MB - kSdMargin), "a good card: all the free space checked");
        Check(Near(uint64_t(r.write_mb), 20, 1) && Near(uint64_t(r.read_mb), 80, 1), "the speeds of writing and reading");
        Check(card.ended && card.files.empty(), "the test files are deleted");
    }
    {
        FakeCard card;
        card.says = card.real = 6 * GB;
        const auto r = Run(card, true);
        Check(r.kind == SdCheckResult::Kind::Good, "a good card, quick check: good");
        Check(Near(uint64_t(r.write_mb), 20, 1), "the quick check measures the speed on its first file");
    }
    {
        FakeCard card;
        card.kind = FakeCard::Kind::Wrap;
        card.says = 8 * GB;
        card.real = 2 * GB;
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::Fake, "a card that wraps, full check: fake");
        Check(r.real == 2 * GB, "... and it says how much it really holds");
        Check(r.checked < 3 * GB, "... and it stops writing soon after the wrap");
    }
    {
        FakeCard card;
        card.kind = FakeCard::Kind::Wrap;
        card.says = 16 * GB;
        card.real = 4 * GB;
        const auto r = Run(card, true);
        Check(r.kind == SdCheckResult::Kind::Fake, "a card that wraps, quick check: fake");
        Check(r.real == 4 * GB || r.real == 8 * GB, "... with what it holds (or a multiple)");
    }
    {
        FakeCard card;
        card.kind = FakeCard::Kind::Lose;
        card.says = 8 * GB;
        card.real = 3 * GB;
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::Fake, "a card that loses what goes past it, full check: fake");
        Check(Near(r.real, 3 * GB, MB), "... with what it holds");
    }
    {
        FakeCard card;
        card.kind = FakeCard::Kind::Lose;
        card.says = 16 * GB;
        card.real = 5 * GB;
        const auto r = Run(card, true);
        Check(r.kind == SdCheckResult::Kind::Fake, "a card that loses what goes past it, quick check: fake");
        Check(Near(r.real, 5 * GB, GB), "... with what it holds, within a gigabyte");
    }
    {
        FakeCard card;
        card.says = card.real = 3 * GB;
        card.broken = {900, 901, 2000};
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::Bad, "a card with parts that fail: bad");
        Check(r.bad == 3 * MB, "... three megabytes");
        Check(r.first_bad == (900 * MB - 512 * MB), "... the first where it is");
    }
    {
        FakeCard card;
        card.says = 1 * GB;
        card.used = 1 * GB - 200 * MB;
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::TooFull, "a card too full to check");
    }
    {
        FakeCard card;
        card.says = card.real = 4 * GB;
        card.stop_after_writes = 300;
        const auto r = Run(card, false);
        Check(r.kind == SdCheckResult::Kind::Stopped, "a check stopped half way");
        Check(card.ended && card.files.empty(), "... deletes its files");
    }
    {
        const double full = SdCheckEstimate(64 * GB, false, 20, 80);
        const double quick = SdCheckEstimate(64 * GB, true, 20, 80);
        Check(full > 3000 && full < 5000, "the full check of 64 GB takes about an hour");
        Check(quick < 200, "the quick check takes a few minutes");
    }
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
