// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "report.hpp"

#include "zip.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef __SWITCH__
#include <switch.h>
#include "http.hpp"
#endif

namespace nxdock::report {
namespace {

bool EndsWithNoCase(const std::string& s, const char* end) {
    const size_t n = std::strlen(end);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)end[i])) return false;
    }
    return true;
}

struct Found {
    std::string name;
    unsigned long long size = 0;
    long long mtime = 0;
};

// The plain files right in `dir` whose name ends with `ext`; no folder below it is opened.
std::vector<Found> List(const std::string& dir, const char* ext) {
    std::vector<Found> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || !EndsWithNoCase(name, ext)) continue;
        struct stat st{};
        if (stat((dir + "/" + name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        out.push_back({name, (unsigned long long)st.st_size, (long long)st.st_mtime});
    }
    closedir(d);
    return out;
}

std::string Bytes(long long b) {
    if (b < 0) return "unknown";
    char t[64];
    if (b >= (1ll << 30)) std::snprintf(t, sizeof(t), "%.2f GiB (%lld bytes)", double(b) / double(1ll << 30), b);
    else std::snprintf(t, sizeof(t), "%.1f MiB (%lld bytes)", double(b) / double(1ll << 20), b);
    return t;
}

} // namespace

std::vector<File> Files(const std::string& sd) {
    std::vector<File> out;
    const std::string own = sd + "/switch/NXDock";
    std::vector<Found> logs = List(own, ".log");
    std::sort(logs.begin(), logs.end(), [](const Found& a, const Found& b) { return a.name < b.name; });
    struct stat st{};
    if (stat((own + "/history.tsv").c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        logs.push_back({"history.tsv", (unsigned long long)st.st_size, (long long)st.st_mtime});
    }
    for (const auto& f : logs) {
        const unsigned long long take = std::min(f.size, kMaxLog);
        out.push_back({own + "/" + f.name, "logs/" + f.name, f.size - take, take});
    }
    const std::string crashes = sd + "/atmosphere/crash_reports";
    std::vector<Found> reports = List(crashes, ".log");
    // the newest first: by the time the card keeps, then by name (Atmosphère begins it with the time)
    std::sort(reports.begin(), reports.end(), [](const Found& a, const Found& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.name > b.name;
    });
    if (reports.size() > size_t(kCrashReports)) reports.resize(size_t(kCrashReports));
    for (const auto& f : reports) {
        out.push_back({crashes + "/" + f.name, "crash_reports/" + f.name, 0, std::min(f.size, kMaxCrash)});
    }
    return out;
}

std::string Info(const ConsoleFacts& f, const std::string& version) {
    std::string out;
    const auto line = [&out](const char* label, const std::string& value) {
        out += label;
        out += ": ";
        out += value;
        out += "\n";
    };
    line("NXDock", version);
    line("Firmware", f.firmware.empty() ? "unknown" : f.firmware);
    line("Atmosphere", f.atmosphere.empty() ? "unknown" : f.atmosphere);
    line("emuMMC", f.emummc == 1 ? "yes" : f.emummc == 0 ? "no" : "unknown");
    line("Sigpatches", f.sigpatches.empty() ? "none found" : f.sigpatches);
    line("Mode", f.applet ? "applet" : "application");
    line("Memory used", Bytes(f.mem_used));
    line("Memory total", Bytes(f.mem_total));
    line("SD card free", Bytes(f.sd_free));
    line("SD card size", Bytes(f.sd_total));
    std::string date = "unknown";
    if (f.when > 0) {
        const std::time_t t = std::time_t(f.when);
        std::tm tm{};
        char text[40];
        if (gmtime_r(&t, &tm) && std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", &tm)) date = text;
    }
    line("Date", date);
    return out;
}

bool WriteZip(const std::string& path, const std::string& info, const std::vector<File>& files) {
    zip::Writer w;
    if (!w.Open(path)) return false;
    const auto from = [](const std::string& data) {
        return [p = &data](uint64_t off, void* buf, size_t n) {
            std::memcpy(buf, p->data() + off, n);
            return true;
        };
    };
    bool ok = w.AddFile("info.txt", info.size(), from(info), true);
    for (const auto& f : files) {
        if (!ok) break;
        // read whole first: a log written meanwhile gives the bytes it had, never a broken entry
        FILE* in = std::fopen(f.path.c_str(), "rb");
        if (!in) continue; // gone since it was listed
        std::string data(size_t(f.size), '\0');
        size_t got = 0;
        if (fseeko(in, off_t(f.from), SEEK_SET) == 0) got = std::fread(data.data(), 1, data.size(), in);
        std::fclose(in);
        data.resize(got);
        ok = w.AddFile(f.name, data.size(), from(data), true);
    }
    return w.Close() && ok;
}

#ifdef __SWITCH__

Result Send(const std::string& url, const std::string& key, const ConsoleFacts& facts) {
    Result r;
    if (url.empty() || key.empty()) {
        r.state = Result::State::NoLibrary;
        return r;
    }
    mkdir("sdmc:/switch/NXDock", 0777);
    mkdir("sdmc:/switch/NXDock/tmp", 0777);
    const std::string path = "sdmc:/switch/NXDock/tmp/report.zip";
    struct stat st{};
    if (!WriteZip(path, Info(facts, NXDOCK_VERSION), Files("sdmc:")) || stat(path.c_str(), &st) != 0) {
        std::remove(path.c_str());
        r.error = "zip";
        return r;
    }
    r.bytes = (unsigned long long)st.st_size;
    if (r.bytes > kMaxZip) {
        std::remove(path.c_str());
        r.state = Result::State::TooLarge;
        return r;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::remove(path.c_str());
        r.error = "zip";
        return r;
    }
    const auto answer = http::Send("POST", url + "/install/report", key, r.bytes,
                                   [f](char* buf, size_t max) { return long(std::fread(buf, 1, max, f)); }, {}, "application/zip");
    std::fclose(f);
    std::remove(path.c_str());
    if (answer.status >= 200 && answer.status < 300) {
        r.state = Result::State::Sent;
    } else {
        r.status = answer.status;
        r.error = answer.error;
    }
    return r;
}

#endif

} // namespace nxdock::report
