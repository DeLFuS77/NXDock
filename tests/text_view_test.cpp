// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// Groups 3 and 4: the text viewer of the explorer (20A): the names it opens, the lines of a file,
// the cap of what it reads, and the lines wrapped.
#include "text_view.hpp"

#include <cstdio>
#include <string>

using namespace nxdock;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

static bool Write(const std::string& path, const std::string& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    const std::string tmp = argc > 1 ? argv[1] : "/tmp";
    Check(IsTextName("install.log") && IsTextName("CONFIG.INI") && IsTextName("a.Json") && IsTextName("x.cfg") && IsTextName("README.md") &&
              IsTextName("notes.txt"),
          "the names it opens");
    Check(!IsTextName("game.nsp") && !IsTextName("app.nro") && !IsTextName("txt") && !IsTextName(".txt") && !IsTextName("a.txt.zip"),
          "and not the others");

    // the lines: LF, CR LF and CR; tabs; a byte order mark; control characters
    auto t = TextOf("\xEF\xBB\xBF" "one\r\ntwo\rthree\n\tfour\x01", false);
    Check(t.ok && t.lines.size() == 4, "four lines");
    Check(t.lines[0] == "one" && t.lines[1] == "two" && t.lines[2] == "three", "every kind of end of line");
    Check(t.lines[3] == "    four\xC2\xB7", "a tab as four spaces, a control character as a dot");
    Check(TextOf("", false).lines.size() == 1, "an empty file has one empty line");
    Check(TextOf("a\n", false).lines.size() == 1, "the last end of line makes no line of its own");

    // the cap: a character cut by it is left out
    t = TextOf("ab\xC3", true);
    Check(t.cut && t.lines.size() == 1 && t.lines[0] == "ab", "half a character at the cut goes");
    t = TextOf("ab\xC3\xA9", true);
    Check(t.lines[0] == "ab\xC3\xA9", "a whole character at the cut stays");
    const std::string path = tmp + "/nxdock_text_view_test.log";
    std::string big;
    for (int i = 0; i < 2000; i++) big += "line " + std::to_string(i) + "\n";
    Check(Write(path, big), "the file is written");
    t = ReadText(path, 100);
    Check(t.ok && t.cut && t.size == big.size(), "a file over the cap is cut and says its size");
    Check(t.lines.size() >= 10 && t.lines.size() <= 15 && t.lines[0] == "line 0", "only its beginning is read");
    t = ReadText(path);
    Check(t.ok && !t.cut && t.lines.size() == 2000 && t.lines[1999] == "line 1999", "under the cap, all of it");
    std::remove(path.c_str());
    Check(!ReadText(tmp + "/nxdock_text_view_missing.txt").ok, "a file that is not there");
    Check(!ReadText(tmp).ok, "a folder is not a text");

    // the rows
    const std::vector<std::string> lines = {"short", "the quick brown fox jumps over the lazy dog", "abcdefghijklmnopqrstuvwxyz", ""};
    auto rows = TextRows(lines, 12, false);
    Check(rows.size() == 4 && rows[1].text == lines[1], "without wrapping, a row each");
    rows = TextRows(lines, 12, true);
    Check(rows[0].text == "short" && rows[0].first && rows[0].line == 0, "a short line as it is");
    Check(rows[1].text == "the quick " && rows[1].first && !rows[2].first && rows[2].line == 1, "a long line at its spaces");
    std::string again;
    int pieces = 0;
    for (const auto& r : rows) {
        if (r.line == 1) {
            again += r.text;
            pieces++;
            Check(Utf8Length(r.text) <= 12, "no row is longer than the width");
        }
    }
    Check(again == lines[1] && pieces == 4, "the line whole again from its rows");
    int alpha = 0;
    for (const auto& r : rows) alpha += r.line == 2;
    Check(alpha == 3, "a word longer than the width is cut in it");
    Check(rows.back().line == 3 && rows.back().text.empty(), "an empty line keeps its row");
    rows = TextRows({"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"}, 8, true);
    Check(rows.size() == 2 && Utf8Length(rows[0].text) == 8 && Utf8Length(rows[1].text) == 2, "wrapped by characters, not bytes");
    Check(Utf8Length("a\xC3\xA9\xE3\x81\x82") == 3, "the length in characters");

    if (failed) return 1;
    std::printf("text_view_test: ok\n");
    return 0;
}
