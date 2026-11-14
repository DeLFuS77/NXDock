// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The text viewer of the file list (20A of the visual test of groups 3 and 4): a text file read
// up to a cap, its lines, and its lines wrapped to a width in characters. Nothing is written.
// Plain C++, tested on the computer.
#pragma once

#include <string>
#include <vector>

namespace nxdock {

// What the viewer reads at most: a larger file shows its beginning, and says so.
inline constexpr size_t kTextCap = 4u << 20;

// The names it opens: .txt, .log, .ini, .json, .cfg and .md, in any case.
bool IsTextName(const std::string& name);

struct TextFile {
    bool ok = false;               // it could be read
    bool cut = false;              // longer than the cap: only its beginning is there
    unsigned long long size = 0;   // the size of the file
    std::vector<std::string> lines;
};
// Reads at most `cap` bytes. CR LF and CR end lines as LF does, a tab is four spaces, other
// control characters show as a middle dot, and a character cut by the cap is left out.
TextFile ReadText(const std::string& path, size_t cap = kTextCap);
// The same from bytes already read (`cut`: the bytes are the beginning of something longer).
TextFile TextOf(const std::string& bytes, bool cut);

// The characters of a UTF-8 text (not its bytes).
size_t Utf8Length(const std::string& s);

// A row on screen: the line it belongs to (from 0) and its text; `first` for the first row of
// its line, which shows its number.
struct TextRow {
    int line = 0;
    std::string text;
    bool first = true;
};
// The rows of the lines: one each, or wrapped at `cols` characters (at the last space when there
// is one far enough in, else in the middle of a word).
std::vector<TextRow> TextRows(const std::vector<std::string>& lines, int cols, bool wrap);

} // namespace nxdock
