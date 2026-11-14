// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the screens of the app (app*.cpp) share: sizes and times in words, the layout of the
// library grid, the rows of Settings, text boxes, the error card, the fraction of a job.
#pragma once

#include "app.hpp"

#include "file_match.hpp"
#include "perf.hpp"
#include "draw.hpp"
#include "errors.hpp"
#include "mtp.hpp"
#include "usb_disks.hpp"
#include "usb_tinfoil.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "mods.hpp"
#include "resume.hpp"
#include "http.hpp"
#include <yyjson.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <ctime>
#include <string>
#include "i18n.hpp"
#include "net.hpp"
#include "uninstall.hpp"
#include "merged_updates.hpp"

namespace nxdock::appui {

// Text of a size: 6.8 GB, 450 MB.
inline std::string SizeText(u64 bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) {
        std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
    } else {
        std::snprintf(text, sizeof(text), "%.0f MB", std::max(1.0, double(bytes) / double(1ull << 20)));
    }
    return text;
}

inline std::string Upper(std::string text) {
    for (auto& c : text) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return text;
}

// The format shown on a cover: that of the game, or of what there is.
inline std::string FormatOf(const Game& game) {
    const CatalogFile* base = game.Base();
    if (base) return Upper(base->format);
    return game.files.empty() ? "" : Upper(game.files.front().format);
}

// Library grid layout: the size of the covers is chosen in Options (large, medium, small), so
// these follow it (GridSize, once per frame).
constexpr float kGridX = 36, kGridTop = 150, kGridGap = 20;

inline int g_columns = 7;

inline float g_cover = (kWidth - 2 * kGridX - kGridGap * 6) / 7;

inline float g_row_pitch = g_cover + kGridGap;

inline float g_view_h = 2 * g_row_pitch - kGridGap; // the rows in sight, whole

inline float g_az_height = g_view_h + 12;

// The A-Z bar on the right edge: as tall as the grid, 48 px wide for a finger.
constexpr float kAzTop = kGridTop - 6;

// The A-Z bar on the right edge of the covers: 26 px drawn, 48 px for a finger.
constexpr float kAzW = 26;

inline float g_az_x = 1251;

// panel: the game in focus is shown in a panel on the right (2B), the covers keep the rest
inline void GridSize(int size, bool panel = false) {
    static constexpr int kCols[] = {7, 9, 12}, kRows[] = {2, 3, 4};
    const int i = std::clamp(size, 0, 2);
    if (panel) {
        // two columns less, as many rows as fit down to the hint bar
        const float width = 800;
        g_columns = kCols[i] - 2;
        g_cover = (width - kGridGap * float(g_columns - 1)) / float(g_columns);
        g_row_pitch = g_cover + kGridGap;
        const int rows = std::max(1, int((500 + kGridGap) / g_row_pitch));
        g_view_h = float(rows) * g_row_pitch - kGridGap;
        g_az_x = kGridX + width + 12;
    } else {
        g_columns = kCols[i];
        g_cover = (kWidth - 2 * kGridX - kGridGap * float(g_columns - 1)) / float(g_columns);
        g_row_pitch = g_cover + kGridGap;
        g_view_h = float(kRows[i]) * g_row_pitch - kGridGap;
        g_az_x = 1251;
    }
    g_az_height = g_view_h + 12;
}

// how far the library is pulled down before letting go reads it again (21A)
constexpr float kPullArm = 64;

inline double Now() {
    return armTicksToNs(armGetSystemTick()) / 1e9;
}

inline void Panel(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::Glass(vg, t, x, y, w, h, r); // glass, with a soft shadow (12A)
}

// Text over several lines inside a width.
inline void TextBox(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, w, str, nullptr);
}

// The console's keyboard. A secret text is hidden while typed and never given as initial text.
inline bool AskText(const char* guide, const std::string& initial, bool secret, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, 255);
    if (secret) {
        swkbdConfigSetPasswordFlag(&kbd, 1);
    } else {
        swkbdConfigSetInitialText(&kbd, initial.c_str());
    }
    char text[256] = {};
    const Result rc = swkbdShow(&kbd, text, sizeof(text));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !text[0]) {
        return false;
    }
    out = text;
    return true;
}

// Settings, in groups: the library, installing, the saves, notices, the look, the wizard, the HOME
// menu, connecting by hand.
enum SettingRow {
    kRowLibrary, kRowForget, kRowShare, kRowUpdates, kRowVersionsList, kRowSpeed,
    kRowDest, kRowPick, kRowScreen, kRowCompress,
    kRowSaveCopy, kRowSaveAuto, // a group of their own (S1 A of 14/11)
    kRowFinish, kRowBattery, kRowNotifyDone,
    kRowLanguage, kRowTheme, kRowStyle, kRowSounds, kRowAnim, kRowFocusView, kRowTouchRumble,
    kRowWizard,
    kRowSelfUpdate, kRowSelfNotice, kRowSelfBack,
    kRowHome, kRowHomeRemove,
    kRowAddress, kRowKey,
    kSettingRows
};

// When something happened, in the console's time zone: "Today 21:52", "Yesterday 19:05",
// "08/11 22:14".
inline std::string WhenText(long long when) {
    if (when <= 0) {
        return "—";
    }
    TimeCalendarTime at{}, now{};
    TimeCalendarAdditionalInfo info{};
    if (R_FAILED(timeToCalendarTimeWithMyRule(u64(when), &at, &info)) ||
        R_FAILED(timeToCalendarTimeWithMyRule(u64(std::time(nullptr)), &now, &info))) {
        return "—";
    }
    char text[48];
    const auto& s = Tr();
    // yesterday: the day before today, also across a month
    TimeCalendarTime yesterday{};
    timeToCalendarTimeWithMyRule(u64(std::time(nullptr) - 86400), &yesterday, &info);
    if (at.year == now.year && at.month == now.month && at.day == now.day) {
        std::snprintf(text, sizeof(text), "%s %02u:%02u", s.today, at.hour, at.minute);
    } else if (at.year == yesterday.year && at.month == yesterday.month && at.day == yesterday.day) {
        std::snprintf(text, sizeof(text), "%s %02u:%02u", s.yesterday, at.hour, at.minute);
    } else {
        std::snprintf(text, sizeof(text), "%02u/%02u %02u:%02u", at.day, at.month, at.hour, at.minute);
    }
    return text;
}

// The height of text over several lines inside a width.
inline float TextBoxHeight(NVGcontext* vg, float w, float size, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    float b[4];
    nvgTextBoxBounds(vg, 0, 0, w, str, nullptr, b);
    return b[3] - b[1];
}

// The title of a packed error (errors.hpp), or the whole line of an older one.
inline std::string ErrorTitle(const std::string& packed) {
    const ErrorText e = UnpackError(packed);
    return e.title.empty() ? e.what : e.title;
}

// The error card (1A): the title in red, what happened, what to do, and the code, small.
// Returns its height; draws nothing when `draw` is false.
inline float ErrorCard(NVGcontext* vg, const Theme& t, float x, float y, float w, const std::string& packed, bool draw) {
    const ErrorText e = UnpackError(packed);
    const More& m = TrM();
    const float pad = 18, iw = w - 2 * pad;
    float h = pad;
    const float title_y = y + h + 12;
    h += e.title.empty() ? 0 : 34;
    const float what_h = e.what.empty() ? 0 : TextBoxHeight(vg, iw, 16, e.what.c_str());
    const float what_y = y + h;
    h += e.what.empty() ? 0 : 18 + what_h + 10;
    const float todo_h = e.todo.empty() ? 0 : TextBoxHeight(vg, iw, 16, e.todo.c_str());
    const float todo_y = y + h;
    h += e.todo.empty() ? 0 : 18 + todo_h + 10;
    const float code_y = y + h + 8;
    h += e.code.empty() ? 0 : 22;
    h += pad - 6;
    if (!draw) {
        return h;
    }
    ui::FillRound(vg, x, y, w, h, 14, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, 14, t.line, 1);
    if (!e.title.empty()) {
        nvgBeginPath(vg);
        nvgCircle(vg, x + pad + 10, title_y, 10);
        nvgFillColor(vg, t.err);
        nvgFill(vg);
        ui::Text(vg, x + pad + 10, title_y + 1, 15, t.panel, "\xC3\x97", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgSave(vg);
        nvgScissor(vg, x + pad + 28, title_y - 16, iw - 28, 32);
        ui::Text(vg, x + pad + 30, title_y, 19, t.err, e.title.c_str());
        nvgRestore(vg);
    }
    if (!e.what.empty()) {
        ui::Text(vg, x + pad, what_y + 8, 13, t.muted, Upper(m.what_happened).c_str());
        TextBox(vg, x + pad, what_y + 18, iw, 16, t.text, e.what.c_str());
    }
    if (!e.todo.empty()) {
        ui::Text(vg, x + pad, todo_y + 8, 13, t.muted, Upper(m.what_to_do).c_str());
        TextBox(vg, x + pad, todo_y + 18, iw, 16, t.text, e.todo.c_str());
    }
    if (!e.code.empty()) {
        char line[96];
        std::snprintf(line, sizeof(line), m.code_line, e.code.c_str());
        ui::Text(vg, x + pad, code_y, 13, t.muted, line);
    }
    return h;
}

// What the internal memory keeps free besides an install.
constexpr u64 kNandMargin = 64ull * 1024 * 1024;

inline std::string Duration(double seconds) {
    char text[32];
    const int sec = int(seconds + 0.5);
    if (sec >= 3600) {
        std::snprintf(text, sizeof(text), "%d h %02d min", sec / 3600, (sec / 60) % 60);
    } else if (sec >= 60) {
        std::snprintf(text, sizeof(text), "%d min %02d s", sec / 60, sec % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d s", sec);
    }
    return text;
}

// Of what the install writes: how far, from the live counters.
// The merge after an install (25G): measuring, then compressing and writing, then the check.
inline float MergeFraction(const InstallQueue::Live& l) {
    const float part = l.merge_total ? std::clamp(float(double(l.merge_done) / double(l.merge_total)), 0.f, 1.f) : 0.f;
    return l.merge_phase <= 1 ? 0.45f * part : l.merge_phase == 2 ? 0.45f + 0.45f * part : 0.9f + 0.1f * part;
}

inline float JobFraction(const InstallQueue::Live& l) {
    if (l.merging) return MergeFraction(l);
    if (l.phase >= 3) return 1.f;
    if (l.total == 0) return 0.f;
    return std::clamp(float(double(l.written) / double(l.total)), 0.f, 1.f);
}

inline bool Undoable(const SessionItem& item) {
    return item.state == SessionItem::State::Installed || item.state == SessionItem::State::Failed;
}

// A time for a column: 8:52, 1:02:10.
inline std::string ShortDuration(double seconds) {
    char text[24];
    const int sec = int(seconds + 0.5);
    if (sec >= 3600) {
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d:%02d", sec / 60, sec % 60);
    }
    return text;
}

inline const char* WhereText(bool nand) {
    return nand ? Tr().internal : "SD";
}

inline std::string SpeedText(double mbs) {
    if (mbs <= 0) {
        return "—";
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%.1f MB/s", mbs);
    return text;
}

inline const char* ResultText(HistoryEntry::Result r) {
    const auto& s = Tr();
    switch (r) {
        case HistoryEntry::Result::Installed: return s.res_installed;
        case HistoryEntry::Result::Failed: return s.failed;
        case HistoryEntry::Result::Cancelled: return s.cancelled;
        case HistoryEntry::Result::Removed: return s.removed;
    }
    return "";
}

inline NVGcolor ResultColor(const Theme& t, HistoryEntry::Result r) {
    switch (r) {
        case HistoryEntry::Result::Installed: return t.ok;
        case HistoryEntry::Result::Failed: return t.err;
        default: return t.muted;
    }
}

} // namespace nxdock::appui
