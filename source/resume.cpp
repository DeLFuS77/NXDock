// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "resume.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nxdock {
namespace {

constexpr const char* kMagic = "NXDOCK-RESUME 1";

unsigned long long Fnv(const std::string& s) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string Escape(const std::string& v) {
    std::string out;
    for (char c : v) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string Unescape(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[++i];
            out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

void FormatJob(std::string& out, const char* head, const ResumeJob& job) {
    out += std::string(head) + "\n";
    for (const auto& [k, v] : job.fields) out += "f " + Escape(k) + " " + Escape(v) + "\n";
    for (const auto& p : job.parts) out += "p " + Escape(p) + "\n";
    out += "end-job\n";
}

std::string Slot(const std::string& dir, int n) { return dir + "/journal-" + std::to_string(n) + ".txt"; }

bool ReadFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool WriteFile(const std::string& path, const void* data, size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, size, f) == size;
    ok = std::fflush(f) == 0 && ok;
    fsync(fileno(f)); // to the card now: the console may go off at any moment
    return std::fclose(f) == 0 && ok;
}

} // namespace

std::string ResumeHex(const void* data, size_t size) {
    static const char* d = "0123456789ABCDEF";
    std::string out;
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i++) {
        out += d[p[i] >> 4];
        out += d[p[i] & 15];
    }
    return out;
}

bool ResumeUnhex(const std::string& hex, void* out, size_t size) {
    if (hex.size() != size * 2) return false;
    auto* p = static_cast<unsigned char*>(out);
    for (size_t i = 0; i < size; i++) {
        int v = 0;
        for (int k = 0; k < 2; k++) {
            const char c = hex[i * 2 + k];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        p[i] = (unsigned char)v;
    }
    return true;
}

std::string FormatResume(const ResumeState& state) {
    std::string out = std::string(kMagic) + "\nseq " + std::to_string(state.seq) + "\n";
    FormatJob(out, "job", state.job);
    for (const auto& w : state.waiting) FormatJob(out, "wait", w);
    for (const auto& p : state.parts) {
        out += "part " + p.content_id + " " + p.placeholder + " " + std::to_string(p.size) + " " + (p.checked ? "1" : "0") + " " +
               (p.sd ? "sd" : "nand") + "\n";
    }
    char end[40];
    std::snprintf(end, sizeof(end), "end %016llX\n", Fnv(out));
    return out + end;
}

bool ParseResume(const std::string& text, ResumeState& state) {
    // the checksum at the end first: anything not whole is refused
    const size_t end = text.rfind("end ");
    if (end == std::string::npos || text.size() != end + 4 + 16 + 1 || text.back() != '\n') return false;
    char want[20];
    std::snprintf(want, sizeof(want), "%016llX", Fnv(text.substr(0, end)));
    if (text.compare(end + 4, 16, want) != 0) return false;
    ResumeState s;
    size_t pos = 0;
    ResumeJob* job = nullptr;
    bool magic = false, have_job = false;
    while (pos < end) {
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos || nl > end) return false;
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!magic) {
            if (line != kMagic) return false;
            magic = true;
        } else if (line.rfind("seq ", 0) == 0) {
            s.seq = std::strtoull(line.c_str() + 4, nullptr, 10);
        } else if (line == "job") {
            job = &s.job;
            have_job = true;
        } else if (line == "wait") {
            s.waiting.emplace_back();
            job = &s.waiting.back();
        } else if (line == "end-job") {
            job = nullptr;
        } else if (line.rfind("f ", 0) == 0 && job) {
            const size_t sp = line.find(' ', 2);
            if (sp == std::string::npos) return false;
            job->fields[Unescape(line.substr(2, sp - 2))] = Unescape(line.substr(sp + 1));
        } else if (line.rfind("p ", 0) == 0 && job) {
            job->parts.push_back(Unescape(line.substr(2)));
        } else if (line.rfind("part ", 0) == 0) {
            char cid[40] = {}, ph[40] = {}, where[8] = {};
            long long size = 0;
            int checked = 0;
            if (std::sscanf(line.c_str() + 5, "%39s %39s %lld %d %7s", cid, ph, &size, &checked, where) != 5) return false;
            ResumePart p;
            p.content_id = cid;
            p.placeholder = ph;
            p.size = size;
            p.checked = checked != 0;
            p.sd = std::strcmp(where, "sd") == 0;
            unsigned char probe[16];
            if (!ResumeUnhex(p.content_id, probe, 16) || !ResumeUnhex(p.placeholder, probe, 16)) return false;
            s.parts.push_back(p);
        } else {
            return false;
        }
    }
    if (!magic || !have_job) return false;
    state = std::move(s);
    return true;
}

bool SaveResume(const std::string& dir, ResumeState& state) {
    mkdir(dir.c_str(), 0777);
    state.seq++;
    const std::string text = FormatResume(state);
    return WriteFile(Slot(dir, int(state.seq % 2)), text.data(), text.size());
}

bool LoadResume(const std::string& dir, ResumeState& state) {
    bool found = false;
    for (int n = 0; n < 2; n++) {
        std::string text;
        ResumeState s;
        if (!ReadFile(Slot(dir, n), text) || !ParseResume(text, s)) continue;
        if (!found || s.seq > state.seq) {
            state = std::move(s);
            found = true;
        }
    }
    return found;
}

void ClearResume(const std::string& dir) {
    if (DIR* d = opendir(dir.c_str())) {
        std::vector<std::string> names;
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') names.push_back(e->d_name);
        }
        closedir(d);
        for (const auto& n : names) std::remove((dir + "/" + n).c_str());
    }
    rmdir(dir.c_str());
}

bool SaveResumeHeader(const std::string& dir, const std::string& content_id, const void* data, size_t size) {
    mkdir(dir.c_str(), 0777);
    return WriteFile(dir + "/" + content_id + ".hdr", data, size);
}

bool LoadResumeHeader(const std::string& dir, const std::string& content_id, void* data, size_t size) {
    std::string text;
    if (!ReadFile(dir + "/" + content_id + ".hdr", text) || text.size() != size) return false;
    std::memcpy(data, text.data(), size);
    return true;
}

void ResumeNote(ResumeState& state, const std::string& content_id, const std::string& placeholder, long long size, bool checked, bool sd) {
    ResumePart& p = ResumePartOf(state, content_id);
    p.placeholder = placeholder;
    p.size = size;
    p.checked = checked;
    p.sd = sd;
}

bool ResumeLookup(const ResumeState& state, const std::string& content_id, bool sd, std::string& placeholder) {
    for (const auto& p : state.parts) {
        if (p.content_id == content_id && p.checked && p.sd == sd) {
            placeholder = p.placeholder;
            return true;
        }
    }
    return false;
}

std::vector<ResumePart> ResumeTakeCut(ResumeState& state) {
    std::vector<ResumePart> cut, kept;
    for (auto& p : state.parts) (p.checked ? kept : cut).push_back(p);
    state.parts = std::move(kept);
    return cut;
}

ResumePart& ResumePartOf(ResumeState& state, const std::string& content_id) {
    for (auto& p : state.parts) {
        if (p.content_id == content_id) return p;
    }
    state.parts.push_back({});
    state.parts.back().content_id = content_id;
    return state.parts.back();
}

int ResumeChecked(const ResumeState& state, long long* bytes) {
    int n = 0;
    long long b = 0;
    for (const auto& p : state.parts) {
        if (!p.checked) continue;
        n++;
        b += p.size;
    }
    if (bytes) *bytes = b;
    return n;
}

} // namespace nxdock
