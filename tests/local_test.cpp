// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Installing without a server, on the computer: the folder list with split files (a FAT32
// folder of parts 00, 01… and name.nsp.00, .01…), reading across the parts, and the bytes of
// the USB protocol of Tinfoil as NS-USBloader expects them.
#include "file_source.hpp"
#include "space.hpp"
#include "usb_tinfoil.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" void log_write(const char*, ...) {}

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void Write(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

int main(int argc, char** argv) {
    const std::string dir = std::string(argc > 1 ? argv[1] : "/tmp") + "/local_test";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/Game.nsp " + dir + "/sub").c_str());
    // a split folder: 00, 01, 02
    Write(dir + "/Game.nsp/00", "AAAA");
    Write(dir + "/Game.nsp/01", "BBBBBB");
    Write(dir + "/Game.nsp/02", "CC");
    // name.xci.00, .01
    Write(dir + "/Other.xci.00", "0123");
    Write(dir + "/Other.xci.01", "456789");
    // a whole file, and things left out
    Write(dir + "/Whole.NSZ", "zz");
    Write(dir + "/notes.txt", "x");
    Write(dir + "/.hidden.nsp", "x");

    const auto list = ListFolder(dir, false);
    CHECK(!list.empty() && list[0].name == "..");
    std::vector<std::string> names;
    for (const auto& e : list) names.push_back(e.name);
    // folders first, then files by name; the text file and the hidden one are not there
    CHECK(names.size() == 5);
    CHECK(names.size() > 1 && names[1] == "sub");
    const LocalEntry* game = nullptr;
    const LocalEntry* other = nullptr;
    const LocalEntry* whole = nullptr;
    for (const auto& e : list) {
        if (e.name == "Game.nsp") game = &e;
        if (e.name == "Other.xci") other = &e;
        if (e.name == "Whole.NSZ") whole = &e;
    }
    CHECK(game && !game->dir && game->parts.size() == 3 && game->size == 12 && game->format == "nsp");
    CHECK(other && other->parts.size() == 2 && other->size == 10 && other->format == "xci");
    CHECK(whole && whole->parts.size() == 1 && whole->format == "nsz");
    CHECK(GameFormat("a.txt").empty() && GameFormat("b.XCZ") == "xcz");

    // reading across the parts
    if (game) {
        FileSource src(game->parts);
        CHECK(src.GetOpenResult() == 0 && src.Size() == 12);
        char buf[16] = {};
        u64 got = 0;
        CHECK(src.Read(buf, 2, 8, &got) == 0 && got == 8 && std::memcmp(buf, "AABBBBBB", 8) == 0);
        CHECK(src.Read(buf, 9, 3, &got) == 0 && std::memcmp(buf, "BCC", 3) == 0);
        CHECK(src.Read(buf, 10, 5, &got) != 0); // past the end
    }
    // a missing part: the split folder is not offered as a game
    std::remove((dir + "/Game.nsp/01").c_str());
    bool still = false;
    for (const auto& e : ListFolder(dir, true)) still |= e.name == "Game.nsp" && !e.dir;
    CHECK(!still);

    // the Tinfoil protocol
    unsigned char head[16] = {'T', 'U', 'L', '0', 12, 0, 0, 0};
    CHECK(tinfoil::ListLength(head) == 12);
    head[0] = 'X';
    CHECK(tinfoil::ListLength(head) == -1);
    const auto files = tinfoil::SplitList("a.nsp\nfolder/b.xci\n\n", 20);
    CHECK(files.size() == 2 && files[1] == "folder/b.xci");
    const auto req = tinfoil::RangeRequest("a.nsp", 0x1122334455ull, 0x800000);
    CHECK(req.size() == 32 + 32 + 5);
    CHECK(std::memcmp(req.data(), "TUC0", 4) == 0 && req[4] == 0 && req[8] == 1);
    CHECK(req[12] == 0x20 + 5);                     // the data that follows the header
    CHECK(req[32] == 0x00 && req[34] == 0x80);       // the size of the range, little endian
    CHECK(req[40] == 0x55 && req[44] == 0x11);       // the offset
    CHECK(req[48] == 5);                             // the length of the name
    CHECK(std::memcmp(req.data() + 64, "a.nsp", 5) == 0);
    const auto bye = tinfoil::ExitRequest();
    CHECK(bye.size() == 32 && bye[8] == 0);
    unsigned char answer[32] = {'T', 'U', 'C', '0', 1, 0, 0, 0, 1, 0, 0, 0, 0x00, 0x00, 0x80};
    CHECK(tinfoil::AnswerLength(answer) == 0x800000);
    answer[4] = 0;
    CHECK(tinfoil::AnswerLength(answer) == -1);

    // the planner: the largest games first, only as many as needed, never archived ones
    {
        std::vector<InstalledApp> apps(4);
        apps[0].name = "small", apps[0].sd_bytes = 1000;
        apps[1].name = "big", apps[1].sd_bytes = 9000;
        apps[2].name = "middle", apps[2].sd_bytes = 4000;
        apps[3].name = "gone", apps[3].archived = true, apps[3].sd_bytes = 20000;
        const auto plan = PlanArchive(apps, 10000, false);
        CHECK(plan.size() == 2 && plan[0] == 1 && plan[1] == 2);
        CHECK(PlanArchive(apps, 5000, false).size() == 1);
        CHECK(PlanArchive(apps, 50000, false).empty()); // not enough even with all
        CHECK(PlanArchive(apps, 100, true).empty());     // nothing in the internal memory
    }

    if (failures) {
        std::printf("local_test: %d failures\n", failures);
        return 1;
    }
    std::printf("local_test: ok\n");
    return 0;
}
