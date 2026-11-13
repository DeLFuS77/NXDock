// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// What installing an optimized game takes away: the updates of it at or below its version.
#include "merged_updates.hpp"

#include <cstdio>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failed++;
    }
}

int main() {
    // a game as Nintendo ships it keeps its updates
    Check(!IsMergedGame(kMetaApplication, 0), "a game at v0 is not merged");
    Check(!UpdateIsInGame(kMetaApplication, 0, kMetaPatch, 0x10000), "a game at v0 keeps its update");
    // an optimized game at v458752 (7.0): the update it holds, and older ones, go
    Check(IsMergedGame(kMetaApplication, 458752), "a game at the version of an update is merged");
    Check(UpdateIsInGame(kMetaApplication, 458752, kMetaPatch, 458752), "the update merged in goes");
    Check(UpdateIsInGame(kMetaApplication, 458752, kMetaPatch, 65536), "an older update goes");
    // a newer update stays, and what is no update is never touched
    Check(!UpdateIsInGame(kMetaApplication, 458752, kMetaPatch, 524288), "a newer update stays");
    Check(!UpdateIsInGame(kMetaApplication, 458752, 0x82, 65536), "a DLC stays");
    Check(!UpdateIsInGame(kMetaPatch, 458752, kMetaPatch, 65536), "installing an update removes nothing here");
    if (failed) return 1;
    std::printf("merged_updates: all right\n");
    return 0;
}
