// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The mark on a cover of the library (2A): installing, waiting and its place, or nothing; the
// update and the DLC of a game count as the game.
#include "queue_mark.hpp"

#include <cstdio>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

struct Job {
    enum class State { Waiting, Running, Done, Failed, Cancelled };
    uint64_t title_id = 0;
    std::string color_key;
    State state = State::Waiting;
};

int main() {
    const uint64_t kart = 0x0100F2E3D4C5B000, farm = 0x0100A1B2C3D4E000;
    std::vector<Job> jobs = {
        {kart | 0x800, "kart", Job::State::Done},          // finished: not a mark
        {farm + 0x1001, "farm", Job::State::Waiting},       // a DLC of the farm, waiting
        {kart, "kart", Job::State::Running},               // the kart, installing
        {0, "loose", Job::State::Waiting},                 // a file without a title, waiting
        {farm, "farm", Job::State::Waiting},               // the farm itself, waiting after its DLC
    };
    auto k = QueueMarkOf(jobs, kart, "kart");
    CHECK(k.state == QueueMark::State::Installing && k.place == 1);
    // the update of the kart is the kart
    CHECK(QueueMarkOf(jobs, kart | 0x800, "x").state == QueueMark::State::Installing);
    // the farm: its first job waiting is the DLC, second in the queue (the kart installs first)
    auto f = QueueMarkOf(jobs, farm, "farm");
    CHECK(f.state == QueueMark::State::Waiting && f.place == 2);
    // without a title, by the key it was queued with
    auto l = QueueMarkOf(jobs, 0, "loose");
    CHECK(l.state == QueueMark::State::Waiting && l.place == 3);
    // a game not in the queue, and a finished one
    CHECK(QueueMarkOf(jobs, 0x0100000000010000, "other").state == QueueMark::State::None);
    jobs[2].state = Job::State::Done;
    k = QueueMarkOf(jobs, kart, "kart");
    CHECK(k.state == QueueMark::State::None && k.place == 0);
    // nothing installing: the first waiting is first
    f = QueueMarkOf(jobs, farm, "farm");
    CHECK(f.state == QueueMark::State::Waiting && f.place == 1);
    // the game codes of updates and DLC
    CHECK(QueueGameOf(kart | 0x800) == kart && QueueGameOf(kart + 0x1005) == kart && QueueGameOf(kart) == kart);
    if (failures) return 1;
    std::printf("queue_mark_test: ok\n");
    return 0;
}
