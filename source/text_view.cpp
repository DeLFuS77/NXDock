// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "text_view.hpp"

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

namespace nxdock {

namespace {

bool Continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// The bytes of the first `chars` characters of s, from byte `from`.
size_t BytesOf(const std::string& s, size_t from, size_t chars) {
    size_t at = from;
    while (at < s.size() && chars > 0) {
        at++;
        while (at < s.size() && Continuation((unsigned char)s[at])) at++;
        chars--;
    }
    return at - from;
}

} // namespace

bool IsTextName(const std::string& name) {
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return false;
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return ext == "txt" || ext == "log" || ext == "ini" || ext == "json" || ext == "cfg" || ext == "md";
}

size_t Utf8Length(const std::string& s) {
    size_t n = 0;
    for (const char c : s) n += !Continuation((unsigned char)c);
    return n;
}

TextFile TextOf(const std::string& bytes, bool cut) {
    TextFile out;
    out.ok = true;
    out.cut = cut;
    out.size = bytes.size();
    size_t end = bytes.size();
    if (cut) {
        // a character cut by the cap is left out
        size_t back = end;
        while (back > 0 && Continuation((unsigned char)bytes[back - 1])) back--;
        if (back > 0) {
            const unsigned char lead = (unsigned char)bytes[back - 1];
            const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (end - (back - 1) < need) end = back - 1;
        }
    }
    std::string line;
    size_t i = 0;
    // a byte order mark at the start is not shown
    if (end >= 3 && (unsigned char)bytes[0] == 0xEF && (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF) i = 3;
    for (; i < end; i++) {
        const unsigned char c = (unsigned char)bytes[i];
        if (c == '\r') {
            out.lines.push_back(line);
            line.clear();
            if (i + 1 < end && bytes[i + 1] == '\n') i++;
        } else if (c == '\n') {
            out.lines.push_back(line);
            line.clear();
        } else if (c == '\t') {
            line += "    ";
        } else if (c < 0x20 || c == 0x7F) {
            line += "\xC2\xB7";
        } else {
            line += char(c);
        }
    }
    if (!line.empty() || out.lines.empty()) out.lines.push_back(line);
    return out;
}

TextFile ReadText(const std::string& path, size_t cap) {
    TextFile out;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    const unsigned long long size = (unsigned long long)st.st_size;
    std::string bytes(size_t(std::min<unsigned long long>(size, cap)), '\0');
    size_t got = 0;
    while (got < bytes.size()) {
        const size_t n = std::fread(&bytes[got], 1, bytes.size() - got, f);
        if (n == 0) break;
        got += n;
    }
    std::fclose(f);
    bytes.resize(got);
    out = TextOf(bytes, size > got);
    out.size = size;
    return out;
}

std::vector<TextRow> TextRows(const std::vector<std::string>& lines, int cols, bool wrap) {
    std::vector<TextRow> rows;
    cols = std::max(cols, 8);
    for (size_t l = 0; l < lines.size(); l++) {
        const std::string& s = lines[l];
        if (!wrap || Utf8Length(s) <= size_t(cols)) {
            rows.push_back({int(l), s, true});
            continue;
        }
        size_t at = 0;
        bool first = true;
        while (at < s.size()) {
            size_t take = BytesOf(s, at, size_t(cols));
            if (at + take < s.size()) {
                // at the last space, when it is not too near the start of the row
                const size_t space = s.rfind(' ', at + take - 1);
                if (space != std::string::npos && space > at && Utf8Length(s.substr(at, space - at)) >= size_t(cols) / 3) {
                    take = space - at + 1;
                }
            }
            rows.push_back({int(l), s.substr(at, take), first});
            first = false;
            at += take;
        }
    }
    return rows;
}

} // namespace nxdock
