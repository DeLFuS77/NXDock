// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The first read that failed, for compress.log: the layer that failed first writes what it was
// (Note), the layers it was read through add where (Add), all in one line; taken once the merge
// gives up (Take). A read that fails is retried by nothing, so one line is enough.
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace nxdock::readnote {

inline std::mutex g_mutex;
inline std::string g_line;
inline bool g_open = true; // a first note can still be written

inline void Note(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (!g_open) return;
    char text[256];
    va_list v;
    va_start(v, fmt);
    std::vsnprintf(text, sizeof(text), fmt, v);
    va_end(v);
    g_line = text;
    g_open = false;
}

inline void Add(const char* fmt, ...) {
    std::lock_guard lock(g_mutex);
    if (g_open || g_line.size() > 600) return;
    char text[200];
    va_list v;
    va_start(v, fmt);
    std::vsnprintf(text, sizeof(text), fmt, v);
    va_end(v);
    g_line += "; ";
    g_line += text;
}

// the line, and a new start
inline std::string Take() {
    std::lock_guard lock(g_mutex);
    std::string line = std::move(g_line);
    g_line.clear();
    g_open = true;
    return line;
}

} // namespace nxdock::readnote
