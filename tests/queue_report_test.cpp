// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the web asks of the queue (22A), read from the answer of the library app: pause, go on and
// cancel a job; anything else, or a cancel without a job, is left out.
#include "queue_report.hpp"

#include <cstdio>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main() {
    const auto c = ParseQueueCommands(R"({"commands":[{"id":"a1","action":"pause"},{"id":"a2","action":"cancel","job":3,"game":"Game"},
        {"id":"a3","action":"cancel"},{"id":"a4","action":"explode"},{"id":"a5","action":"resume"}]})");
    CHECK(c.size() == 3);
    CHECK(c.size() == 3 && c[0].action == "pause" && c[1].action == "cancel" && c[1].job == 3 && c[1].game == "Game" && c[2].action == "resume");
    CHECK(ParseQueueCommands(R"({"commands":[]})").empty());
    CHECK(ParseQueueCommands("not json").empty());
    std::printf(failures ? "%d failed\n" : "queue_report: all passed\n", failures);
    return failures ? 1 : 0;
}
