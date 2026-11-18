// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The report for the library app on an SD card made up here: only the logs of NXDock, its history
// and the newest crash reports go in (never keys, configuration or anything else), a long log
// keeps its end and a long crash report its start, info.txt says what it should, and the zip,
// deflated, reads back byte for byte.
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
