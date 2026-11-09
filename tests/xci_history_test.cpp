// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// On the computer: the files of the secure partition of a game card image (XCI/XCZ) as the
// install core reads them, which container a file is, and the install history file.
#include "container_kind.hpp"
#include "history.hpp"
#include "yati/container/xci.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" void log_write(const char*, ...) {}
extern "C" void log_write_arg(const char*, va_list*) {}

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

struct MemorySource final : sphaira::yati::source::Base {
    std::vector<unsigned char> data;
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override {
        *bytes_read = 0;
        if (off < 0 || off > s64(data.size())) return 1;
        const s64 n = std::min<s64>(size, s64(data.size()) - off);
        std::memcpy(buf, data.data() + off, size_t(n));
        *bytes_read = u64(n);
        return 0;
    }
};

void Put32(std::vector<unsigned char>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void Put64(std::vector<unsigned char>& b, size_t at, uint64_t v) { std::memcpy(b.data() + at, &v, 8); }

struct File {
    std::string name;
    std::vector<unsigned char> data;
};

// An HFS0: header, file table, names, then the files one after another.
std::vector<unsigned char> Hfs0(const std::vector<File>& files) {
    std::string names;
    std::vector<uint32_t> name_at;
    for (const auto& f : files) {
        name_at.push_back(uint32_t(names.size()));
        names += f.name;
        names.push_back('\0');
    }
    while (names.size() % 16) names.push_back('\0');
    const size_t table = 16 + files.size() * 0x40;
    std::vector<unsigned char> out(table + names.size());
    Put32(out, 0, 0x30534648); // HFS0
    Put32(out, 4, uint32_t(files.size()));
    Put32(out, 8, uint32_t(names.size()));
    uint64_t data_off = 0;
    for (size_t i = 0; i < files.size(); i++) {
        const size_t e = 16 + i * 0x40;
        Put64(out, e, data_off);
        Put64(out, e + 8, files[i].data.size());
        Put32(out, e + 16, name_at[i]);
        data_off += files[i].data.size();
    }
    std::memcpy(out.data() + table, names.data(), names.size());
    for (const auto& f : files) out.insert(out.end(), f.data.begin(), f.data.end());
    return out;
}

std::vector<unsigned char> Bytes(size_t n, unsigned char seed) {
    std::vector<unsigned char> b(n);
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)(seed + i * 7);
    return b;
}

std::vector<unsigned char> MakeXci(bool key_area) {
    const std::vector<File> secure_files = {
        {"0123456789abcdef0123456789abcdef.nca", Bytes(5000, 1)},
        {"fedcba9876543210fedcba9876543210.cnmt.ncz", Bytes(700, 2)},
    };
    const auto update = Hfs0({{"system.nca", Bytes(300, 3)}});
    const auto secure = Hfs0(secure_files);
    const auto root = Hfs0({{"update", update}, {"secure", secure}});
    const size_t base = key_area ? 0x1000 : 0;
    std::vector<unsigned char> xci(base + 0xF000, 0);
    std::memcpy(xci.data() + base + 0x100, "HEAD", 4);
    xci.insert(xci.end(), root.begin(), root.end());
    return xci;
}

void TestXci(bool key_area) {
    MemorySource src;
    src.data = MakeXci(key_area);
    const char* label = key_area ? "XCI with key area" : "XCI";
    std::string what;

    what = std::string(label) + ": detected as a game card image";
    Check(nxdock::DetectContainer(src.data.data(), std::min(src.data.size(), nxdock::kContainerHeadSize)) == nxdock::ContainerKind::Xci, what.c_str());

    sphaira::yati::container::Xci xci(&src);
    sphaira::yati::container::Collections c;
    const Result rc = xci.GetCollections(c);
    what = std::string(label) + ": secure partition read";
    Check(rc == 0 && c.size() == 2, what.c_str());
    if (rc != 0 || c.size() != 2) return;
    what = std::string(label) + ": names and sizes of its files";
    Check(c[0].name == "0123456789abcdef0123456789abcdef.nca" && c[0].size == 5000 && c[1].name.ends_with(".cnmt.ncz") && c[1].size == 700, what.c_str());
    // the offsets point at the bytes of each file
    const auto expect0 = Bytes(5000, 1), expect1 = Bytes(700, 2);
    what = std::string(label) + ": offsets point at the files";
    Check(!std::memcmp(src.data.data() + c[0].offset, expect0.data(), 5000) && !std::memcmp(src.data.data() + c[1].offset, expect1.data(), 700), what.c_str());
}

void TestDetect() {
    std::vector<unsigned char> nsp(0x2000, 0);
    std::memcpy(nsp.data(), "PFS0", 4);
    Check(nxdock::DetectContainer(nsp.data(), nsp.size()) == nxdock::ContainerKind::Nsp, "PFS0 detected as NSP/NSZ");
    std::vector<unsigned char> junk(0x2000, 0x41);
    Check(nxdock::DetectContainer(junk.data(), junk.size()) == nxdock::ContainerKind::Unknown, "anything else is unknown");
    Check(nxdock::DetectContainer(nsp.data(), 3) == nxdock::ContainerKind::Unknown, "too short is unknown");
}

void TestHistory(const char* dir) {
    const std::string path = std::string(dir) + "/history.tsv";
    std::remove(path.c_str());
    nxdock::History h(path);
    Check(h.Load().empty(), "history: empty without a file");

    nxdock::HistoryEntry e;
    e.when = 1762790400;
    e.game = "A game\twith a tab";
    e.label = "Update v65536";
    e.format = "xcz";
    e.nand = true;
    e.size = 3ULL * 1024 * 1024 * 1024;
    e.written = 4ULL * 1024 * 1024 * 1024;
    e.seconds = 240.5;
    e.result = nxdock::HistoryEntry::Result::Installed;
    Check(h.Append(e), "history: append");
    e.result = nxdock::HistoryEntry::Result::Failed;
    e.error = "The connection with your library was lost.\nNothing was installed.";
    e.nand = false;
    Check(h.Append(e), "history: append a failed one");

    const auto all = h.Load();
    Check(all.size() == 2, "history: two entries");
    if (all.size() == 2) {
        Check(all[0].result == nxdock::HistoryEntry::Result::Failed && !all[0].nand, "history: the newest first");
        Check(all[1].game == "A game with a tab" && all[1].label == "Update v65536" && all[1].format == "xcz", "history: texts, a tab made a space");
        Check(all[1].nand && all[1].size == 3ULL * 1024 * 1024 * 1024 && all[1].written == 4ULL * 1024 * 1024 * 1024, "history: storage and sizes");
        Check(all[1].when == 1762790400 && all[1].seconds > 240.4 && all[1].seconds < 240.6, "history: date and time taken");
        Check(all[0].error.find('\n') == std::string::npos && all[0].error.find("lost.") != std::string::npos, "history: error kept on one line");
        const double speed = all[1].Speed();
        Check(speed > 12.7 && speed < 12.8, "history: speed in MB/s");
    }

    // kept to the newest kKeep
    for (size_t i = 0; i < nxdock::History::kKeep + 5; i++) {
        e.when = int64_t(i);
        h.Append(e);
    }
    const auto kept = h.Load();
    Check(kept.size() == nxdock::History::kKeep, "history: kept to the newest entries");
    Check(!kept.empty() && kept.front().when == int64_t(nxdock::History::kKeep + 4), "history: the newest stays");

    nxdock::HistoryEntry bad;
    Check(!nxdock::History::Parse("not a line", bad) && !nxdock::History::Parse("2\t1\tsd\t1\t1\t1\tok\tnsp\ta\tb\t", bad), "history: other lines are skipped");
    h.Clear();
    Check(h.Load().empty(), "history: cleared");
}

} // namespace

int main(int argc, char** argv) {
    TestXci(false);
    TestXci(true);
    TestDetect();
    TestHistory(argc > 1 ? argv[1] : "/tmp");
    std::printf("%s\n", g_failed ? "FAILED" : "all passed");
    return g_failed ? 1 : 0;
}
