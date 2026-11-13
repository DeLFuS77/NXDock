// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// An official update over a game NXDock compressed, or over an optimized one: it stops the game.
#include "update_over_game.hpp"

#include <cstdio>
#include <vector>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failed++;
    }
}

int main() {
    constexpr uint32_t v1 = 65536, v7 = 458752, v8 = 524288;
    // no game, or one as Nintendo made it: nothing to say
    Check(UpdateOverGame(BaseGame{}, v1) == UpdateOver::Fine, "no game installed");
    Check(UpdateOverGame(BaseGame{.installed = true}, v1) == UpdateOver::Fine, "a game at v0 takes its update");

    // compressed by NXDock (its program written again with the LZ4 layer)
    const BaseGame compressed{.installed = true, .compressed = true};
    Check(UpdateOverGame(compressed, v1) == UpdateOver::Breaks, "an update over a compressed game stops it");
    Check(StopsTheGame(UpdateOverGame(compressed, v1)), "it is told");
    Check(OriginalHelps(UpdateOverGame(compressed, v1)), "the original first lets it work");

    // optimized (merged with v7 on the computer): an Application at v7, without a recipe
    const BaseGame optimized{.installed = true, .version = v7};
    Check(UpdateOverGame(optimized, v8) == UpdateOver::Breaks, "a newer update over an optimized game stops it");
    Check(OriginalHelps(UpdateOverGame(optimized, v8)), "the original first lets a newer update work");
    Check(UpdateOverGame(optimized, v7) == UpdateOver::InGame, "the update merged in is in the game");
    Check(UpdateOverGame(optimized, v1) == UpdateOver::InGame, "an older update is in the game");
    Check(StopsTheGame(UpdateOverGame(optimized, v1)) && !OriginalHelps(UpdateOverGame(optimized, v1)),
          "one in the game stays out: the original does not help");

    // merged by NXDock with its recipe: a newer update is merged right after it is installed (25G)
    const BaseGame merged{.installed = true, .version = v7, .compressed = true, .merged = true, .recipe = true};
    Check(UpdateOverGame(merged, v8) == UpdateOver::MergedAfter, "a newer update over a merged game is merged after");
    Check(!StopsTheGame(UpdateOverGame(merged, v8)), "and nothing is asked");
    Check(UpdateOverGame(merged, v7) == UpdateOver::InGame, "the update it was merged with is in it");

    // merged by NXDock when Nintendo had compressed it (no recipe): it cannot be merged again
    const BaseGame no_recipe{.installed = true, .version = v7, .compressed = true, .merged = true};
    Check(UpdateOverGame(no_recipe, v8) == UpdateOver::Breaks, "a newer update over a merged game without recipe stops it");
    Check(OriginalHelps(UpdateOverGame(no_recipe, v8)), "the original first lets it work");

    // the queue: a game before its update and its DLC, whatever order they came in
    {
        constexpr uint64_t game = 0x0100ABCD12340000, update = 0x0100ABCD12340800, dlc = 0x0100ABCD12341001, other = 0x0100FFFF00000000;
        Check(GameOf(update) == game && GameOf(dlc) == game && GameOf(game) == game, "the game of each content");
        Check(NextToInstall(std::vector<uint64_t>{update, other, game}) == 2, "the game goes before its update");
        Check(NextToInstall(std::vector<uint64_t>{dlc, game, update}) == 1, "the game goes before its DLC");
        Check(NextToInstall(std::vector<uint64_t>{game, update}) == 0, "in order already");
        Check(NextToInstall(std::vector<uint64_t>{update, other}) == 0, "an update without its game in the queue goes");
        Check(NextToInstall(std::vector<uint64_t>{0, game}) == 0, "one not known keeps its place");
        Check(NextToInstall(std::vector<uint64_t>{update, 0x0100ABCD12350000}) == 0, "another game is not its game");
        Check(NextToInstall(std::vector<uint64_t>{}) == 0, "an empty queue");
    }

    if (failed) return 1;
    std::printf("update_over_game: all right\n");
    return 0;
}
