// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The report for the library app on an SD card made up here: only the logs of NXDock, its history
// and the newest crash reports go in (never keys, configuration or anything else), a long log
// keeps its end and a long crash report its start, info.txt says what it should, and the zip,
// deflated, reads back byte for byte; and what is taken out of them on the way (#4): URLs, hosts,
// IP addresses, keys and pins, the names of games and of files, while the IDs, sizes and codes stay.
#include "brand.hpp"
#include "report.hpp"
#include "zip.hpp"

#include <sys/stat.h>
#include <utime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void Put(const std::string& path, const std::string& text, long long mtime = 0) {
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    if (mtime) {
        utimbuf t{std::time_t(mtime), std::time_t(mtime)};
        utime(path.c_str(), &t);
    }
}

static std::string Lines(const char* what, int count) {
    std::string out;
    char line[96];
    for (int i = 0; i < count; i++) {
        std::snprintf(line, sizeof(line), "[NXDock] %s %d: installed, %d bytes\n", what, i, i * 7919 % 100000);
        out += line;
    }
    return out;
}

int main(int argc, char** argv) {
    const std::string root = std::string(argc > 1 ? argv[1] : "/tmp") + "/report_test";
    const std::string sd = root + "/sd";
    std::system(("rm -rf " + root).c_str());
    std::system(("mkdir -p " + sd + "/switch/NXDock/tmp " + sd + "/switch/NXDock/folder.log " + sd + "/atmosphere/crash_reports/old " +
                 sd + "/config/NXDock " + sd + "/switch/NXDock/saves")
                    .c_str());

    // what goes in
    const std::string install = Lines("install", 120000); // over 4 MiB: its end only
    const std::string compress = Lines("compress", 40);
    const std::string history = "1763400000\tGame\tbase\t0\tok\n";
    CHECK(install.size() > report::kMaxLog);
    Put(sd + "/switch/NXDock/install.log", install);
    Put(sd + "/switch/NXDock/compress.log", compress);
    Put(sd + "/switch/NXDock/history.tsv", history);
    std::map<std::string, std::string> crashes;
    for (int i = 0; i < 7; i++) {
        char name[64];
        std::snprintf(name, sizeof(name), "%011d_0100000000001000.log", 1763400000 + i);
        // the newest is over 1 MiB: its start only
        std::string text = "Atmosphere Crash Report (v4):\nResult: 2168-0002\n" + (i == 6 ? Lines("stack", 30000) : Lines("stack", 10));
        crashes[name] = text;
        Put(sd + "/atmosphere/crash_reports/" + name, text, 1763400000 + i * 60);
    }
    // what never goes in
    Put(sd + "/switch/prod.keys", "master_key_00 = 00000000000000000000000000000000\n");
    Put(sd + "/switch/NXDock/prod.keys", "master_key_00 = 00000000000000000000000000000000\n");
    Put(sd + "/switch/NXDock/title.keys", "0100000000010000 = 00000000000000000000000000000000\n");
    Put(sd + "/config/NXDock/config.ini", "url=http://192.168.1.10:3000\nkey=secret\n");
    Put(sd + "/switch/NXDock/catalog-cache.json", "{}");
    Put(sd + "/switch/NXDock/compressed.txt", "0100000000010000\n");
    Put(sd + "/switch/NXDock/tmp/old.log", "an old log in a folder below\n");
    Put(sd + "/switch/NXDock/saves/slot.log", "a save named as a log\n");
    Put(sd + "/atmosphere/crash_reports/old/00000000001_0100000000001000.log", "in a folder below\n");
    Put(sd + "/atmosphere/crash_reports/dump.bin", "not a log\n");

    const auto files = report::Files(sd);
    std::vector<std::string> names;
    for (const auto& f : files) names.push_back(f.name);
    const std::vector<std::string> want = {
        "logs/compress.log", "logs/install.log", "logs/history.tsv",
        "crash_reports/01763400006_0100000000001000.log", "crash_reports/01763400005_0100000000001000.log",
        "crash_reports/01763400004_0100000000001000.log", "crash_reports/01763400003_0100000000001000.log",
        "crash_reports/01763400002_0100000000001000.log"};
    CHECK(names == want);
    for (const auto& f : files) {
        const std::string rel = f.path.substr(0, sd.size()) == sd ? f.path.substr(sd.size()) : f.path;
        CHECK(rel.find("keys") == std::string::npos && rel.find("config") == std::string::npos);
        CHECK(rel.find("/tmp/") == std::string::npos && rel.find("/saves/") == std::string::npos && rel.find("/old/") == std::string::npos);
    }
    if (files.size() == want.size()) {
        CHECK(files[1].size == report::kMaxLog && files[1].from == install.size() - report::kMaxLog);
        CHECK(files[0].from == 0 && files[0].size == compress.size());
        CHECK(files[3].from == 0 && files[3].size == report::kMaxCrash);
    }

    // info.txt
    ConsoleFacts facts;
    facts.firmware = "19.0.1";
    facts.atmosphere = "1.8.0";
    facts.emummc = 1;
    facts.sigpatches = "sys-patch";
    facts.applet = true;
    facts.mem_total = 3ll << 30;
    facts.mem_used = 300ll << 20;
    facts.sd_free = 12ll << 30;
    facts.sd_total = 238ll << 30;
    facts.when = 1763400000;
    const std::string info = report::Info(facts, "0.9.0-test");
    const auto has = [&](const char* text) { return info.find(text) != std::string::npos; };
    CHECK(has("NXDock: 0.9.0-test\n") && has("Firmware: 19.0.1\n") && has("Atmosphere: 1.8.0\n") && has("emuMMC: yes\n"));
    CHECK(has(brand::kOfficial ? "Build: official\n" : "Build: unofficial\n"));
    CHECK(has("Sigpatches: sys-patch\n") && has("Mode: applet\n") && has("Date: 2025-11-17 17:20:00 UTC\n"));
    CHECK(has("Memory used: 300.0 MiB (314572800 bytes)\n") && has("Memory total: 3.00 GiB (3221225472 bytes)\n"));
    CHECK(has("SD card free: 12.00 GiB (12884901888 bytes)\n") && has("SD card size: 238.00 GiB"));
    const std::string unknown = report::Info(ConsoleFacts{}, "x");
    CHECK(unknown.find("Firmware: unknown\n") != std::string::npos && unknown.find("emuMMC: unknown\n") != std::string::npos &&
          unknown.find("Sigpatches: none found\n") != std::string::npos && unknown.find("Mode: application\n") != std::string::npos &&
          unknown.find("SD card free: unknown\n") != std::string::npos && unknown.find("Date: unknown\n") != std::string::npos);

    // the zip, deflated, read back
    const std::string path = root + "/report.zip";
    CHECK(report::WriteZip(path, info, files));
    struct stat st{};
    CHECK(stat(path.c_str(), &st) == 0 && (unsigned long long)st.st_size < report::kMaxLog / 4); // text packs well
    zip::Reader r;
    CHECK(r.Open(path));
    std::map<std::string, std::string> got;
    for (const auto& e : r.Entries()) {
        std::string out;
        const bool ok = r.Extract(e, [&](const void* d, size_t n) {
            out.append(static_cast<const char*>(d), n);
            return true;
        });
        CHECK(ok && e.method == 8);
        got[e.name] = out;
    }
    CHECK(r.Entries().size() == 1 + want.size() && !r.Entries().empty() && r.Entries()[0].name == "info.txt");
    CHECK(got["info.txt"] == info);
    CHECK(got["logs/install.log"] == install.substr(install.size() - report::kMaxLog));
    CHECK(got["logs/compress.log"] == compress && got["logs/history.tsv"] == history);
    const std::string newest = "01763400006_0100000000001000.log", oldest = "01763400002_0100000000001000.log";
    CHECK(got["crash_reports/" + newest] == crashes[newest].substr(0, report::kMaxCrash));
    CHECK(got["crash_reports/" + oldest] == crashes[oldest]);
    for (const auto& [name, text] : got) CHECK(text.find("master_key") == std::string::npos && text.find("secret") == std::string::npos);

    // what is taken out (#4): addresses, the names of games and of files; IDs, sizes and codes stay
    {
        using report::RedactAddresses;
        using report::RedactLine;
        const auto same = [](const std::string& line) { return RedactLine(line) == line; };
        // 1. URLs, hosts with a port, IPv4, IPv6, MAC, keys and tokens, pins
        CHECK(RedactAddresses("shop https://shop.example.net:8443/api/list?id=7 answered 0x0") == "shop <address> answered 0x0");
        CHECK(RedactAddresses("(see http://10.0.0.5/x).") == "(see <address>).");
        CHECK(RedactAddresses("ftp://user:pw@files.lan/a b") == "<address> b");
        CHECK(RedactAddresses("library at 192.168.1.20:3030, ok") == "library at <address>, ok");
        CHECK(RedactAddresses("from 10.1.2.3") == "from <address>");
        CHECK(RedactAddresses("nas:3030 and mybox.local and localhost:8080 and media.example.com") ==
              "<address> and <address> and <address> and <address>");
        CHECK(RedactAddresses("v6 fe80::1c2b:3cff:fe4d:5e6f%en0 and 2001:0db8:0000:0000:0000:ff00:0042:8329 and ::1") ==
              "v6 <address>%en0 and <address> and <address>");
        CHECK(RedactAddresses("wifi 3c:22:fb:10:aa:0e up") == "wifi <address> up");
        CHECK(RedactAddresses("GET /list?key=abc123&page=2 token=zzz Password=hunter") == "GET /list?<address>&page=2 <address> <address>");
        CHECK(RedactAddresses("Authorization: Bearer eyJhbGciOi.x.y") == "Authorization: <address>");
        CHECK(RedactAddresses("https <address>: sha256//AbCd+/Ef0123456789=") == "https <address>: <address>");
        // what is not an address stays as it was
        for (const char* keep : {"[   12.34] [NXDock] 0.9.0-beta.15: start 1 of this install",
                                 "2026-11-26 10:00:00 mods: deleted 0100A1B2C3D40000/romfs/a.bin",
                                 "[NXDock] 0100A1B2C3D40000: done, 120 MB saved, firmware 19.0.1, result 0x2A3",
                                 "Result: 2168-0002 at 17:20:00 UTC, X[00]: 0000007100004000 (main + 0x1234)",
                                 "[NXDock] prod.keys: read (for Compress games)", "[NXDock] nxdock::report sent, install.log 4 MiB",
                                 "[http] file: 12595257 bytes, 4 connections, pieces of 4194304",
                                 "[NXDock] check: 0100A1B2C3D40800 v65536 has a damaged or missing part"}) {
            CHECK(RedactAddresses(keep) == keep);
            CHECK(same(keep));
        }

        // 2. the lines of NXDock with the name of a game (made-up names)
        std::vector<std::string> names;
        CHECK(RedactLine("[    0.01] [NXDock] 0.9.0-beta.15: Star Lantern Odyssey (USA) (Glimmer Pack) nsz", &names) ==
              "[    0.01] [NXDock] 0.9.0-beta.15: <game> (<game>) nsz");
        CHECK(RedactLine("[    0.01] [NXDock] 0.9.0-beta.15: Mossy Caverns DX (Base game) ", &names) ==
              "[    0.01] [NXDock] 0.9.0-beta.15: <game> (<game>) ");
        CHECK(RedactLine("[    3.50] [NXDock] removed Mossy Caverns DX (Update v1.2.0): 0x0", &names) ==
              "[    3.50] [NXDock] removed <game> (<game>): 0x0");
        CHECK(RedactLine("=== 0100A1B2C3D40000 Pocket Lighthouse ===", &names) == "=== 0100A1B2C3D40000 <game> ===");
        CHECK(same("=== 0100A1B2C3D40000 merged with its update, after installing it ===") && same("=== the check of 3 games ==="));
        CHECK(RedactLine("0100A1B2C3D40000 Pocket Lighthouse: hidden, no program (program 0 bytes)", &names) ==
              "0100A1B2C3D40000 <game>: hidden, no program (program 0 bytes)");
        CHECK(same("[NXDock] 0100A1B2C3D40000: contents not listed (program 1/1, CNMT 0/1 of the game/update)"));
        CHECK(same("[NXDock] report: not sent (status 0, zip)") && same("[NXDock] 0100A1B2C3D40000: not compressed: Could not compress 2002-4058"));
        const std::vector<std::string> want_names = {"Star Lantern Odyssey (USA)", "Glimmer Pack", "Mossy Caverns DX", "Base game",
                                                     "Mossy Caverns DX", "Update v1.2.0", "Pocket Lighthouse", "Pocket Lighthouse"};
        CHECK(names == want_names);
        // the saves over MTP: the folder of the game and the file
        CHECK(RedactLine("[   40.00] [NXDock] MTP: saves: Pocket Lighthouse [0100A1B2C3D40000]/Player 1.zip refused: 0100A1B2C3D40000 is running") ==
              "[   40.00] [NXDock] MTP: saves: <game> [0100A1B2C3D40000]/<file> refused: 0100A1B2C3D40000 is running");
        CHECK(RedactLine("2026-11-26 10:00:00 saves: receiving Mossy Caverns DX/Slot (2).zip (40960 bytes)") ==
              "2026-11-26 10:00:00 saves: receiving <game>/<file> (40960 bytes)");
        // paths: the folders of NXDock and the IDs stay, other folders and the names of files go
        CHECK(RedactLine("[NCA] trying to read local ticket file: sdmc:/Games/Star Lantern (USA)/0100a1b2c3d40000000000000000000a.tik") ==
              "[NCA] trying to read local ticket file: sdmc:/<folder>/<folder>/0100a1b2c3d40000000000000000000a.tik");
        CHECK(RedactLine("[NXDock] HOME shortcut 0500A1B2C3D40000 installed for sdmc:/switch/Pocket Lighthouse.nro") ==
              "[NXDock] HOME shortcut 0500A1B2C3D40000 installed for sdmc:/switch/<file>.nro");
        CHECK(same("[NXDock] HOME shortcut 0500A1B2C3D40000 installed for sdmc:/switch/NXDock/NXDock.nro"));
        CHECK(same("[NXDock] prod.keys: not found in sdmc:/switch/") && same("written to sdmc:/atmosphere/config/system_settings.ini, done"));
        CHECK(RedactLine("copy ums0:/Backups/Mossy Caverns DX [0100A1B2C3D40000][v0].nsz to sdmc:/Inbox/x y.xci: ok") ==
              "copy ums0:/<folder>/<file>.nsz to sdmc:/<folder>/<file>.xci: ok");
        CHECK(RedactLine("from sdmc:/0100A1B2C3D40000[v0].nsp") == "from sdmc:/0100A1B2C3D40000[v0].nsp");

        // 3. the names of the history looked for in the logs too
        const std::string hist =
            "1\t1763400000\tsd\t1000\t1200\t2.0\tok\tnsz\tStar Lantern Odyssey\tGlimmer Pack\t\n"
            "1\t1763400100\tnand\t5000\t5000\t9.5\tfailed\tnsp\tLittle Ox\tv1.0.2 (65536)\tNot enough space\x1f" "2002-0021 at 10.0.0.9\n"
            "old line\twithout\tthe version, http://nas.lan/x\n";
        std::vector<std::string> hn;
        report::CollectNames(hist, report::Kind::History, hn);
        CHECK((hn == std::vector<std::string>{"Star Lantern Odyssey", "Glimmer Pack", "Little Ox", "v1.0.2 (65536)"}));
        CHECK(report::Redact(hist, report::Kind::History, hn) ==
              "1\t1763400000\tsd\t1000\t1200\t2.0\tok\tnsz\t<game>\t<game>\t\n"
              "1\t1763400100\tnand\t5000\t5000\t9.5\tfailed\tnsp\t<game>\t<game>\tNot enough space\x1f" "2002-0021 at <address>\n"
              "old line\twithout\tthe version, <address>\n");
        const std::string log = "[    1.00] [XCI] the Star Lantern Odyssey part\n[    1.10] Little Ox and Glimmer Pack, v1.0.2 (65536), Ox\n";
        CHECK(report::Redact(log, report::Kind::Log, hn) ==
              "[    1.00] [XCI] the <game> part\n[    1.10] <game> and <game>, v1.0.2 (65536), Ox\n");
        CHECK(report::RedactNames("a game named game", {"game", "ga", "Ox"}) == "a game named game"); // too short, or in <game>
        // a crash report: its addresses only
        CHECK(report::Redact("Atmosphere Crash Report (v4):\nStar Lantern Odyssey 10.0.0.9\n", report::Kind::Crash, hn) ==
              "Atmosphere Crash Report (v4):\nStar Lantern Odyssey <address>\n");
        CHECK(report::KindOf("logs/history.tsv") == report::Kind::History && report::KindOf("logs/install.log") == report::Kind::Log &&
              report::KindOf("crash_reports/01763400006_0100000000001000.log") == report::Kind::Crash);

        // and in the zip: the files on the card as they were, the zip without the names
        const std::string sd2 = root + "/sd2";
        std::system(("mkdir -p " + sd2 + "/switch/NXDock " + sd2 + "/atmosphere/crash_reports").c_str());
        const std::string ilog = "[    0.01] [NXDock] 0.9.0-beta.15: Pocket Lighthouse (Base game) nsp\n"
                                 "[    0.20] [NXDock] the library answered: 0x0, 40960 bytes from http://192.168.1.20:3030/install/x\n"
                                 "[    0.30] [NXDock] Pocket Lighthouse, Star Lantern Odyssey\n";
        Put(sd2 + "/switch/NXDock/install.log", ilog);
        Put(sd2 + "/switch/NXDock/history.tsv", hist);
        Put(sd2 + "/atmosphere/crash_reports/01763400000_0100A1B2C3D40000.log", "Crash at 10.0.0.9\n");
        const std::string zpath = root + "/clean.zip";
        CHECK(report::WriteZip(zpath, "info\n", report::Files(sd2)));
        zip::Reader z;
        CHECK(z.Open(zpath));
        std::map<std::string, std::string> in;
        for (const auto& e : z.Entries()) {
            std::string out;
            z.Extract(e, [&](const void* d, size_t n) {
                out.append(static_cast<const char*>(d), n);
                return true;
            });
            in[e.name] = out;
        }
        CHECK(in.size() == 4);
        CHECK(in["logs/install.log"] == "[    0.01] [NXDock] 0.9.0-beta.15: <game> (<game>) nsp\n"
                                        "[    0.20] [NXDock] the library answered: 0x0, 40960 bytes from <address>\n"
                                        "[    0.30] [NXDock] <game>, <game>\n");
        CHECK(in["crash_reports/01763400000_0100A1B2C3D40000.log"] == "Crash at <address>\n");
        for (const auto& [name, text] : in) {
            CHECK(text.find("Lighthouse") == std::string::npos && text.find("Lantern") == std::string::npos && text.find("Little Ox") == std::string::npos);
            CHECK(text.find("192.168") == std::string::npos && text.find("nas.lan") == std::string::npos);
        }
        FILE* kept = std::fopen((sd2 + "/switch/NXDock/install.log").c_str(), "rb");
        std::string card(ilog.size() + 10, '\0');
        card.resize(kept ? std::fread(card.data(), 1, card.size(), kept) : 0);
        if (kept) std::fclose(kept);
        CHECK(card == ilog); // the file on the card stays as it is
    }

    // the deflate of the writer: data that does not pack, an empty file, and one stored next to it
    {
        std::string noise(700000, '\0');
        unsigned x = 12345;
        for (auto& c : noise) {
            x = x * 1103515245u + 12345u;
            c = char(x >> 23);
        }
        const std::string zpath = root + "/mixed.zip";
        zip::Writer w;
        CHECK(w.Open(zpath));
        const auto read = [](const std::string& s) {
            return [p = &s](uint64_t off, void* buf, size_t n) {
                std::memcpy(buf, p->data() + off, n);
                return true;
            };
        };
        const std::string empty;
        CHECK(w.AddFile("noise.bin", noise.size(), read(noise), true));
        CHECK(w.AddFile("empty.txt", 0, read(empty), true));
        CHECK(w.AddFile("stored.txt", compress.size(), read(compress)));
        CHECK(w.Close());
        zip::Reader z;
        CHECK(z.Open(zpath) && z.Entries().size() == 3);
        if (z.Entries().size() == 3) {
            std::string a, b, c;
            const auto into = [](std::string& s) {
                return [p = &s](const void* d, size_t n) {
                    p->append(static_cast<const char*>(d), n);
                    return true;
                };
            };
            CHECK(z.Extract(z.Entries()[0], into(a)) && a == noise && z.Entries()[0].method == 8);
            CHECK(z.Extract(z.Entries()[1], into(b)) && b.empty());
            CHECK(z.Extract(z.Entries()[2], into(c)) && c == compress && z.Entries()[2].method == 0);
        }
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("report_test: all passed\n");
    return failures ? 1 : 0;
}
