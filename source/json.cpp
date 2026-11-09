// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The JSON NXDock reads: the flat answers of the library app. No dependencies, so it is
// tested on the computer (tools/test-json.sh).
#include "http.hpp"

#include <cstdio>
#include <cstdlib>

namespace nxdock::http {
namespace {

bool Space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Where the value of a field of the outer object starts, after its colon and spaces; npos when
// it is not there. A field of the same name inside a nested object is not taken for it.
size_t ValueAt(const std::string& json, const std::string& field) {
    int depth = 0;
    for (size_t p = 0; p < json.size(); p++) {
        const char c = json[p];
        if (c == '{' || c == '[') {
            depth++;
        } else if (c == '}' || c == ']') {
            depth--;
        } else if (c == '"') {
            // a string: a key of the outer object when a colon follows it
            const size_t start = p + 1;
            size_t end = start;
            while (end < json.size() && json[end] != '"') {
                end += json[end] == '\\' ? 2 : 1;
            }
            size_t q = end + 1;
            while (q < json.size() && Space(json[q])) q++;
            if (depth == 1 && q < json.size() && json[q] == ':' && json.compare(start, end - start, field) == 0 && end - start == field.size()) {
                q++;
                while (q < json.size() && Space(json[q])) q++;
                return q;
            }
            p = end;
        }
    }
    return std::string::npos;
}

void AppendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

} // namespace

std::string JsonString(const std::string& json, const std::string& field) {
    size_t p = ValueAt(json, field);
    if (p == std::string::npos || p >= json.size() || json[p] != '"') {
        return "";
    }
    std::string out;
    for (p++; p < json.size() && json[p] != '"'; p++) {
        if (json[p] != '\\' || p + 1 >= json.size()) {
            out += json[p];
            continue;
        }
        const char e = json[++p];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
                if (p + 4 < json.size()) {
                    AppendUtf8(out, unsigned(std::strtoul(json.substr(p + 1, 4).c_str(), nullptr, 16)));
                    p += 4;
                }
                break;
            default: out += e; break;
        }
    }
    return out;
}

long long JsonNumber(const std::string& json, const std::string& field, long long fallback) {
    const size_t p = ValueAt(json, field);
    if (p == std::string::npos || p >= json.size() || !(json[p] == '-' || (json[p] >= '0' && json[p] <= '9'))) {
        return fallback;
    }
    return std::strtoll(json.c_str() + p, nullptr, 10);
}

bool JsonBool(const std::string& json, const std::string& field, bool fallback) {
    const size_t p = ValueAt(json, field);
    if (p == std::string::npos) return fallback;
    if (json.compare(p, 4, "true") == 0) return true;
    if (json.compare(p, 5, "false") == 0) return false;
    return fallback;
}

std::string JsonQuote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

} // namespace nxdock::http
