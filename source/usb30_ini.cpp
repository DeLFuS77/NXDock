// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "usb30_ini.hpp"
#include "ascii.hpp"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace nxdock {

namespace {

constexpr const char* kKey = "usb30_force_enabled";

// A line of the file and what ends it ("\r\n", "\n" or nothing for the last one).
struct Line {
    std::string text, end;
};

std::vector<Line> Split(const std::string& ini) {
    std::vector<Line> out;
    size_t at = 0;
    while (at < ini.size()) {
        const size_t nl = ini.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back({ini.substr(at), ""});
            break;
        }
        size_t stop = nl;
        if (stop > at && ini[stop - 1] == '\r') stop--;
        out.push_back({ini.substr(at, stop - at), ini.substr(stop, nl + 1 - stop)});
        at = nl + 1;
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

// The name of the section a line opens ("usb" for "[usb]"), or empty when it opens none.
std::string SectionOf(const std::string& line) {
    const std::string t = Trim(line);
    if (t.size() < 2 || t.front() != '[') return {};
    const size_t close = t.find(']');
    if (close == std::string::npos) return {};
    return AsciiLower(Trim(t.substr(1, close - 1)));
}

// The line sets the key: 0 no, 1 yes, 2 yes but commented out (";" or "#"). `value` is what
// follows the "=".
int KeyLine(const std::string& line, std::string& value) {
    std::string t = Trim(line);
    bool commented = false;
    while (!t.empty() && (t.front() == ';' || t.front() == '#')) {
        commented = true;
        t = Trim(t.substr(1));
    }
    const std::string key = kKey;
    if (AsciiLower(t.substr(0, key.size())) != key) return 0;
    const std::string rest = Trim(t.substr(key.size()));
    if (rest.empty() || rest.front() != '=') return 0;
    value = Trim(rest.substr(1));
    return commented ? 2 : 1;
}

// "u8!0x1" and the like: true when the number is not 0.
bool ValueOn(std::string v) {
    const size_t semi = v.find(';');
    if (semi != std::string::npos) v = Trim(v.substr(0, semi));
    const size_t bang = v.find('!');
    if (bang != std::string::npos) v = v.substr(bang + 1);
    return std::strtoul(v.c_str(), nullptr, 0) != 0;
}

std::string Setting(bool on) { return std::string(kKey) + " = " + (on ? "u8!0x1" : "u8!0x0"); }

} // namespace

int Usb30InIni(const std::string& ini) {
    int found = -1;
    bool in_usb = false;
    for (const auto& l : Split(ini)) {
        const std::string section = SectionOf(l.text);
        if (!section.empty()) {
            in_usb = section == "usb";
            continue;
        }
        std::string value;
        if (in_usb && KeyLine(l.text, value) == 1) found = ValueOn(value) ? 1 : 0; // the last one counts
    }
    return found;
}

std::string SetUsb30InIni(const std::string& ini, bool on) {
    auto lines = Split(ini);
    const std::string nl = ini.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    // where [usb] is, and its lines that set the key
    int header = -1, first_commented = -1;
    std::vector<int> set_lines;
    bool in_usb = false;
    for (int i = 0; i < int(lines.size()); i++) {
        const std::string section = SectionOf(lines[size_t(i)].text);
        if (!section.empty()) {
            in_usb = section == "usb";
            if (in_usb && header < 0) header = i;
            continue;
        }
        if (!in_usb) continue;
        std::string value;
        const int k = KeyLine(lines[size_t(i)].text, value);
        if (k == 1) set_lines.push_back(i);
        else if (k == 2 && first_commented < 0) first_commented = i;
    }
    if (set_lines.empty() && first_commented >= 0) set_lines.push_back(first_commented);
    if (!set_lines.empty()) {
        // changed where they are, keeping what was before the key (its indent)
        for (const int i : set_lines) {
            std::string& text = lines[size_t(i)].text;
            size_t indent = 0;
            while (indent < text.size() && (text[indent] == ' ' || text[indent] == '\t')) indent++;
            text = text.substr(0, indent) + Setting(on);
        }
    } else if (header >= 0) {
        // right under [usb]
        Line& h = lines[size_t(header)];
        if (h.end.empty()) h.end = nl;
        lines.insert(lines.begin() + header + 1, Line{Setting(on), nl});
    } else {
        // a section of its own at the end, after a line of its own
        if (!lines.empty() && lines.back().end.empty()) lines.back().end = nl;
        lines.push_back({"[usb]", nl});
        lines.push_back({Setting(on), nl});
    }
    std::string out;
    for (const auto& l : lines) out += l.text + l.end;
    return out;
}

} // namespace nxdock
