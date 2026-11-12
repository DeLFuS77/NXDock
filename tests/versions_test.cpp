// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The list of versions of titledb (versions.txt) read on the computer: the newest update of a game,
// the DLC of it, a game without either, and a text that is not the list.
#include "versions_list.hpp"

#include <cstdio>
#include <string>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main() {
    const std::string text = "id|rightsId|version\n"
                             "0100000000010000|01000000000100000000000000000003|0\n"
                             "0100000000010800|00000000000000000000000000000000|393216\n"
                             "0100000000011001|01000000000110010000000000000003|0\n"
                             "0100000000011002|01000000000110020000000000000003|65536\n"
                             "0100000000012001|01000000000120010000000000000003|0\n"
                             "0100000000020000|01000000000200000000000000000003|\n"
                             "0100000000010800|00000000000000000000000000000000|196608\n";
    std::vector<std::pair<uint64_t, uint32_t>> entries;
    CHECK(VersionsList::Parse(text, entries));
    CHECK(entries.size() == 6); // the update twice is one entry, its newest version
    VersionsList list;
    list.Set(entries, 1);
    CHECK(list.Ready());
    CHECK(list.LatestUpdate(0x0100000000010000ull) == 393216);
    CHECK(list.LatestUpdate(0x0100000000020000ull) == 0);
    const auto dlcs = list.Dlcs(0x0100000000010000ull);
    CHECK(dlcs.size() == 2); // 11001 and 11002; 12001 is of another game
    CHECK(list.Dlcs(0x0100000000020000ull).empty());
    CHECK(!VersionsList::Parse("<html>not found</html>", entries));

    if (failures) {
        std::printf("versions_test: %d failed\n", failures);
        return 1;
    }
    std::printf("versions_test: ok\n");
    return 0;
}
