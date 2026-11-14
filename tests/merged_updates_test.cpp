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
    // the control NCA the optimized game shares with its update stays; the rest of the update goes
    const ContentId control{0x12, 0x10, 0xc4, 0x9e, 0xff, 0x42, 0x31, 0xa5, 0xe6, 0x57, 0x74, 0x7c, 0xcb, 0x46, 0xe6, 0xe7};
    const ContentId program{0x63, 0xbf, 0xf3, 0x3b, 0x35, 0xcd, 0xf8, 0x19, 0x80, 0xc2, 0x4e, 0x9c, 0x92, 0x7e, 0x11, 0x51};
    const ContentId update_program{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
    const std::vector<ContentId> game{program, control};
    Check(ContentInGame(game, control.data()), "the control NCA shared with the update stays");
    Check(!ContentInGame(game, update_program.data()), "the program NCA of the update goes");
    Check(!ContentInGame({}, control.data()), "with no NCA of the game, every NCA of the update goes");
    if (failed) return 1;
    std::printf("merged_updates: all right\n");
    return 0;
}
