// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "app.hpp"
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

namespace nxdock {
namespace {

// Text of a size: 6.8 GB, 450 MB.
std::string SizeText(u64 bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) {
        std::snprintf(text, sizeof(text), "%.1f GB", double(bytes) / double(1ull << 30));
    } else {
        std::snprintf(text, sizeof(text), "%.0f MB", std::max(1.0, double(bytes) / double(1ull << 20)));
    }
    return text;
}

// The firmware a title needs, from its required system version: 15.0.0.
std::string FirmwareText(u32 version) {
    if (version == 0) {
        return "";
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%u.%u.%u", version >> 26, (version >> 20) & 0x3F, (version >> 16) & 0xF);
    return text;
}

} // namespace

// The first letter of a name (one UTF-8 character), for a cover not loaded yet.
std::string Initial(const std::string& name) {
    if (name.empty()) {
        return "?";
    }
    size_t len = 1;
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    return name.substr(0, std::min(len, name.size()));
}

std::string VersionText(u32 version) {
    const More& m = TrM();
    if (version == 0) return m.ver_none;
    char text[96];
    // the number of the update (version 196608 is the third); an odd one is shown as it is
    std::snprintf(text, sizeof(text), m.ver_update_fmt, (version >> 16) ? version >> 16 : version);
    return text;
}

// A colour for the placeholder of a cover, the same for a game every time.
unsigned PlaceholderColor(const std::string& key) {
    static constexpr unsigned kColors[] = {0xc8743a, 0x2e7d6b, 0x6a4ae0, 0x3a6fc8, 0xb8406e, 0x8a8f2a, 0x2a8fb0,
                                           0x9a3a3a, 0x4a7a3a, 0x7a4ab0, 0x2f8a8a, 0x8a5a3a};
    return kColors[std::hash<std::string>{}(key) % (sizeof(kColors) / sizeof(kColors[0]))];
}

namespace {

std::string Lower(std::string text) {
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return text;
}

std::string Upper(std::string text) {
    for (auto& c : text) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return text;
}

// Where a name goes in the A-Z bar: 0 for what starts with no Latin letter (digits, signs, other
// scripts), 1-26 for A-Z; an accented Latin letter goes with its letter.
int LetterSlot(const std::string& name) {
    if (name.empty()) {
        return 0;
    }
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c == 0xC3 && name.size() > 1) {
        const unsigned char d = static_cast<unsigned char>(name[1]) & ~0x20; // lower case goes with upper case
        const auto slot = [](char letter) { return letter - 'A' + 1; };
        if (d >= 0x80 && d <= 0x86) return slot('A');
        if (d == 0x87) return slot('C');
        if (d >= 0x88 && d <= 0x8B) return slot('E');
        if (d >= 0x8C && d <= 0x8F) return slot('I');
        if (d == 0x90) return slot('D');
        if (d == 0x91) return slot('N');
        if ((d >= 0x92 && d <= 0x96) || d == 0x98) return slot('O');
        if (d >= 0x99 && d <= 0x9C) return slot('U');
        if (d == 0x9D) return slot('Y');
    }
    return 0;
}

// The format shown on a cover: that of the game, or of what there is.
std::string FormatOf(const Game& game) {
    const CatalogFile* base = game.Base();
    if (base) return Upper(base->format);
    return game.files.empty() ? "" : Upper(game.files.front().format);
}

// The orders of the library, in Options: A-Z, size, not installed first, recent.
constexpr int kSorts = 4;
// The filters of the library: all, not installed, installed, with an update, with DLC.
constexpr int kFilters = 5;

// Library grid layout: the size of the covers is chosen in Options (large, medium, small), so
// these follow it (GridSize, once per frame).
constexpr float kGridX = 36, kGridTop = 150, kGridGap = 20;
int g_columns = 7;
float g_cover = (kWidth - 2 * kGridX - kGridGap * 6) / 7;
float g_row_pitch = g_cover + kGridGap;
float g_view_h = 2 * g_row_pitch - kGridGap; // the rows in sight, whole
float g_az_height = g_view_h + 12;
constexpr float kGridMargin = 10;                      // room for the focus glow, less than the gap
// The A-Z bar on the right edge: as tall as the grid, 48 px wide for a finger.
constexpr float kAzTop = kGridTop - 6;

// The A-Z bar on the right edge of the covers: 26 px drawn, 48 px for a finger.
constexpr float kAzW = 26;
float g_az_x = 1251;
// The panel of the game in focus (2B), on the right of the covers.
constexpr float kPanelX = 884, kPanelW = kWidth - kPanelX - 36;

// panel: the game in focus is shown in a panel on the right (2B), the covers keep the rest
void GridSize(int size, bool panel = false) {
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

// Touch: how far a finger moves before a tap turns into a drag.
constexpr float kDragSlop = 14;

double Now() {
    return armTicksToNs(armGetSystemTick()) / 1e9;
}

void Background(NVGcontext* vg, const Theme& t) {
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, t.bg);
    nvgFill(vg);
}

void Panel(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::Glass(vg, t, x, y, w, h, r); // glass, with a soft shadow (12A)
}

// Text over several lines inside a width.
void TextBox(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, color);
    nvgTextBox(vg, x, y, w, str, nullptr);
}

// The console's keyboard. A secret text is hidden while typed and never given as initial text.
bool AskText(const char* guide, const std::string& initial, bool secret, std::string& out) {
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

// Settings, in groups: the library, installing, notices, the look, the wizard, the HOME menu,
// connecting by hand.
enum SettingRow {
    kRowLibrary, kRowForget, kRowShare, kRowUpdates, kRowVersionsList, kRowSpeed,
    kRowDest, kRowPick, kRowSaveCopy, kRowSaveAuto, kRowScreen, kRowCompress,
    kRowFinish, kRowBattery, kRowNotifyDone,
    kRowLanguage, kRowTheme, kRowStyle, kRowSounds, kRowAnim, kRowFocusView, kRowTouchRumble,
    kRowWizard,
    kRowSelfUpdate, kRowSelfNotice, kRowSelfBack,
    kRowHome, kRowHomeRemove,
    kRowAddress, kRowKey,
    kSettingRows
};
// the first row of each group
constexpr int kGroupStarts[] = {kRowLibrary, kRowDest, kRowFinish, kRowLanguage, kRowWizard, kRowSelfUpdate, kRowHome, kRowAddress};
constexpr int kGroups = int(std::size(kGroupStarts));
// the levels of the notice of little battery; 0 is off
constexpr int kBatterySteps[] = {0, 10, 15, 20, 30};

// When something happened, in the console's time zone: "Today 21:52", "Yesterday 19:05",
// "08/11 22:14".
std::string WhenText(long long when) {
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
float TextBoxHeight(NVGcontext* vg, float w, float size, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    float b[4];
    nvgTextBoxBounds(vg, 0, 0, w, str, nullptr, b);
    return b[3] - b[1];
}

// The title of a packed error (errors.hpp), or the whole line of an older one.
std::string ErrorTitle(const std::string& packed) {
    const ErrorText e = UnpackError(packed);
    return e.title.empty() ? e.what : e.title;
}

// The error card (1A): the title in red, what happened, what to do, and the code, small.
// Returns its height; draws nothing when `draw` is false.
float ErrorCard(NVGcontext* vg, const Theme& t, float x, float y, float w, const std::string& packed, bool draw) {
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

} // namespace

float CompactError(NVGcontext* vg, const Theme& t, float x, float y, float w, const std::string& packed, NVGcolor color, const char* todo) {
    const ErrorText e = UnpackError(packed);
    const std::string head = e.title.empty() ? e.what : e.title;
    const std::string next = e.todo.empty() ? std::string(todo ? todo : "") : e.todo;
    float h = 0;
    if (!head.empty()) {
        TextBox(vg, x, y, w, 15, color, head.c_str());
        h += TextBoxHeight(vg, w, 15, head.c_str()) + 2;
    }
    if (!next.empty()) {
        TextBox(vg, x, y + h, w, 14, t.muted, next.c_str());
        h += TextBoxHeight(vg, w, 14, next.c_str()) + 2;
    }
    if (!e.code.empty()) {
        char line[96];
        std::snprintf(line, sizeof(line), TrM().code_line, e.code.c_str());
        ui::Text(vg, x, y + h + 7, 12, t.muted, line);
        h += 16;
    }
    return h;
}

namespace {

std::string Clip(std::string text, size_t max) {
    if (text.size() > max) {
        text = text.substr(0, max - 1) + "…";
    }
    return text;
}

} // namespace

bool App::Init() {
    perf::Mark("main");
    InitStrings();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
    net::Init();
    perf::Mark("net ready");
    const Config config = Config::Load();
    m_theme = config.light ? LightTheme() : DarkTheme();
    if (!m_gfx.Init()) {
        return false;
    }
    perf::Mark("graphics and fonts ready");
    // remembered: connects by itself; else looks for the app on the network
    m_link.Start(config);
    m_prefs = InstallPrefs::Load();
    m_ui = UiPrefs::Load();
    ui::SetMotion(m_ui.anim);
    if (m_ui.language >= 0) {
        UseLanguage(m_ui.language); // chosen in the wizard; else the console's
    }
    if (m_ui.theme_auto) {
        // the theme follows the console's
        ColorSetId color = ColorSetId_Light;
        if (R_SUCCEEDED(setsysInitialize())) {
            if (R_FAILED(setsysGetColorSetId(&color))) color = ColorSetId_Light;
            setsysExit();
        }
        m_theme = color == ColorSetId_Dark ? DarkTheme() : LightTheme();
    }
    m_theme = StyleTheme(m_ui.style, !m_theme.dark);
    m_psm = R_SUCCEEDED(psmInitialize());
    m_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    perf::Mark("settings and services");
    m_sound.Start();
    perf::Mark("sound");
    InitRumble();
    perf::Mark("rumble");
    m_queue = std::make_unique<InstallQueue>();
    m_queue->SetOptions(m_prefs.connections, m_prefs.download_test);
    m_report = std::make_unique<QueueReport>();
    // cheats and mods that an update would break (19A): the builds of both versions, from the
    // library app (its database, or the file of the update), against what is on the SD card
    m_queue->SetModsCheck([this](u64 app, u32 from, u32 to) -> std::string {
        const Config c = m_link.CurrentConfig();
        if (!c.Linked()) return {};
        const auto build = [&c, app](u32 version) -> std::string {
            char hex[24];
            std::snprintf(hex, sizeof(hex), "%016llX", (unsigned long long)app);
            const auto r = http::Get(c.url + "/install/cheats/" + hex + "?version=" + std::to_string(version), c.key, 15000);
            if (r.status != 200 && r.status != 404) return {};
            yyjson_doc* doc = yyjson_read(r.body.data(), r.body.size(), 0);
            if (!doc) return {};
            const char* b = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "build_id"));
            std::string out = b ? b : "";
            yyjson_doc_free(doc);
            return out;
        };
        const std::string to_build = build(to);
        if (to_build.empty()) return {};
        const auto risk = ModsAtRiskFor("sdmc:/atmosphere", app, build(from), to_build);
        if (!risk.Any()) return {};
        const More& m = TrM();
        std::string what;
        char line[200];
        if (risk.cheats) {
            std::snprintf(line, sizeof(line), m.mr_cheats_fmt, risk.cheats);
            what += std::string("\xE2\x80\xA2 ") + line + "\n";
        }
        if (risk.patches) {
            std::snprintf(line, sizeof(line), m.mr_patches_fmt, risk.patches);
            what += std::string("\xE2\x80\xA2 ") + line + "\n";
        }
        if (risk.exefs) what += std::string("\xE2\x80\xA2 ") + m.mr_exefs + "\n";
        return what;
    });
    // the saves to the library before an update, a repair or a removal (18A), when it is on and
    // a library is linked; on the queue's thread
    m_queue->SetSaveCopy([this](u64 app, int why, u32 to, u32 current, std::function<void(u64, u64)> progress) -> std::string {
        if (!m_ui.save_copy) return {};
        const Config c = m_link.CurrentConfig();
        if (!c.Linked()) return {};
        const More& m = TrM();
        char note[160];
        switch (why) {
            case 0: std::snprintf(note, sizeof(note), m.sc_note_update, VersionText(to).c_str()); break;
            case 1: std::snprintf(note, sizeof(note), "%s", m.sc_note_repair); break;
            case 3: std::snprintf(note, sizeof(note), m.sc_note_back, VersionText(to).c_str()); break;
            default: std::snprintf(note, sizeof(note), "%s", m.sc_note_remove); break;
        }
        return CopySavesBefore(c.url, c.key, app, current, note, progress);
    });
    perf::Mark("install queue");
    m_push = std::make_unique<Push>();
    m_safety = std::make_unique<Safety>();
    m_sysinfo = ReadSystemInfo();
    perf::Mark("push, safety, system info");
    m_time = Now();
    m_last_input = m_time;
    m_home = forwarder::Query();
    perf::Mark("HOME shortcut looked at");
    if (!m_ui.wizard_done) {
        OpenWizard(); // only the first time; repeated from Settings
    } else if (net::IsAppletMode() && !m_home.installed && m_home.nro_found) {
        Notice(TrM().hm_applet_tip);
        m_notice_until = m_time + 8;
    }
    if (m_ui.wizard_done && !ui::Still()) {
        // the opening (4A): the icon assembles itself, then the covers come in
        m_boot_at = m_time;
        m_wave_at = m_time + (ui::Lively() ? 1.25 : 0.5);
    }
    return true;
}

void App::AskHomeShortcut(bool remove) {
    m_home = forwarder::Query();
    const More& m = TrM();
    if (m_home.running_from) {
        Notice(m.hm_in_use_note);
        return;
    }
    if (remove && !m_home.installed) {
        return;
    }
    if (!remove && !m_home.nro_found) {
        Notice(m.hm_no_nro);
        return;
    }
    m_dialog = Dialog{};
    m_dialog.kind = remove ? DialogKind::HomeRemove : DialogKind::HomeAdd;
}

void App::DoHomeShortcut(bool remove) {
    const More& m = TrM();
    const bool was = m_home.installed;
    const Result rc = remove ? forwarder::Remove() : forwarder::Install();
    m_home = forwarder::Query();
    if (R_SUCCEEDED(rc)) {
        NoticeOk(remove ? m.hm_done_remove : was ? m.hm_done_update : m.hm_done_add);
        return;
    }
    if (rc == forwarder::kResultNoNro) {
        Notice(m.hm_no_nro);
    } else if (rc == forwarder::kResultRunning) {
        Notice(m.hm_in_use_note);
    } else {
        char text[200];
        std::snprintf(text, sizeof(text), m.hm_failed_fmt, unsigned(rc));
        Notice(text);
    }
}

void App::Exit() {
    // the game in focus, to find it there the next time
    const std::string focused = FocusedKey();
    if (!focused.empty() && focused != m_ui.focus) {
        m_ui.focus = focused;
        m_ui.Save();
    }
    // the backlight on again before leaving
    ScreenOn();
    if (m_space_thread.joinable()) m_space_thread.join();
    if (m_lbl) lblExit();
    // a check or a download of a new version is let go
    m_updater.reset();
    // a copy, a move or a delete of the file list stops first (what was done stays done)
    if (m_fwork) {
        m_fwork->cancel = true;
        if (m_fwork_thread.joinable()) m_fwork_thread.join();
        m_fwork.reset();
    }
    // what arrives from the computer stops first, so a job reading it ends
    MtpStop();
    UsbLink().Cancel();
    // a job running is cancelled and cleaned up before the network goes
    m_queue.reset();
    UsbLink().Stop();
    UsbDisksStop();
    m_push.reset();
    m_sound.Stop();
    m_speed.Cancel();
    m_link.Stop();
    m_covers.Stop();
    m_covers.Free(m_gfx.vg());
    m_gfx.Exit();
    if (m_psm) {
        psmExit();
    }
    if (m_nifm) {
        nifmExit();
    }
    net::Exit();
}

void App::CheckBattery() {
    if (!m_psm || m_time - m_battery_checked < 5) {
        return;
    }
    m_battery_checked = m_time;
    u32 percent = 0;
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
        m_battery = int(std::min(percent, 100u));
    }
    if (R_SUCCEEDED(psmGetChargerType(&charger))) {
        m_charging = charger != PsmChargerType_Unconnected;
    }
}

// A cover still on its way (as chosen, A): a small blue circle turning at its corner, over the placeholder.
int App::CoverOf(const Game& g) {
    const int image = m_covers.Get(g.cover);
    if (image || !g.installed.base || !g.title_id || !m_covers.Missing(g.cover)) return image;
    // the library has no cover for it (or not yet one it could fetch): the icon the game has on
    // the console, rather than only its first letter
    char path[24];
    std::snprintf(path, sizeof(path), "ns:%016llX", static_cast<unsigned long long>(g.title_id));
    return m_covers.Get(path);
}

void App::CoverLoading(NVGcontext* vg, float x, float y, float size, const std::string& path) {
    if (size < 30 || !m_covers.Loading(path)) return;
    const float r = std::clamp(size * 0.06f, 5.f, 11.f);
    const float m = std::max(5.f, size * 0.05f);
    const float cx = x + size - m - r - 3, cy = y + size - m - r - 3;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r + 4);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.35f));
    nvgFill(vg);
    ui::Spinner(vg, cx, cy, r, m_theme.work, m_time);
}

void App::CheckWifi() {
    if (!m_nifm || m_time - m_wifi_checked < 3) {
        return;
    }
    m_wifi_checked = m_time;
    NifmInternetConnectionType type{};
    u32 strength = 0;
    NifmInternetConnectionStatus status{};
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
        // not connected to any network
        m_wifi = -1;
        m_wifi_ssid.clear();
        m_wifi_ip = 0;
        return;
    }
    m_wifi_cable = type == NifmInternetConnectionType_Ethernet;
    m_wifi = m_wifi_cable ? 3 : int(std::min(strength, 3u));
    m_wifi_ssid.clear();
    if (!m_wifi_cable) {
        auto profile = std::make_unique<NifmNetworkProfileData>();
        if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(profile.get()))) {
            const auto& w = profile->wireless_setting_data;
            m_wifi_ssid.assign(w.ssid, strnlen(w.ssid, std::min<size_t>(w.ssid_len, sizeof(w.ssid))));
        }
    }
    u32 ip = 0;
    m_wifi_ip = R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) ? ip : 0;
}

void App::DrawWifiCard(NVGcontext* vg) {
    if (!m_wifi_open || m_wifi < -1) {
        return;
    }
    const Theme& t = m_theme;
    const auto& m = TrM();
    const float w = 330, y = 80;
    const float x = std::clamp(m_wifi_rect.x + m_wifi_rect.w / 2 - w / 2, 16.f, float(kWidth) - 16 - w);
    struct Row {
        const char* label;
        std::string value;
    };
    std::vector<Row> rows;
    const char* words[] = {m.wf_l0, m.wf_l1, m.wf_l2, m.wf_l3};
    if (m_wifi >= 0) {
        if (!m_wifi_cable) {
            rows.push_back({m.wf_network, m_wifi_ssid.empty() ? std::string("-") : m_wifi_ssid});
            char level[96];
            std::snprintf(level, sizeof(level), m.wf_level_fmt, words[std::clamp(m_wifi, 0, 3)], m_wifi);
            rows.push_back({m.wf_coverage, level});
        }
        if (m_wifi_ip) {
            char ip[24];
            std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", m_wifi_ip & 0xFF, (m_wifi_ip >> 8) & 0xFF, (m_wifi_ip >> 16) & 0xFF,
                          m_wifi_ip >> 24);
            rows.push_back({m.wf_console, ip});
        }
        const Config c = m_link.CurrentConfig();
        if (!c.url.empty()) {
            const size_t scheme = c.url.find("://");
            rows.push_back({m.wf_library, c.url.substr(scheme == std::string::npos ? 0 : scheme + 3)});
        }
    }
    const bool weak = m_wifi == 1 && !m_wifi_cable;
    const float h = m_wifi < 0 ? 110 : 72 + 30 * float(rows.size()) + (weak ? 46 : 0);
    m_wifi_card = {x, y, w, h, 0};
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 6, w, h, 14);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 6, w, h, 14, 24, nvgRGBAf(0, 0, 0, 0.35f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::FillRound(vg, x, y, w, h, 14, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, 14, t.line, 1);
    const char* title = m_wifi < 0 ? m.wf_none : m_wifi_cable ? m.wf_cable : m.wf_title;
    ui::Text(vg, x + 18, y + 26, 18, t.text, title);
    if (m_wifi < 0) {
        TextBox(vg, x + 18, y + 46, w - 36, 15, t.muted, m.wf_none_hint);
        return;
    }
    float ry = y + 44;
    if (!m_wifi_cable) {
        // the coverage as a bar, in the colour of the fan
        const std::array<float, 4> pct{0.08f, 0.35f, 0.68f, 0.96f};
        const NVGcolor lit = m_wifi <= 0 ? t.muted : m_wifi == 1 ? t.warn : t.work;
        ui::FillRound(vg, x + 18, ry, w - 36, 6, 3, t.panel2);
        ui::FillRound(vg, x + 18, ry, (w - 36) * pct[size_t(std::clamp(m_wifi, 0, 3))], 6, 3, lit);
        ry += 22;
    } else {
        ry += 8;
    }
    for (const auto& r : rows) {
        ui::Text(vg, x + 18, ry + 10, 15, t.muted, r.label);
        nvgSave(vg);
        nvgScissor(vg, x + 120, ry - 4, w - 138, 28);
        ui::Text(vg, x + w - 18, ry + 10, 15, t.text, r.value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        ry += 30;
    }
    if (weak) {
        TextBox(vg, x + 18, ry + 2, w - 36, 14, t.warn, m.wf_weak);
    }
}

void App::ActivateSetting(int row) {
    const auto& s = Tr();
    const Config current = m_link.CurrentConfig();
    switch (row) {
        case kRowTheme:
            // light, dark, as the console
            ApplyTheme(((m_ui.theme_auto ? 2 : m_theme.dark ? 1 : 0) + 1) % 3);
            break;
        case kRowStyle:
            // the next style, its wave from its card
            SetStyle((m_ui.style + 1) % kStyles, -1, -1);
            break;
        case kRowLanguage:
            // the console's, then each language NXDock speaks
            m_ui.language = (m_ui.language + 2) % (LanguageCount() + 1) - 1;
            UseLanguage(m_ui.language);
            m_ui.Save();
            break;
        case kRowSounds:
            m_ui.sounds = !m_ui.sounds;
            m_ui.Save();
            break;
        case kRowWizard:
            OpenWizard();
            break;
        case kRowSelfUpdate:
            AskSelfUpdate();
            break;
        case kRowSelfNotice:
            m_ui.self_update_notice = !m_ui.self_update_notice;
            m_ui.Save();
            break;
        case kRowSelfBack:
            if (m_update_has_old) {
                m_dialog = Dialog{};
                m_dialog.kind = DialogKind::SelfBack;
            }
            break;
        case kRowHome:
            AskHomeShortcut(false);
            break;
        case kRowHomeRemove:
            AskHomeShortcut(true);
            break;
        case kRowScreen: {
            // normal, dimmed after a minute, off for a long queue (and dimmed for a short one)
            const int next = ((m_ui.screen_off_auto ? 2 : m_ui.saver ? 1 : 0) + 1) % 3;
            m_ui.saver = next >= 1;
            m_ui.screen_off_auto = next == 2;
            m_ui.Save();
            break;
        }
        case kRowDest:
            // the SD card, or asking each time; the internal memory is only ever chosen for one install
            m_ui.dest = m_ui.dest == 2 ? 0 : 2;
            m_ui.Save();
            break;
        case kRowPick:
            m_ui.pick = (m_ui.pick + 1) % 3;
            m_ui.Save();
            break;
        case kRowFinish:
            m_ui.finish_effect = !m_ui.finish_effect;
            m_ui.Save();
            break;
        case kRowBattery: {
            int at = 0;
            for (int k = 1; k < 5; k++) {
                if (m_ui.battery_notice && m_ui.battery_level == kBatterySteps[k]) at = k;
            }
            const int next = (at + 1) % 5;
            m_ui.battery_notice = next != 0;
            if (next) m_ui.battery_level = kBatterySteps[next];
            m_ui.Save();
            break;
        }
        case kRowUpdates:
            m_ui.check_updates = !m_ui.check_updates;
            m_ui.Save();
            break;
        case kRowVersionsList:
            m_ui.versions_list = !m_ui.versions_list;
            m_ui.Save();
            break;
        case kRowSaveCopy:
            m_ui.save_copy = !m_ui.save_copy;
            m_ui.Save();
            break;
        case kRowSaveAuto: {
            // no, every 1, 3, 7, 14 or 30 days
            static constexpr int steps[] = {0, 1, 3, 7, 14, 30};
            int at = 0;
            for (int k = 0; k < 6; k++) {
                if (m_ui.save_auto_days == steps[k]) at = k;
            }
            m_ui.save_auto_days = steps[(at + 1) % 6];
            m_ui.Save();
            break;
        }
        case kRowNotifyDone:
            m_ui.notify_done = !m_ui.notify_done;
            m_ui.Save();
            break;
        case kRowCompress:
            // turned on only after its window (24A); off at once
            if (m_ui.compress_on) {
                m_ui.compress_on = false;
                m_ui.Save();
            } else {
                m_dialog = Dialog{};
                m_dialog.kind = DialogKind::CompressIntro;
            }
            break;
        case kRowAnim:
            m_ui.anim = (m_ui.anim + 1) % 3;
            ui::SetMotion(m_ui.anim);
            m_ui.Save();
            break;
        case kRowFocusView:
            // the game in focus: a band under the covers, or a panel on the right (2A, 2B)
            m_ui.focus_view = m_ui.focus_view == 1 ? 0 : 1;
            m_ui.Save();
            GridSize(m_ui.library_size, m_ui.focus_view == 1);
            m_scroll = m_scroll_target = 0;
            KeepFocusVisible();
            break;
        case kRowTouchRumble:
            m_ui.touch_rumble = !m_ui.touch_rumble;
            m_ui.Save();
            TouchTick(); // felt at once when it is turned on
            break;
        case kRowLibrary:
            // the pairing screen, or the library when linked
            m_tab = Tab::Library;
            if (!current.Linked()) {
                m_link.Search();
            }
            break;
        case kRowForget:
            if (current.Linked()) {
                // asked first: pairing again needs a new code
                m_dialog = Dialog{};
                m_dialog.kind = DialogKind::Forget;
            }
            break;
        case kRowAddress: {
            std::string url;
            if (AskText(s.address, !m_manual_url.empty() ? m_manual_url : current.url.empty() ? "http://" : current.url, false, url)) {
                m_manual_url = url;
            }
            break;
        }
        case kRowKey: {
            std::string key;
            const std::string url = !m_manual_url.empty() ? m_manual_url : current.url;
            if (!url.empty() && AskText(s.console_key, "", true, key)) {
                m_link.UseKey(url, key);
            }
            break;
        }
        case kRowShare:
            m_prefs.share_installed = m_prefs.share_installed == 1 ? 0 : 1;
            m_prefs.Save();
            if (m_prefs.share_installed == 0) {
                m_push->SendInventory("{\"off\":true}"); // the app forgets it
            }
            m_inventory_sent = ~0u;
            break;
        case kRowSpeed:
            if (m_speed.Running()) {
                m_speed.Cancel();
            } else if (current.Linked()) {
                m_speed_kept = false;
                m_speed.Start(current.url, current.key);
            }
            break;
    }
}

void App::ActivatePairRow(int row, const Link::View& v) {
    if (row < int(v.found.size())) {
        m_link.Choose(row);
        return;
    }
    std::string url;
    if (AskText(Tr().address, "http://", false, url)) {
        m_link.UseAddress(url);
    }
}

void App::UpdatePairing(u64 down, u64 held, const Link::View& v) {
    const int rows = int(v.found.size()) + 1; // the apps found and "Type the address"
    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;
    if (Repeat(down, held, up) && m_pair_focus > 0) m_pair_focus--;
    if (Repeat(down, held, dn) && m_pair_focus < rows - 1) m_pair_focus++;
    m_pair_focus = std::clamp(m_pair_focus, 0, rows - 1);
    const bool busy = v.state == Link::State::Searching || v.state == Link::State::Reconnecting;
    if ((down & HidNpadButton_A) && !busy) {
        ActivatePairRow(m_pair_focus, v);
    }
    if (down & HidNpadButton_X) {
        // a new code from the same app, or a new look at the network
        if (v.chosen >= 0 && v.chosen < int(v.found.size()) && v.state != Link::State::Offline) {
            m_link.Choose(v.chosen);
        } else {
            m_link.Search();
        }
    }
    if (down & HidNpadButton_Y) {
        m_link.SetRemember(!v.remember);
    }
}

bool App::Repeat(u64 down, u64 held, u64 mask) {
    if (down & mask) {
        m_held_dir = down & mask;
        m_held_since = m_time;
        m_next_repeat = m_time + 0.35;
        return true;
    }
    if ((held & m_held_dir) && (m_held_dir & mask) && m_time >= m_next_repeat) {
        // held for more than a second, it goes twice as fast
        m_next_repeat = m_time + (m_time - m_held_since > 1.35 ? 0.045 : 0.09);
        return true;
    }
    return false;
}

float App::MaxScroll() const {
    const int rows = (int(m_view.size()) + g_columns - 1) / g_columns;
    return std::max(0.f, rows * g_row_pitch - kGridGap - g_view_h);
}

// On a cover of the library: Installed in green; what it lacks of the library (its update, its DLC)
// in cyan below.
void App::InstalledBadge(NVGcontext* vg, const Game& g, float x, float y, float s) {
    // only what the console has, as small round marks in the corner (5A): a green tick when the
    // game is on it, under it a cyan arrow when an update is missing, a cyan plus for a DLC
    if (!g.installed.base) return;
    const Theme& t = m_theme;
    const float r = s < 110 ? 10.f : 13.f;
    const float cx = x + s - 8 - r;
    float cy = y + 8 + r;
    const auto mark = [&](NVGcolor fill) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r + 2);
        nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.28f));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, fill);
        nvgFill(vg);
    };
    mark(t.ok);
    ui::CheckMark(vg, cx, cy + 0.5f, r * 1.05f, nvgRGB(255, 255, 255), s < 110 ? 2.f : 2.6f);
    const bool update = g.UpdateAvailable(), dlc = g.MissingDlc();
    if (!update && !dlc) return;
    cy += 2 * r + 6;
    mark(Hex(0x22bfe3));
    const NVGcolor ink = Hex(0x082430);
    const float k = r * 0.45f;
    nvgBeginPath(vg);
    if (update) {
        nvgMoveTo(vg, cx, cy + k);
        nvgLineTo(vg, cx, cy - k);
        nvgMoveTo(vg, cx - k * 0.8f, cy - k * 0.2f);
        nvgLineTo(vg, cx, cy - k);
        nvgLineTo(vg, cx + k * 0.8f, cy - k * 0.2f);
    } else {
        nvgMoveTo(vg, cx - k, cy);
        nvgLineTo(vg, cx + k, cy);
        nvgMoveTo(vg, cx, cy - k);
        nvgLineTo(vg, cx, cy + k);
    }
    nvgStrokeColor(vg, ink);
    nvgStrokeWidth(vg, s < 110 ? 2.f : 2.4f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

void App::FollowPreflight() {
    if (!m_preflight.valid() || m_preflight.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto jobs = m_preflight.get();
    if (m_dialog.kind == DialogKind::Preflight) m_dialog = Dialog{};
    if (m_preflight_dropped) return;
    AddJobs(std::move(jobs));
}

void App::DestCards(NVGcontext* vg, float x, float y, float w, float h, bool nand, unsigned long long need, HitKind kind) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const float gap = 12, cw = (w - gap) / 2;
    for (int d = 0; d < 2; d++) {
        const float cx = x + d * (cw + gap);
        const bool on = (d == 1) == nand;
        NVGcolor bg = t.work;
        bg.a = on ? 0.14f : 0.f;
        ui::FillRound(vg, cx, y, cw, h, 14, t.panel);
        if (on) ui::FillRound(vg, cx, y, cw, h, 14, bg);
        ui::StrokeRound(vg, cx + 1, y + 1, cw - 2, h - 2, 13, on ? t.work : t.line, on ? 2.5f : 1.f);
        AddHit(cx, y - 4, cw, h + 8, kind, d);
        // the sign: SD, or the chip of the internal memory
        const float ix = cx + 14, iy = y + (h - 40) / 2;
        ui::FillRound(vg, ix, iy, 40, 40, 9, on ? t.work : t.panel2);
        if (d == 0) {
            ui::Text(vg, ix + 20, iy + 21, 15, on ? t.on_work : t.muted, "SD", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        } else {
            ui::StrokeRound(vg, ix + 11, iy + 11, 18, 18, 3, on ? t.on_work : t.muted, 2.f);
            ui::FillRound(vg, ix + 16, iy + 16, 8, 8, 1, on ? t.on_work : t.muted);
        }
        const float tx = ix + 54;
        ui::Text(vg, tx, y + h / 2 - 10, 18, on ? t.text : t.muted, d == 0 ? s.sd_card : s.internal);
        const s64 free_b = d == 0 ? m_sd_free_bytes : m_nand_free_bytes;
        char line[160] = "";
        NVGcolor col = t.muted;
        if (free_b >= 0) {
            if (on && need > 0) {
                const u64 left = u64(free_b) > need ? u64(free_b) - need : 0;
                std::snprintf(line, sizeof(line), m.dc_left_fmt, SizeText(u64(free_b)).c_str(), SizeText(left).c_str());
                if (u64(free_b) < need) col = t.err;
            } else {
                std::snprintf(line, sizeof(line), m.dc_free_fmt, SizeText(u64(free_b)).c_str());
            }
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, tx, y, cw - (tx - cx) - 44, h);
        ui::Text(vg, tx, y + h / 2 + 13, 13, col, line);
        nvgRestore(vg);
        // the round mark of the one chosen
        const float rx = cx + cw - 26, ry = y + h / 2;
        nvgBeginPath(vg);
        nvgCircle(vg, rx, ry, on ? 7.5f : 10);
        nvgStrokeColor(vg, on ? t.work : t.muted);
        nvgStrokeWidth(vg, on ? 6.f : 2.f);
        nvgStroke(vg);
    }
}

void App::KeepFocusVisible() {
    const float row_top = (m_focus / g_columns) * g_row_pitch;
    if (row_top < m_scroll_target) {
        m_scroll_target = row_top;
    } else if (row_top + g_cover > m_scroll_target + g_view_h) {
        m_scroll_target = row_top + g_cover - g_view_h;
    }
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
}

void App::ReadTouch(u64& down) {
    HidTouchScreenState state{};
    perf::ReadTouch(state);
    if (m_wake_touch) {
        // the finger that woke the screen: nothing until it is lifted
        if (state.count == 0) m_wake_touch = false;
        return;
    }
    if (state.count > 0) {
        const float x = state.touches[0].x, y = state.touches[0].y;
        if (!m_touching) {
            m_touching = true;
            m_dragging = false;
            m_swipe = false;
            m_swipe_dx = 0;
            m_long_fired = false;
            m_touch_x0 = m_touch_x_prev = x;
            m_touch_y0 = m_touch_y_prev = y;
            m_touch_t_prev = m_time;
            m_drag_v = m_drag_vx = 0;
            m_fling_v = 0;      // a finger stops the grid
            m_list_fling = 0;
            m_touch_at = m_time;
            m_drag_scroll0 = m_scroll;
            // a list drawn last frame under the finger: dragged, it moves its focus row by row
            m_list_drag = m_list_pitch > 0 && m_dialog.kind == DialogKind::None && !m_menu && !m_wifi_open && m_quick < 0 &&
                          m_list_area.Contains(x, y);
            m_drag_steps = 0;
            // the A-Z bar on the right edge: tapped or dragged along, it jumps to the letter
            m_az_touch = AzBarShown() && m_dialog.kind == DialogKind::None && !m_menu && m_quick < 0 && x >= g_az_x - 22 &&
                         x < g_az_x + kAzW + 12 && y >= kAzTop - 8 && y < kAzTop + g_az_height + 8;
            // what is under the finger reacts at once (15A)
            m_press_live = false;
            m_press_cover = -1;
            if (!m_az_touch) {
                for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
                    if (it->rect.Contains(x, y)) {
                        m_press_rect = it->rect;
                        m_press_kind = it->kind;
                        m_press_cover = it->kind == HitKind::Cover ? it->index : -1;
                        m_press_live = true;
                        m_press_at = m_time;
                        break;
                    }
                }
            }
        }
        // the speed of the finger, smoothed (16A)
        const double dt = m_time - m_touch_t_prev;
        if (dt > 0.001) {
            const float vy = float((y - m_touch_y_prev) / dt), vx = float((x - m_touch_x_prev) / dt);
            m_drag_v = m_drag_v * 0.4f + vy * 0.6f;
            m_drag_vx = m_drag_vx * 0.4f + vx * 0.6f;
            m_touch_y_prev = y;
            m_touch_x_prev = x;
            m_touch_t_prev = m_time;
        }
        m_touch_x = x;
        m_touch_y = y;
        // held still for a moment: a long press (the menu of a file, as − does)
        if (!m_long_fired && !m_dragging && m_tab == Tab::More && m_more == MoreScreen::Files && m_time - m_touch_at > 0.55 && std::fabs(x - m_touch_x0) <= kDragSlop &&
            std::fabs(y - m_touch_y0) <= kDragSlop && m_dialog.kind == DialogKind::None) {
            m_long_fired = true;
            m_press_live = false; // the menu opens: the wave under the finger goes
            m_release_at = m_time;
            LongPress(x, y);
            if (m_files_menu) TouchTick();
        }
        if (m_az_touch) {
            const int slot = std::clamp(int((y - kAzTop) / (g_az_height / 27)), 0, 26);
            JumpToLetter(slot, 0);
            return;
        }
        const float dx = x - m_touch_x0, dy = y - m_touch_y0;
        const bool moved = std::fabs(dx) > kDragSlop || std::fabs(dy) > kDragSlop;
        if (moved && m_press_live) {
            m_press_live = false; // a drag, not a press: the wave goes
            m_release_at = m_time;
        }
        // held still on a cover: its quick menu (18A)
        if (!moved && !m_long_fired && m_press_cover >= 0 && m_press_kind == HitKind::Cover && GridTab() && !m_detail &&
            m_dialog.kind == DialogKind::None && !m_menu && m_quick < 0 && m_time - m_press_at >= 0.45) {
            m_long_fired = true;
            m_press_live = false;
            m_release_at = m_time;
            OpenQuick(m_press_cover);
            TouchTick();
            return;
        }
        const bool in_grid = GridTab() && !m_detail && m_touch_y0 >= kGridTop - 10 && m_touch_y0 < kGridTop + g_view_h &&
                             m_touch_x0 < g_az_x - 22;
        if (!m_dragging && !m_swipe && moved && !m_long_fired) {
            // sideways: the tab beside, or back from a page (20A); up and down: the grid or the list
            const bool calm = !m_wizard && m_dialog.kind == DialogKind::None && !m_menu && m_quick < 0 && !m_wifi_open;
            if (std::fabs(dx) > std::fabs(dy) * 1.2f && calm) {
                m_swipe = true;
            } else if (std::fabs(dy) > kDragSlop && (in_grid || m_list_drag)) {
                m_dragging = true;
                if (m_list_drag) ListDragStarts();
            }
        }
        if (m_swipe) {
            // the content follows the finger; from a page only to the right (back), and with resistance
            const bool sub = InSubScreen();
            float follow = dx;
            if (sub && follow < 0) follow *= 0.15f;
            m_swipe_dx = ui::Still() ? 0.f : follow * (sub ? 0.6f : 0.45f);
        } else if (m_dragging && m_list_drag) {
            // a row dragged up shows the next one: the focus goes down by one, as with the pad
            const int steps = int((m_touch_y0 - y) / m_list_pitch);
            m_drag_pending += steps - m_drag_steps;
            m_drag_steps = steps;
        } else if (m_dragging) {
            // the grid follows the finger; past its ends it gives a little (16A), at the top that
            // is pulling to read the library again (21A)
            const float raw = m_drag_scroll0 - dy, top = m_refreshing ? -46.f : 0.f, max = MaxScroll();
            float at = raw;
            if (ui::Still()) at = std::clamp(raw, 0.f, max);
            else if (raw < top) at = top + (raw - top) * 0.42f;
            else if (raw > max) at = max + (raw - max) * 0.42f;
            m_scroll = m_scroll_target = at;
            m_scroll_seen_at = m_time;
        }
    } else if (m_touching && m_az_touch) {
        m_touching = m_az_touch = false;
    } else if (m_touching) {
        m_touching = false;
        const float dx = m_touch_x - m_touch_x0;
        const bool moved = std::fabs(dx) > kDragSlop || std::fabs(m_touch_y - m_touch_y0) > kDragSlop;
        if (m_press_live) {
            m_press_live = false;
            m_release_at = m_time;
        }
        if (m_swipe) {
            // far enough, or quick enough: the tab beside (or back); else it springs back
            const bool far = std::fabs(dx) > 150 || (std::fabs(m_drag_vx) > 700 && std::fabs(dx) > 40);
            const bool sub = InSubScreen();
            if (far && sub && dx > 0) {
                down |= HidNpadButton_B;
                TouchTick();
                m_swipe_dx = 0;
            } else if (far && !sub) {
                const int dir = dx < 0 ? 1 : -1;
                m_tab = Tab((int(m_tab) + dir + kTabs) % kTabs);
                // the new tab comes in from where the finger left the old one
                m_tab_from_next = float(dir) * std::max(80.f, 380.f - std::fabs(m_swipe_dx));
                m_swipe_dx = 0;
                TouchTick();
            } else {
                m_swipe_back = m_swipe_dx;
                m_swipe_back_at = m_time;
                m_swipe_dx = 0;
            }
            m_swipe = false;
        } else if (m_dragging && m_list_drag) {
            // let go with speed: the list goes on a few rows, slowing down (16A)
            if (!ui::Still() && std::fabs(m_drag_v) > 400 && m_list_pitch > 0) {
                m_list_fling = -m_drag_v;
                m_list_fling_d = 0;
                m_list_fling_pitch = m_list_pitch;
            }
        } else if (m_dragging) {
            if (m_scroll < -kPullArm && m_tab == Tab::Library && !m_shop_active && !m_refreshing &&
                m_catalog.GetState() != Catalog::State::Loading) {
                // pulled far enough: the library is read again (21A)
                const Config c = m_link.CurrentConfig();
                if (c.Linked()) {
                    m_refreshing = true;
                    m_refresh_at = m_time;
                    m_catalog.Load(c.url, c.key);
                    TouchTick();
                }
            }
            if (!ui::Still() && std::fabs(m_drag_v) > 250) {
                m_fling_v = std::clamp(-m_drag_v, -4200.f, 4200.f);
                m_fling_end_told = false;
            }
            m_scroll_target = std::clamp(m_scroll, m_refreshing ? -46.f : 0.f, MaxScroll());
        } else if (!moved && !m_long_fired) {
            if (Tap(m_touch_x, m_touch_y, down)) TouchTick();
        }
        m_dragging = false;
        m_list_drag = false;
    }
}

void App::ListDragStarts() {
    // the screens with a side list too: the one dragged takes the focus
    if (m_tab != Tab::More) return;
    if (m_more == MoreScreen::Files) m_files_side = false;
    if (m_more == MoreScreen::Saves) m_vault_side = 0;
}

bool App::Tap(float x, float y, u64& down) {
    if (m_wifi_open) {
        // the WiFi card is on top: a tap on it does nothing, anywhere else closes it
        if (!m_wifi_card.Contains(x, y)) m_wifi_open = false;
        return true;
    }
    // Last drawn on top wins: walk backwards.
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->rect.Contains(x, y)) {
            continue;
        }
        switch (it->kind) {
            case HitKind::Button: down |= it->rect.button; break;
            case HitKind::Tab: m_tab = Tab(it->index); break;
            case HitKind::Wifi: m_wifi_open = true; break;
            case HitKind::Chip:
                if (m_filter != it->index) m_wave_at = m_time; // the covers come in as a wave (9A)
                m_filter = it->index;
                RebuildView();
                break;
            case HitKind::SearchChip:
                // the chip of the search: a tap clears it
                m_search.clear();
                RebuildView();
                break;
            case HitKind::Cover:
                // First tap marks it, a second tap opens it (as A does).
                if (m_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_focus = it->index;
                    KeepFocusVisible();
                    // marked by a finger: a bubble on it opens it (19A)
                    m_touch_focus = true;
                    m_touch_focus_at = m_time;
                }
                break;
            case HitKind::QueueStrip: m_tab = Tab::Queue; break;
            case HitKind::SettingsRow:
                // as in the library: the first tap marks a row, a second one uses it
                if (m_setting == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_setting = it->index;
                }
                break;
            case HitKind::StyleCard:
                // a card of a style is chosen at once, its wave from the finger
                m_setting = kRowStyle;
                SetStyle(it->index, m_touch_x, m_touch_y);
                break;
            case HitKind::PairRow:
                if (m_pair_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_pair_focus = it->index;
                }
                break;
            case HitKind::Remember: down |= HidNpadButton_Y; break;
            case HitKind::Install:
                // the button acts at once, as a button of a question does
                m_detail_focus = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::DetailRow:
                if (m_detail_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_detail_focus = it->index;
                }
                break;
            case HitKind::Dest:
                m_to_nand = it->index == 1;
                m_dest_touched = true;
                break;
            case HitKind::FilesDest:
                m_files_nand = it->index == 1;
                break;
            case HitKind::QueueRow:
                if (m_queue_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_queue_focus = it->index;
                }
                break;
            case HitKind::InstalledView:
                if (m_installed_view != it->index) {
                    m_installed_view = it->index;
                    m_installed_focus = 0;
                    m_console_app = 0;
                }
                break;
            case HitKind::ShopChip:
                if (it->index == int(m_shops.size())) {
                    // a shop more: the list, where it is added
                    LeaveShop();
                    m_shop_cache.clear();
                    m_shop_cached = -1;
                    m_shop_focus = int(m_shops.size());
                } else if (it->index != m_shop_opening) {
                    OpenShopAt(it->index);
                }
                break;
            case HitKind::ConsoleSize:
                if (m_ui.console_size != it->index) {
                    m_ui.console_size = it->index;
                    m_ui.Save();
                }
                break;
            case HitKind::InstalledRow:
                if (m_installed_view == 0) {
                    // In the console: a game of the list, or a line of its page
                    int& focus = m_console_app ? m_console_item : m_console_focus;
                    if (focus == it->index) {
                        down |= HidNpadButton_A;
                    } else {
                        focus = it->index;
                    }
                    break;
                }
                if (m_installed_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_installed_focus = it->index;
                }
                break;
            case HitKind::WizItem: {
                // as everywhere: the first tap marks it, a second one uses it
                const int group = it->index / 100, item = it->index % 100;
                if (m_wiz_group == group && m_wiz_item == item) {
                    ActivateWizard(group, item);
                } else {
                    m_wiz_group = group;
                    m_wiz_item = item;
                }
                break;
            }
            case HitKind::WizButton:
                // Back and Next act at once, as the buttons of a question
                m_wiz_group = int(WizardGroups().size()) - 1;
                m_wiz_item = it->index;
                ActivateWizard(m_wiz_group, it->index);
                break;
            case HitKind::WizMusic: down |= HidNpadButton_Minus; break;
            case HitKind::MenuBack: m_menu = false; break;
            case HitKind::MenuRow:
                // a row of the menu acts at once, as a button of a question does
                m_menu_focus = it->index;
                ActivateMenu(it->index);
                break;
            case HitKind::MoreTile:
                if (m_tile == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_tile = it->index;
                }
                break;
            case HitKind::MorePlace:
                m_files_side = true;
                if (m_place == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_place = it->index;
                }
                break;
            case HitKind::MoreRow:
                m_files_side = false;
                if (m_file_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_file_focus = it->index;
                }
                break;
            case HitKind::MoreMark:
                m_files_side = false;
                m_file_focus = it->index;
                down |= HidNpadButton_Y;
                break;
            case HitKind::MoreSide:
                break;
            case HitKind::FilesMenuRow:
                // a row of the menu acts at once, as a button of a question does
                m_files_menu_focus = it->index;
                FilesDo(it->index);
                break;
            case HitKind::FilesMenuBack:
                m_files_menu = false;
                m_vault_menu = false;
                break;
            case HitKind::VaultKind:
                if (m_vault_kind != it->index) {
                    m_vault_kind = it->index;
                    OpenVault();
                }
                break;
            case HitKind::VaultMenuRow:
                m_vault_menu_focus = it->index;
                VaultMenuDo(it->index);
                break;
            case HitKind::MtpUnit:
                m_mtp_unit = it->index;
                ToggleMtpUnit(it->index);
                break;
            case HitKind::ConsoleMissing:
                m_console_missing = !m_console_missing;
                m_console_focus = 0;
                break;
            case HitKind::FilesView:
                if (m_files_all != (it->index == 1)) FilesDo(kFaView);
                break;
            case HitKind::FilesAfter:
                m_files_after = !m_files_after;
                break;
            case HitKind::ModRow: {
                const int side = it->index >= 100 ? 1 : 0, row = it->index >= 100 ? it->index - 100 : 0;
                if (m_mods_side == side && m_mods_focus == row) {
                    down |= HidNpadButton_A;
                } else {
                    m_mods_side = side;
                    m_mods_focus = row;
                }
                break;
            }
            case HitKind::ShopRow:
                if (m_shop_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_shop_focus = it->index;
                }
                break;
            case HitKind::CompressRow:
                if (m_compress_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_compress_focus = it->index;
                }
                break;
            case HitKind::VersionChip:
                down |= HidNpadButton_Minus;
                break;
            case HitKind::SdChoice:
                if (it->index >= 2 || m_sd_choice == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_sd_choice = it->index;
                }
                break;
            case HitKind::SpaceAction:
                if (m_space_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_space_focus = it->index;
                }
                break;
            case HitKind::PickRow:
                if (m_more == MoreScreen::Mods) {
                    if (m_mods_pick == it->index) down |= HidNpadButton_A;
                    else m_mods_pick = it->index;
                    break;
                }
                if (m_pick_focus == it->index) {
                    down |= m_pick_kind == 0 ? HidNpadButton_Y : HidNpadButton_A;
                } else {
                    m_pick_focus = it->index;
                }
                break;
            case HitKind::VaultGame:
                m_vault_side = 0;
                if (m_vault_focus != it->index) m_vault_copy = 0;
                m_vault_focus = it->index;
                break;
            case HitKind::VaultCopy:
                if (m_vault_side == 1 && m_vault_copy == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_vault_side = 1;
                    m_vault_copy = it->index;
                }
                break;
            case HitKind::DialogBack: down |= HidNpadButton_B; break; // as B: each question answers its own way
            case HitKind::DialogLine:
                if (m_dialog.entry == it->index) {
                    m_dialog.focus = 1;
                    down |= HidNpadButton_A;
                } else {
                    m_dialog.entry = it->index;
                }
                break;
            case HitKind::DialogButton:
                // a button of the question acts at once: tapping it is the choice
                m_dialog.focus = it->index;
                down |= HidNpadButton_A;
                break;
            case HitKind::OpenBubble: down |= HidNpadButton_A; break; // the bubble on a cover tapped once (19A)
            case HitKind::QuickRow: ActivateQuick(it->index); break;    // a row of the quick menu acts at once (18A)
            case HitKind::QuickBack: m_quick = -1; break;
        }
        return true;
    }
    return false;
}

void App::Update(u64 down, u64 held) {
    if (m_dialog.kind != DialogKind::None) {
        UpdateDialog(down, held);
        return;
    }
    // the WiFi card: the right stick opens and closes it, B or A closes it
    if (down & HidNpadButton_StickR) {
        // on the grid the right stick jumps by letter: a click while it is pushed, or just after a
        // jump, is part of that and does not open the WiFi
        const u64 pushed = HidNpadButton_StickRUp | HidNpadButton_StickRDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight;
        const bool jumping = GridTab() && !m_detail && !m_wifi_open && ((held & pushed) || m_time - m_rstick_at < 0.3 || m_time - m_jump_at < 0.3);
        if (!jumping) {
            m_wifi_open = !m_wifi_open && m_wifi > -2;
            return;
        }
        down &= ~u64(HidNpadButton_StickR);
    }
    if (m_wifi_open) {
        if (down & (HidNpadButton_B | HidNpadButton_A)) m_wifi_open = false;
        return;
    }
    if (down & HidNpadButton_Plus) {
        // leaving stops an install (nothing is left half done): asked first, Stay in focus
        if (m_queue && m_queue->Busy()) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Quit;
            return;
        }
        m_quit = true;
        return;
    }
    if (m_wizard) {
        UpdateWizard(down, held);
        return;
    }
    if (m_detail && GridTab()) {
        if (m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
            m_detail = false;
        } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
            // as everywhere, L and R change the tab; the page is there again when coming back
            m_tab = Tab((int(m_tab) + ((down & HidNpadButton_R) ? 1 : kTabs - 1)) % kTabs);
            return;
        } else {
            UpdateDetail(down, held);
            return;
        }
    }
    if (m_quick >= 0) {
        // the quick menu of a cover held (18A)
        if (GridTab() && !m_detail && m_quick < int(m_view.size())) {
            UpdateQuick(down, held);
            return;
        }
        m_quick = -1;
    }
    if (m_menu) {
        if (GridTab()) {
            UpdateMenu(down, held);
            return;
        }
        m_menu = false;
    }
    // L and R change the tab from anywhere, as on the HOME menu; a tool of More stays open, and is
    // there again when coming back
    const bool in_tool = (m_tab == Tab::More && m_more != MoreScreen::Tiles) || // B leaves the tool first
                         (m_tab == Tab::Installed && m_installed_view == 0 && m_console_app != 0) || // or the page of a game
                         (m_tab == Tab::Shops && m_shop_active); // or the shop open: the list of shops again
    if (down & HidNpadButton_L) {
        m_tab = Tab((int(m_tab) + kTabs - 1) % kTabs);
    }
    if (down & HidNpadButton_R) {
        m_tab = Tab((int(m_tab) + 1) % kTabs);
    }

    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;

    switch (m_tab) {
        case Tab::Library: {
            const Link::View link = m_link.Snapshot();
            if (link.state != Link::State::Connected) {
                UpdatePairing(down, held, link);
                break;
            }
            UpdateLibrary(down, held);
            break;
        }
        case Tab::Shops:
            // the list of shops, or the games of the one open (as the library)
            if (m_shop_active) UpdateLibrary(down, held);
            else UpdateShops(down, held);
            break;
        case Tab::Queue:
            UpdateQueue(down, held);
            break;
        case Tab::Installed:
            UpdateInstalled(down, held);
            break;
        case Tab::More:
            UpdateMore(down, held);
            break;
        case Tab::Settings:
            if (m_setting == kRowStyle) {
                // left and right go through the styles, as their cards are laid out
                if (Repeat(down, held, HidNpadButton_AnyLeft)) SetStyle((m_ui.style + kStyles - 1) % kStyles, -1, -1);
                if (Repeat(down, held, HidNpadButton_AnyRight)) SetStyle((m_ui.style + 1) % kStyles, -1, -1);
            }
            if (Repeat(down, held, up) && m_setting > 0) m_setting--;
            if (Repeat(down, held, dn) && m_setting < kSettingRows - 1) m_setting++;
            if (down & HidNpadButton_A) ActivateSetting(m_setting);
            if (down & HidNpadButton_X) ActivateSetting(kRowSpeed);
            break;
        default:
            break;
    }
    if ((down & HidNpadButton_B) && m_tab != Tab::Library && !in_tool) {
        m_tab = Tab::Library;
    }
}

bool App::Frame() {
    if (!appletMainLoop() || m_quit) {
        return false;
    }
    perf::FrameBegin();
    const double now = Now();
    const float dt = float(std::min(0.1, now - m_time));
    m_time = now;

    padUpdate(&m_pad);
    u64 down = padGetButtonsDown(&m_pad);
    u64 held = padGetButtons(&m_pad);
#ifdef NXDOCK_PERF
    {
        u64 scripted = 0;
        perf::Script(scripted, m_quit);
        down |= scripted & ~m_perf_held;
        held |= scripted;
        m_perf_held = scripted;
    }
#endif
    // the clock of the animations, and the buttons whose symbols pulse
    ui::Tick(m_time);
    ui::NotePress(down);
    if (m_dialog.kind != m_dialog_drawn) {
        m_dialog_drawn = m_dialog.kind;
        m_dialog_at = m_time;
    }
    HidTouchScreenState touch{};
    perf::ReadTouch(touch);
    const bool new_touch = touch.count > 0 && !m_touching && !m_wake_touch;
    if (down || held || touch.count > 0) {
        m_last_input = m_time;
    }
    if (FollowScreenOff(down, new_touch)) {
        // the input that lit the screen, or stopped the countdown, does nothing else
    } else if (m_boot_at > 0 && m_time - m_boot_at < 1.5 && (down || new_touch)) {
        // the opening is skipped by any button or touch, which does nothing else (4A)
        m_boot_at = -100;
        m_wave_at = -100;
        m_wake_touch = new_touch;
        down = 0;
    } else if ((m_saver_on || TourActive()) && (down || new_touch)) {
        // the input that lights the screen again, or ends the walk, does nothing else
        m_saver_on = false;
        if (TourActive()) EndTour();
        m_wake_touch = new_touch;
        down = 0;
    }
    const u64 focus_before = FocusSignature();
    { perf::Timer t_("frame: ReadTouch"); ReadTouch(down); }
    // a list being dragged: one row a frame, as a press of the pad
    if (m_drag_pending > 0 && !(down & HidNpadButton_AnyDown)) {
        down |= HidNpadButton_Down;
        m_drag_pending--;
    } else if (m_drag_pending < 0 && !(down & HidNpadButton_AnyUp)) {
        down |= HidNpadButton_Up;
        m_drag_pending++;
    }
    if (m_dialog.kind != DialogKind::None || m_menu || m_quick >= 0) {
        m_drag_pending = 0;
        m_list_fling = 0;
    }
    GridSize(m_ui.library_size, m_ui.focus_view == 1);
    { perf::Timer t_("frame: Update"); Update(down, held); }
    { perf::Timer t_("frame: FollowPreflight"); FollowPreflight(); }
    if (m_tab != m_tab_seen) {
        // the Library again: what the console has now, also what DBI or the HOME menu changed
        if (m_tab_seen == Tab::Shops) LeaveShopsTab();
        if (m_tab == Tab::Shops) EnterShopsTab();
        // on a thread of its own, at most every few seconds: going through the tab never waits for it
        if (m_tab == Tab::Library && m_queue && !m_queue->Busy() && m_time - m_installed_read_at > 4) {
            m_installed_read_at = m_time;
            m_queue->ReadInstalledAgain();
        }
        m_tab_seen = m_tab;
    }
    { perf::Timer t_("frame: PlaySounds"); PlaySounds(down, focus_before); }
    { perf::Timer t_("frame: FollowTour"); FollowTour(); }
    // saver mode: dark after a minute without input while something installs
    if (m_queue && !m_queue->Busy()) {
        m_saver_on = false;
    } else if (m_ui.saver && !m_wizard && m_time - m_last_input > 60) {
        m_saver_on = true;
    }
    { perf::Timer t_("frame: FollowLink"); FollowLink(); }
    if (m_refreshing && m_catalog.GetState() != Catalog::State::Loading && m_time - m_refresh_at > 0.4) {
        // the library read again by pulling it down (21A): said in green, and the grid goes back up
        m_refreshing = false;
        if (m_catalog.GetState() == Catalog::State::Ready) {
            char text[160];
            std::snprintf(text, sizeof(text), TrM().pr_done_fmt, int(m_games.size()));
            NoticeOk(text);
        }
        if (!m_dragging) m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
    }
    if (m_tick_off_at > 0 && m_time >= m_tick_off_at) {
        // the end of the short vibration of a touch (17A)
        m_tick_off_at = -1;
        if (m_rumble_step < 0) SendRumble(false);
    }
    { perf::Timer t_("frame: FollowQueue"); FollowQueue(); }
    { perf::Timer t_("frame: FollowFilesWork"); FollowFilesWork(); }
    { perf::Timer t_("frame: FollowSelfUpdate"); FollowSelfUpdate(); }
    { perf::Timer t_("frame: FollowSaveAuto"); FollowSaveAuto(); }
    { perf::Timer t_("frame: FollowFinish"); FollowFinish(); }
    { perf::Timer t_("frame: FollowUsb"); FollowUsb(); }
    { perf::Timer t_("frame: FollowPush"); FollowPush(); }
    { perf::Timer t_("frame: FollowQueueAsk"); FollowQueueAsk(); }
    { perf::Timer t_("frame: FollowQueueWeb"); FollowQueueWeb(); }
    { perf::Timer t_("frame: CheckSdSpace"); CheckSdSpace(); }
    { perf::Timer t_("frame: CheckBattery"); CheckBattery(); }
    { perf::Timer t_("frame: CheckWifi"); CheckWifi(); }

    // The grid goes on after the finger with its speed, slowing down; past an end it stops quickly
    // and comes back (16A)
    if (!m_dragging && m_fling_v != 0) {
        const float max = MaxScroll(), top = m_refreshing ? -46.f : 0.f;
        m_scroll = std::clamp(m_scroll + m_fling_v * dt, top - 90.f, max + 90.f);
        const bool out = m_scroll < top || m_scroll > max;
        m_fling_v *= std::exp(-dt * (out ? 18.f : 3.f));
        if (out && !m_fling_end_told) {
            m_fling_end_told = true;
            TouchTick(); // the end, felt
        }
        m_scroll_target = m_scroll;
        m_scroll_seen_at = m_time;
        if (std::fabs(m_fling_v) < 30 || !GridTab() || m_detail) {
            m_fling_v = 0;
            m_scroll_target = std::clamp(m_scroll, top, max);
        }
    }
    // a list let go with speed goes on a few rows
    if (m_list_fling != 0 && !m_touching) {
        m_list_fling_d += m_list_fling * dt;
        m_list_fling *= std::exp(-dt * 5.f);
        const int steps = m_list_fling_pitch > 0 ? int(m_list_fling_d / m_list_fling_pitch) : 0;
        if (steps) {
            m_drag_pending += steps;
            m_list_fling_d -= float(steps) * m_list_fling_pitch;
        }
        if (std::fabs(m_list_fling) < 150) m_list_fling = 0;
    }

    // Ease the grid toward where it should be (frame-rate independent).
    if (!m_dragging) {
        m_scroll += (m_scroll_target - m_scroll) * (1.f - std::exp(-dt * 14.f));
        if (std::fabs(m_scroll_target - m_scroll) < 0.5f) m_scroll = m_scroll_target;
    }

    m_gfx.CheckResize();
    perf::Lap(perf::kUpdate);
    NVGcontext* vg = m_gfx.BeginFrame();
    perf::Lap(perf::kAcquire);
    perf::Count(m_covers.Upload(vg));
    perf::Lap(perf::kUpload);
    Draw(vg);
    perf::Lap(perf::kDraw);
    m_gfx.EndFrame();
    perf::Lap(perf::kPresent);
    return !m_quit;
}

u64 App::FocusSignature() const {
    // every place a focus can be, mixed into one number
    u64 h = 1469598103934665603ull;
    const auto mix = [&](long long v) { h = (h ^ u64(v)) * 1099511628211ull; };
    mix(int(m_tab));
    mix(m_detail);
    mix(m_focus);
    mix(m_filter);
    mix(m_setting);
    mix(m_pair_focus);
    mix(m_detail_focus);
    mix(m_to_nand);
    mix(m_queue_focus);
    mix(m_installed_view);
    mix(m_installed_focus);
    mix(int(m_console_app & 0xFFFFFFF));
    mix(m_console_focus);
    mix(m_console_item);
    mix(m_ui.console_size);
    mix(int(std::count(m_console_marks.begin(), m_console_marks.end(), 1)));
    mix(int(m_dialog.kind));
    mix(m_dialog.focus);
    mix(m_menu);
    mix(m_menu_focus);
    mix(m_ui.sort);
    mix(m_wizard);
    mix(m_wiz_step);
    mix(m_wiz_group);
    mix(m_wiz_item);
    return h;
}

void App::PlaySounds(u64 down, u64 before) {
    if (!m_ui.sounds) {
        return;
    }
    if (down & HidNpadButton_A) {
        m_sound.Play(synth::Effect::Select);
    } else if (down & HidNpadButton_B) {
        m_sound.Play(synth::Effect::Back);
    } else if (FocusSignature() != before) {
        m_sound.Play(synth::Effect::Move);
    }
}

void App::AddHints(const std::vector<ui::HitRect>& rects) {
    for (const auto& r : rects) {
        m_hits.push_back({r, HitKind::Button, 0});
    }
}

void App::Draw(NVGcontext* vg) {
    m_hits.clear();
    m_hint_rects.clear();
    m_list_pitch = 0; // set again by the list drawn, if any
    const bool low = m_queue && m_queue->Busy() && m_battery >= 0 && m_battery < 20 && !m_charging;
    {
        perf::Timer t_("draw: background and aurora");
        StepStyle();
        Background(vg, m_theme);
        // the slow lights behind, paused while installing on little battery
        if (!low) ui::Aurora(vg, m_theme);
    }
    // the light of the game in focus (1A), behind the library and the page of a game; paused, as
    // the aurora, while installing on little battery
    if (!m_wizard && GridTab() && !low) {
        const bool shown = m_tab == Tab::Shops ? m_shop_active : m_link.Snapshot().state == Link::State::Connected;
        if (shown && m_detail && m_detail_game >= 0 && m_detail_game < int(m_games.size())) {
            perf::Timer t_("draw: backdrop");
            DrawBackdrop(vg, m_games[m_detail_game], true);
        } else if (shown && !m_detail && !m_view.empty()) {
            perf::Timer t_("draw: backdrop");
            DrawBackdrop(vg, m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]], false);
        }
    }
    // the scene of the style, over the light of the game, paused as the aurora
    if (!low) ui::Scene(vg, m_theme);
    if (m_wizard) {
        DrawWizard(vg);
        AddHints(m_hint_rects);
        if (m_time < m_notice_until && !m_notice.empty()) {
            const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48;
            ui::FillRound(vg, (kWidth - w) / 2, 600, w, 44, 22, m_theme.panel2);
            ui::Text(vg, kWidth / 2, 622, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        DrawTouchFeedback(vg);
        return;
    }
    ui::HitRect tabs[kTabs];
    int queued = 0;
    if (m_queue) {
        queued = m_queue->Waiting() + (m_queue->GetLive().running ? 1 : 0);
    }
    const float sd_used = m_sd_total_bytes > 0 && m_sd_free_bytes >= 0 ? 1.f - float(double(m_sd_free_bytes) / double(m_sd_total_bytes)) : -1.f;
    perf::Timer t_top("draw: top bar");
    const float capsule = ui::TopBar(vg, m_theme, int(m_tab), queued, tabs, m_sd_free.c_str(), m_battery, m_charging, m_wifi, &m_wifi_rect, sd_used);
    if (m_wifi > -2) m_hits.push_back({m_wifi_rect, HitKind::Wifi, 0});
    if (m_shop_active && m_tab == Tab::Library) {
        // which shop the library shows
        char badge[160];
        std::snprintf(badge, sizeof(badge), TrM().sh_badge, m_shop_name.c_str());
        const float bx = tabs[kTabs - 1].x + tabs[kTabs - 1].w + 30;
        const float bw = std::min({260.f, ui::TextWidth(vg, 14, badge) + 28, capsule - 12 - bx});
        NVGcolor bg = m_theme.work;
        bg.a = 0.16f;
        ui::FillRound(vg, bx, 22, bw, 28, 14, bg);
        nvgSave(vg);
        nvgScissor(vg, bx + 8, 22, bw - 16, 28);
        ui::Text(vg, bx + 14, 36, 14, m_theme.text, badge);
        nvgRestore(vg);
    }
    for (int i = 0; i < kTabs; i++) {
        m_hits.push_back({tabs[i], HitKind::Tab, i});
        m_tab_rects[i] = tabs[i];
    }
    if (m_tab != m_shown_tab) {
        // the content comes in from the side of the new tab (L wraps from the first to the last);
        // slid by a finger, from where the finger left the old one (20A)
        const int from = int(m_shown_tab), to = int(m_tab);
        m_tab_dir = ((to - from + kTabs) % kTabs) <= 2 ? 1.f : -1.f;
        m_shown_tab = m_tab;
        m_tab_at = m_time;
        m_tab_from = m_tab_from_next;
        m_tab_from_next = 0;
        m_quick = -1;
    }
    if (m_detail != m_shown_detail) {
        m_shown_detail = m_detail;
        m_detail_at = m_time;
    }
    const float tab_e = float(std::min(1.0, (m_time - m_tab_at) / 0.22));
    nvgSave(vg);
    float shift = 0;
    if (tab_e < 1 && !ui::Still()) {
        const float k = 1 - tab_e;
        shift = m_tab_from != 0 ? m_tab_from * k * k * k : 30 * m_tab_dir * k * k;
    } else {
        m_tab_from = 0;
    }
    // a finger sliding sideways moves the content; let go too soon, it springs back (20A)
    shift += m_swipe_dx;
    if (m_swipe_back != 0) {
        const float back = 1 - ui::EaseOut(ui::Appear(m_swipe_back_at, 0.22f));
        shift += m_swipe_back * back;
        if (back <= 0) m_swipe_back = 0;
    }
    if (shift != 0) nvgTranslate(vg, shift, 0);
    t_top.Stop();
    perf::Timer t_tab("draw: the tab");
    switch (m_tab) {
        case Tab::Library: {
            const Link::View link = m_link.Snapshot();
            if (link.state == Link::State::Connected) {
                if (m_detail) {
                    DrawDetail(vg);
                } else {
                    DrawLibrary(vg);
                }
            } else {
                DrawPairing(vg, link);
            }
            break;
        }
        case Tab::Shops:
            FollowShop();
            if (!m_shop_active) DrawShops(vg);
            else if (m_detail) DrawDetail(vg);
            else DrawLibrary(vg);
            break;
        case Tab::Queue: DrawQueue(vg); break;
        case Tab::Installed: DrawInstalled(vg); break;
        case Tab::More: DrawMore(vg); break;
        case Tab::Settings: DrawSettings(vg); break;
    }
    nvgRestore(vg);
    t_tab.Stop();
    perf::Timer t_over("draw: over the tab");
    DrawTransitions(vg);
    DrawJumpLetter(vg);
    AddHints(m_hint_rects);
    if (m_menu && GridTab() && !m_detail) {
        DrawMenu(vg);
    }
    if (m_quick >= 0 && GridTab() && !m_detail) {
        DrawQuick(vg);
    }
    if (m_dialog.kind != DialogKind::None) {
        DrawDialog(vg);
    }
    if (m_time < m_notice_until && !m_notice.empty()) {
        // a short notice above the hint bar
        const float mark = m_notice_ok ? 28.f : 0.f; // something done: a green tick before it
        const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48 + mark;
        // it comes up from below with a little spring
        const float rise = ui::EaseOutBack(ui::Appear(m_notice_at, 0.35f));
        const float x = (kWidth - w) / 2, y = 600 + (1 - rise) * 34;
        nvgGlobalAlpha(vg, float(std::min(1.0, (m_notice_until - m_time) * 3)) * std::min(1.f, rise * 1.5f));
        NVGcolor fill = m_theme.panel2;
        if (m_notice_ok) {
            fill = m_theme.ok;
            fill.a = 0.18f;
            ui::FillRound(vg, x, y, w, 44, 22, m_theme.panel2);
        }
        ui::FillRound(vg, x, y, w, 44, 22, fill);
        ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, 43, 22, m_notice_ok ? m_theme.ok : m_theme.work, 1.5f);
        if (m_notice_ok) ui::CheckMark(vg, x + 30, y + 22, 14, m_theme.ok, 2.6f);
        ui::Text(vg, x + (w + mark) / 2, y + 22, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    DrawFinish(vg);
    DrawTour(vg);
    DrawWifiCard(vg);
    DrawBoot(vg);
    DrawStyleWave(vg);
    DrawTouchFeedback(vg);
    DrawSaver(vg);
    DrawScreenOff(vg);
}

void App::DrawTransitions(NVGcontext* vg) {
    // what is new appears out of the background: a veil of it that clears (the top bar stays)
    const auto veil = [&](float amount) {
        NVGcolor c = m_theme.bg;
        c.a = amount;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 73, kWidth, kHeight - 73);
        nvgFillColor(vg, c);
        nvgFill(vg);
    };
    const float tab_e = float(std::min(1.0, (m_time - m_tab_at) / 0.22));
    if (tab_e < 1) {
        veil((m_tab_from != 0 ? 0.45f : 0.9f) * (1 - tab_e)); // slid by a finger: it was already moving
    }
    const double age = m_time - m_detail_at;
    if (!GridTab() || age >= 0.3 || m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
        return;
    }
    // the page of a game grows out of its cover, and goes back into it
    const float p = float(age / 0.3), e = 1 - (1 - p) * (1 - p) * (1 - p);
    veil((ui::Lively() ? 0.4f : 0.85f) * (1 - e)); // lively, its parts come in one after another (7A)
    const float gx = kGridX + (m_focus % g_columns) * (g_cover + kGridGap);
    const float gy = kGridTop + (m_focus / g_columns) * g_row_pitch - m_scroll;
    const float dx = 48, dy = 72 + 34, ds = 330;
    const float k = m_detail ? e : 1 - e; // 0: in the grid, 1: on the page
    const float x = gx + (dx - gx) * k, y = gy + (dy - gy) * k, s = g_cover + (ds - g_cover) * k;
    const Game& g = m_games[m_detail_game];
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 10, s + 60, s + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x + 6, y + 12, s - 12, s, 14, 30, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, m_theme, x, y, s, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 10 + 4 * k,
              CoverOf(g));
    CoverLoading(vg, x, y, s, g.cover);
}

void App::InitRumble() {
    // the console in the hands, and player 1's controllers (two Joy-Con or a Pro Controller)
    m_vib_ok[0] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_handheld, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
    m_vib_ok[1] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_dual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
    m_vib_ok[2] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_full, 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
}

void App::SendRumble(bool on, float strength) {
    // short and gentle: a quarter of the strength, at the frequencies the console uses for a tap
    HidVibrationValue v[2];
    for (auto& value : v) {
        value.amp_low = on ? 0.25f * strength : 0.f;
        value.freq_low = 160.f;
        value.amp_high = on ? 0.20f * strength : 0.f;
        value.freq_high = 320.f;
    }
    if (m_vib_ok[0] && (hidGetNpadStyleSet(HidNpadIdType_Handheld) & HidNpadStyleTag_NpadHandheld)) {
        hidSendVibrationValues(m_vib_handheld, v, 2);
    }
    const u32 style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (m_vib_ok[1] && (style & HidNpadStyleTag_NpadJoyDual)) {
        hidSendVibrationValues(m_vib_dual, v, 2);
    }
    if (m_vib_ok[2] && (style & HidNpadStyleTag_NpadFullKey)) {
        hidSendVibrationValues(m_vib_full, v, 1);
    }
}

void App::TouchTick() {
    // a tap of 18 ms at less than half the strength of the end of a queue (17A)
    if (!m_ui.touch_rumble || m_rumble_step >= 0) return;
    SendRumble(true, 0.45f);
    m_tick_off_at = m_time + 0.018;
}

void App::FollowFinish() {
    // two pulses of 70 ms, 120 ms apart
    if (m_rumble_step >= 0 && m_time >= m_rumble_next) {
        static constexpr double kGaps[] = {0.07, 0.12, 0.07};
        const bool on = m_rumble_step % 2 == 0;
        SendRumble(on);
        if (m_rumble_step < 3) {
            m_rumble_next = m_time + kGaps[m_rumble_step];
            m_rumble_step++;
        } else {
            m_rumble_step = -1;
        }
    }
    if (!m_queue) {
        return;
    }
    const bool busy = m_queue->Busy();
    if (!busy && m_queue_was_busy) {
        // the queue has just ended: what this batch did
        for (const auto& j : m_queue->Jobs()) {
            const bool ended = j.state == InstallJob::State::Done || j.state == InstallJob::State::Failed;
            if (!ended || std::find(m_counted_jobs.begin(), m_counted_jobs.end(), j.id) != m_counted_jobs.end()) {
                continue;
            }
            m_counted_jobs.push_back(j.id);
            if (j.state == InstallJob::State::Done) {
                m_batch_done++;
                m_batch_bytes += j.size;
                // the last game installed falls into the dock (10A)
                m_finish_cover = j.cover;
                m_finish_key = j.color_key;
                m_finish_name = j.game;
            } else {
                m_batch_failed++;
            }
        }
        if (m_batch_done > 0) {
            const auto& s = Tr();
            char text[160];
            if (m_batch_failed > 0) {
                std::snprintf(text, sizeof(text), s.finish_some_failed, m_batch_done, m_batch_failed);
            } else {
                std::snprintf(text, sizeof(text), s.finish_all, m_batch_done, SizeText(m_batch_bytes).c_str());
            }
            if (m_batch_failed > 0) Notice(text);
            else NoticeOk(text);
            m_notice_until = m_time + 5;
            if (m_ui.finish_effect && m_batch_failed == 0 && ui::Lively()) {
                // the notice comes up once the game is in the dock
                m_notice_at = m_time + 1.0;
                m_notice_until = m_time + 6;
            }
            if (m_ui.finish_effect) {
                m_finish_at = m_time;
                m_rumble_step = 0;
                m_rumble_next = m_time;
                if (m_ui.sounds) {
                    m_sound.Play(synth::Effect::Finish);
                }
            }
        }
        m_batch_done = m_batch_failed = 0;
        m_batch_bytes = 0;
    }
    m_queue_was_busy = busy;
}

namespace {

// The cover that falls into the dock at the end of a queue (10A), drawn by ui::Dock between its
// back and its front.
struct Falling {
    const Theme* theme;
    float x, y, s, angle;
    unsigned color;
    const char* initial;
    int image;
};

void DrawFalling(NVGcontext* vg, void* data) {
    const auto& f = *static_cast<const Falling*>(data);
    nvgSave(vg);
    nvgTranslate(vg, f.x + f.s / 2, f.y + f.s / 2);
    nvgRotate(vg, f.angle);
    nvgTranslate(vg, -f.s / 2, -f.s / 2);
    nvgBeginPath(vg);
    nvgRect(vg, -16, -6, f.s + 32, f.s + 30);
    nvgFillPaint(vg, nvgBoxGradient(vg, 2, 8, f.s - 4, f.s, 10, 16, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, *f.theme, 0, 0, f.s, f.color, f.initial, nullptr, false, false, 10, f.image);
    nvgRestore(vg);
}

} // namespace

void App::DrawFinish(NVGcontext* vg) {
    // 10A: the cover of the last game installed falls into the slot of the dock (the drawing of the
    // icon), its bar lights, blue and violet sparks fly, and the green notice comes up. Gentle
    // animations show the game in the dock without the fall; with none, it shows still. The
    // buttons keep working all along.
    const double t = m_time - m_finish_at;
    const double total = 1.9;
    if (t < 0 || t > total) {
        return;
    }
    const Theme& th = m_theme;
    // the glow of the edges, as before
    const float glow = float(t < 0.25 ? t / 0.25 : std::max(0.0, 1 - (t - 0.25) / 1.15));
    NVGcolor edge = th.work;
    edge.a = 0.5f * glow;
    NVGcolor clear = th.work;
    clear.a = 0;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillPaint(vg, nvgBoxGradient(vg, 60, 60, kWidth - 120, kHeight - 120, 40, 220, clear, edge));
    nvgFill(vg);

    const bool lively = ui::Lively();
    // everything in and out: a light dark veil behind the dock
    const float shown = float(t < 0.15 ? t / 0.15 : t > 1.5 ? std::max(0.0, 1 - (t - 1.5) / 0.4) : 1.0);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 73, kWidth, 664 - 73);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.35f * shown));
    nvgFill(vg);
    nvgGlobalAlpha(vg, shown);
    const float ds = 256, dx = (kWidth - ds) / 2, dy = 196;
    // the light under the dock
    NVGcolor halo = th.work;
    halo.a = 0.3f;
    nvgBeginPath(vg);
    nvgCircle(vg, dx + ds / 2, dy + 170, 220);
    nvgFillPaint(vg, nvgRadialGradient(vg, dx + ds / 2, dy + 170, 40, 220, halo, clear));
    nvgFill(vg);
    // the fall: faster and faster, from above and a little turned, into the slot
    const float p = lively ? float(std::clamp((t - 0.1) / 0.55, 0.0, 1.0)) : 1.f;
    const float q = p * p;
    const float cs = 90 * (1.25f - 0.25f * q);
    const std::string initial = Initial(m_finish_name);
    Falling f{&th, dx + ds / 2 - cs / 2, dy - 170 + (90 + 170 - 0) * q, cs, -0.07f * (1 - q), PlaceholderColor(m_finish_key), initial.c_str(),
              m_covers.Get(m_finish_cover)};
    const float bar = lively ? ui::EaseOut(float(std::clamp((t - 0.6) / 0.4, 0.0, 1.0))) : 1.f;
    ui::Dock(vg, dx, dy, ds, bar, DrawFalling, &f);
    // the sparks, out of the slot
    if (lively && t > 0.62 && t < 1.35) {
        const float u = float((t - 0.62) / 0.7);
        for (int i = 0; i < 14; i++) {
            const float a = float(i) / 14 * 2 * NVG_PI + 0.2f;
            const float d = ui::EaseOut(u) * (110 + float(i % 3) * 40);
            nvgBeginPath(vg);
            nvgCircle(vg, dx + ds / 2 + std::cos(a) * d, dy + 114 + std::sin(a) * d * 0.7f, 5 * (1 - 0.7f * u));
            nvgFillColor(vg, Hex(i % 2 ? 0x7c5cff : 0x3db7ea, 1 - u));
            nvgFill(vg);
        }
    }
    nvgGlobalAlpha(vg, 1.f);
}

void App::DrawTouchFeedback(NVGcontext* vg) {
    // 15A: what the finger presses darkens a little and a soft wave grows out of the finger; let go,
    // the wave fades. With the animations off, it is lit while pressed, without the wave.
    const Theme& t = m_theme;
    if (m_az_touch || m_swipe || m_dragging) return;
    const bool held = m_touching && m_press_live;
    const double since = m_time - m_release_at;
    if (m_press_at < 0 || (!held && (m_release_at < m_press_at || since > 0.35))) {
        // nothing pressed, or its wave is gone
    } else {
        ui::HitRect r = m_press_rect;
        float radius = 10;
        if (m_press_kind == HitKind::Cover) {
            // the cover itself, not the gap around it
            r.x += kGridGap / 2;
            r.w -= kGridGap;
            r.h = std::min(r.h, g_cover);
        } else if (m_press_kind == HitKind::Tab || m_press_kind == HitKind::Chip) {
            radius = r.h / 2;
        }
        const float fade = held ? 1.f : float(std::max(0.0, 1 - since / 0.35));
        nvgSave(vg);
        nvgIntersectScissor(vg, r.x, r.y, r.w, r.h);
        if (m_press_kind != HitKind::Cover) {
            ui::FillRound(vg, r.x, r.y, r.w, r.h, radius, nvgRGBAf(0, 0, 0, (t.dark ? 0.10f : 0.05f) * fade));
        }
        if (ui::Still()) {
            NVGcolor lit = t.work;
            lit.a = 0.18f * fade;
            ui::FillRound(vg, r.x, r.y, r.w, r.h, radius, lit);
        } else {
            const float far = std::hypot(std::max(m_touch_x0 - r.x, r.x + r.w - m_touch_x0), std::max(m_touch_y0 - r.y, r.y + r.h - m_touch_y0));
            const float grow = ui::EaseOut(float(std::min(1.0, (m_time - m_press_at) / 0.45)));
            NVGcolor wave = t.dark ? nvgRGBAf(1, 1, 1, 0.16f) : t.work;
            if (!t.dark) wave.a = 0.16f;
            wave.a *= fade;
            nvgBeginPath(vg);
            nvgCircle(vg, m_touch_x0, m_touch_y0, std::max(8.f, far * grow));
            nvgFillColor(vg, wave);
            nvgFill(vg);
        }
        nvgRestore(vg);
    }
    // 18A: held still on a cover, a ring fills around the finger until its quick menu opens; on a
    // file of More › Files, until its menu opens
    const bool cover_hold = m_press_kind == HitKind::Cover && GridTab() && !m_detail && m_quick < 0;
    const bool file_hold = (m_press_kind == HitKind::MoreRow || m_press_kind == HitKind::MoreMark) && m_tab == Tab::More &&
                           m_more == MoreScreen::Files && !m_files_menu && !m_fwork;
    if (m_touching && m_press_live && !m_long_fired && (cover_hold || file_hold)) {
        const double hold = cover_hold ? 0.45 : 0.55;
        const float p = float((m_time - m_press_at - 0.12) / (hold - 0.12));
        if (p > 0) {
            NVGcolor track = t.work;
            track.a = 0.25f;
            nvgBeginPath(vg);
            nvgCircle(vg, m_touch_x0, m_touch_y0, 38);
            nvgStrokeColor(vg, track);
            nvgStrokeWidth(vg, 6);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgArc(vg, m_touch_x0, m_touch_y0, 38, -NVG_PI / 2, -NVG_PI / 2 + 2 * NVG_PI * std::min(1.f, p), NVG_CW);
            nvgStrokeColor(vg, t.work);
            nvgLineCap(vg, NVG_ROUND);
            nvgStroke(vg);
        }
    }
}

void App::Notice(const std::string& text) {
    m_notice = text;
    m_notice_at = m_time;
    m_notice_until = m_time + 3.5;
    m_notice_ok = false;
}

void App::NoticeOk(const std::string& text) {
    Notice(text);
    m_notice_ok = true;
    m_notice_until = m_time + 5;
}

void App::CheckSdSpace() {
    if (m_space_running && m_space_ready) {
        // what the thread read: the same as was read here before
        m_space_thread.join();
        m_space_running = false;
        m_space_ready = false;
        const SpaceRead& r = m_space_read;
        if (r.sd_free >= 0) {
            m_sd_free_bytes = r.sd_free;
            m_sd_free = SizeText(u64(r.sd_free));
        }
        if (r.sd_total > 0) m_sd_total_bytes = r.sd_total;
        m_nand_free_bytes = r.nand_free;
        m_nand_total_bytes = r.nand_total;
    }
    if (m_space_running || m_time - m_sd_checked < 10) {
        return;
    }
    m_sd_checked = m_time;
    m_space_running = true;
    // the values it starts from are kept when a read fails, as before
    m_space_read = SpaceRead{-1, -1, m_nand_free_bytes, m_nand_total_bytes};
    m_space_thread = std::thread([this] {
        SpaceRead& r = m_space_read;
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&fs))) {
            char root[FS_MAX_PATH] = "/";
            s64 free_bytes = 0, total_bytes = 0;
            if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, root, &free_bytes))) r.sd_free = free_bytes;
            if (R_SUCCEEDED(fsFsGetTotalSpace(&fs, root, &total_bytes)) && total_bytes > 0) r.sd_total = total_bytes;
            fsFsClose(&fs);
        }
        StorageSpace(NcmStorageId_BuiltInUser, r.nand_free, r.nand_total);
        m_space_ready = true;
    });
}

// Starts the catalog and the covers of the library linked, and takes a new catalog when it comes.
void App::FollowLink() {
    const Link::View link = m_link.Snapshot();
    if (link.state == Link::State::Connected) {
        const Config c = m_link.CurrentConfig();
        if (c.url != m_loaded_url) {
            m_loaded_url = c.url;
            m_covers.Stop();
            m_covers.Free(m_gfx.vg());
            m_covers.Start(c.url, c.key);
            m_catalog.Load(c.url, c.key);
        }
    }
    if (!m_shop_active && m_catalog.Version() != m_games_version) {
        m_games_version = m_catalog.Version();
        // the same game stays in focus; the first time, the one in focus when NXDock was closed
        m_keep_key = m_focus_restored ? FocusedKey() : m_ui.focus;
        {
            perf::Timer timer("app: take the catalog");
            m_games = m_catalog.TakeGames();
            m_games_epoch++;
        }
        if (!m_games.empty()) {
            if (!m_focus_restored) perf::Mark("first list of games taken");
            m_focus_restored = true;
        }
        RebuildView();
    }
    // once at start (the wizard's "look for updates"): how many updates the library has for what
    // is installed; − Options installs them
    if (!m_updates_told && m_focus_restored && !m_wizard && m_tab == Tab::Library) {
        m_updates_told = true;
        const size_t updates = m_ui.check_updates ? PendingUpdates().size() : 0;
        if (updates > 0) {
            char text[200];
            std::snprintf(text, sizeof(text), Tr().updates_found, int(updates));
            Notice(text);
            m_notice_until = m_time + 6;
        }
    }
}

std::string App::FocusedKey() const {
    if (m_focus < 0 || m_focus >= int(m_view.size()) || m_view[m_focus] >= int(m_games.size())) {
        return "";
    }
    return m_games[m_view[m_focus]].key;
}

void App::RebuildView() {
    perf::Timer timer("app: rebuild the view");
    // what the filters show and how many updates there are: counted here, the grid only reads them
    static_assert(kFilters == 5);
    m_filter_counts[0] = int(m_games.size());
    m_filter_counts[1] = m_filter_counts[2] = m_filter_counts[3] = m_filter_counts[4] = 0;
    m_pending_updates = 0;
    for (const auto& g : m_games) {
        m_filter_counts[1] += !g.installed.base;
        m_filter_counts[2] += g.installed.base;
        m_filter_counts[3] += g.UpdateAvailable() || g.MissingDlc();
        m_filter_counts[4] += g.DlcCount() > 0;
        m_pending_updates += UpdatePending(g);
    }
    if (m_keys_epoch != m_games_epoch || m_lower_names.size() != m_games.size()) {
        m_keys_epoch = m_games_epoch;
        m_lower_names.resize(m_games.size());
        m_letter_slots.resize(m_games.size());
        for (size_t i = 0; i < m_games.size(); i++) {
            m_lower_names[i] = Lower(m_games[i].name);
            m_letter_slots[i] = static_cast<unsigned char>(LetterSlot(m_games[i].name));
        }
        // A-Z once for this list (equal names keep their order, as the stable sorts below did)
        std::vector<int> order(m_games.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = int(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const auto sa = m_letter_slots[size_t(a)], sb = m_letter_slots[size_t(b)];
            return sa != sb ? sa < sb : m_lower_names[size_t(a)] < m_lower_names[size_t(b)];
        });
        m_name_rank.resize(m_games.size());
        for (size_t r = 0; r < order.size(); r++) m_name_rank[size_t(order[r])] = int(r);
    }
    const std::string keep = !m_keep_key.empty() ? m_keep_key : FocusedKey();
    m_keep_key.clear();
    m_view.clear();
    const std::string search = Lower(m_search);
    for (int i = 0; i < int(m_games.size()); i++) {
        const Game& g = m_games[i];
        bool keep = true;
        switch (m_filter) {
            case 1: keep = !g.installed.base; break;
            case 2: keep = g.installed.base; break;
            case 3: keep = g.UpdateAvailable() || g.MissingDlc(); break;
            case 4: keep = g.DlcCount() > 0; break;
            default: break;
        }
        if (keep && !search.empty()) {
            keep = m_lower_names[size_t(i)].find(search) != std::string::npos;
        }
        if (keep) {
            m_view.push_back(i);
        }
    }
    // the order: A-Z (with what starts with no letter first), size, or not installed first
    const auto& slots = m_letter_slots;
    const auto& rank = m_name_rank;
    std::fill(std::begin(m_letter_present), std::end(m_letter_present), false);
    for (int i : m_view) m_letter_present[slots[size_t(i)]] = true;
    const auto by_name = [&](int a, int b) { return rank[size_t(a)] < rank[size_t(b)]; };
    switch (m_ui.sort) {
        case 1:
            std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) {
                const u64 sa = m_games[a].SizeOfAll(), sb = m_games[b].SizeOfAll();
                return sa != sb ? sa > sb : by_name(a, b);
            });
            break;
        case 2:
            std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) {
                const bool ia = m_games[a].installed.base, ib = m_games[b].installed.base;
                return ia != ib ? !ia : by_name(a, b);
            });
            break;
        case 3:
            // the newest in the library first; without dates, as A-Z
            std::stable_sort(m_view.begin(), m_view.end(), [&](int a, int b) {
                const long long da = m_games[a].added, db = m_games[b].added;
                return da != db ? da > db : by_name(a, b);
            });
            break;
        default: std::stable_sort(m_view.begin(), m_view.end(), by_name); break;
    }
    if (!keep.empty()) {
        for (int v = 0; v < int(m_view.size()); v++) {
            if (m_games[m_view[v]].key == keep) {
                m_focus = v;
                break;
            }
        }
    }
    m_focus = std::clamp(m_focus, 0, std::max(0, int(m_view.size()) - 1));
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
    KeepFocusVisible();
}

void App::UpdateLibrary(u64 down, u64 held) {
    // the left stick and the pad move the focus; the right stick jumps by letter (5A)
    const u64 left = HidNpadButton_Left | HidNpadButton_StickLLeft, right = HidNpadButton_Right | HidNpadButton_StickLRight;
    const u64 up = HidNpadButton_Up | HidNpadButton_StickLUp, dn = HidNpadButton_Down | HidNpadButton_StickLDown;
    const int count = int(m_view.size());
    const int before = m_focus;
    if (m_shop_active && (down & HidNpadButton_B)) {
        // B leaves the shop: the list of shops again
        LeaveShop();
        m_shop_cache.clear();
        m_shop_cached = -1;
        return;
    }
    if (m_shop_active && (down & (HidNpadButton_ZL | HidNpadButton_ZR)) && !m_shops.empty()) {
        // ZL and ZR: the shop before or after
        const int n = int(m_shops.size());
        const int at = std::clamp(m_shop_opening, 0, n - 1);
        if (n > 1) OpenShopAt((at + ((down & HidNpadButton_ZR) ? 1 : n - 1)) % n);
        return;
    }
    if (Repeat(down, held, left) && m_focus % g_columns > 0) m_focus--;
    if (Repeat(down, held, right) && m_focus % g_columns < g_columns - 1 && m_focus + 1 < count) m_focus++;
    if (Repeat(down, held, up) && m_focus >= g_columns) m_focus -= g_columns;
    if (Repeat(down, held, dn) && m_focus + g_columns < count) m_focus += g_columns;
    if (count > 0) {
        const bool r_up = Repeat(down, held, HidNpadButton_StickRUp);
        const bool r_down = Repeat(down, held, HidNpadButton_StickRDown);
        if (held & (HidNpadButton_StickRUp | HidNpadButton_StickRDown)) m_rstick_at = m_time;
        if (r_up || r_down) {
            if (m_ui.sort == 0) {
                JumpToLetter(-1, r_down ? 1 : -1);
            } else {
                // other orders have no letters: a page (two rows)
                const int page = 2 * g_columns;
                m_focus = std::clamp(m_focus + (r_down ? page : -page), 0, count - 1);
            }
        }
    }
    if (m_focus != before) {
        KeepFocusVisible();
        m_touch_focus = false; // the pad took the focus: no bubble
        m_fling_v = 0;
    }
    if (down & (HidNpadButton_ZL)) {
        m_filter = (m_filter + kFilters - 1) % kFilters;
        m_wave_at = m_time;
        RebuildView();
    }
    if (down & (HidNpadButton_ZR)) {
        m_filter = (m_filter + 1) % kFilters;
        m_wave_at = m_time;
        RebuildView();
    }
    if ((down & HidNpadButton_A) && count > 0) {
        OpenDetail(m_focus);
    }
    if (down & HidNpadButton_Minus) {
        // Options (5A): the order and Update all; with updates to install, the focus is on it
        m_menu = true;
        m_menu_focus = m_pending_updates == 0 ? m_ui.sort : kSorts + 1;
    }
    if (down & HidNpadButton_Y) {
        std::string text;
        if (AskText(Tr().search_hint, m_search, false, text) || !m_search.empty()) {
            m_search = text;
            m_wave_at = m_time;
            RebuildView();
        }
    }
    if (down & HidNpadButton_X) {
        if (m_catalog.GetState() == Catalog::State::Failed ||
            (m_games.empty() && !m_shop_active && m_catalog.GetState() == Catalog::State::Ready)) {
            // again: after an error, or an empty library that may have games now
            const Config c = m_link.CurrentConfig();
            m_catalog.Load(c.url, c.key);
        } else if (m_games.empty() && m_catalog.GetState() == Catalog::State::Loading) {
            // a library that takes long to prepare its catalog: stop waiting
            m_catalog.Cancel();
        } else {
            m_tab = Tab::Queue;
        }
    }
}

void App::AskUpdateAll() {
    auto jobs = PendingUpdates();
    if (jobs.empty()) {
        return;
    }
    // every update goes where its game is; the question lists where, the internal memory too
    m_sd_checked = -100;
    CheckSdSpace();
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::UpdateAll;
    m_dialog.jobs = std::move(jobs);
}

void App::ActivateMenu(int row) {
    if (row == kSorts) {
        m_ui.library_size = (m_ui.library_size + 1) % 3;
        m_ui.Save();
        GridSize(m_ui.library_size, m_ui.focus_view == 1);
        m_scroll = m_scroll_target = 0;
        KeepFocusVisible();
        return;
    }
    if (row < kSorts) {
        if (m_ui.sort != row) {
            m_ui.sort = row;
            m_ui.Save();
            RebuildView();
        }
        m_menu = false;
        return;
    }
    m_menu = false;
    AskUpdateAll();
}

void App::UpdateMenu(u64 down, u64 held) {
    const int rows = m_pending_updates == 0 ? kSorts + 1 : kSorts + 2;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_menu_focus > 0) m_menu_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_menu_focus < rows - 1) m_menu_focus++;
    m_menu_focus = std::clamp(m_menu_focus, 0, rows - 1);
    if (down & HidNpadButton_A) {
        ActivateMenu(m_menu_focus);
    } else if ((down & HidNpadButton_Minus) && rows > kSorts + 1) {
        // − as its row shows: Update all
        ActivateMenu(kSorts + 1);
    } else if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        m_menu = false;
    }
}

bool App::AzBarShown() const {
    return GridTab() && !m_detail && m_ui.sort == 0 && !m_view.empty() && MaxScroll() > 0;
}

void App::JumpToLetter(int slot, int dir) {
    if (m_view.empty()) {
        return;
    }
    // where each letter starts in the view (the view is in A-Z order)
    int first[27];
    std::fill(std::begin(first), std::end(first), -1);
    for (int v = int(m_view.size()) - 1; v >= 0; v--) {
        first[LetterSlot(m_games[m_view[v]].name)] = v;
    }
    const int here = LetterSlot(m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]].name);
    int to = -1;
    if (dir == 0) {
        // the letter asked, or the nearest one with games (after it first)
        for (int d = 0; d < 27 && to < 0; d++) {
            if (slot + d < 27 && first[slot + d] >= 0) to = slot + d;
            else if (slot - d >= 0 && first[slot - d] >= 0) to = slot - d;
        }
    } else {
        for (int s = here + dir; s >= 0 && s < 27; s += dir) {
            if (first[s] >= 0) {
                to = s;
                break;
            }
        }
        if (to < 0) {
            to = here; // already at the first or the last letter: to its start
        }
    }
    if (to < 0) {
        return;
    }
    if (first[to] != m_focus || dir != 0) {
        m_focus = first[to];
        KeepFocusVisible();
    }
    if (m_jump_letter != to || m_time - m_jump_at > 0.3) {
        m_jump_at = m_time;
    }
    m_jump_letter = to;
}

void App::DrawAzBar(NVGcontext* vg) {
    const Theme& t = m_theme;
    if (!AzBarShown()) {
        // the plain scroll indicator, when there is no letter to jump to
        if (MaxScroll() > 0 && GridTab() && !m_view.empty()) {
            const float track_h = g_view_h;
            const float thumb_h = track_h * g_view_h / (g_view_h + MaxScroll());
            const float thumb_y = kGridTop + (track_h - thumb_h) * (m_scroll / MaxScroll());
            NVGcolor c = t.muted;
            c.a = 0.45f;
            ui::FillRound(vg, g_az_x + 13, thumb_y, 4, thumb_h, 2, c);
        }
        return;
    }
    const bool* present = m_letter_present;
    const int here = LetterSlot(m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]].name);
    NVGcolor back = t.panel2;
    back.a = m_az_touch ? 0.95f : 0.7f;
    ui::FillRound(vg, g_az_x, kAzTop, kAzW, g_az_height, kAzW / 2, back);
    if (m_az_touch) {
        ui::StrokeRound(vg, g_az_x + .5f, kAzTop + .5f, kAzW - 1, g_az_height - 1, kAzW / 2, t.work, 1.5f);
    }
    const float pitch = g_az_height / 27;
    for (int i = 0; i < 27; i++) {
        const char label[2] = {i == 0 ? '#' : char('A' + i - 1), 0};
        const float y = kAzTop + pitch * (i + 0.5f);
        NVGcolor c = i == here ? t.work : t.muted;
        if (!present[i]) c.a = 0.35f;
        ui::Text(vg, g_az_x + kAzW / 2, y, i == here ? 13 : 11, c, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    // the whole edge answers to the finger (see ReadTouch); nothing here is a tap target of its own
}

void App::DrawJumpLetter(NVGcontext* vg) {
    const double age = m_time - m_jump_at;
    const double hold = m_az_touch ? 1e9 : 0.6; // stays while the finger is on the bar
    if (m_jump_letter < 0 || age > hold + 0.25 || !GridTab() || m_detail) {
        return;
    }
    const Theme& t = m_theme;
    const float alpha = float(age < hold ? 1.0 : 1 - (age - hold) / 0.25);
    const char label[2] = {m_jump_letter == 0 ? '#' : char('A' + m_jump_letter - 1), 0};
    const float s = 150, x = (kWidth - s) / 2, y = kGridTop + g_view_h / 2 - s / 2;
    nvgGlobalAlpha(vg, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, x - 40, y - 30, s + 80, s + 90);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, s, s, 28, 40, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    NVGcolor panel = t.panel;
    panel.a = 0.94f;
    ui::FillRound(vg, x, y, s, s, 28, panel);
    ui::StrokeRound(vg, x + 1, y + 1, s - 2, s - 2, 27, t.work, 2);
    ui::Text(vg, x + s / 2, y + s / 2 + 4, 96, t.text, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgGlobalAlpha(vg, 1.f);
}

void App::DrawMenu(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    // everything behind goes a little dark; a tap there closes it
    nvgBeginPath(vg);
    nvgRect(vg, 0, 73, kWidth, kHeight - 73);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.35f));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::MenuBack);
    const size_t updates = m_pending_updates;
    const int rows = updates ? kSorts + 2 : kSorts + 1;
    const float w = 440, rh = 56, pad = 10;
    const float h = pad * 2 + 34 + kSorts * rh + rh + 13 + (updates ? rh : 0);
    const float x = kWidth - 36 - w, y = 664 - 14 - h;
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 20, w + 60, h + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, w, h, 16, 36, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, w, h, 16);
    AddHit(x, y, w, h, HitKind::Button); // a tap on the panel itself does nothing
    ui::Text(vg, x + 22, y + pad + 17, 14, t.muted, s.options);
    const char* sorts[] = {s.sort_az, s.sort_size, s.sort_missing, s.sort_recent};
    float ry = y + pad + 34;
    for (int i = 0; i < rows; i++) {
        if (i == kSorts) {
            nvgBeginPath(vg);
            nvgRect(vg, x + 16, ry + 6, w - 32, 1);
            nvgFillColor(vg, t.line);
            nvgFill(vg);
            ry += 13;
        }
        if (i == m_menu_focus) {
            ui::FillRound(vg, x + pad, ry, w - 2 * pad, rh, 10, t.panel2);
            ui::StrokeRound(vg, x + pad, ry, w - 2 * pad, rh, 10, t.work, 3);
        }
        AddHit(x + pad, ry, w - 2 * pad, rh, HitKind::MenuRow, i);
        if (i == kSorts) {
            // the size of the covers: A changes it, the menu stays open to see it
            const char* sizes[] = {TrM().lb_size_big, TrM().ic_size_mid, TrM().lb_size_small};
            ui::Text(vg, x + pad + 26, ry + rh / 2 + 1, 20, t.work, ui::glyph::A, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            ui::Text(vg, x + pad + 50, ry + rh / 2, 18, t.text, TrM().lb_size);
            ui::Text(vg, x + w - pad - 18, ry + rh / 2, 16, t.work, sizes[std::clamp(m_ui.library_size, 0, 2)], NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        } else if (i < kSorts) {
            const bool on = m_ui.sort == i;
            nvgBeginPath(vg);
            nvgCircle(vg, x + pad + 26, ry + rh / 2, on ? 7.5f : 10);
            if (on) {
                nvgStrokeColor(vg, t.work);
                nvgStrokeWidth(vg, 6);
            } else {
                nvgStrokeColor(vg, t.muted);
                nvgStrokeWidth(vg, 2);
            }
            nvgStroke(vg);
            ui::Text(vg, x + pad + 50, ry + rh / 2, 18, t.text, sorts[i]);
        } else {
            ui::Text(vg, x + pad + 26, ry + rh / 2 + 1, 22, t.work, ui::glyph::Minus, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            ui::Text(vg, x + pad + 50, ry + rh / 2, 18, t.text, s.update_all);
            ui::Text(vg, x + w - pad - 18, ry + rh / 2, 15, t.muted, std::to_string(updates).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        ry += rh;
    }
    std::vector<ui::HitRect> rects;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.choose, HidNpadButton_A}};
    if (updates) hints.push_back({ui::glyph::Minus, s.update_all, HidNpadButton_Minus});
    hints.push_back({ui::glyph::B, s.close, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, rects);
    AddHints(rects);
}

void App::OpenDetail(int view_index) {
    if (view_index < 0 || view_index >= int(m_view.size())) {
        return;
    }
    m_detail = true;
    m_touch_focus = false;
    m_detail_game = m_view[view_index];
    m_detail_from_console = false;
    m_detail_first = 0;
    // where, as chosen in the wizard: the internal memory still asks every time (EnqueuePicked)
    m_to_nand = false; // the SD card, always; the internal memory only when chosen for this install
    m_dest_touched = false;
    // what is missing is marked: the game if not installed, a newer update, the DLC not installed;
    // or, as chosen in the wizard, only the game, or the game and its update
    const Game& g = m_games[m_detail_game];
    m_detail_pick.assign(g.files.size(), 0);
    const CatalogFile* newest = g.NewestUpdate();
    for (size_t i = 0; i < g.files.size(); i++) {
        const CatalogFile& f = g.files[i];
        switch (f.kind) {
            case CatalogFile::Kind::Game: m_detail_pick[i] = !g.installed.base; break;
            case CatalogFile::Kind::Update: m_detail_pick[i] = m_ui.pick != 1 && &f == newest && f.version > g.installed.patch; break;
            case CatalogFile::Kind::Dlc: m_detail_pick[i] = m_ui.pick == 0 && !g.installed.dlc.count(f.title_id); break;
        }
    }
    // what is missing is already marked: the focus starts on Install, after the rows
    int rows = 0;
    for (const auto& f : g.files) rows += f.kind != CatalogFile::Kind::Update || &f == newest;
    m_detail_focus = rows;
}

void App::UpdateDetail(u64 down, u64 held) {
    const Game& g = m_games[m_detail_game];
    // the rows: the files shown (the game, the newest update, every DLC), then Install
    std::vector<int> rows;
    const CatalogFile* newest = g.NewestUpdate();
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind != CatalogFile::Kind::Update || &g.files[i] == newest) rows.push_back(i);
    }
    const int count = int(rows.size()) + 1;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_detail_focus > 0) m_detail_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_detail_focus < count - 1) m_detail_focus++;
    if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
        // the SD card or the internal memory, for this install only (it asks before going there)
        m_to_nand = !m_to_nand;
        m_dest_touched = true;
    }
    if (down & HidNpadButton_A) {
        if (m_detail_focus < int(rows.size())) {
            m_detail_pick[rows[m_detail_focus]] ^= 1;
        } else {
            EnqueuePicked();
        }
    }
    if ((down & HidNpadButton_Y) && (g.installed.base || !g.installed.dlc.empty())) {
        if (m_queue->StartVerify(g.title_id)) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Verify;
            m_dialog.game = m_detail_game;
            m_dialog.single = true;
        } else {
            Notice(Tr().verify_wait);
        }
    }
    if ((down & HidNpadButton_Minus) && g.installed.base && !m_shop_active) {
        OpenVersions(); // another version of the update (20A)
        return;
    }
    if ((down & HidNpadButton_X) && g.installed.base && !m_shop_active) {
        // the cheats and mods of this game; B comes back here
        OpenMods();
        OpenModsGame(g.title_id, g.name);
        m_mods_from_detail = true;
        m_tab = Tab::More;
        return;
    }
    if (down & HidNpadButton_B) {
        m_detail = false;
        if (m_detail_from_console) {
            // opened from In the console: back there
            m_detail_from_console = false;
            m_tab = Tab::Installed;
        }
    }
}

void App::DrawLibrary(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const Config linked = m_link.CurrentConfig();
    const size_t scheme = linked.url.find("://");
    const std::string where = linked.url.empty() ? std::string() : linked.url.substr(scheme == std::string::npos ? 0 : scheme + 3);
    const Catalog::State state = m_catalog.GetState();

    if (m_games.empty()) {
        // loading, or it could not be read, or there is nothing in it
        if (m_wifi == -1 && state != Catalog::State::Loading) {
            DrawNoNetwork(vg, s.retry);
            return;
        }
        if (state == Catalog::State::Ready && !m_shop_active) {
            // the library answered with no game at all: say so, and how to fill it
            const More& m = TrM();
            ui::Text(vg, kWidth / 2, 300, 24, t.text, m.lb_empty_t, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgSave(vg);
            nvgFontFace(vg, "ui");
            nvgFontSize(vg, 17);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
            nvgTextLineHeight(vg, 1.3f);
            nvgFillColor(vg, t.muted);
            nvgTextBox(vg, kWidth / 2 - 330, 330, 660, m.lb_empty, nullptr);
            nvgRestore(vg);
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, m.lb_reload, HidNpadButton_X}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                        m_hint_rects);
            return;
        }
        if (state == Catalog::State::Failed) {
            // the card of an error, as in the queue: what happened, what to do, the code small
            const std::string error = m_catalog.Error();
            const float cw = 700, ch = ErrorCard(vg, t, 0, 0, cw, error, false);
            ErrorCard(vg, t, (kWidth - cw) / 2, std::max(96.f, 360 - ch / 2), cw, error, true);
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, s.retry, HidNpadButton_X}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                        m_hint_rects);
        } else {
            // the shape of the library while it comes, a light running over it (14A); below, how far
            // a large library is in preparing its catalog the first time
            int done = 0, total = 0;
            char line[160];
            const bool preparing = m_catalog.Preparing(done, total);
            if (preparing && total > 0) {
                std::snprintf(line, sizeof(line), TrM().cat_preparing_fmt, done, total);
            } else {
                std::snprintf(line, sizeof(line), "%s", preparing ? TrM().cat_preparing : s.loading_library);
            }
            DrawSkeleton(vg, line, total > 0 ? float(done) / float(total) : 0.f, preparing && total > 0);
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, s.cancel, HidNpadButton_X}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                        m_hint_rects);
        }
        return;
    }
    // the list kept on the SD card is on screen: whether a new one is coming or did not come is
    // said on the left of the hint bar, in place of the address (option A of the visual test)
    std::string status = where;
    {
        int done = 0, total = 0;
        const bool refreshing = state == Catalog::State::Loading;
        if ((refreshing || m_catalog.Stale()) && !m_shop_active) {
            char line[200];
            if (refreshing && m_catalog.Preparing(done, total) && total > 0) {
                std::snprintf(line, sizeof(line), TrM().cat_preparing_fmt, done, total);
            } else if (refreshing) {
                std::snprintf(line, sizeof(line), "%s", s.loading_library);
            } else {
                std::snprintf(line, sizeof(line), "%s", TrM().cat_stale_short);
            }
            status = line;
        }
    }

    // Filters, with what each shows; in a shop, a pill for each shop
    float x = kGridX;
    if (m_shop_active) {
        DrawShopChips(vg);
    } else {
    const int* counts = m_filter_counts;
    const char* names[] = {s.all, s.not_installed, TrM().lb_installed, s.with_update, s.dlc};
    char chips[kFilters][64];
    float chip_x[kFilters], chip_w[kFilters];
    for (int i = 0; i < kFilters; i++) {
        std::snprintf(chips[i], sizeof(chips[i]), "%s · %d", names[i], counts[i]);
        chip_x[i] = x;
        chip_w[i] = ui::Chip(vg, t, x, 92, chips[i], false);
        AddHit(x - 4, 82, chip_w[i] + 8, 54, HitKind::Chip, i); // taller than drawn: easy to hit
        x += chip_w[i] + 10;
    }
    // the pill of the filter chosen glides to it (9A), its name over it
    const int on = std::clamp(m_filter, 0, kFilters - 1);
    const float px = m_chip_x.Step(chip_x[on]), pw = m_chip_w.Step(chip_w[on]);
    ui::FillRound(vg, px, 92, pw, 34, 17, t.work);
    nvgSave(vg);
    nvgIntersectScissor(vg, px, 92, pw, 34);
    ui::Text(vg, chip_x[on] + chip_w[on] / 2, 109, 16, t.on_work, chips[on], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    }
    if (!m_search.empty()) {
        const std::string label = "“" + m_search + "”  ×";
        const float w = ui::Chip(vg, t, x + 10, 92, label.c_str(), true);
        AddHit(x + 6, 82, w + 8, 54, HitKind::SearchChip);
    }
    // checking what the console has: a small sign, the marks come when it ends
    if (m_queue && m_queue->ReadingInstalled()) {
        const char* checking = TrM().lb_checking;
        const float cw = ui::TextWidth(vg, 13, checking);
        ui::Spinner(vg, kWidth - kGridX - cw - 150, 109, 6, t.work, m_time);
        ui::Text(vg, kWidth - kGridX - cw - 138, 109, 13, t.muted, checking);
    }
    // the order, on the right (changed in Options)
    const char* sorts[] = {s.sort_az, s.sort_size, s.sort_missing, s.sort_recent};
    ui::Text(vg, kWidth - kGridX, 109, 15, t.muted, sorts[std::clamp(m_ui.sort, 0, kSorts - 1)], NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    // Grid of covers, clipped to its viewport and scrolled.
    nvgSave(vg);
    nvgScissor(vg, 0, kGridTop - kGridMargin, kWidth, g_view_h + 2 * kGridMargin);
    if (m_focus != m_lift_focus) {
        // the cover in focus lifts and leans a little toward where the focus came from
        if (m_lift_focus >= 0) {
            m_lift_dx = float((m_focus % g_columns) - (m_lift_focus % g_columns));
            m_lift_dy = float((m_focus / g_columns) - (m_lift_focus / g_columns));
        }
        m_lift_focus = m_focus;
        m_lift_at = m_time;
    }
    // only the rows that can be on screen are looked at: a library of thousands costs as one of a few
    const int row_lo = std::max(0, int(std::floor((m_scroll - kGridMargin - g_cover) / g_row_pitch)));
    const int row_hi = std::max(0, int(std::floor((m_scroll + g_view_h + kGridMargin) / g_row_pitch)) + 1);
    const int i_lo = std::min(int(m_view.size()), row_lo * g_columns);
    const int i_hi = std::min(int(m_view.size()), (row_hi + 1) * g_columns);
    for (int i = i_lo; i < i_hi; i++) {
        const float cx = kGridX + (i % g_columns) * (g_cover + kGridGap);
        const float cy = kGridTop + (i / g_columns) * g_row_pitch - m_scroll;
        if (cy + g_cover < kGridTop - kGridMargin || cy > kGridTop + g_view_h + kGridMargin) {
            continue;
        }
        const Game& g = m_games[m_view[i]];
        // Only the visible part of a cover can be tapped.
        const float top = std::max(cy, kGridTop - kGridMargin), bottom = std::min(cy + g_cover, kGridTop + g_view_h + kGridMargin);
        if (bottom > top) {
            AddHit(cx - kGridGap / 2, top, g_cover + kGridGap, bottom - top, HitKind::Cover, i);
        }
        if (i == m_focus) {
            continue; // drawn last, over its neighbours
        }
        // a wave from the top left when the filter changes or NXDock opens (9A, 4A), and the cover
        // under a finger sinks a little (15A)
        const int col = i % g_columns, row = i / g_columns - int(m_scroll / g_row_pitch);
        const double at = m_wave_at + 0.035 * double(std::max(0, row) + col);
        const float in = m_time - m_wave_at < 1.5 ? ui::Appear(at, 0.42f) : 1.f;
        const float pressed = i == m_press_cover && m_press_live && m_touching ? ui::EaseOut(ui::Appear(m_press_at, 0.08f)) : 0.f;
        nvgSave(vg);
        if (in < 1 || pressed > 0) {
            const float k = ui::Lively() ? ui::EaseOutBack(in) : 1.f;
            const float sc = (0.9f + 0.1f * k) * (1 - 0.06f * pressed);
            nvgTranslate(vg, cx + g_cover / 2, cy + g_cover / 2 + (1 - k) * 16);
            nvgScale(vg, sc, sc);
            nvgTranslate(vg, -(cx + g_cover / 2), -(cy + g_cover / 2));
            nvgGlobalAlpha(vg, std::min(1.f, in * 1.25f));
        }
        // no format on the covers: it is in the band of the game in focus (5A)
        ui::Cover(vg, t, cx, cy, g_cover, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 10, CoverOf(g));
        CoverLoading(vg, cx, cy, g_cover, g.cover);
        InstalledBadge(vg, g, cx, cy, g_cover);
        nvgRestore(vg);
    }
    if (m_focus >= 0 && m_focus < int(m_view.size())) {
        // a little more room above and below for the lifted cover and its glow
        nvgScissor(vg, 0, kGridTop - kGridMargin - 10, kWidth, g_view_h + 2 * kGridMargin + 20);
        const float cx = kGridX + (m_focus % g_columns) * (g_cover + kGridGap);
        const float cy = kGridTop + (m_focus / g_columns) * g_row_pitch - m_scroll;
        const Game& g = m_games[m_view[m_focus]];
        const float in = m_time - m_wave_at < 1.5 ? ui::Appear(m_wave_at + 0.035 * double(m_focus % g_columns), 0.3f) : 1.f;
        nvgGlobalAlpha(vg, std::min(1.f, in * 1.25f));
        DrawLiftedCover(vg, g, cx, cy, g_cover);
        nvgGlobalAlpha(vg, 1.f);
        if (m_touch_focus && m_quick < 0 && cy + g_cover > kGridTop && cy < kGridTop + g_view_h) {
            // marked by a finger: a bubble on it says a second touch opens it (19A)
            const char* open = TrM().tap_open;
            const float bw = ui::TextWidth(vg, 17, open) + 48, bh = 40;
            const float bx = cx + g_cover / 2 - bw / 2, by = std::min(cy + g_cover - bh / 2 - 6, kGridTop + g_view_h - bh);
            const float pop = ui::EaseOutBack(ui::Appear(m_touch_focus_at + 0.1, 0.3f));
            nvgSave(vg);
            nvgTranslate(vg, bx + bw / 2, by + bh / 2);
            nvgScale(vg, 0.6f + 0.4f * pop, 0.6f + 0.4f * pop);
            nvgTranslate(vg, -(bx + bw / 2), -(by + bh / 2));
            nvgGlobalAlpha(vg, std::min(1.f, pop * 1.5f));
            nvgBeginPath(vg);
            nvgRect(vg, bx - 20, by - 10, bw + 40, bh + 34);
            nvgFillPaint(vg, nvgBoxGradient(vg, bx, by + 6, bw, bh, bh / 2, 14, nvgRGBAf(0, 0, 0, 0.35f), nvgRGBAf(0, 0, 0, 0)));
            nvgFill(vg);
            ui::FillRound(vg, bx, by, bw, bh, bh / 2, t.work);
            ui::Text(vg, bx + 18, by + bh / 2, 17, t.on_work, open);
            // the arrow
            const float ax = bx + bw - 20, ay = by + bh / 2;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ax - 3, ay - 6);
            nvgLineTo(vg, ax + 3, ay);
            nvgLineTo(vg, ax - 3, ay + 6);
            nvgStrokeColor(vg, t.on_work);
            nvgStrokeWidth(vg, 2.2f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
            nvgRestore(vg);
            AddHit(bx - 6, by - 6, bw + 12, bh + 12, HitKind::OpenBubble);
        }
    }
    nvgRestore(vg);
    if (m_view.empty()) {
        ui::Text(vg, kGridX + (g_az_x - kGridX) / 2, kGridTop + g_view_h / 2, 18, t.muted, s.empty_filter, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // pulled down at the top: the icon of NXDock and a ring that fills; let go full, the library is
    // read again (21A)
    if ((m_scroll < -2 || m_refreshing) && !m_shop_active) {
        const float pull = std::max(0.f, -m_scroll);
        const float p = std::clamp(pull / kPullArm, 0.f, 1.f);
        const float cxp = kGridX + (g_az_x - kGridX) / 2, cyp = kGridTop + std::max(26.f, pull / 2) - 4;
        nvgGlobalAlpha(vg, m_refreshing ? 1.f : std::min(1.f, p * 1.4f));
        ui::Brand(vg, cxp - 16, cyp - 16, 32);
        NVGcolor track = t.work;
        track.a = 0.2f;
        nvgBeginPath(vg);
        nvgCircle(vg, cxp, cyp, 25);
        nvgStrokeColor(vg, track);
        nvgStrokeWidth(vg, 3.5f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        if (m_refreshing) {
            const float a0 = ui::Still() ? 0.f : float(m_time) * 6.f;
            nvgArc(vg, cxp, cyp, 25, a0, a0 + 1.8f, NVG_CW);
        } else {
            nvgArc(vg, cxp, cyp, 25, -NVG_PI / 2, -NVG_PI / 2 + 2 * NVG_PI * std::max(0.01f, p), NVG_CW);
        }
        nvgStrokeColor(vg, t.work);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
        nvgGlobalAlpha(vg, 1.f);
    }

    // The A-Z bar, or the scroll indicator when the order has no letters; with the bar, a thin line
    // beside it says where the grid is while it moves (16A)
    DrawAzBar(vg);
    if (AzBarShown() && m_time - m_scroll_seen_at < 1.0 && MaxScroll() > 0) {
        const float track_h = g_view_h, thumb_h = std::max(30.f, track_h * g_view_h / (g_view_h + MaxScroll()));
        const float thumb_y = kGridTop + (track_h - thumb_h) * std::clamp(m_scroll / MaxScroll(), 0.f, 1.f);
        NVGcolor c = t.muted;
        c.a = 0.55f * float(std::min(1.0, (1.0 - (m_time - m_scroll_seen_at)) * 3));
        ui::FillRound(vg, g_az_x - 9, thumb_y, 4, thumb_h, 2, c);
    }

    // The game in focus: a band under the covers (2A) or a panel on the right (2B), as chosen in
    // Settings; the queue at work floats as a pill (3A), nothing when it is empty
    const bool pill = QueueAtWork();
    if (!pill) m_pill_shown = false; // it pops in again the next time
    if (!m_view.empty()) {
        const Game& g = m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]];
        if (m_ui.focus_view == 1) DrawFocusPanel(vg, g, pill);
        else DrawFocusBand(vg, g, pill);
    }
    if (pill && (m_ui.focus_view != 1 || m_view.empty())) DrawQueuePill(vg, kWidth - 36 - 400, 600, 400);

    static const std::string shop_glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.view, HidNpadButton_A}};
    if (m_shop_active) {
        // in a shop: ZL and ZR go to the shop before or after, B to the list of shops
        if (m_shops.size() > 1) hints.push_back({shop_glyphs.c_str(), TrM().sh_shop_hint, HidNpadButton_ZR});
    } else {
        hints.push_back({ui::glyph::ZR, s.filter, HidNpadButton_ZR});
    }
    hints.push_back({ui::glyph::Y, s.search, HidNpadButton_Y});
    hints.push_back({ui::glyph::X, s.queue, HidNpadButton_X});
    // Options: the order, and Update all when there is something to update
    const size_t updates = m_pending_updates;
    m_update_hint = s.options;
    if (updates) {
        m_update_hint += std::string(" · ") + s.update_all + " " + std::to_string(updates);
    }
    hints.push_back({ui::glyph::Minus, m_update_hint.c_str(), HidNpadButton_Minus});
    if (m_shop_active) hints.push_back({ui::glyph::B, TrM().sh_shops_hint, HidNpadButton_B});
    else hints.push_back({ui::glyph::Plus, s.exit, HidNpadButton_Plus});
    ui::HintBar(vg, t, status.c_str(), hints, m_hint_rects);
}

namespace {
std::string ShortDuration(double seconds);
float JobFraction(const InstallQueue::Live& l);
} // namespace

void App::DrawBackdrop(NVGcontext* vg, const Game& g, bool at_once) {
    // 1A: the cover of the game in focus, made tiny and soft (Covers::Thumb), stretched behind
    // everything under a veil, so the screen takes the colours of the game. It changes when the
    // focus rests 0.15 s (moving fast, it does not flicker) and fades in; a cover without a picture
    // gives its colour. Three images: the one shown, the one fading out, and a spare that the GPU is
    // no longer drawing, so writing it never touches a picture in use.
    const Theme& t = m_theme;
    const std::string want = g.cover.empty() ? "#" + g.key : g.cover;
    if (want != m_bd_want) {
        m_bd_want = want;
        m_bd_want_at = m_time;
    }
    if (m_bd_key != m_bd_want && (at_once || m_time - m_bd_want_at >= 0.15)) {
        unsigned char px[Covers::kThumb * Covers::kThumb * 4];
        const bool has = !g.cover.empty() && m_covers.Thumb(g.cover, px);
        // a picture still on its way is waited for; one that never comes gives the colour
        if (has || g.cover.empty() || !m_covers.Loading(g.cover)) {
            int slot = 0;
            while (slot == m_bd_front || slot == m_bd_back) slot++;
            if (has) {
                if (!m_bd_img[slot]) m_bd_img[slot] = nvgCreateImageRGBA(vg, Covers::kThumb, Covers::kThumb, 0, px);
                else nvgUpdateImage(vg, m_bd_img[slot], px);
            }
            m_bd_has[slot] = has && m_bd_img[slot];
            m_bd_color[slot] = PlaceholderColor(g.key);
            m_bd_back = m_bd_front;
            m_bd_front = slot;
            m_bd_key = m_bd_want;
            m_bd_at = m_time;
        }
    }
    if (m_bd_front < 0) return;
    const float base = t.dark ? 0.55f : 0.42f;
    const auto layer = [&](int slot, float alpha) {
        if (slot < 0 || alpha <= 0.01f) return;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, kWidth, kHeight);
        if (m_bd_has[slot]) {
            // square, wider than the screen: its middle shows
            nvgFillPaint(vg, nvgImagePattern(vg, -100, (kHeight - 1480) / 2, 1480, 1480, 0, m_bd_img[slot], base * alpha));
        } else {
            nvgFillPaint(vg, nvgRadialGradient(vg, 360, 300, 60, 900, Hex(m_bd_color[slot], (t.dark ? 0.30f : 0.20f) * alpha), Hex(0, 0)));
        }
        nvgFill(vg);
    };
    const float fade = ui::Still() ? 1.f : ui::EaseOut(float(std::min(1.0, (m_time - m_bd_at) / 0.35)));
    if (fade < 1) layer(m_bd_back, 1);
    layer(m_bd_front, fade);
    // the veil: the text stays easy to read, more of it near the bottom
    NVGcolor top = t.bg, mid = t.bg, low = t.bg;
    top.a = 0.62f;
    mid.a = 0.55f;
    low.a = 0.84f;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, 400);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, 400, top, mid));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 400, kWidth, kHeight - 400);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 400, 0, kHeight, mid, low));
    nvgFill(vg);
}

void App::FocusPills(NVGcontext* vg, const Game& g, float x, float y, float right, bool wrap) {
    // what matters of the game in focus, in small pills: its format, its size, its DLC, whether
    // it is on the console (green), and what it lacks (cyan)
    const Theme& t = m_theme;
    const auto& s = Tr();
    struct Pill { std::string text; NVGcolor color; bool tinted; int mark; }; // mark: 0 none, 1 tick, 2 arrow
    std::vector<Pill> pills;
    const std::string format = FormatOf(g);
    if (!format.empty()) pills.push_back({format, t.text, false, 0});
    pills.push_back({SizeText(g.SizeOfAll()), t.text, false, 0});
    if (!g.Dlc().empty()) pills.push_back({std::to_string(g.Dlc().size()) + " DLC", t.text, false, 0});
    if (g.installed.base) pills.push_back({s.on_console, t.ok, true, 1});
    else pills.push_back({s.not_on_console, t.muted, false, 0});
    if (g.installed.base && g.UpdateAvailable() && g.NewestUpdate()) pills.push_back({VersionText(g.NewestUpdate()->version), t.cyan, true, 2});
    else if (g.installed.base && g.MissingDlc()) pills.push_back({TrM().lb_needs_dlc, t.cyan, true, 0});
    float px = x, py = y;
    for (const auto& p : pills) {
        const float w = ui::TextWidth(vg, 15, p.text.c_str()) + 24 + (p.mark ? 18 : 0);
        if (px + w > right) {
            if (!wrap || px == x) break;
            px = x;
            py += 34;
        }
        NVGcolor fill = p.tinted ? p.color : t.panel2;
        fill.a = p.tinted ? 0.13f : 0.85f;
        ui::FillRound(vg, px, py, w, 28, 14, fill);
        NVGcolor edge = p.tinted ? p.color : t.line;
        edge.a = p.tinted ? 0.5f : 1.f;
        ui::StrokeRound(vg, px + .5f, py + .5f, w - 1, 27, 13.5f, edge, 1);
        float tx = px + 12;
        if (p.mark == 1) {
            ui::CheckMark(vg, tx + 6, py + 14, 11, p.color, 2);
            tx += 18;
        } else if (p.mark == 2) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, tx + 6, py + 20);
            nvgLineTo(vg, tx + 6, py + 8);
            nvgMoveTo(vg, tx + 1.5f, py + 12.5f);
            nvgLineTo(vg, tx + 6, py + 8);
            nvgLineTo(vg, tx + 10.5f, py + 12.5f);
            nvgStrokeColor(vg, p.color);
            nvgStrokeWidth(vg, 2);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
            tx += 18;
        }
        ui::Text(vg, tx, py + 14, 15, p.color, p.text.c_str());
        px += w + 8;
    }
}

namespace {

// The button of the game in focus: A and "View and install", in the blue of work; tapping it is A.
float FocusButton(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, const char* label) {
    nvgBeginPath(vg);
    nvgRect(vg, x - 20, y - 10, w + 40, h + 36);
    NVGcolor glow = t.work;
    glow.a = 0.35f;
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 6, w, h, 16, 18, glow, Hex(0, 0)));
    nvgFill(vg);
    ui::FillRound(vg, x, y, w, h, 16, t.work);
    const float gw = ui::SymbolWidth(vg, 24, ui::glyph::A);
    const float lw = ui::TextWidth(vg, 19, label);
    const float start = x + (w - gw - 10 - lw) / 2;
    ui::Symbol(vg, t, start + gw / 2, y + h / 2, 24, ui::glyph::A, t.on_work, HidNpadButton_A);
    ui::Text(vg, start + gw + 10, y + h / 2, 19, t.on_work, label);
    return w;
}

} // namespace

void App::DrawFocusBand(NVGcontext* vg, const Game& g, bool pill) {
    // 2A: under the covers, the name in large, its pills, and a button that opens it
    const Theme& t = m_theme;
    const bool big = m_ui.library_size == 0;
    const float top = kGridTop + g_view_h + (big ? 14.f : 10.f);
    const char* open = TrM().fv_open;
    const float bh = big ? 56.f : 46.f, bw = ui::TextWidth(vg, 19, open) + 90;
    const float bx = kWidth - 36 - bw, by = top + 2;
    nvgSave(vg);
    nvgScissor(vg, kGridX, top - 6, bx - 24 - kGridX, 48);
    ui::Title(vg, kGridX, top + (big ? 18.f : 14.f), big ? 30.f : 24.f, t.text, g.name.c_str());
    nvgRestore(vg);
    // the pills stop before the button, and before the pill of the queue when it is beside them
    const float py = top + (big ? 40.f : 34.f);
    const float right = (pill && py + 28 > 596) ? kWidth - 36 - 400 - 16 : bx - 16;
    FocusPills(vg, g, kGridX, py, right, false);
    FocusButton(vg, t, bx, by, bw, bh, open);
    m_hits.push_back({{bx, by, bw, bh, HidNpadButton_A}, HitKind::Button, 0});
}

void App::DrawFocusPanel(NVGcontext* vg, const Game& g, bool pill) {
    // 2B: a panel on the right with its cover, its name, its pills and the button; the queue at
    // work at its foot
    const Theme& t = m_theme;
    const float x = kPanelX, y = kGridTop - 10, w = kPanelW, h = 664 - 14 - y;
    ui::Glass(vg, t, x, y, w, h, 18);
    const float cs = pill ? 160.f : 180.f, cx = x + (w - cs) / 2, cy = y + 18;
    nvgBeginPath(vg);
    nvgRect(vg, cx - 20, cy - 10, cs + 40, cs + 40);
    nvgFillPaint(vg, nvgBoxGradient(vg, cx + 4, cy + 10, cs - 8, cs, 12, 22, nvgRGBAf(0, 0, 0, t.dark ? 0.45f : 0.2f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 12, CoverOf(g));
    CoverLoading(vg, cx, cy, cs, g.cover);
    const float ny = cy + cs + 26;
    nvgSave(vg);
    nvgScissor(vg, x + 20, ny - 20, w - 40, 40);
    if (ui::TextWidth(vg, 24, g.name.c_str()) > w - 40) ui::Marquee(vg, x + 20, ny, w - 40, 24, t.text, g.name.c_str(), m_time);
    else ui::Title(vg, x + 20, ny, 24, t.text, g.name.c_str());
    nvgRestore(vg);
    FocusPills(vg, g, x + 20, ny + 22, x + w - 20, true);
    const char* open = TrM().fv_open;
    const float bh = 52, by = pill ? y + h - 18 - 56 - 12 - bh : y + h - 18 - bh;
    FocusButton(vg, t, x + 20, by, w - 40, bh, open);
    m_hits.push_back({{x + 20, by, w - 40, bh, HidNpadButton_A}, HitKind::Button, 0});
    if (pill) DrawQueuePill(vg, x + 12, y + h - 12 - 56, w - 24);
}

bool App::QueueAtWork() const {
    return m_queue && (m_queue->Busy() || m_queue->GetLive().running);
}

void App::DrawQueuePill(NVGcontext* vg, float x, float y, float w) {
    // 3A: the queue at work, floating: the cover in a ring that fills, what is installed, a thin bar
    // and its figures; tapping it opens the queue
    const Theme& t = m_theme;
    const auto& s = Tr();
    if (!m_pill_shown) {
        m_pill_shown = true;
        m_pill_at = m_time;
    }
    const float h = 56;
    const float pop = ui::EaseOutBack(ui::Appear(m_pill_at, 0.35f));
    nvgSave(vg);
    nvgTranslate(vg, x + w / 2, y + h / 2 + (1 - pop) * 20);
    nvgScale(vg, 0.92f + 0.08f * pop, 0.92f + 0.08f * pop);
    nvgTranslate(vg, -(x + w / 2), -(y + h / 2));
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f));
    nvgBeginPath(vg);
    nvgRect(vg, x - 20, y - 10, w + 40, h + 40);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 8, w, h, h / 2, 22, nvgRGBAf(0, 0, 0, t.dark ? 0.45f : 0.16f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    NVGcolor fill = t.panel;
    fill.a = t.dark ? 0.9f : 0.94f;
    ui::FillRound(vg, x, y, w, h, h / 2, fill);
    NVGcolor edge = nvgLerpRGBA(t.line, t.work, 0.45f);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, h / 2 - .5f, edge, 1.2f);
    AddHit(x, y, w, h, HitKind::QueueStrip);

    const auto live = m_queue->GetLive();
    const auto jobs = m_queue->Jobs();
    const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const InstallJob& j) { return j.id == live.job; });
    const float frac = live.running ? JobFraction(live) : 0.f;
    const NVGcolor bar = live.merging ? t.violet : t.work;
    // the ring with the cover inside
    const float rcx = x + 8 + 22, rcy = y + h / 2;
    NVGcolor track = bar;
    track.a = 0.2f;
    nvgBeginPath(vg);
    nvgCircle(vg, rcx, rcy, 21);
    nvgStrokeColor(vg, track);
    nvgStrokeWidth(vg, 3.5f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    if (live.running && live.total > 0) {
        nvgArc(vg, rcx, rcy, 21, -NVG_PI / 2, -NVG_PI / 2 + 2 * NVG_PI * std::max(0.01f, frac), NVG_CW);
    } else {
        const float a0 = ui::Still() ? 0.f : float(m_time) * 5.f;
        nvgArc(vg, rcx, rcy, 21, a0, a0 + 1.5f, NVG_CW);
    }
    nvgStrokeColor(vg, bar);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
    if (it != jobs.end()) {
        ui::Cover(vg, t, rcx - 15, rcy - 15, 30, PlaceholderColor(it->color_key), Initial(it->game).c_str(), nullptr, false, false, 15,
                  m_covers.Get(it->cover));
    }
    // what, and how far
    const float tx = x + 62, keyw = 34, tw = w - 62 - keyw - 14;
    std::string head = s.preparing;
    if (it != jobs.end()) {
        char title[160];
        std::snprintf(title, sizeof(title), s.installing_name, it->game.c_str());
        head = title;
    }
    char figures[200] = "";
    if (live.running) {
        if (live.merging) {
            std::snprintf(figures, sizeof(figures), "%s… %d %%", TrM().qm_merging, int(frac * 100));
        } else if (live.copying_save) {
            std::snprintf(figures, sizeof(figures), "%s…", TrM().sc_copying);
        } else if (live.phase >= 3) {
            std::snprintf(figures, sizeof(figures), "%s…", s.registering);
        } else if (live.total == 0) {
            std::snprintf(figures, sizeof(figures), "%s… · %.0f MB/s", s.preparing, live.net_rate);
        } else {
            const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
            char rest[64] = "";
            if (left >= 0) std::snprintf(rest, sizeof(rest), TrM().qs_left_fmt, ShortDuration(left).c_str());
            std::snprintf(figures, sizeof(figures), "%d %% · %.0f MB/s%s%s", int(frac * 100), live.write_rate, rest[0] ? " · " : "", rest);
        }
    }
    const int waiting = m_queue->Waiting();
    if (waiting > 0) {
        char more[48];
        std::snprintf(more, sizeof(more), s.in_queue_count, waiting);
        std::strncat(figures, figures[0] ? " · " : "", sizeof(figures) - std::strlen(figures) - 1);
        std::strncat(figures, more, sizeof(figures) - std::strlen(figures) - 1);
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, tx, y, tw, h);
    ui::Text(vg, tx, y + 17, 15, t.text, head.c_str());
    ui::Progress(vg, t, tx, y + 29, tw, 5, frac, bar, m_time, live.running);
    ui::Text(vg, tx, y + 44, 13, t.muted, figures);
    nvgRestore(vg);
    ui::Symbol(vg, t, x + w - 14 - keyw / 2, rcy, 24, ui::glyph::X, t.text, HidNpadButton_X);
    nvgRestore(vg);
}

void App::DrawBoot(NVGcontext* vg) {
    // 4A: the icon assembles itself (the card falls into the dock, the bar lights, the lamp comes
    // on) with the name under it, then it flies to its place in the top bar while the library
    // shows; gentle animations only fade the icon. Any button or touch skips it (Frame).
    if (m_boot_at < 0 || m_wizard) return;
    const double a = m_time - m_boot_at;
    const bool lively = ui::Lively();
    if (a < 0 || a > (lively ? 1.5 : 0.7)) return;
    const Theme& t = m_theme;
    float veil, x = 540, y = 200, s = 200, card = 1, bar = 1, lamp = 1, name = 0;
    if (lively) {
        veil = a < 1.05 ? 1.f : float(std::max(0.0, 1 - (a - 1.05) / 0.35));
        card = ui::EaseOutBack(float(std::clamp((a - 0.15) / 0.5, 0.0, 1.0)));
        bar = ui::EaseOut(float(std::clamp((a - 0.55) / 0.45, 0.0, 1.0)));
        lamp = float(std::clamp((a - 0.95) / 0.2, 0.0, 1.0));
        name = float(std::clamp((a - 0.35) / 0.3, 0.0, 1.0)) * float(std::clamp((1.1 - a) / 0.2, 0.0, 1.0));
        const float m = float(std::clamp((a - 1.05) / 0.45, 0.0, 1.0));
        const float e = m < 0.5f ? 4 * m * m * m : 1 - std::pow(-2 * m + 2, 3.f) / 2;
        x += (36 - x) * e;
        y += (19 - y) * e;
        s += (34 - s) * e;
    } else {
        veil = a < 0.35 ? 1.f : float(std::max(0.0, 1 - (a - 0.35) / 0.35));
        name = veil;
    }
    NVGcolor bg = t.bg;
    bg.a = veil;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, bg);
    nvgFill(vg);
    nvgGlobalAlpha(vg, lively ? 1.f : veil);
    // a soft light under the icon while it is in the middle; the other styles have their own touch
    if (veil > 0.01f && lively && t.style != kStyleNXDock) {
        DrawBootStyle(vg, a, x + s / 2, y + s / 2, s, veil);
    } else if (veil > 0.01f) {
        NVGcolor glow = t.work;
        glow.a = 0.25f * veil;
        nvgBeginPath(vg);
        nvgCircle(vg, x + s / 2, y + s / 2, s);
        nvgFillPaint(vg, nvgRadialGradient(vg, x + s / 2, y + s / 2, s * 0.3f, s, glow, Hex(0, 0)));
        nvgFill(vg);
    }
    ui::Brand(vg, x, y, s, card, bar, lamp);
    if (name > 0.01f) {
        nvgGlobalAlpha(vg, name);
        if (t.style == kStyleHuntress) nvgTextLetterSpacing(vg, 6);
        ui::Title(vg, kWidth / 2, 448, 36, t.text, t.style == kStyleHuntress ? "NXDOCK" : "NXDock", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgTextLetterSpacing(vg, 0);
    }
    nvgGlobalAlpha(vg, 1.f);
}

void App::DrawSkeleton(NVGcontext* vg, const char* line, float frac, bool known) {
    // 14A: the shape of the library while it is read, a light running over it, and below how far
    const Theme& t = m_theme;
    NVGcolor ghost = nvgLerpRGBA(t.panel, t.muted, 0.16f);
    ghost.a = 0.9f;
    const float widths[] = {110, 140, 120, 170, 80};
    float x = kGridX;
    for (float w : widths) {
        NVGcolor c = ghost;
        c.a = 0.55f;
        ui::FillRound(vg, x, 92, w, 34, 17, c);
        x += w + 10;
    }
    const int rows = int((g_view_h + kGridGap) / g_row_pitch);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < g_columns; c++) {
            const float cx = kGridX + c * (g_cover + kGridGap), cy = kGridTop + r * g_row_pitch;
            ui::FillRound(vg, cx, cy, g_cover, g_cover, 10, ghost);
            if (ui::Still()) continue;
            // the light: a soft band that crosses each cover, one after another
            const float p = float(std::fmod(m_time * 0.7 + (r + c) * 0.08, 1.6)) - 0.3f;
            const float gx = cx + p * g_cover * 1.4f, bw = g_cover * 0.35f;
            nvgSave(vg);
            nvgIntersectScissor(vg, cx, cy, g_cover, g_cover);
            const NVGcolor clear = nvgRGBAf(1, 1, 1, 0), light = nvgRGBAf(1, 1, 1, t.dark ? 0.08f : 0.55f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, cy, g_cover, g_cover, 10);
            nvgFillPaint(vg, nvgLinearGradient(vg, gx - bw, cy, gx, cy, clear, light));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, gx, cy, bw, g_cover);
            nvgFillPaint(vg, nvgLinearGradient(vg, gx, cy, gx + bw, cy, light, clear));
            nvgFill(vg);
            nvgRestore(vg);
        }
    }
    const float by = kGridTop + g_view_h + 16;
    NVGcolor c = ghost;
    c.a = 0.55f;
    ui::FillRound(vg, kGridX, by, 360, 30, 8, c);
    ui::Text(vg, kGridX, by + 58, 16, t.muted, line);
    ui::Progress(vg, t, kGridX, by + 76, 460, 6, known ? frac : 0.f, t.work, m_time, true);
}

std::vector<int> App::QuickRows() const {
    // 18A: install what it lacks to the SD card, its page, and for a game on the console its check
    // and its cheats and mods
    std::vector<int> rows = {0, 1};
    if (m_quick < 0 || m_quick >= int(m_view.size())) return rows;
    const Game& g = m_games[m_view[m_quick]];
    if (g.installed.base || !g.installed.dlc.empty()) rows.push_back(2);
    if (g.installed.base && !m_shop_active) rows.push_back(3);
    return rows;
}

void App::OpenQuick(int view_index) {
    if (view_index < 0 || view_index >= int(m_view.size())) return;
    m_quick = view_index;
    m_quick_focus = 0;
    m_quick_at = m_time;
    m_focus = view_index;
    m_touch_focus = false;
    KeepFocusVisible();
}

void App::UpdateQuick(u64 down, u64 held) {
    const int rows = int(QuickRows().size());
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_quick_focus > 0) m_quick_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_quick_focus < rows - 1) m_quick_focus++;
    if (down & HidNpadButton_A) ActivateQuick(m_quick_focus);
    else if (down & (HidNpadButton_B | HidNpadButton_Plus)) m_quick = -1;
}

void App::ActivateQuick(int row) {
    const auto rows = QuickRows();
    if (m_quick < 0 || row < 0 || row >= int(rows.size())) {
        m_quick = -1;
        return;
    }
    const int view = m_quick;
    m_quick = -1;
    switch (rows[size_t(row)]) {
        case 0:
            // what is missing, as the page would mark it, to the SD card
            OpenDetail(view);
            m_detail = false;
            m_to_nand = false;
            EnqueuePicked();
            break;
        case 1: OpenDetail(view); break;
        case 2: {
            OpenDetail(view);
            m_detail = false;
            const Game& g = m_games[m_detail_game];
            if (m_queue->StartVerify(g.title_id)) {
                m_dialog = Dialog{};
                m_dialog.kind = DialogKind::Verify;
                m_dialog.game = m_detail_game;
                m_dialog.single = true;
            } else {
                Notice(Tr().verify_wait);
            }
            break;
        }
        case 3: {
            const Game& g = m_games[m_view[size_t(view)]];
            OpenMods();
            OpenModsGame(g.title_id, g.name);
            m_mods_from_detail = false;
            m_tab = Tab::More;
            break;
        }
    }
}

void App::DrawQuick(NVGcontext* vg) {
    // 18A: beside the cover held, a small menu of glass; behind it a little dark, a tap there closes it
    const Theme& t = m_theme;
    const auto& s = Tr();
    if (m_quick < 0 || m_quick >= int(m_view.size())) {
        m_quick = -1;
        return;
    }
    nvgBeginPath(vg);
    nvgRect(vg, 0, 73, kWidth, 664 - 73);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.25f));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::QuickBack);
    const auto rows = QuickRows();
    m_quick_focus = std::clamp(m_quick_focus, 0, int(rows.size()) - 1);
    const float rh = 54, pad = 8, w = 320, h = pad * 2 + rh * float(rows.size());
    const float gx = kGridX + (m_quick % g_columns) * (g_cover + kGridGap);
    const float gy = kGridTop + (m_quick / g_columns) * g_row_pitch - m_scroll;
    float x = gx + g_cover + 16;
    if (x + w > kWidth - 24) x = gx - 16 - w;
    const float y = std::clamp(gy - 10, 84.f, 650.f - h);
    const float pop = ui::EaseOutBack(ui::Appear(m_quick_at, 0.3f));
    const float ox = x < gx ? x + w : x; // it grows out of the side of the cover
    nvgSave(vg);
    nvgTranslate(vg, ox, y + 30);
    nvgScale(vg, 0.85f + 0.15f * pop, 0.85f + 0.15f * pop);
    nvgTranslate(vg, -ox, -(y + 30));
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.5f));
    draw::Popup(vg, t, x, y, w, h, 18); // over the covers it is read clearly
    AddHit(x, y, w, h, HitKind::Button); // the panel itself does nothing
    const char* labels[] = {TrM().qm_install_sd, TrM().qm_page, s.verify, TrM().t_mods};
    for (size_t i = 0; i < rows.size(); i++) {
        const float ry = y + pad + float(i) * rh;
        const bool on = int(i) == m_quick_focus;
        if (on) {
            NVGcolor sel = t.work;
            sel.a = 0.18f;
            ui::FillRound(vg, x + pad, ry, w - 2 * pad, rh, 12, sel);
            ui::StrokeRound(vg, x + pad + 1, ry + 1, w - 2 * pad - 2, rh - 2, 11, t.work, 2);
        }
        AddHit(x + pad, ry, w - 2 * pad, rh, HitKind::QuickRow, int(i));
        // its little picture, in blue
        const float ix = x + pad + 24, iy = ry + rh / 2;
        nvgStrokeColor(vg, t.work);
        nvgStrokeWidth(vg, 2.2f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgBeginPath(vg);
        switch (rows[i]) {
            case 0: // an arrow into a tray
                nvgMoveTo(vg, ix, iy - 9);
                nvgLineTo(vg, ix, iy + 3);
                nvgMoveTo(vg, ix - 5, iy - 2);
                nvgLineTo(vg, ix, iy + 3);
                nvgLineTo(vg, ix + 5, iy - 2);
                nvgMoveTo(vg, ix - 9, iy + 5);
                nvgLineTo(vg, ix - 9, iy + 9);
                nvgLineTo(vg, ix + 9, iy + 9);
                nvgLineTo(vg, ix + 9, iy + 5);
                nvgStroke(vg);
                break;
            case 1: // a page
                nvgRoundedRect(vg, ix - 8, iy - 10, 16, 20, 3);
                nvgMoveTo(vg, ix - 4, iy - 3);
                nvgLineTo(vg, ix + 4, iy - 3);
                nvgMoveTo(vg, ix - 4, iy + 3);
                nvgLineTo(vg, ix + 4, iy + 3);
                nvgStroke(vg);
                break;
            case 2: // a tick in a circle
                nvgCircle(vg, ix, iy, 9);
                nvgStroke(vg);
                ui::CheckMark(vg, ix, iy + 0.5f, 10, t.work, 2.2f);
                break;
            default: // a wand
                nvgMoveTo(vg, ix - 8, iy + 8);
                nvgLineTo(vg, ix + 5, iy - 5);
                nvgStroke(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, ix + 7, iy - 7, 2.6f);
                nvgFillColor(vg, t.work);
                nvgFill(vg);
                break;
        }
        ui::Text(vg, x + pad + 50, ry + rh / 2, 17, t.text, labels[rows[i]]);
    }
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.choose, HidNpadButton_A}, {ui::glyph::B, s.close, HidNpadButton_B}}, rects);
    AddHints(rects);
}

bool App::InSubScreen() const {
    // a page or a tool open in a tab: sliding to the right goes back, as B (20A)
    return (GridTab() && m_detail) || (m_tab == Tab::More && m_more != MoreScreen::Tiles) ||
           (m_tab == Tab::Installed && m_installed_view == 0 && m_console_app != 0) || (m_tab == Tab::Shops && m_shop_active);
}

void App::DrawLiftedCover(NVGcontext* vg, const Game& g, float x, float y, float s) {
    const Theme& t = m_theme;
    const double age = m_time - m_lift_at;
    // up in a fifth of a second, slowing at the end
    const float p = float(std::min(1.0, age / 0.2));
    const float e = 1 - (1 - p) * (1 - p) * (1 - p);
    // under a finger it sinks a little (15A)
    const float pressed = m_press_cover == m_focus && m_press_live && m_touching ? ui::EaseOut(ui::Appear(m_press_at, 0.08f)) : 0.f;
    const float lift = 6 * e * (1 - pressed), scale = (1 + 0.05f * e) * (1 - 0.05f * pressed);
    // the lean settles like a card set down: a few degrees, gone in a quarter of a second
    const float settle = float(std::exp(-age * 10.0) * std::cos(age * 15.0));
    const float angle = -0.045f * settle * std::clamp(m_lift_dx, -1.f, 1.f);
    const float squash = 1 - 0.035f * settle * std::fabs(std::clamp(m_lift_dy, -1.f, 1.f));

    // the shadow under it, on the grid
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 10, s + 60, s + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x + 8, y + 10 + lift, s - 16, s, 14, 26, nvgRGBAf(0, 0, 0, (t.dark ? 0.5f : 0.3f) * e),
                                    nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);

    nvgSave(vg);
    nvgTranslate(vg, x + s / 2, y + s / 2 - lift);
    nvgRotate(vg, angle);
    nvgScale(vg, scale, scale * squash);
    nvgTranslate(vg, -s / 2, -s / 2);
    ui::Focus(vg, t, 0, 0, s, s, 10, m_time);
    ui::Cover(vg, t, 0, 0, s, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 10, CoverOf(g));
    CoverLoading(vg, 0, 0, s, g.cover);
    InstalledBadge(vg, g, 0, 0, s);
    // a light that passes once over it
    if (age > 0.08 && age < 0.75) {
        const float pos = float((age - 0.08) / 0.67);
        nvgIntersectScissor(vg, 0, 0, s, s);
        nvgTranslate(vg, s / 2, s / 2);
        nvgRotate(vg, 0.45f);
        const float bx = -s + 2 * s * pos, bw = s * 0.22f;
        const NVGcolor clear = nvgRGBAf(1, 1, 1, 0), light = nvgRGBAf(1, 1, 1, 0.22f * (1 - pos * 0.5f));
        nvgBeginPath(vg);
        nvgRect(vg, bx - bw, -s, bw, 2 * s);
        nvgFillPaint(vg, nvgLinearGradient(vg, bx - bw, 0, bx, 0, clear, light));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, bx, -s, bw, 2 * s);
        nvgFillPaint(vg, nvgLinearGradient(vg, bx, 0, bx + bw, 0, light, clear));
        nvgFill(vg);
    }
    nvgRestore(vg);
}

void App::DrawDetail(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const Game& g = m_games[m_detail_game];
    const float top = 72, bottom = 664;

    (void)bottom; // the cover itself, soft, is behind everything (DrawBackdrop, 6A)
    // 7A: its parts come in one after another, from the right; gentle animations only fade them
    const auto in = [&](int i) { return ui::EaseOut(ui::Appear(m_detail_at + 0.06 + 0.04 * i, 0.34f)); };
    const auto enter = [&](int i) {
        nvgSave(vg);
        const float k = in(i);
        if (ui::Lively()) nvgTranslate(vg, 26 * (1 - k), 0);
        nvgGlobalAlpha(vg, k);
    };

    // the cover
    const float cs = 330, cx = 48, cy = top + 34;
    if (m_time - m_detail_at >= 0.3) { // while the page opens, the cover is still on its way (DrawTransitions)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx + 4, cy + 14, cs - 8, cs, 14);
        nvgFillPaint(vg, nvgBoxGradient(vg, cx, cy + 14, cs, cs, 14, 30, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, CoverOf(g));
        CoverLoading(vg, cx, cy, cs, g.cover);
        // its reflection on the floor, fading away in thin bands (6A)
        const float ry = cy + cs + 10, rh = 120, band = 10;
        const int image = CoverOf(g);
        for (int b = 0; b < int(rh / band); b++) {
            nvgSave(vg);
            nvgIntersectScissor(vg, cx, ry + b * band, cs, band);
            nvgGlobalAlpha(vg, (t.dark ? 0.24f : 0.3f) * (1 - float(b) / (rh / band)));
            nvgTranslate(vg, 0, 2 * ry);
            nvgScale(vg, 1, -1);
            ui::Cover(vg, t, cx, ry, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, image);
            nvgRestore(vg);
        }
    }

    // name and facts
    const float rx = cx + cs + 44, rw = kWidth - rx - 48;
    enter(0);
    nvgSave(vg);
    nvgScissor(vg, rx, cy - 4, rw, 64);
    ui::Title(vg, rx, cy + 26, 36, t.text, g.name.c_str());
    nvgRestore(vg);
    nvgRestore(vg);
    const CatalogFile* newest = g.NewestUpdate();
    u64 picked_size = 0;
    for (size_t i = 0; i < g.files.size(); i++) {
        if (m_detail_pick[i]) picked_size += g.files[i].size;
    }
    u32 firmware = 0;
    for (const auto& f : g.files) {
        if (f.kind != CatalogFile::Kind::Dlc) firmware = std::max(firmware, f.required_system); // a DLC's is its game's version
    }
    struct Fact { const char* label; std::string value; NVGcolor color; };
    std::vector<Fact> facts;
    if (newest) {
        facts.push_back({s.version, VersionText(newest->version), t.text});
    }
    facts.push_back({s.format, FormatOf(g), t.text});
    facts.push_back({s.will_take, SizeText(picked_size), t.text});
    if (firmware) facts.push_back({s.firmware, FirmwareText(firmware) + " " + s.or_newer, t.text});
    const bool versions = g.installed.base && !m_shop_active && !VersionRows(g).empty();
    if (!m_marks_read) {
        m_marks_read = true;
        m_compressed_marks = Compressor::Marks();
    }
    const bool compressed = std::any_of(m_compressed_marks.begin(), m_compressed_marks.end(), [&g](const CompressedGame& c) {
        return c.app_id == g.title_id;
    });
    if (g.installed.base) {
        facts.push_back({s.on_console, VersionText(g.installed.patch), t.ok});
    } else {
        facts.push_back({s.on_console, "—", t.muted});
    }
    if (compressed && g.installed.base) {
        // 24A: what it saved
        u64 saved = 0;
        for (const auto& c : m_compressed_marks) {
            if (c.app_id == g.title_id) saved = c.saved;
        }
        char text[96];
        std::snprintf(text, sizeof(text), TrM().cz_saves_fmt, SizeText(saved).c_str());
        facts.insert(facts.end() - 1, {TrM().cz_badge, text, t.work});
    }
    enter(1);
    float fx = rx;
    const float fy = cy + 62;
    for (size_t i = 0; i < facts.size(); i++) {
        const auto& f = facts[i];
        const bool chip = versions && i + 1 == facts.size(); // the version on the console: − changes it (20A)
        const float w = std::max(ui::TextWidth(vg, 14, f.label), ui::TextWidth(vg, 17, f.value.c_str())) + 28 + (chip ? 34 : 0);
        ui::FillRound(vg, fx, fy, w, 56, 10, Hex(0, t.dark ? 0.25f : 0.04f));
        ui::StrokeRound(vg, fx + .5f, fy + .5f, w - 1, 55, 10, chip ? t.work : t.line, chip ? 1.5f : 1.f);
        ui::Text(vg, fx + 14, fy + 18, 14, t.muted, f.label);
        ui::Text(vg, fx + 14, fy + 39, 17, f.color, f.value.c_str());
        if (chip) {
            ui::Symbol(vg, t, fx + w - 22, fy + 28, 18, ui::glyph::Minus, t.work, HidNpadButton_Minus);
            AddHit(fx, fy, w, 56, HitKind::VersionChip, 0);
        }
        fx += w + 10;
    }
    nvgRestore(vg);

    // what to install: the game, the newest update, every DLC
    std::vector<int> rows;
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind != CatalogFile::Kind::Update || &g.files[i] == newest) rows.push_back(i);
    }
    float ly = fy + 76;
    const float lh = 46;
    // room below for where it goes (two large cards) and Install
    const float list_h = std::min({float(rows.size()) * lh + 12, 250.f, std::max(2 * lh + 12, 664 - 14 - 52 - 18 - 64 - 16 - ly)});
    enter(2);
    ui::Glass(vg, t, rx, ly, rw, list_h, 12);
    ListArea(rx, ly, rw, list_h, lh);
    nvgSave(vg);
    nvgScissor(vg, rx, ly, rw, list_h);
    // keep the row in focus in sight; on Install the list stays where it was
    const int visible = std::max(1, int((list_h - 12) / lh));
    if (m_detail_focus < int(rows.size())) {
        if (m_detail_focus < m_detail_first) m_detail_first = m_detail_focus;
        if (m_detail_focus >= m_detail_first + visible) m_detail_first = m_detail_focus - visible + 1;
    }
    m_detail_first = std::clamp(m_detail_first, 0, std::max(0, int(rows.size()) - visible));
    const int first = m_detail_first;
    for (int r = first; r < int(rows.size()); r++) {
        const CatalogFile& f = g.files[rows[r]];
        const float y = ly + 6 + (r - first) * lh;
        if (r == m_detail_focus) {
            ui::FillRound(vg, rx + 6, y, rw - 12, lh - 4, 8, t.panel2);
            ui::StrokeRound(vg, rx + 6, y, rw - 12, lh - 4, 8, t.work, 3);
        }
        AddHit(rx + 6, y, rw - 12, lh - 4, HitKind::DetailRow, r);
        const bool on = m_detail_pick[rows[r]];
        ui::FillRound(vg, rx + 20, y + 9, 24, 24, 6, on ? t.work : Hex(0, 0));
        if (on) {
            ui::CheckMark(vg, rx + 32, y + 21, 13, t.on_work, 2.4f);
        } else {
            ui::StrokeRound(vg, rx + 20.5f, y + 9.5f, 23, 23, 6, t.muted, 1.5f);
        }
        std::string label;
        bool installed = false, fresh = false;
        switch (f.kind) {
            case CatalogFile::Kind::Game: label = s.base_game; installed = g.installed.base; break;
            case CatalogFile::Kind::Update:
                label = VersionText(f.version);
                fresh = g.installed.base && f.version > g.installed.patch;
                installed = g.installed.base && f.version <= g.installed.patch;
                break;
            case CatalogFile::Kind::Dlc:
                label = s.dlc_prefix + (f.dlc_name.empty() ? f.name : f.dlc_name);
                installed = g.installed.dlc.count(f.title_id) > 0;
                break;
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, rx + 56, y, rw - 56 - 200, lh);
        const float tw = ui::Text(vg, rx + 56, y + 21, 17, t.text, label.c_str()) - (rx + 56);
        nvgRestore(vg);
        if (fresh || installed) {
            const char* tag = fresh ? s.new_tag : s.installed_tag;
            const NVGcolor color = fresh ? t.cyan : t.ok;
            const float tx = rx + 56 + std::min(tw, rw - 270) + 10, tagw = ui::TextWidth(vg, 12, tag) + 14;
            ui::StrokeRound(vg, tx, y + 10, tagw, 22, 6, color, 1);
            ui::Text(vg, tx + tagw / 2, y + 21, 12, color, tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        ui::Text(vg, rx + rw - 18, y + 21, 15, t.muted, (Upper(f.format) + " · " + SizeText(f.size)).c_str(),
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
    nvgRestore(vg);

    // where, in large (the SD card unless chosen otherwise), and Install saying where
    const float cards_y = ly + list_h + 16;
    enter(3);
    DestCards(vg, rx, cards_y, rw, 64, m_to_nand, picked_size, HitKind::Dest);
    nvgRestore(vg);
    const float dy = cards_y + 64 + 18;
    char install[160];
    std::snprintf(install, sizeof(install), m_to_nand ? TrM().dc_install_nand_fmt : TrM().dc_install_sd_fmt, SizeText(picked_size).c_str());
    const float bw = ui::TextWidth(vg, 20, install) + 64, bx = kWidth - 48 - bw;
    // under the cards, in the room left of Install: it never runs under the button
    enter(4);
    const float k4 = in(4);
    if (m_to_nand) TextBox(vg, rx, dy - 2, std::max(120.f, bx - rx - 20), 14, t.orange, TrM().dc_once);
    const bool can = picked_size > 0;
    if (m_detail_focus == int(rows.size())) {
        ui::Focus(vg, t, bx, dy - 4, bw, 52, 12, m_time);
    }
    nvgGlobalAlpha(vg, (can ? 1.f : 0.45f) * k4);
    ui::FillRound(vg, bx, dy - 4, bw, 52, 12, t.work);
    ui::Text(vg, bx + bw / 2, dy + 22, 20, t.on_work, install, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    AddHit(bx, dy - 4, bw, 52, HitKind::Install, int(rows.size()));
    static const std::string where_glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.select, HidNpadButton_A}, {where_glyphs.c_str(), TrM().dc_switch, HidNpadButton_ZR}};
    if (g.installed.base || !g.installed.dlc.empty()) {
        hints.push_back({ui::glyph::Y, s.verify, HidNpadButton_Y});
    }
    if (g.installed.base && !m_shop_active) {
        hints.push_back({ui::glyph::X, TrM().t_mods, HidNpadButton_X}); // its cheats and mods (10A)
    }
    if (versions) hints.push_back({ui::glyph::Minus, TrM().ver_hint, HidNpadButton_Minus}); // another version (20A)
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

std::vector<int> App::VersionRows(const Game& g) const {
    std::vector<int> rows;
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind == CatalogFile::Kind::Update) rows.push_back(i);
    }
    std::sort(rows.begin(), rows.end(), [&g](int a, int b) { return g.files[a].version > g.files[b].version; });
    return rows;
}

void App::OpenVersions() {
    const Game& g = m_games[m_detail_game];
    const auto rows = VersionRows(g);
    if (rows.empty() || !g.installed.base) return;
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Versions;
    m_dialog.game = m_detail_game;
    m_dialog.focus = 1;
    // the focus on the version installed, or the newest
    m_dialog.entry = 0;
    for (int r = 0; r < int(rows.size()); r++) {
        if (g.files[rows[r]].version == g.installed.patch) m_dialog.entry = r;
    }
}

namespace {

std::string ShortDuration(double seconds);

std::string FileLabel(const InstallJob& j) {
    return j.label.empty() ? j.game : j.game + " · " + j.label;
}

std::string Duration(double seconds) {
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

// The jobs in the order the queue screen shows them: the one installing, those waiting, then
// the finished ones, the latest first.
std::vector<InstallJob> QueueOrder(std::vector<InstallJob> jobs) {
    std::vector<InstallJob> out;
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Running) out.push_back(j);
    }
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Waiting) out.push_back(j);
    }
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) {
        if (it->state != InstallJob::State::Running && it->state != InstallJob::State::Waiting) out.push_back(*it);
    }
    return out;
}

// Of what the install writes: how far, from the live counters.
// The merge after an install (25G): measuring, then compressing and writing, then the check.
float MergeFraction(const InstallQueue::Live& l) {
    const float part = l.merge_total ? std::clamp(float(double(l.merge_done) / double(l.merge_total)), 0.f, 1.f) : 0.f;
    return l.merge_phase <= 1 ? 0.45f * part : l.merge_phase == 2 ? 0.45f + 0.45f * part : 0.9f + 0.1f * part;
}

float JobFraction(const InstallQueue::Live& l) {
    if (l.merging) return MergeFraction(l);
    if (l.phase >= 3) return 1.f;
    if (l.total == 0) return 0.f;
    return std::clamp(float(double(l.written) / double(l.total)), 0.f, 1.f);
}

} // namespace

void App::EnqueuePicked() {
    const auto& s = Tr();
    const Game& g = m_games[m_detail_game];
    const Config c = m_link.CurrentConfig();
    int picked = 0;
    std::vector<InstallJob> jobs;
    // the game first, then the update, then the DLC
    for (const auto kind : {CatalogFile::Kind::Game, CatalogFile::Kind::Update, CatalogFile::Kind::Dlc}) {
        for (size_t i = 0; i < g.files.size(); i++) {
            const CatalogFile& f = g.files[i];
            if (!m_detail_pick[i] || f.kind != kind) {
                continue;
            }
            picked++;
            std::string label;
            switch (f.kind) {
                case CatalogFile::Kind::Game: label = s.base_game; break;
                case CatalogFile::Kind::Update: label = VersionText(f.version); break;
                case CatalogFile::Kind::Dlc: label = f.dlc_name.empty() ? f.name : f.dlc_name; break;
            }
            // a newer firmware than the console's is told by the check before installing (2A)
            InstallJob job = JobFor(g, f);
            job.label = label;
            job.nand = m_to_nand;
            jobs.push_back(std::move(job));
        }
    }
    if (!picked) {
        Notice(s.nothing_picked);
        return;
    }
    if (jobs.empty()) {
        return;
    }
    if (m_ui.dest == 2 && !m_dest_touched) {
        // "ask each time" (wizard): SD card or internal memory, and the internal memory asks again
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Where;
        m_dialog.jobs = std::move(jobs);
        m_dialog.focus = 1; // the SD card, the default
        return;
    }
    if (m_to_nand) {
        // never to the internal memory without asking: the question (1A), Cancel in focus
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand;
        m_dialog.jobs = std::move(jobs);
        m_to_nand = false; // the next one goes to the SD card again
        return;
    }
    AddJobs(std::move(jobs));
    m_to_nand = false;
}

InstallJob App::JobFor(const Game& g, const CatalogFile& f) {
    const auto& s = Tr();
    const Config c = m_link.CurrentConfig();
    InstallJob job;
    job.game = g.name;
    switch (f.kind) {
        case CatalogFile::Kind::Game: job.label = s.base_game; break;
        case CatalogFile::Kind::Update: job.label = VersionText(f.version); break;
        case CatalogFile::Kind::Dlc: job.label = f.dlc_name.empty() ? f.name : f.dlc_name; break;
    }
    job.version = f.version;
    job.resumable = !m_shop_active; // a journal while it installs (23A); a shop's key is not written down
    job.cover = g.cover;
    job.color_key = g.key;
    job.file_id = f.id;
    const bool whole = f.url.rfind("http://", 0) == 0 || f.url.rfind("https://", 0) == 0;
    const bool path = !f.url.empty() && f.url[0] == '/' && f.url.rfind("//", 0) != 0 && f.url.find('@') == std::string::npos;
    job.url = whole ? f.url : c.url + f.url;
    // a key goes only to the server it belongs to: a file elsewhere comes without it
    if (m_shop_active) job.key = http::SameOrigin(job.url, m_shop_url) ? m_shop_key : "";
    else job.key = (whole ? http::SameOrigin(job.url, c.url) : path) ? c.key : "";
    job.format = f.format;
    job.kind = f.kind;
    job.title_id = f.title_id;
    job.size = f.size;
    if (f.kind == CatalogFile::Kind::Dlc) {
        job.dlc_needs = f.required_system; // for a DLC, the version of its game it needs
    } else {
        job.required_system = f.required_system;
    }
    return job;
}

bool App::UpdatePending(const Game& g) const {
    if (!g.UpdateAvailable()) {
        return false;
    }
    const u32 hos = hosversionGet();
    const u32 console = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    const CatalogFile* f = g.NewestUpdate();
    return !(f->required_system && (f->required_system & 0xFFFF0000) > console); // else the console cannot run it yet
}

std::vector<InstallJob> App::PendingUpdates() {
    std::vector<InstallJob> jobs;
    for (const auto& g : m_games) {
        if (!UpdatePending(g)) {
            continue;
        }
        const CatalogFile* f = g.NewestUpdate();
        InstallJob job = JobFor(g, *f);
        job.label = VersionText(g.installed.patch) + " → " + VersionText(f->version);
        job.nand = g.installed.base_nand;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

std::string App::ContentLabel(const Game& g, const NcmContentMetaKey& key) const {
    const auto& s = Tr();
    switch (key.type) {
        case NcmContentMetaType_Application: return s.base_game;
        case NcmContentMetaType_Patch: return VersionText(key.version);
        default:
            for (const auto& f : g.files) {
                if (f.kind == CatalogFile::Kind::Dlc && f.title_id == key.id) {
                    return s.dlc_prefix + (f.dlc_name.empty() ? f.name : f.dlc_name);
                }
            }
            char id[40];
            std::snprintf(id, sizeof(id), "%s%016lX", s.dlc_prefix, key.id);
            return id;
    }
}

bool App::RepairJobs(const InstallQueue::VerifyView& v, std::vector<InstallJob>& out) {
    out.clear();
    if (m_dialog.game < 0 || m_dialog.game >= int(m_games.size())) {
        return false;
    }
    const Game& g = m_games[m_dialog.game];
    bool all = true;
    for (const auto& c : v.result) {
        if (c.bad.empty()) {
            continue;
        }
        const CatalogFile* match = nullptr;
        for (const auto& f : g.files) {
            const bool kind = (c.key.type == NcmContentMetaType_Application && f.kind == CatalogFile::Kind::Game) ||
                              (c.key.type == NcmContentMetaType_Patch && f.kind == CatalogFile::Kind::Update) ||
                              (c.key.type == NcmContentMetaType_AddOnContent && f.kind == CatalogFile::Kind::Dlc);
            if (kind && f.title_id == c.key.id && f.version == c.key.version) {
                match = &f;
            }
        }
        if (!match) {
            all = false;
            continue;
        }
        InstallJob job = JobFor(g, *match);
        job.label = ContentLabel(g, c.key) + " · " + Tr().repair_tag;
        job.file_id += ":repair";
        job.nand = c.nand;
        job.repair = c.bad;
        job.size = c.bad_bytes;
        out.push_back(std::move(job));
    }
    return all && !out.empty();
}

// The queue as the web shows it (22A): each job, and for the one installing its stage, speed and
// time left.
std::string App::QueueJson() {
    const auto jobs = m_queue->Jobs();
    const auto live = m_queue->GetLive();
    std::string out = std::string("{\"paused\":") + (m_queue->Paused() ? "true" : "false") + ",\"jobs\":[";
    bool first = true;
    for (const auto& j : jobs) {
        const char* state = j.state == InstallJob::State::Waiting   ? "waiting"
                            : j.state == InstallJob::State::Running ? "running"
                            : j.state == InstallJob::State::Done    ? "done"
                            : j.state == InstallJob::State::Failed  ? "failed"
                                                                    : "cancelled";
        char head[160];
        std::snprintf(head, sizeof(head), "{\"id\":%d,\"state\":\"%s\",\"size\":%llu", j.id, state, (unsigned long long)j.size);
        std::string item = head;
        item += ",\"game\":" + http::JsonQuote(j.game) + ",\"label\":" + http::JsonQuote(j.label);
        if (j.state == InstallJob::State::Running && live.running && live.job == j.id) {
            const char* stage = live.copying_save ? "copy" : live.phase >= 3 ? "register" : live.phase <= 1 ? "network" : "write";
            const double rate = live.total ? live.write_rate : live.net_rate;
            const double left = live.write_rate > 0.1 && live.total ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1048576.0) : 0;
            char more[200];
            std::snprintf(more, sizeof(more), ",\"stage\":\"%s\",\"percent\":%.1f,\"speed\":%.1f,\"left\":%d", stage, double(JobFraction(live)) * 100.0, rate,
                          int(left));
            item += more;
        }
        if (j.state == InstallJob::State::Failed && !j.error.empty()) {
            // the title of the packed error is enough for the web
            item += ",\"error\":" + http::JsonQuote(UnpackError(j.error).title);
        }
        item += "}";
        out += (first ? "" : ",") + item;
        first = false;
    }
    return out + "]}";
}

void App::FollowQueueWeb() {
    if (!m_queue || !m_report) return;
    const Config c = m_link.CurrentConfig();
    if (!c.Linked()) return;
    m_report->SetLink(c.url, c.key);
    const auto jobs = m_queue->Jobs();
    bool busy = false;
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running) {
            busy = true;
            if (std::find(m_report_jobs.begin(), m_report_jobs.end(), j.id) == m_report_jobs.end()) m_report_jobs.push_back(j.id);
        }
    }
    // what the queue does, every two seconds while it works, and once when it stops
    if ((busy && m_time - m_report_at >= 2) || (!busy && m_report_busy)) {
        m_report_at = m_time;
        m_report->Live(QueueJson());
    }
    if (!busy && m_report_busy) {
        // the end of the queue: what went well and what failed, for the phone (21A)
        std::string items;
        for (const auto& j : jobs) {
            if (std::find(m_report_jobs.begin(), m_report_jobs.end(), j.id) == m_report_jobs.end()) continue;
            if (j.state != InstallJob::State::Done && j.state != InstallJob::State::Failed) continue;
            if (!items.empty()) items += ",";
            items += "{\"game\":" + http::JsonQuote(j.game) + ",\"label\":" + http::JsonQuote(j.label) + ",\"ok\":" +
                     (j.state == InstallJob::State::Done ? "true" : "false");
            if (j.state == InstallJob::State::Failed) items += ",\"error\":" + http::JsonQuote(UnpackError(j.error).title);
            items += "}";
        }
        if (m_ui.notify_done && !items.empty() && !m_queue->Paused()) m_report->Done("{\"items\":[" + items + "]}");
        m_report_jobs.clear();
    }
    m_report_busy = busy;
    // what the web asked
    const More& m = TrM();
    for (auto& cmd : m_report->TakeCommands()) {
        if (cmd.action == "pause") {
            m_queue->SetPaused(true);
            Notice(m.qw_paused_web);
        } else if (cmd.action == "resume") {
            m_queue->SetPaused(false);
            Notice(m.qw_resumed);
        } else {
            m_web_cancels.push_back(std::move(cmd));
        }
    }
    // a cancel is asked about on the screen first, as what the web sends to install
    if (!m_web_cancels.empty() && m_dialog.kind == DialogKind::None && !m_wizard) {
        const QueueCommand cmd = m_web_cancels.front();
        m_web_cancels.erase(m_web_cancels.begin());
        const auto it = std::find_if(jobs.begin(), jobs.end(), [&cmd](const InstallJob& j) { return j.id == cmd.job; });
        if (it != jobs.end() && (it->state == InstallJob::State::Waiting || it->state == InstallJob::State::Running)) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::WebCancel;
            m_dialog.entry = cmd.job;
            m_dialog.text = it->game + (it->label.empty() ? "" : " \xC2\xB7 " + it->label);
        }
    }
}

void App::CheckResume() {
    m_resume_checked = true;
    auto state = std::make_unique<ResumeState>();
    if (!LoadResume(kResumeDir, *state)) return;
    m_resume = std::move(state);
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Resume;
    m_dialog.focus = 1;
}

void App::FollowQueueAsk() {
    if (!m_queue || m_dialog.kind != DialogKind::None || m_wizard) return;
    if (!m_resume_checked && !TourActive()) {
        CheckResume();
        if (m_dialog.kind != DialogKind::None) return;
    }
    if (m_mods_after) {
        // the update asked about ended: Cheats and mods of the game, for the new version (19A)
        bool pending = false;
        for (const auto& j : m_queue->Jobs()) {
            if ((j.title_id & ~0x1FFFull) == m_mods_after &&
                (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running)) {
                pending = true;
            }
        }
        if (!pending) {
            const unsigned long long app = m_mods_after;
            m_mods_after = 0;
            OpenMods();
            OpenModsGame(app, m_mods_after_name);
            m_tab = Tab::More;
        }
    }
    const auto ask = m_queue->PendingAsk();
    if (!ask.waiting) return;
    m_dialog = Dialog{};
    m_dialog.kind = ask.why == 10 ? DialogKind::ModsAsk : DialogKind::SaveCopyAsk;
    m_dialog.text = ask.game;
    m_dialog.from = ask.error;
    m_dialog.entry = ask.why;
    m_dialog.ids = {ask.app};
}

void App::FollowPush() {
    const Link::View link = m_link.Snapshot();
    const Config c = m_link.CurrentConfig();
    const bool connected = link.state == Link::State::Connected && c.Linked();
    m_push->SetLink(connected ? c.url : std::string(), c.key);
    if (!connected) {
        return;
    }
    for (auto& item : m_push->Take()) {
        m_pushed.push_back(std::move(item));
    }
    if (m_dialog.kind != DialogKind::None || m_wizard || TourActive()) {
        return; // the wizard asks about sharing itself; what the web sent waits for it to end
    }
    // once, the first time a library is linked: share what is installed? (2A)
    if (m_prefs.share_installed < 0) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Share;
        return;
    }
    // what the web sent: asked about, never installed without A (1A)
    if (!m_pushed.empty()) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Push;
        for (const auto& p : m_pushed) {
            m_dialog.push.push_back(p.id);
            if (m_dialog.from.empty()) m_dialog.from = p.from;
            InstallJob job;
            bool found = false;
            for (const auto& g : m_games) {
                for (const auto& f : g.files) {
                    if (f.id == p.file_id) {
                        job = JobFor(g, f);
                        found = true;
                    }
                }
            }
            if (!found) {
                const auto& s = Tr();
                job.game = p.game;
                job.label = p.kind == "update" ? VersionText(p.version)
                          : p.kind == "dlc" ? p.name : std::string(s.base_game);
                job.color_key = p.game;
                job.file_id = p.file_id;
                job.url = c.url + p.url;
                job.key = c.key;
                job.format = p.format;
                job.kind = p.kind == "update" ? CatalogFile::Kind::Update : p.kind == "dlc" ? CatalogFile::Kind::Dlc : CatalogFile::Kind::Game;
                job.title_id = p.title_id;
                job.size = p.size;
            }
            m_dialog.jobs.push_back(std::move(job));
        }
        m_pushed.clear();
        return;
    }
    // the inventory, when the user said yes: at the start and after every install or removal
    if (m_prefs.share_installed == 1 && (m_inventory_sent != m_queue->Version() || m_inventory_url != c.url) && !m_queue->Busy()) {
        m_inventory_sent = m_queue->Version();
        m_inventory_url = c.url;
        std::vector<InstalledContent> list;
        ReadInstalled(&list);
        m_push->SendInventory(InventoryJson(list));
    }
}

bool TitleFromName(const std::string& name, u64& id, u32& version) {
    id = 0;
    version = 0;
    for (size_t at = name.find('['); at != std::string::npos; at = name.find('[', at + 1)) {
        if (at + 18 > name.size() || name[at + 17] != ']' || name[at + 1] != '0') continue;
        u64 v = 0;
        bool hex = true;
        for (size_t i = at + 1; i < at + 17 && hex; i++) {
            const char c = name[i];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) hex = false;
            v = v << 4 | u64(d < 0 ? 0 : d);
        }
        if (!hex) continue;
        id = v;
        const size_t vat = name.find("[v", at + 18);
        if (vat != std::string::npos) version = u32(std::strtoul(name.c_str() + vat + 2, nullptr, 10));
        return true;
    }
    return false;
}

int TitleKind(u64 id) {
    const u64 low = id & 0xFFF;
    return low == 0 ? 0 : low == 0x800 ? 1 : 2;
}

// An update in `jobs` that would stop its game (update_over_game.hpp): its game comes neither in
// these jobs nor in the queue (the queue puts a game before its update), and the console has it
// compressed by NXDock or optimized. One for each job; Fine for the rest.
std::vector<App::UpdateCheck> App::UpdatesThatStop(const std::vector<InstallJob>& jobs) const {
    const auto idof = [](const InstallJob& j, u32& version) {
        u64 id = j.title_id;
        version = j.version;
        if (j.origin != InstallJob::Origin::Library || !id) {
            u64 named = 0;
            u32 v = 0;
            if (TitleFromName(j.game + " " + j.label, named, v)) {
                if (!id) id = named;
                if (!version) version = v;
            }
        }
        return id;
    };
    std::set<u64> coming;
    const auto queued = m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
    for (const auto* list : {&jobs, &queued}) {
        for (const auto& j : *list) {
            if (list == &queued && j.state != InstallJob::State::Waiting && j.state != InstallJob::State::Running) continue;
            u32 v = 0;
            const u64 id = idof(j, v);
            if (id && TitleKind(id) == 0) coming.insert(id);
        }
    }
    std::vector<UpdateCheck> out(jobs.size());
    std::map<u64, BaseGame> seen;
    for (size_t i = 0; i < jobs.size(); i++) {
        u32 version = 0;
        const u64 id = idof(jobs[i], version);
        if (!id || TitleKind(id) != 1 || jobs[i].downgrade || !jobs[i].repair.empty()) continue;
        const u64 app = GameOf(id);
        if (coming.count(app)) continue;
        if (!seen.count(app)) seen[app] = BaseGameOf(app);
        // a version not known is taken as a newer one
        out[i] = {UpdateOverGame(seen[app], version ? version : ~0u), seen[app], app};
    }
    return out;
}

// The original games of the library that let those updates work, to go before them.
std::vector<InstallJob> App::OriginalsFor(const std::vector<InstallJob>& jobs) {
    std::vector<InstallJob> out;
    std::set<u64> done;
    for (const auto& c : UpdatesThatStop(jobs)) {
        if (!OriginalHelps(c.over) || done.count(c.app)) continue;
        for (const auto& g : m_games) {
            if (g.title_id != c.app) continue;
            const CatalogFile* base = g.Base();
            if (!base) break;
            InstallJob job = JobFor(g, *base);
            job.nand = g.installed.base_nand;
            job.reinstall_ok = true; // the same version as the one compressed: it takes its place
            if (c.base.version > 0) job.downgrade = true; // an optimized game is at the version of its update
            out.push_back(std::move(job));
            done.insert(c.app);
            break;
        }
    }
    return out;
}

void App::AddJobs(std::vector<InstallJob> jobs) {
    const auto& s = Tr();
    // a file from the SD card, a USB disk or the computer: the cover and the name of its game in
    // the library, found by the title ID in its name ("Game [0100…][v0].nsz")
    for (auto& j : jobs) {
        if (!j.cover.empty()) continue;
        const size_t at = j.game.find("[01");
        if (at == std::string::npos || at + 18 > j.game.size() || j.game[at + 17] != ']') continue;
        u64 id = 0;
        bool hex = true;
        for (size_t i = at + 1; i < at + 17; i++) {
            const char c = j.game[i];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) hex = false;
            id = id << 4 | u64(v < 0 ? 0 : v);
        }
        if (!hex) continue;
        // the game of an update (…800) or of a DLC (its game's ID plus 0x1000 and a number)
        const u64 low = id & 0xFFF;
        const u64 app = low == 0 || low == 0x800 ? id & ~u64(0xFFF) : (id - 0x1000) & ~u64(0xFFF);
        for (const auto& g : m_games) {
            if (g.title_id != app) continue;
            j.cover = g.cover;
            j.color_key = g.key;
            if (j.title_id == 0) j.title_id = id;
            break;
        }
    }
    // the files of the SD card or a USB disk: what each needs, read before anything (on a thread)
    if (std::any_of(jobs.begin(), jobs.end(), [](const InstallJob& j) { return j.origin == InstallJob::Origin::File && !j.preflighted; })) {
        m_preflight_dropped = false;
        m_preflight = std::async(std::launch::async, [jobs]() mutable {
            PreflightFiles(jobs);
            return jobs;
        });
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Preflight;
        m_dialog.single = true;
        return;
    }
    // a DLC whose game is older than it asks (neither installed nor coming in this queue)
    {
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        const auto idof = [](const InstallJob& j) {
            u64 id = j.title_id;
            u32 v = 0;
            if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
            return id;
        };
        std::map<u64, u32> coming;
        for (const auto& j : jobs) {
            const u64 id = idof(j);
            if (id && TitleKind(id) == 1) {
                u64 named = 0;
                u32 v = j.version;
                if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, named, v);
                coming[id & ~u64(0xFFF)] = std::max(coming[id & ~u64(0xFFF)], v);
            }
        }
        bool any = false;
        for (const auto& j : jobs) {
            const u64 id = idof(j);
            if (!j.dlc_needs || j.dlc_ok || !id || TitleKind(id) != 2) continue;
            const u64 app = (id - 0x1000) & ~u64(0xFFF);
            u32 have = coming.count(app) ? coming[app] : 0;
            const auto it = installed.find(app);
            if (it != installed.end()) have = std::max(have, it->second.patch);
            if (j.dlc_needs > have) any = true;
        }
        if (any) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::DlcNeeds;
            m_dialog.jobs = std::move(jobs);
            return;
        }
        for (auto& j : jobs) j.dlc_ok = true;
    }
    // an official update over a game compressed by NXDock, or over an optimized one: the game would
    // not start with it; asked first, with the original game as the way out
    {
        const auto over = UpdatesThatStop(jobs);
        if (std::any_of(over.begin(), over.end(), [](const UpdateCheck& c) { return StopsTheGame(c.over); })) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::UpdateStops;
            m_dialog.jobs = std::move(jobs);
            m_dialog.checks = over;
            m_dialog.single = OriginalsFor(m_dialog.jobs).empty();
            return;
        }
    }
    // the same version installed already: asked first, as it would end at once and change nothing
    {
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        bool any = false;
        for (auto& j : jobs) {
            if (j.reinstall_ok || !j.repair.empty() || j.downgrade || j.resumed) continue;
            u64 id = j.title_id;
            u32 version = j.version;
            if (j.origin != InstallJob::Origin::Library) {
                u64 named = 0;
                u32 v = 0;
                if (TitleFromName(j.game + " " + j.label, named, v)) {
                    id = named;
                    version = v;
                }
            }
            // an optimized game with an update of it under it: installing it takes that update
            // away (merged_updates.hpp), so it is no repeat and nothing is asked
            const auto it = id && TitleKind(id) == 0 ? installed.find(id) : installed.end();
            if (it != installed.end() && nxdock::ReinstallTakesUpdate(version, it->second.patch)) {
                j.reinstall_ok = true;
                continue;
            }
            if (id && HasContent(id, version)) {
                any = true;
            } else {
                j.reinstall_ok = true; // not installed: nothing to ask
            }
        }
        if (any) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Reinstall;
            m_dialog.jobs = std::move(jobs);
            return;
        }
    }
    // the firmware, the keys and Atmosphère against what the contents ask for (2A): a window
    // only when something does not fit; NXDock never installs firmware or sigpatches
    u32 required = 0;
    bool unchecked = false;
    for (auto& j : jobs) {
        if (!j.checked) {
            unchecked = true;
            required = std::max(required, j.required_system);
        }
    }
    if (unchecked) {
        const PreCheck check = CheckBeforeInstall(m_sysinfo, required);
        for (auto& j : jobs) j.checked = true;
        if (check.Warns()) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::PreCheck;
            m_dialog.jobs = std::move(jobs);
            m_dialog.check = check;
            return;
        }
    }
    if (!m_risk_ok && m_safety && m_safety->Risky() && !jobs.empty()) {
        // not on an emuMMC, or Nintendo reachable: said once, before the first install
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Risk;
        m_dialog.jobs = std::move(jobs);
        return;
    }
    int added = 0, duplicate = 0;
    for (auto& job : jobs) {
        if (m_queue->Add(std::move(job))) {
            added++;
        } else {
            duplicate++;
        }
    }
    if (added) {
        NoticeOk(s.added_to_queue);
        // little battery and no charger: said before the install starts, it is not stopped
        if (m_ui.battery_notice && m_battery >= 0 && m_battery < m_ui.battery_level && !m_charging) {
            char text[200];
            std::snprintf(text, sizeof(text), s.battery_low, m_battery);
            Notice(text);
            m_notice_until = m_time + 6;
        }
        // back to the library: the strip shows the install going on
        m_detail = false;
    } else if (duplicate) {
        Notice(m_queue->Undoing() ? s.removing : s.already_in_queue);
    }
}

void App::FollowQueue() {
    if (!m_queue) {
        return;
    }
    // the best of the last speed test sets how many connections each download uses
    if (!m_speed_kept) {
        const SpeedTest::View v = m_speed.Snapshot();
        if (v.stage == SpeedTest::Stage::Done) {
            m_speed_kept = true;
            const int best = (v.net[0] >= v.net[1] && v.net[0] >= v.net[2]) ? 1 : (v.net[1] >= v.net[2] ? 2 : 4);
            if (v.net[best == 1 ? 0 : best == 2 ? 1 : 2] > 0 && best != m_prefs.connections) {
                m_prefs.connections = best;
                m_prefs.Save();
                m_queue->SetOptions(m_prefs.connections, m_prefs.download_test);
            }
        }
    }
    if (m_queue->Version() == m_queue_version) {
        return;
    }
    // a job ended: what the console has now, and the free space
    m_queue_version = m_queue->Version();
    const auto installed = m_queue->Installed();
    for (auto& g : m_games) {
        const auto it = installed.find(g.title_id);
        g.installed = it != installed.end() ? it->second : InstalledState{};
    }
    RebuildView();
    m_sd_checked = -100;
}

void App::UpdateQueue(u64 down, u64 held) {
    const auto rows = QueueOrder(m_queue->Jobs());
    const int count = int(rows.size());
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_queue_focus > 0) m_queue_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_queue_focus < count - 1) m_queue_focus++;
    m_queue_focus = std::clamp(m_queue_focus, 0, std::max(0, count - 1));
    if (count == 0) {
        return;
    }
    const InstallJob& j = rows[m_queue_focus];
    if (down & HidNpadButton_Minus) {
        AskScreenOff(); // the screen off while it installs (11A)
        return;
    }
    if (down & HidNpadButton_X) {
        if (j.state == InstallJob::State::Running) {
            // the one installing is cancelled only after a question, Keep installing in focus
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::QueueCancel;
            m_dialog.entry = j.id;
            m_dialog.text = j.game + (j.label.empty() ? "" : " · " + j.label);
            return;
        }
        m_queue->Remove(j.id); // a waiting or finished one goes at once
    }
    if ((down & HidNpadButton_A) && (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled)) {
        m_queue->Retry(j.id);
    }
    if (down & HidNpadButton_Y) {
        m_queue->Clear();
    }
    if (down & HidNpadButton_ZR) {
        const bool paused = !m_queue->Paused();
        m_queue->SetPaused(paused);
        Notice(paused ? TrM().qw_paused : TrM().qw_resumed);
    }
}

void App::DrawQueueStrip(NVGcontext* vg, float qy) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    Panel(vg, t, kGridX, qy, kWidth - 2 * kGridX, 54, 12);
    AddHit(kGridX, qy, kWidth - 2 * kGridX, 54, HitKind::QueueStrip);
    const auto live = m_queue->GetLive();
    if (!live.running) {
        const float qw = ui::Text(vg, 58, qy + 27, 16, t.muted, s.queue_empty) - 58;
        ui::Text(vg, 58 + qw + 12, qy + 27, 15, t.muted, m_queue->Busy() ? s.preparing : s.queue_empty_hint);
        StripKey(vg, qy);
        return;
    }
    const auto jobs = m_queue->Jobs();
    const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const InstallJob& j) { return j.id == live.job; });
    if (it == jobs.end()) {
        return;
    }
    // as chosen (1A): the picture, the name and how many wait, the bar, then the figures
    ui::Cover(vg, t, 54, qy + 10, 34, PlaceholderColor(it->color_key), Initial(it->game).c_str(), nullptr, false, false, 7,
              m_covers.Get(it->cover));
    CoverLoading(vg, 54, qy + 10, 34, it->cover);
    char title[160];
    std::snprintf(title, sizeof(title), s.installing_name, it->game.c_str());
    std::string head = title;
    const int waiting = m_queue->Waiting();
    if (waiting > 0) {
        char more[48];
        std::snprintf(more, sizeof(more), s.in_queue_count, waiting);
        head += std::string(" · ") + more;
    }
    nvgSave(vg);
    nvgScissor(vg, 100, qy, 380, 54);
    ui::Text(vg, 104, qy + 27, 16, t.text, head.c_str());
    nvgRestore(vg);

    // the four stages, as figures: network, decompress, write, check
    char figures[200];
    const float frac = JobFraction(live);
    if (live.merging) {
        std::snprintf(figures, sizeof(figures), "%s… %d %%", TrM().qm_merging, int(frac * 100));
    } else if (live.copying_save) {
        std::snprintf(figures, sizeof(figures), "%s…", TrM().sc_copying);
    } else if (live.phase >= 3) {
        std::snprintf(figures, sizeof(figures), "%s…", s.registering);
    } else if (live.total == 0) {
        std::snprintf(figures, sizeof(figures), "%s… · %s %.0f MB/s", s.preparing, s.network, live.net_rate);
    } else {
        // 45 % · 25 MB/s · 2:10 left
        const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
        char rest[64] = "";
        if (left >= 0) std::snprintf(rest, sizeof(rest), TrM().qs_left_fmt, ShortDuration(left).c_str());
        std::snprintf(figures, sizeof(figures), "%d %% · %.0f MB/s%s%s", int(frac * 100), live.write_rate, rest[0] ? " · " : "", rest);
    }
    const float fw = ui::TextWidth(vg, 15, figures);
    const float key_x = StripKey(vg, qy);
    const float bar_x = 494, bar_end = key_x - 16 - fw - 18;
    if (bar_end - bar_x > 40) {
        ui::Progress(vg, t, bar_x, qy + 23, bar_end - bar_x, 8, frac, live.merging ? t.violet : t.work, m_time);
    }
    ui::Text(vg, key_x - 16, qy + 27, 15, t.muted, figures, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
}

float App::StripKey(NVGcontext* vg, float qy) {
    // "X Queue" at the right end of the strip; returns where it starts
    const float lw = ui::TextWidth(vg, 15, Tr().queue);
    const float lx = kWidth - kGridX - 16 - lw;
    ui::Text(vg, lx, qy + 27, 15, m_theme.text, Tr().queue);
    ui::Text(vg, lx - 16, qy + 28, 22, m_theme.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    return lx - 30;
}

void App::DrawQueue(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const auto rows = QueueOrder(m_queue->Jobs());
    if (rows.empty()) {
        ui::Text(vg, kWidth / 2, 320, 22, t.text, s.queue_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::Text(vg, kWidth / 2, 356, 16, t.muted, s.queue_empty_hint, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    m_queue_focus = std::clamp(m_queue_focus, 0, int(rows.size()) - 1);
    const auto live = m_queue->GetLive();
    const bool running = live.running && rows.front().state == InstallJob::State::Running;
    float y = 98;

    // The game being installed, with its four stages (3A).
    if (running) {
        const InstallJob& j = rows.front();
        const float px = 48, py = y, pw = kWidth - 96, ph = 236;
        Panel(vg, t, px, py, pw, ph, 16);
        if (m_queue_focus == 0) {
            ui::Focus(vg, t, px, py, pw, ph, 16, m_time);
        }
        AddHit(px, py, pw, ph, HitKind::QueueRow, 0);
        ui::Cover(vg, t, px + 24, py + 24, 120, PlaceholderColor(j.color_key), Initial(j.game).c_str(), nullptr, false, false, 10,
                  m_covers.Get(j.cover));
        CoverLoading(vg, px + 24, py + 24, 120, j.cover);
        const float cx = px + 168;
        nvgSave(vg);
        nvgScissor(vg, cx, py + 16, pw - 168 - 140, 48);
        const std::string sub = j.label + " · " + Upper(j.format) + " · " + (j.nand ? s.internal : "SD") +
                                (live.merging ? std::string(" · ") + TrM().qm_tag : std::string());
        // a long name glides to be read whole; what it is keeps its room after it
        const float room = pw - 168 - 140, sub_w = std::min(ui::TextWidth(vg, 16, sub.c_str()), room * 0.45f);
        const float nw = ui::Marquee(vg, cx, py + 40, room - sub_w - 14, 24, t.text, j.game.c_str(), m_time) - cx;
        ui::Text(vg, cx + nw + 14, py + 41, 16, t.muted, sub.c_str());
        nvgRestore(vg);

        const float frac = JobFraction(live);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d %%", int(frac * 100));
        ui::Text(vg, px + pw - 24, py + 40, 26, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        char eta[200];
        if (live.merging) {
            // installed, now merged with the game (25G, as chosen: A)
            const u64 left_bytes = live.merge_total > live.merge_done ? live.merge_total - live.merge_done : 0;
            const double left = live.merge_rate > 0.1 && live.merge_phase < 3 ? double(left_bytes) / (live.merge_rate * 1024 * 1024) : -1;
            std::snprintf(eta, sizeof(eta), "%s… %s / %s%s%s · %s", TrM().qm_merging, SizeText(live.merge_done).c_str(),
                          SizeText(live.merge_total).c_str(), left >= 0 ? (std::string(" · ") + s.left + " ").c_str() : "",
                          left >= 0 ? Duration(left).c_str() : "", TrM().qm_playable);
        } else if (live.copying_save) {
            std::snprintf(eta, sizeof(eta), "%s… %s", TrM().sc_copying, live.copy_total ? (SizeText(live.copy_done) + " / " + SizeText(live.copy_total)).c_str() : "");
        } else if (live.phase >= 3) {
            std::snprintf(eta, sizeof(eta), "%s…", s.registering);
        } else if (live.total == 0) {
            std::snprintf(eta, sizeof(eta), "%s… · %s", s.preparing, Duration(live.elapsed).c_str());
        } else {
            const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
            std::snprintf(eta, sizeof(eta), "%s / %s%s%s", SizeText(live.written).c_str(), SizeText(live.total).c_str(),
                          left >= 0 ? (std::string(" · ") + s.left + " ").c_str() : "", left >= 0 ? Duration(left).c_str() : "");
        }
        const float ew = ui::Text(vg, cx, py + 70, 16, t.muted, eta) - cx;
        if (live.saved > 0 && !live.merging) {
            // what the console had already, in the green of what went well (16A)
            char saved[64];
            std::snprintf(saved, sizeof(saved), TrM().saved_fmt, SizeText(live.saved).c_str());
            const float sx = cx + ew + 12;
            nvgBeginPath(vg);
            nvgCircle(vg, sx + 4, py + 70, 4);
            nvgFillColor(vg, t.ok);
            nvgFill(vg);
            ui::Text(vg, sx + 14, py + 70, 16, t.ok, saved);
        }
        ui::Progress(vg, t, cx, py + 88, pw - 168 - 24, 12, frac, live.merging ? t.violet : t.work, m_time);

        struct Lane { std::string title; NVGcolor color; std::string value; std::string unit; float meter; int segments; };
        char v[32];
        std::vector<Lane> lanes;
        if (live.merging) {
            // the three steps of the merge: read and measure, compress and write, check
            const float part = live.merge_total ? std::clamp(float(double(live.merge_done) / double(live.merge_total)), 0.f, 1.f) : 0.f;
            const auto step = [&](int phase) { return live.merge_phase > phase ? 1.f : live.merge_phase == phase ? part : 0.f; };
            const auto rate = [&](int phase) {
                if (live.merge_phase != phase) return std::string(live.merge_phase > phase ? "100" : "\u2014");
                std::snprintf(v, sizeof(v), "%.0f", live.merge_rate);
                return std::string(v);
            };
            const auto unit = [&](int phase) { return std::string(live.merge_phase == phase ? "MB/s" : live.merge_phase > phase ? "%" : ""); };
            lanes.push_back({TrM().qm_read, t.work, rate(1), unit(1), step(1), -1});
            lanes.push_back({TrM().qm_pack, t.violet, rate(2), unit(2), step(2), -1});
            lanes.push_back({TrM().qm_check, t.ok, live.merge_phase >= 3 ? rate(3) : "\u2014", live.merge_phase >= 3 ? "MB/s" : TrM().qm_at_end, step(3), -1});
        } else {
        std::snprintf(v, sizeof(v), "%.0f", live.net_rate);
        lanes.push_back({s.network, t.work, v, "MB/s", live.source_size ? float(double(live.net) / double(live.source_size)) : 0.f, -1});
        if (live.threads == 0) {
            lanes.push_back({s.decompress, t.violet, "—", s.not_compressed, -1, 0});
        } else {
            std::snprintf(v, sizeof(v), "%.0f", live.ready_rate);
            lanes.push_back({s.decompress, t.violet, v, "MB/s", -1, std::max(0, live.threads)});
        }
        std::snprintf(v, sizeof(v), "%.0f", live.write_rate);
        lanes.push_back({j.nand ? s.write_nand : s.write_sd, t.orange, v, "MB/s", live.total ? float(double(live.written) / double(live.total)) : 0.f, -1});
        std::snprintf(v, sizeof(v), "%u", live.checked);
        lanes.push_back({s.check_sha, t.ok, v, std::string(s.parts_of) + " " + std::to_string(live.ncas),
                         live.ncas ? float(live.checked) / float(live.ncas) : 0.f, -1});
        }
        const int nl = int(lanes.size());
        const float lw = (pw - 168 - 24 - (nl - 1) * 14) / nl;
        for (int i = 0; i < nl; i++) {
            const Lane& lane = lanes[i];
            const float lx = cx + i * (lw + 14), ly = py + 118;
            ui::FillRound(vg, lx, ly, lw, 92, 12, t.panel2);
            ui::StrokeRound(vg, lx + .5f, ly + .5f, lw - 1, 91, 12, t.line, 1);
            nvgBeginPath(vg);
            nvgCircle(vg, lx + 18, ly + 20, 5);
            nvgFillColor(vg, lane.color);
            nvgFill(vg);
            nvgSave(vg);
            nvgScissor(vg, lx, ly, lw - 8, 92);
            ui::Text(vg, lx + 30, ly + 20, 14, t.muted, lane.title.c_str());
            const float vw = ui::Text(vg, lx + 14, ly + 48, 22, t.text, lane.value.c_str()) - (lx + 14);
            ui::Text(vg, lx + 14 + vw + 6, ly + 50, 14, t.muted, lane.unit.c_str());
            nvgRestore(vg);
            if (lane.segments < 0) {
                // with the moving stripes of the main bar while it works, still once it is full
                const float frac = std::clamp(lane.meter, 0.f, 1.f);
                ui::Progress(vg, t, lx + 14, ly + 70, lw - 28, 6, frac, lane.color, m_time, frac < 1.f);
            } else {
                // One segment per decompression thread; the fourth core is the system's. The ones
                // at work carry the moving stripes.
                const float sw = (lw - 28 - 12) / 4;
                for (int k = 0; k < 4; k++) {
                    if (k < lane.segments) {
                        ui::Progress(vg, t, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 1.f, lane.color, m_time, true);
                    } else {
                        NVGcolor c = lane.color;
                        c.a = 0.25f;
                        ui::FillRound(vg, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 3, c);
                    }
                }
            }
        }
        y = py + ph + 26;
        if (live.merging) {
            // what the job did already: the update, installed
            ui::Text(vg, 62, y, 13, t.muted, TrM().qm_installed);
            const std::string what = SizeText(live.installed_bytes) + " \u00b7 " + Duration(live.installed_seconds);
            const float mx = 62 + ui::TextWidth(vg, 13, TrM().qm_installed) + 20;
            nvgBeginPath(vg);
            nvgCircle(vg, mx, y, 7);
            nvgFillColor(vg, t.ok);
            nvgFill(vg);
            ui::CheckMark(vg, mx, y, 8, t.panel, 2.f);
            ui::Text(vg, mx + 14, y, 13, t.ok, what.c_str());
            y += 26;
        }
    }

    // Waiting, then done; the list scrolls to keep the row in focus in sight.
    const int first_row = running ? 1 : 0;
    // a failed row in focus shows what happened and what to do under the list (1A)
    const InstallJob& focused = rows[m_queue_focus];
    // or, installed but not merged with its game, why (25G)
    const std::string& card_text = focused.state == InstallJob::State::Done ? focused.merge_error : focused.error;
    const bool card = (focused.state == InstallJob::State::Failed || focused.state == InstallJob::State::Done) && !card_text.empty();
    const float card_h = card ? ErrorCard(vg, t, 48, 0, kWidth - 96, card_text, false) : 0;
    const float list_top = y, list_bottom = card ? std::max(y + 100, 650 - card_h - 12) : 640;
    if (card) {
        ErrorCard(vg, t, 48, 650 - card_h, kWidth - 96, card_text, true);
    }
    const float pitch = 54;
    ListArea(48, 98, kWidth - 96, list_bottom - 98, pitch);
    const int visible = std::max(1, int((list_bottom - list_top - 34) / pitch));
    const int focus_in_list = std::max(0, m_queue_focus - first_row);
    const int skip = std::max(0, focus_in_list - visible + 1);
    bool shown_waiting = false, shown_done = false;
    int drawn = 0;
    for (int r = first_row; r < int(rows.size()); r++) {
        const InstallJob& j = rows[r];
        if (r - first_row < skip) {
            continue;
        }
        if (drawn >= visible) {
            break;
        }
        const bool waiting = j.state == InstallJob::State::Waiting;
        if (waiting && !shown_waiting) {
            ui::Text(vg, 62, y, 13, t.muted, s.in_queue);
            y += 22;
            shown_waiting = true;
        } else if (!waiting && !shown_done) {
            y += shown_waiting ? 12 : 0;
            ui::Text(vg, 62, y + 8, 13, t.muted, s.done);
            y += 30;
            shown_done = true;
        }
        if (y + 44 > list_bottom) {
            break;
        }
        if (r == m_queue_focus) {
            ui::Focus(vg, t, 54, y - 5, kWidth - 108, 54, 10, m_time);
        }
        AddHit(54, y - 5, kWidth - 108, 54, HitKind::QueueRow, r);
        nvgGlobalAlpha(vg, waiting ? 0.55f : 1.f);
        ui::Cover(vg, t, 62, y, 44, PlaceholderColor(j.color_key), Initial(j.game).c_str(), nullptr, false, false, 8, m_covers.Get(j.cover));
        CoverLoading(vg, 62, y, 44, j.cover);
        nvgSave(vg);
        nvgScissor(vg, 122, y, 600, 44);
        ui::Marquee(vg, 122, y + 22, 590, 17, t.text, FileLabel(j).c_str(), m_time);
        nvgRestore(vg);
        std::string info = Upper(j.format) + " · " + SizeText(j.size) + " · " + (j.nand ? s.internal_short : "SD");
        if (j.state == InstallJob::State::Done) info += " · " + Duration(j.seconds);
        if (j.state == InstallJob::State::Done && j.saved > 0) {
            char saved[64];
            std::snprintf(saved, sizeof(saved), TrM().saved_fmt, SizeText(j.saved).c_str());
            info += std::string(" · ") + saved;
        }
        if (j.state == InstallJob::State::Done && j.merged) {
            char merged[96];
            std::snprintf(merged, sizeof(merged), TrM().qm_merged_fmt, SizeText(j.merge_saved).c_str());
            info += std::string(" \u00b7 ") + merged;
        }
        if (j.state == InstallJob::State::Done && !j.merge_error.empty()) {
            // installed, not merged: why, in place of the figures (the game waits in Compress games)
            nvgSave(vg);
            nvgScissor(vg, 730, y, 340, 44);
            ui::Text(vg, 1070, y + 22, 14, t.orange, ErrorTitle(j.merge_error).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
        } else if (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled) {
            // why, in place of the figures
            nvgSave(vg);
            nvgScissor(vg, 730, y, 340, 44);
            ui::Text(vg, 1070, y + 22, 14, j.state == InstallJob::State::Failed ? t.err : t.muted, ErrorTitle(j.error).c_str(),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
        } else {
            ui::Text(vg, 1070, y + 22, 15, t.muted, info.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        switch (j.state) {
            case InstallJob::State::Waiting:
                ui::Text(vg, 1218, y + 22, 15, t.muted, s.waiting, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            case InstallJob::State::Done: {
                const float cw = ui::TextWidth(vg, 15, s.checked);
                ui::Text(vg, 1218, y + 22, 15, t.ok, s.checked, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgCircle(vg, 1218 - cw - 14, y + 22, 9);
                nvgFillColor(vg, t.ok);
                nvgFill(vg);
                ui::CheckMark(vg, 1218 - cw - 14, y + 22, 10, t.panel, 2.2f);
                break;
            }
            case InstallJob::State::Failed:
                ui::Text(vg, 1218, y + 22, 15, t.err, s.failed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            case InstallJob::State::Cancelled:
                ui::Text(vg, 1218, y + 22, 15, t.muted, s.cancelled, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            default: break;
        }
        nvgGlobalAlpha(vg, 1.f);
        y += pitch;
        drawn++;
    }

    const InstallJob& f = rows[m_queue_focus];
    std::vector<ui::Hint> hints;
    if (f.state == InstallJob::State::Failed || f.state == InstallJob::State::Cancelled) {
        hints.push_back({ui::glyph::A, s.retry, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::X, f.state == InstallJob::State::Running ? s.cancel : s.remove, HidNpadButton_X});
    hints.push_back({ui::glyph::Y, s.clear_done, HidNpadButton_Y});
    if (running) hints.push_back({ui::glyph::Minus, TrM().so_button, HidNpadButton_Minus}); // the screen off (11A)
    const bool paused = m_queue->Paused();
    if (paused || m_queue->Waiting() > 0) hints.push_back({ui::glyph::ZR, paused ? TrM().qw_go_on : TrM().qw_pause, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, paused ? TrM().qw_paused : m_prefs.download_test ? s.download_test : live.merging ? TrM().qm_note : (running ? s.no_sleep : s.queue_done_hint), hints,
                m_hint_rects);
}

void App::DrawSettings(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const bool running = m_speed.Running();
    const Link::View link = m_link.Snapshot();
    const Config current = m_link.CurrentConfig();
    const bool linked = current.Linked();

    // the rows in their groups (the recommended option A of the visual test of 11/11)
    std::string library = s.not_linked;
    if (linked) {
        const std::string host = current.url.substr(current.url.find("://") == std::string::npos ? 0 : current.url.find("://") + 3);
        library = Clip((current.name.empty() ? link.app_name : current.name) + " · " + host, 36);
    }
    const std::string address = !m_manual_url.empty() ? Clip(m_manual_url, 32) : s.not_set;
    const char* picks[] = {s.wiz_pick_missing, s.wiz_pick_base, s.wiz_pick_base_update};
    const char* screens[] = {m.st_scr_normal, m.st_scr_dim, m.st_scr_off};
    char battery[64];
    if (m_ui.battery_notice) std::snprintf(battery, sizeof(battery), m.st_battery_fmt, m_ui.battery_level);
    else std::snprintf(battery, sizeof(battery), "%s", s.no_word);
    const std::string language = m_ui.language < 0 ? std::string(s.wiz_console_lang) + " · " + LanguageName(ConsoleLanguage())
                                                   : std::string(LanguageName(m_ui.language));
    const int theme = m_ui.theme_auto ? 2 : t.dark ? 1 : 0;
    const char* themes[] = {s.light, s.dark, s.wiz_theme_auto};
    struct Row { const char* label; std::string value; bool accent; };
    const auto yes = [&](bool on) { return std::string(on ? s.yes_word : s.no_word); };
    // how often the saves are copied by themselves (9A)
    char save_auto[64];
    if (m_ui.save_auto_days > 0) std::snprintf(save_auto, sizeof(save_auto), TrT().sv_auto_fmt, m_ui.save_auto_days);
    else std::snprintf(save_auto, sizeof(save_auto), "%s", s.no_word);
    // the version, and the newer one when it was found (5A)
    const Updater::View up = m_updater ? m_updater->Snapshot() : Updater::View{};
    const bool self_newer = up.stage == Updater::Stage::Newer;
    char self_update[160];
    if (self_newer) std::snprintf(self_update, sizeof(self_update), TrT().up_have_new_fmt, NXDOCK_VERSION, up.version.c_str());
    else std::snprintf(self_update, sizeof(self_update), TrT().up_have_fmt, NXDOCK_VERSION);
    const Row rows[kSettingRows] = {
        {s.library, library, linked},
        {s.forget, "", false},
        {s.share_row, yes(m_prefs.share_installed == 1), m_prefs.share_installed == 1},
        {m.st_updates, yes(m_ui.check_updates), m_ui.check_updates},
        {TrT().ms_row, yes(m_ui.versions_list), m_ui.versions_list},
        {s.speed_test, running ? s.cancel : s.start, linked},
        {m.st_dest, m_ui.dest == 2 ? s.wiz_ask_each : s.sd_card, true},
        {m.st_pick, picks[std::clamp(m_ui.pick, 0, 2)], true},
        {m.sc_row, yes(m_ui.save_copy), m_ui.save_copy},
        {TrT().sv_auto_row, save_auto, m_ui.save_auto_days > 0},
        {m.st_screen, screens[m_ui.screen_off_auto ? 2 : m_ui.saver ? 1 : 0], m_ui.saver || m_ui.screen_off_auto},
        {m.cz_row, yes(m_ui.compress_on), m_ui.compress_on},
        {m.st_finish, yes(m_ui.finish_effect), m_ui.finish_effect},
        {m.st_battery, battery, m_ui.battery_notice},
        {m.qw_notify_row, yes(m_ui.notify_done), m_ui.notify_done},
        {s.wiz_language, language, true},
        {s.theme, themes[theme], true},
        {m.style_row, StyleName(m_ui.style), true},
        {s.sounds_row, yes(m_ui.sounds), m_ui.sounds},
        {m.anim_row, m_ui.anim == 2 ? m.anim_none : m_ui.anim == 1 ? m.anim_soft : m.anim_full, m_ui.anim != 2},
        {m.st_focus_row, m_ui.focus_view == 1 ? m.st_focus_panel : m.st_focus_band, true},
        {m.st_touch_rumble, yes(m_ui.touch_rumble), m_ui.touch_rumble},
        {s.wiz_row, s.wiz_repeat, true},
        {TrT().up_row, self_update, self_newer},
        {TrT().up_notice_row, yes(m_ui.self_update_notice), m_ui.self_update_notice},
        {TrT().up_back_row, m_update_has_old ? TrT().up_back_kept : TrT().up_back_none, false},
        {m.hm_row, m_home.running_from ? m.hm_in_use : m_home.installed ? m.hm_on : s.no_word, m_home.installed},
        {m.hm_remove_row, "", false},
        {s.manual_address, address, !m_manual_url.empty()},
        {s.manual_key, linked ? s.key_saved : s.not_set, linked},
    };
    const char* groups[kGroups] = {m.st_g_library, m.st_g_install, m.st_g_notices, m.st_g_look, m.st_g_wizard, TrT().st_g_nxdock, m.st_g_home, m.st_g_manual};
    const float x = 48, w = 540, h = 56, gap = 8, head = 34;
    // where each row goes: a title before the first of each group
    float top_of[kSettingRows];
    float at = 0;
    for (int i = 0; i < kSettingRows; i++) {
        if (std::find(std::begin(kGroupStarts), std::end(kGroupStarts), i) != std::end(kGroupStarts)) at += head;
        top_of[i] = at;
        at += h + gap;
    }
    // the rows scroll when they do not fit, keeping the one in focus (and its title) in sight
    const float view_top = 88, view_bottom = 652;
    const float list_h = at + 8 + 70;
    const bool first_of_group = std::find(std::begin(kGroupStarts), std::end(kGroupStarts), m_setting) != std::end(kGroupStarts);
    const float focus_top = top_of[m_setting] - (first_of_group ? head : 0);
    float target = m_settings_scroll;
    if (focus_top - target < 0) target = focus_top;
    if (top_of[m_setting] + h + 12 - target > view_bottom - view_top) target = top_of[m_setting] + h + 12 - (view_bottom - view_top);
    if (m_setting == kSettingRows - 1) target = std::max(target, list_h - (view_bottom - view_top));
    m_settings_scroll = std::clamp(target, 0.f, std::max(0.f, list_h - (view_bottom - view_top)));
    nvgSave(vg);
    nvgScissor(vg, 0, view_top - 8, 600, view_bottom - view_top + 8);
    const float y0 = view_top + 8 - m_settings_scroll;
    int group = 0;
    for (int i = 0; i < kSettingRows; i++) {
        const float y = y0 + top_of[i];
        if (group < kGroups && kGroupStarts[group] == i) {
            if (y > view_top - 8 && y - 26 < view_bottom) draw::Section(vg, t, x + 6, y - 13, groups[group]);
            group++;
        }
        // only the rows in sight are drawn (the GPU has room for so many strokes a frame)
        if (y + h + 12 < view_top - 8 || y - 12 > view_bottom) continue;
        const bool dim = ((i == kRowForget || i == kRowSpeed || i == kRowShare) && !linked) ||
                         (i == kRowHomeRemove && (!m_home.installed || m_home.running_from)) ||
                         (i == kRowHome && m_home.running_from) || (i == kRowSelfBack && !m_update_has_old);
        nvgGlobalAlpha(vg, dim ? 0.45f : 1.f);
        Panel(vg, t, x, y, w, h, 14);
        if (i == m_setting) {
            ui::Focus(vg, t, x, y, w, h, 14, m_time);
        }
        // only the part in sight can be tapped
        const float hit_top = std::max(y, view_top - 8), hit_bottom = std::min(y + h, view_bottom);
        if (hit_bottom > hit_top) {
            AddHit(x, hit_top, w, hit_bottom - hit_top, HitKind::SettingsRow, i);
        }
        const float vw = std::min(260.f, ui::TextWidth(vg, 16, rows[i].value.c_str()));
        nvgSave(vg);
        nvgIntersectScissor(vg, x + 16, y, w - 32 - vw - 14, h);
        ui::Text(vg, x + 22, y + h / 2, 18, i == kRowForget ? t.warn : t.text, rows[i].label);
        nvgRestore(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, x + w - 22 - vw, y, vw, h);
        ui::Text(vg, x + w - 22, y + h / 2, 16, rows[i].accent ? t.work : t.muted, rows[i].value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        nvgGlobalAlpha(vg, 1.f);
    }
    const float end_y = y0 + at;
    if (linked && !link.enabled) {
        TextBox(vg, x, end_y + 4, w, 15, t.warn, s.link_off_warning);
    } else if (!linked) {
        TextBox(vg, x, end_y + 4, w, 15, t.muted, s.need_config);
    }
    nvgRestore(vg);

    if (m_setting == kRowStyle) DrawStylePanel(vg, 620, 96, kWidth - 620 - 48, 548);
    else DrawSpeedPanel(vg, 620, 96, kWidth - 620 - 48, 548);

    // A changes a choice; the rows that open something say what they do
    const char* act = m.st_change;
    if (m_setting == kRowAddress || m_setting == kRowKey) act = s.edit;
    else if (m_setting == kRowLibrary || m_setting == kRowWizard) act = m.open;
    else if (m_setting == kRowForget) act = m.fg_go;
    else if (m_setting == kRowSpeed) act = running ? s.cancel : s.start;
    else if (m_setting == kRowHome) act = m_home.installed ? m.hm_update : m.hm_make;
    else if (m_setting == kRowSelfUpdate) act = m.open;
    else if (m_setting == kRowSelfBack) act = TrT().up_back_go;
    else if (m_setting == kRowHomeRemove) act = m.hm_remove_go;
    if (m_setting == kRowStyle) {
        ui::HintBar(vg, t, nullptr, {{ui::glyph::DPad, act, HidNpadButton_AnyRight}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, act, HidNpadButton_A}, {ui::glyph::X, running ? s.cancel : s.speed_test, HidNpadButton_X},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawNoNetwork(NVGcontext* vg, const char* retry) {
    const Theme& t = m_theme;
    const More& m = TrM();
    // the fan of the WiFi, empty, and what to do
    const float cx = kWidth / 2, cy = 270;
    NVGcolor c = t.muted;
    for (int i = 1; i <= 3; i++) {
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy + 22, 14.f * float(i), -2.36f, -0.79f, NVG_CW);
        nvgStrokeColor(vg, c);
        nvgStrokeWidth(vg, 4);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy + 22, 4);
    nvgFillColor(vg, c);
    nvgFill(vg);
    ui::Text(vg, cx, cy + 74, 24, t.text, m.nn_title, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgSave(vg);
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, 17);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, t.muted);
    nvgTextBox(vg, cx - 330, cy + 100, 660, m.wf_none_hint, nullptr);
    nvgRestore(vg);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::X, retry, HidNpadButton_X}, {ui::glyph::Plus, Tr().exit, HidNpadButton_Plus}},
                m_hint_rects);
}

void App::DrawPairing(NVGcontext* vg, const Link::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    using State = Link::State;
    if (m_wifi == -1 && v.state != State::Waiting) {
        // no network: nothing below can work, said in large
        DrawNoNetwork(vg, s.search_again);
        return;
    }
    const float top = 100, ph = 520;
    const float lx = 48, lw = 560, rx = 640, rw = kWidth - 640 - 48;

    // Reconnecting with a remembered library: just say so.
    if (v.state == State::Reconnecting) {
        ui::Spinner(vg, kWidth / 2, 330, 20, t.work, m_time);
        ui::Text(vg, kWidth / 2, 384, 20, t.muted, s.reconnecting, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ui::glyph::Plus, s.exit, HidNpadButton_Plus}}, m_hint_rects);
        return;
    }

    // Left: the libraries found, and typing an address.
    Panel(vg, t, lx, top, lw, ph, 16);
    ui::Text(vg, lx + 26, top + 36, 22, t.text, s.find_library);
    TextBox(vg, lx + 26, top + 56, lw - 52, 15, t.muted, s.found_hint);
    float y = top + 100;
    const int rows = int(v.found.size()) + 1;
    for (int i = 0; i < rows; i++) {
        const bool typed = i == int(v.found.size());
        const float rh = 64;
        ui::FillRound(vg, lx + 22, y, lw - 44, rh, 12, t.panel2);
        ui::StrokeRound(vg, lx + 22.5f, y + .5f, lw - 45, rh - 1, 12, t.line, 1);
        if (i == m_pair_focus) {
            ui::Focus(vg, t, lx + 22, y, lw - 44, rh, 12, m_time);
        }
        AddHit(lx + 22, y, lw - 44, rh, HitKind::PairRow, i);
        if (typed) {
            ui::Text(vg, lx + 44, y + 24, 17, t.text, s.type_address);
            ui::Text(vg, lx + 44, y + 46, 14, t.muted, s.type_address_hint);
        } else {
            const auto& lib = v.found[i];
            ui::Text(vg, lx + 44, y + 24, 17, t.text, lib.name.c_str());
            const size_t scheme = lib.url.find("://");
            const std::string where = lib.url.substr(scheme == std::string::npos ? 0 : scheme + 3) + "  ·  " + lib.version;
            ui::Text(vg, lx + 44, y + 46, 14, t.muted, where.c_str());
            if (i == v.chosen) {
                const float cw = ui::TextWidth(vg, 14, s.chosen);
                ui::Text(vg, lx + lw - 44, y + rh / 2, 14, t.ok, s.chosen, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgCircle(vg, lx + lw - 44 - cw - 14, y + rh / 2, 9);
                nvgFillColor(vg, t.ok);
                nvgFill(vg);
                ui::CheckMark(vg, lx + lw - 44 - cw - 14, y + rh / 2, 10, t.panel, 2.2f);
            }
        }
        y += rh + 10;
    }
    if (v.state == State::Searching) {
        ui::Spinner(vg, lx + 36, y + 18, 8, t.work, m_time);
        ui::Text(vg, lx + 54, y + 18, 15, t.muted, s.searching);
        ui::Progress(vg, t, lx + 26, y + 40, lw - 52, 5, v.progress, t.work, m_time);
    } else if (v.state == State::Choose && v.found.empty()) {
        TextBox(vg, lx + 26, y + 6, lw - 52, 15, t.warn, s.none_found);
    } else if (v.state == State::Offline) {
        CompactError(vg, t, lx + 26, y + 6, lw - 52, v.error, t.warn, TrM().pr_todo_search);
    }

    // Right: the code and what to do with it.
    Panel(vg, t, rx, top, rw, ph, 16);
    ui::Text(vg, rx + 26, top + 36, 22, t.text, s.approve_title);
    TextBox(vg, rx + 26, top + 56, rw - 52, 15, t.muted, s.approve_hint);

    const float cy = top + 118;
    const bool has_code = v.state == State::Waiting && !v.code.empty();
    // eight boxes in two groups, as the app shows the code
    const float bw = 50, bh = 66, bg = 8, dash = 22;
    const float total = 8 * bw + 6 * bg + dash + 2 * bg;
    float bx = rx + (rw - total) / 2;
    size_t ci = 0;
    for (int i = 0; i < 8; i++) {
        if (i == 4) {
            ui::Text(vg, bx + dash / 2, cy + bh / 2, 30, t.muted, "–", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            bx += dash + bg;
        }
        ui::FillRound(vg, bx, cy, bw, bh, 10, t.panel2);
        ui::StrokeRound(vg, bx + .5f, cy + .5f, bw - 1, bh - 1, 10, has_code ? t.work : t.line, has_code ? 1.5f : 1);
        if (has_code) {
            while (ci < v.code.size() && v.code[ci] == '-') ci++;
            if (ci < v.code.size()) {
                const char c[2] = {v.code[ci++], 0};
                ui::Text(vg, bx + bw / 2, cy + bh / 2 + 2, 34, t.text, c, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
        }
        bx += bw + bg;
    }

    // The QR (4A): the address of the app chosen, so a phone opens the library; the app shows the
    // notice to pair on any of its pages. The steps go on its right.
    const float qy = cy + bh + 26, qs = 150;
    std::string chosen_url;
    if (v.chosen >= 0 && v.chosen < int(v.found.size())) {
        chosen_url = v.found[v.chosen].url;
    } else if (!v.url.empty()) {
        chosen_url = v.url;
    }
    if (!chosen_url.empty()) {
        ui::Qr(vg, rx + 26, qy, qs, chosen_url + "/");
        const float tx = rx + 26 + qs + 22, tw = rw - 52 - qs - 22;
        const size_t scheme = chosen_url.find("://");
        const std::string host = chosen_url.substr(scheme == std::string::npos ? 0 : scheme + 3);
        TextBox(vg, tx, qy + 18, tw, 15, t.muted, s.qr_scan);
        const float h1 = TextBoxHeight(vg, tw, 15, s.qr_scan);
        nvgSave(vg);
        nvgScissor(vg, tx, qy, tw, qs);
        ui::Text(vg, tx, qy + 18 + h1 + 16, 18, t.text, host.c_str());
        nvgRestore(vg);
        TextBox(vg, tx, qy + 18 + h1 + 34, tw, 15, t.muted, s.qr_notice);
    } else {
        ui::FillRound(vg, rx + 26, qy, qs, qs, 10, t.panel2);
        ui::StrokeRound(vg, rx + 26.5f, qy + .5f, qs - 1, qs - 1, 10, t.line, 1);
        ui::Text(vg, rx + 26 + qs / 2, qy + qs / 2, 22, t.muted, "QR", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // What happens now.
    const float sy = qy + qs + 34;
    if (v.state == State::Waiting) {
        char line[160];
        std::snprintf(line, sizeof(line), s.waiting_accept, v.seconds_left / 60, v.seconds_left % 60);
        ui::Spinner(vg, rx + 36, sy, 8, t.work, m_time);
        ui::Text(vg, rx + 54, sy, 16, t.work, line);
    } else if (v.state == State::Asking) {
        ui::Spinner(vg, rx + 36, sy, 8, t.work, m_time);
    } else if (!v.error.empty() && v.state != State::Offline) {
        // what happened, and what to do now: another code
        CompactError(vg, t, rx + 26, sy - 12, rw - 52, v.error, v.state == State::Failed ? t.err : t.warn, TrM().pr_todo_code);
    }

    // Remember: on by default; off, the link lasts until NXDock closes.
    const float my = top + ph - 70;
    AddHit(rx + 16, my - 8, rw - 32, 52, HitKind::Remember);
    ui::FillRound(vg, rx + 26, my, 30, 30, 8, v.remember ? t.work : t.panel2);
    if (!v.remember) {
        ui::StrokeRound(vg, rx + 26.5f, my + .5f, 29, 29, 8, t.muted, 1.5f);
    } else {
        ui::CheckMark(vg, rx + 41, my + 15, 16, t.on_work, 2.6f);
    }
    ui::Text(vg, rx + 68, my + 15, 17, t.text, s.remember);
    ui::Text(vg, rx + rw - 26, my + 16, 22, t.muted, ui::glyph::Y, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    const bool code_shown = v.state == State::Waiting || v.state == State::Expired || v.state == State::Refused;
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.choose, HidNpadButton_A},
                 {ui::glyph::X, code_shown ? s.new_code : s.search_again, HidNpadButton_X},
                 {ui::glyph::Y, TrM().pr_remember, HidNpadButton_Y},
                 {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                m_hint_rects);
}

void App::DrawSpeedPanel(NVGcontext* vg, float x, float y, float w, float h) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const SpeedTest::View v = m_speed.Snapshot();
    Panel(vg, t, x, y, w, h, 16);
    const float px = x + 24, pw = w - 48;
    ui::Text(vg, px, y + 34, 21, t.text, s.speed_test);

    // The mode: an applet gets small buffers, which caps the speed.
    const bool applet = net::IsAppletMode();
    TextBox(vg, px, y + 58, pw, 14, applet ? t.warn : t.muted, applet ? s.applet_warning : s.app_mode);

    // One line per measure: label, bar and value. Bars share a scale so they compare at a glance.
    double top = std::max({v.net[0], v.net[1], v.net[2], v.sd, 20.0});
    if (v.live > top) top = v.live;
    const int best = (v.net[0] >= v.net[1] && v.net[0] >= v.net[2]) ? 0 : (v.net[1] >= v.net[2] ? 1 : 2);
    struct Line { const char* label; double value; SpeedTest::Stage stage; NVGcolor color; };
    const Line lines[] = {
        {s.conn1, v.net[0], SpeedTest::Stage::Net1, t.work},
        {s.conn2, v.net[1], SpeedTest::Stage::Net2, t.work},
        {s.conn4, v.net[2], SpeedTest::Stage::Net4, t.work},
        {s.sd_write, v.sd, SpeedTest::Stage::Sd, t.orange},
    };
    float ly = y + 128;
    for (int i = 0; i < 4; i++) {
        const auto& l = lines[i];
        const bool now = v.stage == l.stage;
        const double value = now ? v.live : l.value;
        ui::Text(vg, px, ly, 16, now ? t.text : t.muted, l.label);
        char text[48];
        if (value > 0) {
            std::snprintf(text, sizeof(text), "%.1f MB/s", value);
        } else {
            std::snprintf(text, sizeof(text), "%s", now ? "…" : s.waiting_test);
        }
        const float vw = ui::Text(vg, px + pw, ly, 16, value > 0 ? t.text : t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        (void)vw;
        if (i < 3 && v.stage == SpeedTest::Stage::Done && i == best && l.value > 0) {
            const float bw = ui::TextWidth(vg, 12, s.best) + 14;
            const float tx = px + ui::TextWidth(vg, 16, l.label) + 10;
            ui::FillRound(vg, tx, ly - 11, bw, 22, 6, t.ok);
            ui::Text(vg, tx + bw / 2, ly, 12, nvgRGB(255, 255, 255), s.best, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        ui::Progress(vg, t, px, ly + 18, pw, 8, float(value / top), l.color, m_time, now);
        ly += 66;
    }

    // Progress of the stage running, or the error, or what the test does.
    const float by = y + h - 70;
    if (v.stage == SpeedTest::Stage::Failed) {
        CompactError(vg, t, px, by - 8, pw, v.error, t.err, TrM().sp_todo);
    } else if (m_speed.Running()) {
        ui::Progress(vg, t, px, by + 8, pw, 6, v.progress, t.muted, m_time, true);
    } else {
        TextBox(vg, px, by, pw, 14, t.muted, s.test_note);
    }
}

namespace {

// Room an install to the internal memory needs over its size, as the install core asks.
constexpr u64 kNandMargin = 64ull * 1024 * 1024;

bool Undoable(const SessionItem& item) {
    return item.state == SessionItem::State::Installed || item.state == SessionItem::State::Failed;
}

// A time for a column: 8:52, 1:02:10.
std::string ShortDuration(double seconds) {
    char text[24];
    const int sec = int(seconds + 0.5);
    if (sec >= 3600) {
        std::snprintf(text, sizeof(text), "%d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    } else {
        std::snprintf(text, sizeof(text), "%d:%02d", sec / 60, sec % 60);
    }
    return text;
}

const char* WhereText(bool nand) {
    return nand ? Tr().internal : "SD";
}

std::string SpeedText(double mbs) {
    if (mbs <= 0) {
        return "—";
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%.1f MB/s", mbs);
    return text;
}

// What a list of the session frees, on each storage: "7.0 GB on the SD card · 4.2 GB in the internal memory".
std::string FreedText(const std::vector<SessionItem>& items) {
    const auto& s = Tr();
    u64 sd = 0, nand = 0;
    for (const auto& i : items) {
        (i.nand ? nand : sd) += i.size;
    }
    std::string out;
    char part[96];
    if (sd) {
        std::snprintf(part, sizeof(part), s.freed_sd, SizeText(sd).c_str());
        out = part;
    }
    if (nand) {
        std::snprintf(part, sizeof(part), s.freed_nand, SizeText(nand).c_str());
        out += (out.empty() ? "" : " · ") + std::string(part);
    }
    return out;
}

// A round mark before a result: a tick, a cross, or nothing.
void ResultMark(NVGcontext* vg, const Theme& t, float cx, float cy, HistoryEntry::Result r) {
    if (r == HistoryEntry::Result::Installed) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 9);
        nvgFillColor(vg, t.ok);
        nvgFill(vg);
        ui::CheckMark(vg, cx, cy, 10, t.panel, 2.2f);
    } else if (r == HistoryEntry::Result::Failed) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 9);
        nvgFillColor(vg, t.err);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 3.5f, cy - 3.5f);
        nvgLineTo(vg, cx + 3.5f, cy + 3.5f);
        nvgMoveTo(vg, cx + 3.5f, cy - 3.5f);
        nvgLineTo(vg, cx - 3.5f, cy + 3.5f);
        nvgStrokeColor(vg, t.panel);
        nvgStrokeWidth(vg, 2.2f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 8);
        nvgStrokeColor(vg, t.muted);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }
}

const char* ResultText(HistoryEntry::Result r) {
    const auto& s = Tr();
    switch (r) {
        case HistoryEntry::Result::Installed: return s.res_installed;
        case HistoryEntry::Result::Failed: return s.failed;
        case HistoryEntry::Result::Cancelled: return s.cancelled;
        case HistoryEntry::Result::Removed: return s.removed;
    }
    return "";
}

NVGcolor ResultColor(const Theme& t, HistoryEntry::Result r) {
    switch (r) {
        case HistoryEntry::Result::Installed: return t.ok;
        case HistoryEntry::Result::Failed: return t.err;
        default: return t.muted;
    }
}

// The glyphs of ZL and ZR side by side, for the hint that switches views.
const char* ViewGlyphs() {
    static const std::string glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    return glyphs.c_str();
}

} // namespace

void App::DrawInstalled(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    if (m_history_version != m_queue->Version()) {
        m_history = History(HistoryPath()).Load();
        m_history_version = m_queue->Version();
    }
    const int session = int(m_queue->Session().size());
    ReadConsole();
    const auto console = m_space ? m_space->Snapshot() : Space::View{};
    const char* names[] = {TrM().ic_console, s.this_session, s.history};
    const int counts[] = {int(console.apps.size()), session, int(m_history.size())};
    float x = 48;
    for (int i = 0; i < 3; i++) {
        char chip[64];
        std::snprintf(chip, sizeof(chip), "%s · %d", names[i], counts[i]);
        const float w = ui::Chip(vg, t, x, 92, chip, i == m_installed_view);
        AddHit(x - 4, 82, w + 8, 54, HitKind::InstalledView, i);
        x += w + 10;
    }
    if (m_installed_view == 0) {
        DrawConsole(vg, console);
    } else if (m_installed_view == 1) {
        DrawSession(vg);
    } else {
        DrawHistory(vg);
    }
}

void App::DrawSession(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const auto items = m_queue->Session();
    if (items.empty()) {
        ui::Text(vg, kWidth / 2, 330, 22, t.text, s.session_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::Text(vg, kWidth / 2, 366, 16, t.muted, s.session_empty_hint, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ViewGlyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    m_installed_focus = std::clamp(m_installed_focus, 0, int(items.size()) - 1);
    const float top = 148, pitch = 72;
    const int visible = 5;
    ListArea(48, top, kWidth - 96, visible * pitch, pitch);
    const int first = std::clamp(m_installed_focus - visible + 1, 0, std::max(0, int(items.size()) - visible));
    for (int r = first; r < int(items.size()) && r < first + visible; r++) {
        const SessionItem& i = items[r];
        const float y = top + (r - first) * pitch;
        if (r == m_installed_focus) {
            ui::FillRound(vg, 48, y, kWidth - 96, pitch - 6, 12, t.panel2);
            ui::Focus(vg, t, 48, y, kWidth - 96, pitch - 6, 12, m_time);
        }
        AddHit(48, y, kWidth - 96, pitch - 6, HitKind::InstalledRow, r);
        const bool gone = i.state == SessionItem::State::Removed;
        nvgGlobalAlpha(vg, gone ? 0.5f : 1.f);
        ui::Cover(vg, t, 62, y + 5, 56, PlaceholderColor(i.color_key), Initial(i.game).c_str(), nullptr, false, false, 9, m_covers.Get(i.cover));
        CoverLoading(vg, 62, y + 5, 56, i.cover);
        nvgSave(vg);
        nvgScissor(vg, 134, y, 540, pitch);
        ui::Text(vg, 134, y + 23, 18, t.text, i.game.c_str());
        if (i.state == SessionItem::State::Failed && !i.error.empty()) {
            ui::Text(vg, 134, y + 47, 14, t.err, ErrorTitle(i.error).c_str()); // why it could not be removed
        } else {
            ui::Text(vg, 134, y + 47, 14, t.muted, (i.label + " · " + Upper(i.format)).c_str());
        }
        nvgRestore(vg);
        ui::Text(vg, 690, y + 33, 16, t.muted, WhereText(i.nand));
        ui::Text(vg, 930, y + 33, 16, t.muted, SizeText(i.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        const std::string when = WhenText(i.when);
        const size_t space = when.rfind(' ');
        ui::Text(vg, 960, y + 33, 16, t.muted, space == std::string::npos ? when.c_str() : when.substr(space + 1).c_str());
        const float ry = y + 33;
        switch (i.state) {
            case SessionItem::State::Installed: {
                const float w = ui::TextWidth(vg, 15, s.res_installed);
                ui::Text(vg, 1218, ry, 15, t.ok, s.res_installed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                ResultMark(vg, t, 1218 - w - 14, ry, HistoryEntry::Result::Installed);
                break;
            }
            case SessionItem::State::Removing: {
                const float w = ui::TextWidth(vg, 15, s.removing);
                ui::Text(vg, 1218, ry, 15, t.work, s.removing, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                ui::Spinner(vg, 1218 - w - 16, ry, 8, t.work, m_time);
                break;
            }
            case SessionItem::State::Removed:
                ui::Text(vg, 1218, ry, 15, t.muted, s.removed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            case SessionItem::State::Failed:
                ui::Text(vg, 1218, ry, 15, t.err, s.remove_failed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
        }
        nvgGlobalAlpha(vg, 1.f);
    }

    // Undo all, below the list
    std::vector<SessionItem> left;
    for (const auto& i : items) {
        if (Undoable(i)) left.push_back(i);
    }
    const float by = 520, bh = 72;
    Panel(vg, t, 48, by, kWidth - 96, bh, 14);
    if (!left.empty()) {
        char text[256];
        std::snprintf(text, sizeof(text), s.undo_bar, FreedText(left).c_str());
        const float bw = ui::TextWidth(vg, 18, s.undo_all) + 76, bx = kWidth - 48 - 16 - bw;
        nvgSave(vg);
        nvgScissor(vg, 70, by, bx - 90, bh);
        ui::Text(vg, 70, by + bh / 2, 16, t.muted, text);
        nvgRestore(vg);
        ui::FillRound(vg, bx, by + 12, bw, bh - 24, 12, t.work);
        ui::Text(vg, bx + 22, by + bh / 2 + 1, 24, t.on_work, ui::glyph::Y, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, bx + 54, by + bh / 2, 18, t.on_work, s.undo_all);
        m_hint_rects.push_back({bx, by + 4, bw, bh - 8, HidNpadButton_Y});
    } else {
        ui::Text(vg, 70, by + bh / 2, 16, t.muted, m_queue->Undoing() ? s.removing : s.session_empty_hint);
    }

    std::vector<ui::Hint> hints;
    if (Undoable(items[m_installed_focus])) {
        hints.push_back({ui::glyph::X, s.remove_this, HidNpadButton_X});
    }
    if (!left.empty()) {
        hints.push_back({ui::glyph::Y, s.undo_all, HidNpadButton_Y});
    }
    hints.push_back({ViewGlyphs(), s.view_switch, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::DrawHistory(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    if (m_history.empty()) {
        ui::Text(vg, kWidth / 2, 340, 22, t.muted, s.history_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ViewGlyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    // the summary: how many installed, how much, how fast
    int installed = 0;
    u64 bytes = 0;
    double seconds = 0, best = 0;
    for (const auto& e : m_history) {
        if (e.result != HistoryEntry::Result::Installed) continue;
        installed++;
        bytes += e.size;
        seconds += e.seconds;
        best = std::max(best, e.Speed());
    }
    char part[96];
    float sx = 50;
    const auto summary = [&](const char* value, const char* rest, bool value_first) {
        if (value_first) {
            sx = ui::Text(vg, sx, 152, 16, t.text, value) + 6;
            sx = ui::Text(vg, sx, 152, 16, t.muted, rest) + 26;
        } else {
            sx = ui::Text(vg, sx, 152, 16, t.muted, rest) + 6;
            sx = ui::Text(vg, sx, 152, 16, t.text, value) + 26;
        }
    };
    std::snprintf(part, sizeof(part), s.hist_installed, installed);
    ui::Text(vg, sx, 152, 16, t.text, part);
    sx += ui::TextWidth(vg, 16, part) + 26;
    std::snprintf(part, sizeof(part), s.hist_total, SizeText(bytes).c_str());
    ui::Text(vg, sx, 152, 16, t.text, part);
    sx += ui::TextWidth(vg, 16, part) + 26;
    if (seconds > 0.5) {
        summary(SpeedText(double(bytes) / seconds / (1024.0 * 1024.0)).c_str(), s.hist_avg, false);
        summary(SpeedText(best).c_str(), s.hist_best, false);
    }

    // the columns
    const float top = 178;
    const float c_date = 62, c_cover = 186, c_name = 242, c_where = 662, c_size = 830, c_time = 912, c_speed = 1026, c_result = 1050;
    ui::Text(vg, c_date, top, 13, t.muted, s.col_date);
    ui::Text(vg, c_name, top, 13, t.muted, s.col_game);
    ui::Text(vg, c_where, top, 13, t.muted, s.col_where);
    ui::Text(vg, c_size, top, 13, t.muted, s.col_size, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Text(vg, c_time, top, 13, t.muted, s.col_time, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Text(vg, c_speed, top, 13, t.muted, s.col_speed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Text(vg, c_result, top, 13, t.muted, s.col_result);

    m_installed_focus = std::clamp(m_installed_focus, 0, int(m_history.size()) - 1);
    const float list_top = 196, pitch = 64;
    const int visible = 7;
    ListArea(48, list_top, kWidth - 96, visible * pitch, pitch);
    const int first = std::clamp(m_installed_focus - visible + 1, 0, std::max(0, int(m_history.size()) - visible));
    for (int r = first; r < int(m_history.size()) && r < first + visible; r++) {
        const HistoryEntry& e = m_history[r];
        const float y = list_top + (r - first) * pitch, cy = y + pitch / 2 - 3;
        if (r == m_installed_focus) {
            ui::FillRound(vg, 48, y, kWidth - 96, pitch - 6, 10, t.panel2);
            ui::Focus(vg, t, 48, y, kWidth - 96, pitch - 6, 10, m_time);
        } else if (r > first) {
            nvgBeginPath(vg);
            nvgRect(vg, 58, y - 3, kWidth - 116, 1);
            nvgFillColor(vg, t.line);
            nvgFill(vg);
        }
        AddHit(48, y, kWidth - 96, pitch - 6, HitKind::InstalledRow, r);
        ui::Text(vg, c_date, cy, 15, t.muted, WhenText(e.when).c_str());
        ui::Cover(vg, t, c_cover, cy - 22, 44, PlaceholderColor(e.game), Initial(e.game).c_str(), nullptr, false, false, 8, 0);
        nvgSave(vg);
        nvgScissor(vg, c_name, y, c_where - c_name - 16, pitch);
        ui::Text(vg, c_name, cy - 10, 16, t.text, e.game.c_str());
        if (e.result == HistoryEntry::Result::Failed && !e.error.empty()) {
            ui::Text(vg, c_name, cy + 12, 13, t.err, ErrorTitle(e.error).c_str());
        } else {
            ui::Text(vg, c_name, cy + 12, 13, t.muted, (e.label + " · " + Upper(e.format)).c_str());
        }
        nvgRestore(vg);
        ui::Text(vg, c_where, cy, 15, t.muted, e.nand ? s.internal_short : "SD");
        ui::Text(vg, c_size, cy, 15, t.muted, SizeText(e.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        const bool done = e.result == HistoryEntry::Result::Installed;
        ui::Text(vg, c_time, cy, 15, t.muted, e.seconds > 0 && e.result != HistoryEntry::Result::Removed ? ShortDuration(e.seconds).c_str() : "—",
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, c_speed, cy, 15, t.muted, done ? SpeedText(e.Speed()).c_str() : "—", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ResultMark(vg, t, c_result + 9, cy, e.result);
        nvgSave(vg);
        nvgScissor(vg, c_result + 24, y, 1232 - c_result - 24, pitch);
        ui::Text(vg, c_result + 26, cy, 15, ResultColor(t, e.result), ResultText(e.result));
        nvgRestore(vg);
    }
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.details, HidNpadButton_A}, {ui::glyph::Minus, s.clear_history, HidNpadButton_Minus},
                 {ViewGlyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::AskUndo(std::vector<int> jobs) {
    if (m_queue->Busy() || m_queue->Undoing()) {
        Notice(Tr().undo_wait_queue);
        return;
    }
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Undo;
    m_dialog.undo = std::move(jobs);
}

void App::UpdateInstalled(u64 down, u64 held) {
    if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
        m_installed_view = (m_installed_view + ((down & HidNpadButton_ZR) ? 1 : 2)) % 3;
        m_installed_focus = 0;
        m_console_app = 0;
        return;
    }
    if (m_installed_view == 0) {
        UpdateConsole(down, held);
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_installed_focus > 0) m_installed_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown)) m_installed_focus++; // clamped when drawn
    if (m_installed_view == 1) {
        const auto items = m_queue->Session();
        if (items.empty()) {
            return;
        }
        m_installed_focus = std::clamp(m_installed_focus, 0, int(items.size()) - 1);
        const SessionItem& item = items[m_installed_focus];
        if ((down & HidNpadButton_X) && Undoable(item)) {
            AskUndo({item.job});
        }
        if ((down & HidNpadButton_Y) && std::any_of(items.begin(), items.end(), Undoable)) {
            AskUndo({});
        }
    } else {
        if (m_history.empty()) {
            return;
        }
        m_installed_focus = std::clamp(m_installed_focus, 0, int(m_history.size()) - 1);
        if (down & HidNpadButton_A) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::HistoryInfo;
            m_dialog.entry = m_installed_focus;
            m_dialog.single = true;
        }
        if (down & HidNpadButton_Minus) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::ClearHistory;
        }
    }
}

void App::DrawDialog(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    // everything behind goes dark; a tap there is Cancel
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f * ui::EaseOut(ui::Appear(m_dialog_at, 0.2f))));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::DialogBack);

    struct Line {
        std::string text, tag, right;
        NVGcolor color;
        int mark = 0; // 1: fine, 2: a warning (the check before installing)
        bool focus = false; // the row in focus (the versions of a game, 20A)
        int hit = -1;       // a row that answers to touch
    };
    std::string title, body, note, primary, error;
    std::vector<Line> lines;
    bool can = true, bar = false;
    bool verify_bar = false, verify_running = false;
    std::string cancel_label = s.cancel;
    float verify_frac = 0;
    std::string verify_text;
    u64 size = 0;
    switch (m_dialog.kind) {
        case DialogKind::Nand: {
            title = s.nand_title;
            body = s.nand_body;
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.label, "", Upper(j.format) + " · " + SizeText(j.size), t.text});
            }
            primary = s.nand_confirm;
            bar = m_nand_total_bytes > 0 && m_nand_free_bytes >= 0; // unknown: no bar
            if (m_nand_free_bytes >= 0 && size + kNandMargin > u64(m_nand_free_bytes)) {
                can = false;
                char text[128];
                std::snprintf(text, sizeof(text), s.nand_no_room, SizeText(size + kNandMargin - u64(m_nand_free_bytes)).c_str());
                error = text;
            }
            break;
        }
        case DialogKind::Undo: {
            const auto items = m_queue->Session();
            std::vector<SessionItem> chosen;
            for (const auto& i : items) {
                const bool wanted = m_dialog.undo.empty() || std::find(m_dialog.undo.begin(), m_dialog.undo.end(), i.job) != m_dialog.undo.end();
                if (wanted && Undoable(i)) chosen.push_back(i);
            }
            title = m_dialog.undo.empty() ? s.undo_title : s.undo_one_title;
            for (const auto& i : chosen) {
                size += i.size;
                lines.push_back({i.game + " · " + i.label, WhereText(i.nand), SizeText(i.size), t.text});
            }
            note = s.undo_note;
            char text[96];
            std::snprintf(text, sizeof(text), s.undo_confirm, int(chosen.size()), SizeText(size).c_str());
            primary = text;
            can = !chosen.empty();
            break;
        }
        case DialogKind::UpdateAll: {
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.game + " · " + j.label, WhereText(j.nand), SizeText(j.size), t.text});
            }
            char text[96];
            std::snprintf(text, sizeof(text), s.update_all_title, int(m_dialog.jobs.size()));
            title = text;
            note = s.update_all_note;
            std::snprintf(text, sizeof(text), s.update_all_confirm, int(m_dialog.jobs.size()), SizeText(size).c_str());
            primary = text;
            break;
        }
        case DialogKind::Verify: {
            if (m_dialog.game < 0 || m_dialog.game >= int(m_games.size())) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            const Game& g = m_games[m_dialog.game];
            char text[160];
            std::snprintf(text, sizeof(text), s.verify_title, g.name.c_str());
            title = text;
            const auto v = m_queue->GetVerify();
            verify_bar = true;
            verify_frac = v.bytes ? float(double(v.bytes_done) / double(v.bytes)) : 0.f;
            verify_running = v.running;
            std::snprintf(text, sizeof(text), s.verify_progress, v.parts_done, v.parts, SizeText(v.bytes_done).c_str(), SizeText(v.bytes).c_str());
            verify_text = text;
            m_dialog.single = true;
            if (v.done) {
                if (R_FAILED(v.rc) && !IsCancelled(v.rc)) {
                    error = InstallErrorText(v.rc, 0, 0, -1, false, false);
                }
                bool bad = false;
                for (const auto& c : v.result) {
                    std::string right;
                    if (c.bad.empty()) {
                        right = s.verify_fine;
                    } else {
                        char b[96];
                        std::snprintf(b, sizeof(b), s.verify_bad, u32(c.bad.size()), c.parts);
                        right = b;
                        bad = true;
                    }
                    lines.push_back({ContentLabel(g, c.key), WhereText(c.nand), right, c.bad.empty() ? t.ok : t.err});
                }
                std::vector<InstallJob> repairs;
                if (bad && RepairJobs(v, repairs)) {
                    u64 bytes = 0;
                    for (const auto& j : repairs) bytes += j.size;
                    std::snprintf(text, sizeof(text), s.repair_confirm, SizeText(bytes).c_str());
                    primary = text;
                    note = s.repair_note;
                    m_dialog.single = false;
                } else if (bad) {
                    error = s.repair_missing;
                } else if (error.empty()) {
                    note = s.verify_all_ok;
                }
            }
            break;
        }
        case DialogKind::Push: {
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.game + " · " + j.label, "", Upper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[160];
            std::snprintf(text, sizeof(text), s.push_title, int(m_dialog.jobs.size()));
            title = text;
            std::snprintf(text, sizeof(text), s.push_from, m_dialog.from.c_str());
            body = text;
            std::snprintf(text, sizeof(text), s.push_install, SizeText(size).c_str());
            primary = text;
            cancel_label = s.not_now;
            break;
        }
        case DialogKind::Risk: {
            title = s.risk_title;
            // the reasons, then what is recommended
            if (m_safety->EmuMmc() != Safety::Answer::Yes) {
                body += std::string("• ") + s.risk_sysmmc + "\n";
            }
            if (m_safety->NintendoBlocked() == Safety::Answer::No) {
                body += std::string("• ") + s.risk_online + "\n";
            }
            note = s.risk_body;
            primary = s.risk_go;
            break;
        }
        case DialogKind::VaultRestore: {
            const More& m = TrM();
            title = m.restore_title;
            const std::string game = m_dialog.game >= 0 && m_dialog.game < int(m_vault_saves.size()) ? m_vault_saves[m_dialog.game].name : "";
            char text[512];
            std::snprintf(text, sizeof(text), m.restore_body, game.c_str(), m_dialog.save.note.empty() ? m.no_note : m_dialog.save.note.c_str(),
                          WhenText(m_dialog.save.when).c_str());
            body = text;
            primary = m.restore_go;
            break;
        }
        case DialogKind::SpaceClean:
        case DialogKind::SpaceArchive:
        case DialogKind::SpaceMove:
        case DialogKind::SpaceCard:
        case DialogKind::SpaceDump:
            SpaceDialogText(title, body, primary);
            break;
        case DialogKind::Preflight:
            title = TrM().pf_title;
            body = TrM().pf_body;
            cancel_label = s.cancel;
            break;
        case DialogKind::DlcNeeds: {
            const More& m = TrM();
            const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
            for (const auto& j : m_dialog.jobs) {
                u64 id = j.title_id;
                u32 v = 0;
                if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
                if (!j.dlc_needs || !id || TitleKind(id) != 2) continue;
                const u64 app = (id - 0x1000) & ~u64(0xFFF);
                u32 have = 0;
                const auto it = installed.find(app);
                if (it != installed.end()) have = it->second.patch;
                for (const auto& o : m_dialog.jobs) {
                    u64 oid = o.title_id;
                    u32 ov = o.version;
                    if (o.origin != InstallJob::Origin::Library) TitleFromName(o.game + " " + o.label, oid, ov);
                    if (oid && TitleKind(oid) == 1 && (oid & ~u64(0xFFF)) == app) have = std::max(have, ov);
                }
                if (j.dlc_needs <= have) continue;
                char right[200];
                std::snprintf(right, sizeof(right), m.dn_line_fmt, VersionText(j.dlc_needs).c_str(), VersionText(have).c_str());
                lines.push_back({j.game + " · " + j.label, "", right, t.orange});
            }
            title = m.dn_title;
            body = m.dn_body;
            primary = m.dn_go;
            break;
        }
        case DialogKind::UpdateStops: {
            const More& m = TrM();
            bool only_in_game = true;
            const auto& checks = m_dialog.checks;
            for (size_t i = 0; i < checks.size(); i++) {
                if (!StopsTheGame(checks[i].over)) continue;
                const auto& j = m_dialog.jobs[i];
                only_in_game &= checks[i].over == UpdateOver::InGame;
                const char* why = checks[i].over == UpdateOver::InGame ? m.us_ingame : checks[i].base.compressed ? m.us_compressed : m.us_optimized;
                lines.push_back({j.game + " · " + j.label, "", why, t.orange});
            }
            title = only_in_game ? m.e_ingame_t : m.e_stops_t;
            body = only_in_game ? std::string(m.e_ingame_w) + " " + m.e_ingame_d : std::string(m.us_body);
            if (m_dialog.single && !only_in_game) body += std::string("\n") + m.us_no_original;
            cancel_label = m.us_leave;
            primary = m.us_go;
            break;
        }
        case DialogKind::Reinstall: {
            const More& m = TrM();
            int count = 0;
            for (const auto& j : m_dialog.jobs) {
                if (j.reinstall_ok) continue;
                count++;
                lines.push_back({j.game + " · " + j.label, "", Upper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[200];
            std::snprintf(text, sizeof(text), count == 1 ? m.ri_title_one : m.ri_title_fmt, count);
            title = text;
            body = m.ri_body;
            primary = m.ri_go;
            break;
        }
        case DialogKind::ConsoleRemove: {
            const More& m = TrM();
            char text[300];
            if (!m_dialog.ids.empty()) {
                // several games, each whole
                std::snprintf(text, sizeof(text), m.ic_rm_many_fmt, int(m_dialog.ids.size()));
                title = text;
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                for (const auto app : m_dialog.ids) {
                    for (const auto& a : v.apps) {
                        if (a.app_id != app) continue;
                        int updates = 0, dlc = 0;
                        for (const auto& c : a.contents) {
                            updates += c.type == NcmContentMetaType_Patch;
                            dlc += c.type == NcmContentMetaType_AddOnContent;
                        }
                        std::string what = a.name;
                        if (updates || dlc) {
                            std::snprintf(text, sizeof(text), m.ic_with_fmt, updates, dlc);
                            what += std::string(" · ") + text;
                        }
                        size += a.Bytes();
                        lines.push_back({what, "", SizeText(a.Bytes()), t.text});
                    }
                }
            } else {
                std::snprintf(text, sizeof(text), m.ic_rm_title_fmt, m_dialog.text.c_str());
                title = text;
            }
            for (const auto& c : m_console_remove) {
                if (!m_dialog.ids.empty()) break;
                size += c.bytes;
                lines.push_back({ContentLabel(c, m_console_app), c.nand ? s.internal_short : "SD", SizeText(c.bytes), t.text});
            }
            std::snprintf(text, sizeof(text), m.ic_rm_body_fmt, SizeText(size).c_str());
            body = text;
            primary = m.ic_rm_go;
            break;
        }
        case DialogKind::ShopRemove: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.sh_remove_title,
                          m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size()) ? m_shops[size_t(m_dialog.entry)].name.c_str() : "");
            title = text;
            body = m.sh_remove_body;
            primary = m.sh_remove;
            break;
        }
        case DialogKind::VaultError:
            title = m_dialog.text.empty() ? TrM().t_saves : m_dialog.text;
            error = m_dialog.from;
            break;
        case DialogKind::CompressIntro:
        case DialogKind::CompressGo:
        case DialogKind::CompressUndo:
        case DialogKind::CompressInfo:
        case DialogKind::CompressStopsOriginal:
        case DialogKind::CompressStopsRemove:
            CompressDialogText(title, body, note, primary);
            break;
        case DialogKind::Versions: {
            const More& m = TrM();
            const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
            const auto rows = VersionRows(g);
            m_dialog.entry = std::clamp(m_dialog.entry, 0, std::max(0, int(rows.size()) - 1));
            title = m.ver_title;
            body = g.name;
            // a window of five around the focus
            const int first = std::clamp(m_dialog.entry - 2, 0, std::max(0, int(rows.size()) - 5));
            for (int r = first; r < int(rows.size()) && r < first + 5; r++) {
                const CatalogFile& f = g.files[rows[r]];
                const bool here = f.version == g.installed.patch;
                Line l{VersionText(f.version), here ? s.installed_tag : "",
                       Upper(f.format) + " \xC2\xB7 " + SizeText(f.size), t.text};
                l.focus = r == m_dialog.entry;
                l.hit = r;
                lines.push_back(l);
            }
            const CatalogFile& chosen = g.files[rows[size_t(m_dialog.entry)]];
            char text[96];
            if (chosen.version == g.installed.patch) {
                primary = m.ver_installed;
                can = false;
            } else if (chosen.version < g.installed.patch) {
                std::snprintf(text, sizeof(text), m.ver_back_fmt, VersionText(chosen.version).c_str());
                primary = text;
                note = m.ver_note;
            } else {
                std::snprintf(text, sizeof(text), m.ver_install_fmt, VersionText(chosen.version).c_str());
                primary = text;
            }
            break;
        }
        case DialogKind::Downgrade: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.dg_title_fmt, m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            title = text;
            std::snprintf(text, sizeof(text), m.dg_body_fmt, VersionText(u32(m_dialog.size)).c_str(),
                          m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            body = text;
            note = m.ver_note;
            primary = m.dg_go;
            break;
        }
        case DialogKind::Resume: {
            const More& m = TrM();
            title = m.rs_title;
            if (m_resume) {
                const auto& f = m_resume->job.fields;
                const auto get = [&f](const char* k) {
                    auto it = f.find(k);
                    return it == f.end() ? std::string() : it->second;
                };
                body = get("game") + (get("label").empty() ? "" : " \xC2\xB7 " + get("label"));
                long long bytes = 0;
                const int parts = ResumeChecked(*m_resume, &bytes);
                char text[300];
                std::snprintf(text, sizeof(text), m.rs_parts_fmt, parts, SizeText(u64(bytes)).c_str());
                body += std::string("\n") + text;
                if (!m_resume->waiting.empty()) {
                    std::snprintf(text, sizeof(text), m.rs_waiting_fmt, int(m_resume->waiting.size()));
                    body += std::string("\n") + text;
                }
            }
            note = m.rs_note;
            primary = m.rs_go;
            cancel_label = m.rs_later;
            break;
        }
        case DialogKind::QueueCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qc_title, m_dialog.text.c_str());
            title = text;
            body = m.qc_body;
            primary = m.qw_cancel_go;
            cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::Quit:
            title = TrM().quit_title;
            body = TrM().quit_body;
            primary = TrM().quit_go;
            cancel_label = TrM().quit_stay;
            break;
        case DialogKind::Forget:
            title = TrM().fg_title;
            body = TrM().fg_body;
            primary = TrM().fg_go;
            break;
        case DialogKind::HomeAdd:
            title = m_home.installed ? TrM().hm_update_title : TrM().hm_add_title;
            body = TrM().hm_add_body;
            primary = m_home.installed ? TrM().hm_update : TrM().hm_make;
            break;
        case DialogKind::HomeRemove:
            title = TrM().hm_remove_title;
            body = TrM().hm_remove_body;
            primary = TrM().hm_remove_go;
            break;
        case DialogKind::ResumeDiscard: {
            const More& m = TrM();
            title = m.rd_title;
            long long bytes = 0;
            const int parts = m_resume ? ResumeChecked(*m_resume, &bytes) : 0;
            char text[300];
            std::snprintf(text, sizeof(text), m.rd_body_fmt, parts, SizeText(u64(bytes)).c_str());
            body = text;
            primary = m.rd_go;
            cancel_label = s.back;
            break;
        }
        case DialogKind::WebCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qw_cancel_title, m_dialog.text.c_str());
            title = text;
            body = m.qw_cancel_body;
            primary = m.qw_cancel_go;
            cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::ModsAsk: {
            const More& m = TrM();
            title = m.mr_title;
            body = m_dialog.text + "\n" + m_dialog.from + "\n" + m.mr_body;
            primary = m.mr_go;
            cancel_label = m.mr_not_now;
            break;
        }
        case DialogKind::SaveCopyAsk: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.sc_ask_title, m_dialog.text.c_str());
            title = text;
            body = m.sc_ask_body;
            error = m_dialog.from;
            primary = m_dialog.entry == 2 ? m.sc_go_remove : m.sc_go_on;
            break;
        }
        case DialogKind::MtpNand: {
            const More& m = TrM();
            title = m.nand_q_title;
            body = m.nand_q_body;
            primary = m.allow_nand;
            break;
        }
        case DialogKind::PreCheck: {
            const More& m = TrM();
            title = m.pc_title;
            if (!m_dialog.jobs.empty()) {
                const auto& j0 = m_dialog.jobs.front();
                body = j0.game + " \xC2\xB7 " + j0.label;
                if (m_dialog.jobs.size() > 1) {
                    char text[256];
                    std::snprintf(text, sizeof(text), m.pc_and_more, body.c_str(), int(m_dialog.jobs.size()) - 1);
                    body = text;
                }
            }
            const auto add = [&](const char* name, const PreCheck::Line& l) {
                const int mark = l.state == PreCheck::State::Fine ? 1 : l.state == PreCheck::State::Warn ? 2 : 0;
                lines.push_back({name, "", l.right, mark == 2 ? t.warn : t.muted, mark});
            };
            add(m.pc_firmware, m_dialog.check.firmware);
            add(m.pc_keys, m_dialog.check.keys);
            add(m.pc_ams, m_dialog.check.atmosphere);
            note = m.pc_note;
            primary = m.pc_go;
            break;
        }
        case DialogKind::Share:
            title = s.share_title;
            body = s.share_body;
            primary = s.share_yes;
            cancel_label = s.no_word;
            break;
        case DialogKind::Where:
            title = s.where_title;
            body = s.where_body;
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.label, "", Upper(j.format) + " · " + SizeText(j.size), t.text});
            }
            primary = s.sd_card;
            break;
        case DialogKind::ClearHistory:
            title = s.clear_title;
            body = s.clear_body;
            primary = s.clear_confirm;
            break;
        case DialogKind::HistoryInfo: {
            if (m_dialog.entry < 0 || m_dialog.entry >= int(m_history.size())) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            const HistoryEntry& e = m_history[m_dialog.entry];
            title = e.game;
            lines.push_back({e.label + " · " + Upper(e.format), "", e.nand ? s.internal : s.sd_card, t.text});
            lines.push_back({s.col_date, "", WhenText(e.when), t.text});
            lines.push_back({s.col_size, "", SizeText(e.size), t.text});
            if (e.written) lines.push_back({s.col_written, "", SizeText(e.written), t.text});
            if (e.seconds > 0) lines.push_back({s.col_time, "", Duration(e.seconds), t.text});
            if (e.result == HistoryEntry::Result::Installed) lines.push_back({s.col_speed, "", SpeedText(e.Speed()), t.text});
            lines.push_back({s.col_result, "", ResultText(e.result), ResultColor(t, e.result)});
            if (e.result == HistoryEntry::Result::Failed) error = e.error;
            break;
        }
        case DialogKind::None: return;
        default: {
            ToolDialog d;
            d.cancel = s.cancel;
            if (!ToolDialogText(d)) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            title = d.title;
            body = d.body;
            note = d.note;
            primary = d.primary;
            error = d.error;
            cancel_label = d.cancel;
            can = d.can;
            m_dialog.single = d.single;
            verify_bar = d.bar;
            verify_running = d.running;
            verify_frac = d.frac;
            verify_text = d.bar_text;
            for (const auto& l : d.lines) lines.push_back({l.text, "", l.right, l.warn ? t.warn : t.text, l.warn ? 2 : 0});
            break;
        }
    }

    // the layout, from the top
    const float w = 740, pad = 32, iw = w - 2 * pad;
    const int shown = std::min<int>(int(lines.size()), m_dialog.kind == DialogKind::HistoryInfo ? 8 : 5);
    const bool more = int(lines.size()) > shown;
    // rows a finger chooses are 48 px high; the others 40
    const float lp = std::any_of(lines.begin(), lines.end(), [](const Line& l) { return l.hit >= 0; }) ? 48.f : 40.f;
    float h = pad + 40;
    const float body_h = body.empty() ? 0 : TextBoxHeight(vg, iw, 17, body.c_str()) + 16;
    const float note_h = note.empty() ? 0 : TextBoxHeight(vg, iw, 15, note.c_str()) + 14;
    const float list_h = lines.empty() ? 0 : shown * lp + (more ? 30 : 0) + 12 + 16;
    const float err_h = error.empty() ? 0 : ErrorCard(vg, t, 0, 0, iw, error, false) + 14;
    h += body_h + list_h + (bar ? 64 : 0) + (verify_bar ? 54 : 0) + err_h + note_h + 60 + pad - 8;
    const float x = (kWidth - w) / 2, y = std::max(80.f, (kHeight - 56 - h) / 2);
    // it grows out of the middle with a soft spring
    const float pop = ui::EaseOutBack(ui::Appear(m_dialog_at, 0.32f));
    nvgSave(vg);
    nvgTranslate(vg, x + w / 2, y + h / 2);
    nvgScale(vg, 0.88f + 0.12f * pop, 0.88f + 0.12f * pop);
    nvgTranslate(vg, -(x + w / 2), -(y + h / 2));
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 10, y + 6, w + 20, h + 20, 22);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, w, h, 18, 40, nvgRGBAf(0, 0, 0, 0.5f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, w, h, 18); // nearly solid: what is behind does not show through
    // a tap on the panel itself does nothing
    AddHit(x, y, w, h, HitKind::Button);

    float cy = y + pad;
    if (ui::TextWidth(vg, 26, title.c_str()) > iw) {
        // cut with an ellipsis, on a whole character
        const std::string dots = "\xE2\x80\xA6";
        while (!title.empty() && ui::TextWidth(vg, 26, (title + dots).c_str()) > iw) {
            size_t cut = title.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) cut--;
            title.erase(cut);
        }
        title += dots;
    }
    nvgSave(vg);
    nvgScissor(vg, x + pad, cy - 4, iw, 44);
    ui::Text(vg, x + pad, cy + 16, 26, t.text, title.c_str());
    nvgRestore(vg);
    cy += 44;
    if (!body.empty()) {
        TextBox(vg, x + pad, cy, iw, 17, t.muted, body.c_str());
        cy += body_h;
    }
    if (!lines.empty()) {
        const float lh = shown * lp + (more ? 30 : 0) + 12;
        ui::FillRound(vg, x + pad, cy, iw, lh, 12, t.panel2);
        ui::StrokeRound(vg, x + pad + .5f, cy + .5f, iw - 1, lh - 1, 12, t.line, 1);
        for (int i = 0; i < shown; i++) {
            const Line& l = lines[i];
            const float ly = cy + 6 + i * lp + lp / 2;
            if (l.focus) {
                ui::FillRound(vg, x + pad + 4, ly - lp / 2 + 2, iw - 8, lp - 4, 8, t.panel);
                ui::StrokeRound(vg, x + pad + 4, ly - lp / 2 + 2, iw - 8, lp - 4, 8, t.work, 2.5f);
            }
            if (l.hit >= 0) AddHit(x + pad, ly - lp / 2, iw, lp, HitKind::DialogLine, l.hit);
            if (i > 0) {
                nvgBeginPath(vg);
                nvgRect(vg, x + pad + 14, ly - lp / 2, iw - 28, 1);
                nvgFillColor(vg, t.line);
                nvgFill(vg);
            }
            const float rw = ui::TextWidth(vg, 15, l.right.c_str());
            float tx = x + pad + 16;
            if (l.mark) {
                // a filled tick, or a warning triangle in amber
                const float mx = tx + 10;
                if (l.mark == 1) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, mx, ly, 10);
                    nvgFillColor(vg, t.ok);
                    nvgFill(vg);
                    ui::CheckMark(vg, mx, ly, 10, t.panel, 2.2f);
                } else {
                    nvgBeginPath(vg);
                    nvgMoveTo(vg, mx, ly - 10);
                    nvgLineTo(vg, mx + 11, ly + 9);
                    nvgLineTo(vg, mx - 11, ly + 9);
                    nvgClosePath(vg);
                    nvgFillColor(vg, t.warn);
                    nvgFill(vg);
                    ui::Text(vg, mx, ly + 2, 13, t.panel, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                }
                tx += 30;
            }
            nvgSave(vg);
            nvgScissor(vg, tx, ly - lp / 2, iw - 32 - rw - 140, lp);
            tx = ui::Text(vg, tx, ly, 17, m_dialog.kind == DialogKind::HistoryInfo ? t.muted : t.text, l.text.c_str());
            const bool colored = m_dialog.kind == DialogKind::HistoryInfo || m_dialog.kind == DialogKind::Verify ||
                                 m_dialog.kind == DialogKind::PreCheck;
            nvgRestore(vg);
            if (!l.tag.empty()) {
                const float tw = ui::TextWidth(vg, 13, l.tag.c_str()) + 16;
                const float tagx = std::min(tx + 12, x + pad + iw - 16 - rw - 16 - tw);
                ui::StrokeRound(vg, tagx, ly - 11, tw, 22, 6, t.line, 1);
                ui::Text(vg, tagx + tw / 2, ly, 13, t.muted, l.tag.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
            ui::Text(vg, x + pad + iw - 16, ly, 15, colored ? l.color : t.muted, l.right.c_str(),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        if (more) {
            char text[48];
            std::snprintf(text, sizeof(text), s.more_count, int(lines.size()) - shown);
            ui::Text(vg, x + pad + 16, cy + 6 + shown * lp + 14, 15, t.muted, text);
        }
        cy += lh + 16;
    }
    if (bar) {
        // the internal memory: used, this install, free after
        const float bw = iw, bh = 18;
        ui::FillRound(vg, x + pad, cy, bw, bh, 9, t.panel2);
        if (m_nand_total_bytes > 0 && m_nand_free_bytes >= 0) {
            const double total = double(m_nand_total_bytes);
            const double used = std::max(0.0, total - double(m_nand_free_bytes));
            const float uw = float(used / total) * bw;
            const float tw = std::min(bw - uw, float(double(size) / total) * bw);
            nvgSave(vg);
            nvgIntersectScissor(vg, x + pad, cy, bw, bh);
            NVGcolor used_c = t.muted;
            used_c.a = 0.45f;
            ui::FillRound(vg, x + pad, cy, uw + 9, bh, 9, used_c);
            nvgBeginPath(vg);
            nvgRect(vg, x + pad + uw, cy, tw, bh);
            nvgFillColor(vg, can ? t.work : t.err);
            nvgFill(vg);
            nvgRestore(vg);
            ui::StrokeRound(vg, x + pad + .5f, cy + .5f, bw - 1, bh - 1, 9, t.line, 1);
            char a[64], b[64], c[96];
            std::snprintf(a, sizeof(a), s.used_fmt, SizeText(u64(used)).c_str());
            std::snprintf(b, sizeof(b), s.will_take_fmt, SizeText(size).c_str());
            const u64 after = u64(m_nand_free_bytes) > size ? u64(m_nand_free_bytes) - size : 0;
            std::snprintf(c, sizeof(c), s.free_after_fmt, SizeText(after).c_str(), SizeText(u64(m_nand_total_bytes)).c_str());
            float lx = x + pad;
            const float ly = cy + bh + 20;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, used_c);
            lx = ui::Text(vg, lx + 18, ly, 15, t.muted, a) + 22;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, can ? t.work : t.err);
            lx = ui::Text(vg, lx + 18, ly, 15, t.muted, b) + 22;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, t.panel2);
            ui::StrokeRound(vg, lx + .5f, ly - 5.5f, 11, 11, 3, t.line, 1);
            ui::Text(vg, lx + 18, ly, 15, t.muted, c);
        } else {
            ui::Text(vg, x + pad, cy + bh / 2, 15, t.muted, TrM().pc_unknown);
        }
        cy += 64;
    }
    if (verify_bar) {
        ui::Progress(vg, t, x + pad, cy + 4, iw, 10, verify_running ? verify_frac : 1.f, t.work, m_time, verify_running);
        ui::Text(vg, x + pad, cy + 34, 15, t.muted, verify_text.c_str());
        cy += 54;
    }
    if (!error.empty()) {
        ErrorCard(vg, t, x + pad, cy, iw, error, true);
        cy += err_h;
    }
    if (!note.empty()) {
        TextBox(vg, x + pad, cy, iw, 15, t.muted, note.c_str());
        cy += note_h;
    }

    // the buttons: Cancel (or Close) first in focus, the action on the right
    const bool info = m_dialog.single;
    if ((!can || info) && m_dialog.focus == 1) m_dialog.focus = 0;
    const float bh = 52, by = y + h - pad - bh + 8;
    float bx = x + w - pad;
    struct Button { const char* glyph; std::string label; bool primary; int index; u64 mask; };
    std::vector<Button> buttons;
    buttons.push_back({ui::glyph::B, info ? s.close : cancel_label, false, 0, HidNpadButton_B});
    u64 extra_mask = 0;
    const int extra = info ? -1 : DialogExtra(extra_mask);
    if (extra >= 0) {
        // a third choice, between the two: the internal memory (Where), the SD card instead (Nand),
        // leaving some out (DlcNeeds, Reinstall), discarding (Resume)
        const More& m = TrM();
        const char* label = m_dialog.kind == DialogKind::ContentDetails ? TrT().cd_ticket_go
                            : m_dialog.kind == DialogKind::Where     ? s.internal
                            : m_dialog.kind == DialogKind::Nand      ? m.nand_sd_button
                            : m_dialog.kind == DialogKind::DlcNeeds  ? m.dn_skip
                            : m_dialog.kind == DialogKind::Reinstall ? m.ri_skip
                                                                     : m.rs_discard;
        buttons.push_back({extra_mask == HidNpadButton_Y ? ui::glyph::Y : ui::glyph::X, label, false, extra, extra_mask});
    }
    if (!info) buttons.push_back({ui::glyph::A, primary, true, 1, HidNpadButton_A});
    // three long labels that do not fit get a smaller size
    float all = 14 * float(buttons.size() - 1);
    for (const auto& b : buttons) all += ui::TextWidth(vg, 18, b.label.c_str()) + 78;
    const bool small = all > iw;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const float bw = small ? ui::TextWidth(vg, 15, it->label.c_str()) + 62 : ui::TextWidth(vg, 18, it->label.c_str()) + 78;
        bx -= bw;
        const bool enabled = !it->primary || can;
        nvgGlobalAlpha(vg, (enabled ? 1.f : 0.4f) * std::min(1.f, pop * 1.4f));
        if (m_dialog.focus == it->index) {
            ui::Focus(vg, t, bx, by, bw, bh, 12, m_time);
        }
        if (it->primary) {
            ui::FillRound(vg, bx, by, bw, bh, 12, t.work);
        } else {
            ui::FillRound(vg, bx, by, bw, bh, 12, t.panel2);
            ui::StrokeRound(vg, bx + 1, by + 1, bw - 2, bh - 2, 12, t.line, 2);
        }
        const NVGcolor fg = it->primary ? t.on_work : t.text;
        ui::Symbol(vg, t, bx + (small ? 27 : 34), by + bh / 2, small ? 21 : 24, it->glyph, fg, it->mask);
        ui::Text(vg, bx + (small ? 45 : 56), by + bh / 2, small ? 15 : 18, fg, it->label.c_str());
        nvgGlobalAlpha(vg, 1.f);
        if (enabled) {
            AddHit(bx, by, bw, bh, HitKind::DialogButton, it->index);
        }
        bx -= 14;
    }

    nvgGlobalAlpha(vg, 1.f);
    nvgRestore(vg);
    // its own hint bar, over the one of the screen
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.choose, HidNpadButton_A}, {ui::glyph::B, info ? s.close : cancel_label.c_str(), HidNpadButton_B}}, rects);
    AddHints(rects);
}

void App::UpdateDialog(u64 down, u64 held) {
    if (ToolDialogInput(down, held)) return;
    const bool info = m_dialog.single;
    if (m_dialog.kind == DialogKind::Verify && (down & (HidNpadButton_B | HidNpadButton_A)) && m_queue->GetVerify().running) {
        // stops the check; the question closes
        m_queue->CancelVerify();
        m_dialog.kind = DialogKind::None;
        return;
    }
    if (m_dialog.kind == DialogKind::Versions) {
        const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
        const int n = int(VersionRows(g).size());
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_dialog.entry > 0) {
            m_dialog.entry--;
            m_dialog.focus = 1;
        }
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_dialog.entry < n - 1) {
            m_dialog.entry++;
            m_dialog.focus = 1;
        }
    }
    u64 extra_mask = 0;
    const int extra = info ? -1 : DialogExtra(extra_mask);
    if (extra >= 0) {
        // Cancel, the third choice, the action, from left to right
        const int order[] = {0, extra, 1};
        int pos = m_dialog.focus == extra ? 1 : m_dialog.focus == 1 ? 2 : 0;
        if (Repeat(down, held, HidNpadButton_AnyLeft) && pos > 0) pos--;
        if (Repeat(down, held, HidNpadButton_AnyRight) && pos < 2) pos++;
        m_dialog.focus = order[pos];
        if ((down & extra_mask) || ((down & HidNpadButton_A) && m_dialog.focus == extra)) {
            DialogExtraChosen();
            return;
        }
    } else if (!info) {
        if (Repeat(down, held, HidNpadButton_AnyLeft)) m_dialog.focus = 0;
        if (Repeat(down, held, HidNpadButton_AnyRight)) m_dialog.focus = 1; // a disabled action falls back when drawn
    }
    if (m_dialog.kind == DialogKind::Preflight && (down & HidNpadButton_B)) {
        // not waited for: what it reads is dropped
        m_preflight_dropped = true;
        m_dialog = Dialog{};
        return;
    }
    const bool declined = (down & HidNpadButton_B) || ((down & HidNpadButton_A) && m_dialog.focus == 0);
    if (declined) {
        if (m_dialog.kind == DialogKind::Resume) {
            // decided later: the journal stays, and it is asked again the next time NXDock opens
            m_resume.reset();
        } else if (m_dialog.kind == DialogKind::ResumeDiscard) {
            // back to the question of going on
            m_dialog.kind = DialogKind::Resume;
            m_dialog.focus = 1;
            return;
        } else if (m_dialog.kind == DialogKind::SaveCopyAsk || m_dialog.kind == DialogKind::ModsAsk) {
            m_queue->Answer(false);
        } else if (m_dialog.kind == DialogKind::Push) {
            for (const auto& id : m_dialog.push) m_push->Answer(id, false);
        } else if (m_dialog.kind == DialogKind::Share) {
            m_prefs.share_installed = 0;
            m_prefs.Save();
        } else if (m_dialog.kind == DialogKind::UpdateStops) {
            // those updates stay out; the rest goes on
            const auto checks = m_dialog.checks;
            std::vector<InstallJob> rest;
            for (size_t i = 0; i < checks.size(); i++) {
                if (!StopsTheGame(checks[i].over)) rest.push_back(std::move(m_dialog.jobs[i]));
            }
            m_dialog = Dialog{};
            if (!rest.empty()) AddJobs(std::move(rest));
            return;
        }
        m_dialog.kind = DialogKind::None;
        return;
    }
    if (down & HidNpadButton_A) {
        const DialogKind before = m_dialog.kind;
        if (m_dialog.focus == 1 && !info) {
            ConfirmDialog();
        }
        if (m_dialog.kind == before) {
            m_dialog.kind = DialogKind::None; // unless the choice led to another question
        }
    }
}

int App::DialogExtra(u64& mask) const {
    switch (m_dialog.kind) {
        case DialogKind::Where: mask = HidNpadButton_X; return 2;
        case DialogKind::Nand: mask = HidNpadButton_Y; return 3;
        case DialogKind::DlcNeeds:
        case DialogKind::Reinstall:
        case DialogKind::Resume: mask = HidNpadButton_X; return 2;
        case DialogKind::ContentDetails: mask = HidNpadButton_X; return 2;
        default: mask = 0; return -1;
    }
}

void App::DialogExtraChosen() {
    if (m_dialog.kind == DialogKind::ContentDetails) {
        ConsoleTicketBack();
        return;
    }
    if (m_dialog.kind == DialogKind::Resume) {
        // discarding is asked once more, Back in focus
        m_dialog.kind = DialogKind::ResumeDiscard;
        m_dialog.focus = 0;
        return;
    }
    if (m_dialog.kind == DialogKind::Where) {
        auto jobs = std::move(m_dialog.jobs);
        for (auto& j : jobs) j.nand = true;
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand; // never to the internal memory without its question
        m_dialog.jobs = std::move(jobs);
        return;
    }
    if (m_dialog.kind == DialogKind::Nand) {
        // to the SD card instead, the default
        auto jobs = std::move(m_dialog.jobs);
        for (auto& j : jobs) j.nand = false;
        m_dialog = Dialog{};
        AddJobs(std::move(jobs));
        return;
    }
    if (m_dialog.kind == DialogKind::DlcNeeds) {
        // those DLC stay out; the rest goes on
        std::vector<InstallJob> rest;
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        for (auto& j : m_dialog.jobs) {
            u64 id = j.title_id;
            u32 v = 0;
            if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
            bool behind = false;
            if (j.dlc_needs && id && TitleKind(id) == 2) {
                const u64 app = (id - 0x1000) & ~u64(0xFFF);
                u32 have = 0;
                const auto it = installed.find(app);
                if (it != installed.end()) have = it->second.patch;
                for (const auto& o : m_dialog.jobs) {
                    u64 oid = o.title_id;
                    u32 ov = o.version;
                    if (o.origin != InstallJob::Origin::Library) TitleFromName(o.game + " " + o.label, oid, ov);
                    if (oid && TitleKind(oid) == 1 && (oid & ~u64(0xFFF)) == app) have = std::max(have, ov);
                }
                behind = j.dlc_needs > have;
            }
            if (!behind) {
                j.dlc_ok = true;
                rest.push_back(std::move(j));
            }
        }
        m_dialog = Dialog{};
        if (!rest.empty()) AddJobs(std::move(rest));
        return;
    }
    if (m_dialog.kind == DialogKind::Reinstall) {
        // skipped: those installed stay out, the rest goes on
        std::vector<InstallJob> rest;
        for (auto& j : m_dialog.jobs) {
            if (j.reinstall_ok) rest.push_back(std::move(j));
        }
        m_dialog = Dialog{};
        if (!rest.empty()) AddJobs(std::move(rest));
        return;
    }
}

void App::ConfirmDialog() {
    switch (m_dialog.kind) {
        case DialogKind::Nand: {
            // checked again at the moment of the choice: nothing goes there when it does not fit
            u64 size = 0;
            for (const auto& j : m_dialog.jobs) size += j.size;
            if (m_nand_free_bytes >= 0 && size + kNandMargin > u64(m_nand_free_bytes)) {
                return;
            }
            AddJobs(std::move(m_dialog.jobs));
            break;
        }
        case DialogKind::UpdateAll:
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::SaveCopyAsk:
            m_queue->Answer(true);
            break;
        case DialogKind::Versions: {
            const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
            const auto rows = VersionRows(g);
            if (rows.empty()) break;
            const CatalogFile& f = g.files[rows[size_t(std::clamp(m_dialog.entry, 0, int(rows.size()) - 1))]];
            if (f.version == g.installed.patch) break;
            InstallJob job = JobFor(g, f);
            job.nand = g.installed.base_nand; // where the game is, as its update goes there too
            if (f.version > g.installed.patch) {
                AddJobs({job}); // a newer one: a plain update
                break;
            }
            // an older one: asked once more, with what it means for the save
            job.downgrade = true;
            const u32 now = g.installed.patch;
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Downgrade;
            m_dialog.jobs = {job};
            m_dialog.size = now;
            return;
        }
        case DialogKind::Downgrade:
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::WebCancel:
        case DialogKind::QueueCancel:
            m_queue->Remove(m_dialog.entry); // a waiting one goes; the one installing is cancelled
            break;
        case DialogKind::Quit:
            m_quit = true;
            break;
        case DialogKind::Forget:
            m_speed.Cancel();
            m_link.Forget();
            m_tab = Tab::Library;
            break;
        case DialogKind::HomeAdd:
        case DialogKind::HomeRemove:
            DoHomeShortcut(m_dialog.kind == DialogKind::HomeRemove);
            break;
        case DialogKind::CompressIntro:
        case DialogKind::CompressGo:
        case DialogKind::CompressUndo:
        case DialogKind::CompressStopsOriginal:
        case DialogKind::CompressStopsRemove:
            ConfirmCompressDialog();
            break;
        case DialogKind::Resume:
            if (m_resume) AddJobs(ResumeJobs(*m_resume, m_link.CurrentConfig().key));
            m_resume.reset();
            break;
        case DialogKind::ResumeDiscard:
            // every placeholder of the journal goes, and the journal
            if (m_resume) DiscardResume(*m_resume);
            m_resume.reset();
            NoticeOk(TrM().rs_discarded);
            break;
        case DialogKind::ModsAsk:
            // the update goes on; when it ends, Cheats and mods opens for the new version
            m_mods_after = m_dialog.ids.empty() ? 0 : m_dialog.ids[0];
            m_mods_after_name = m_dialog.text;
            m_queue->Answer(true);
            break;
        case DialogKind::Where:
            for (auto& j : m_dialog.jobs) j.nand = false;
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Push:
            for (const auto& id : m_dialog.push) m_push->Answer(id, true);
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Risk:
            m_risk_ok = true;
            AddJobs(std::move(m_dialog.jobs));
            if (m_after_risk != MoreScreen::Tiles) {
                const MoreScreen screen = m_after_risk;
                m_after_risk = MoreScreen::Tiles;
                if (m_dialog.kind == DialogKind::Risk) m_dialog.kind = DialogKind::None;
                OpenMoreScreen(screen);
            }
            break;
        case DialogKind::MtpNand:
            MtpAllowNand(true);
            break;
        case DialogKind::SpaceClean:
        case DialogKind::SpaceArchive:
        case DialogKind::SpaceMove:
        case DialogKind::SpaceCard:
        case DialogKind::SpaceDump:
            ConfirmSpaceDialog();
            break;
        case DialogKind::UpdateStops: {
            // the original games first (the queue puts them before their updates); an update
            // whose game has no original in the library, or is in the game already, stays out
            auto originals = OriginalsFor(m_dialog.jobs);
            std::set<u64> have;
            for (const auto& o : originals) have.insert(o.title_id);
            const auto checks = UpdatesThatStop(m_dialog.jobs);
            std::vector<InstallJob> jobs = std::move(originals);
            for (size_t i = 0; i < checks.size(); i++) {
                if (StopsTheGame(checks[i].over) && !(OriginalHelps(checks[i].over) && have.count(checks[i].app))) continue;
                jobs.push_back(std::move(m_dialog.jobs[i]));
            }
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::DlcNeeds: {
            auto jobs = std::move(m_dialog.jobs);
            for (auto& j : jobs) j.dlc_ok = true;
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::Reinstall: {
            auto jobs = std::move(m_dialog.jobs);
            for (auto& j : jobs) j.reinstall_ok = true;
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::ConsoleRemove:
            // taking contents off while the queue installs could take those of the install going on
            if (m_queue && (m_queue->Busy() || m_queue->Undoing())) {
                Notice(TrM().cz_busy);
            } else if (!m_dialog.ids.empty()) {
                // several games, whole: what the console has of each
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                std::vector<std::pair<unsigned long long, std::vector<InstalledApp::Content>>> games;
                u64 bytes = 0;
                m_console_done.clear();
                for (const auto app : m_dialog.ids) {
                    for (const auto& a : v.apps) {
                        if (a.app_id != app || a.contents.empty()) continue;
                        games.push_back({app, a.contents});
                        bytes += a.Bytes();
                        HistoryEntry e;
                        e.when = std::time(nullptr);
                        e.game = a.name;
                        e.label = TrM().ic_whole_game;
                        e.size = a.Bytes();
                        e.nand = a.nand_bytes > 0 && a.sd_bytes == 0;
                        e.result = HistoryEntry::Result::Removed;
                        m_console_done.push_back(e);
                    }
                }
                char text[200];
                std::snprintf(text, sizeof(text), TrM().ic_done_many_fmt, int(games.size()), SizeText(bytes).c_str());
                m_console_done_text = text;
                if (m_space && m_space->StartRemove(games)) {
                    m_console_removing.clear();
                    for (const auto& g : games) m_console_removing.insert(g.first);
                    m_console_picked.clear();
                } else {
                    m_console_done.clear();
                    Notice(TrM().cz_busy);
                }
            } else if (m_space && m_space->StartRemove(m_console_app, m_console_remove)) {
                // one game, or a part of it
                u64 bytes = 0;
                bool base = false;
                m_console_done.clear();
                for (const auto& c : m_console_remove) {
                    bytes += c.bytes;
                    base = base || c.type == NcmContentMetaType_Application;
                    HistoryEntry e;
                    e.when = std::time(nullptr);
                    e.game = m_dialog.text;
                    e.label = ContentLabel(c, m_console_app);
                    e.size = c.bytes;
                    e.nand = c.nand;
                    e.result = HistoryEntry::Result::Removed;
                    m_console_done.push_back(e);
                }
                char text[300];
                std::snprintf(text, sizeof(text), TrM().ic_done_one_fmt, m_dialog.text.c_str(), SizeText(bytes).c_str());
                m_console_done_text = text;
                m_console_removing.clear();
                if (base) m_console_removing.insert(m_console_app);
                m_console_marks.clear();
            } else {
                Notice(TrM().cz_busy);
            }
            break;
        case DialogKind::ShopRemove:
            if (m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size())) {
                m_shops.erase(m_shops.begin() + m_dialog.entry);
                SaveShops(ShopsPath(), m_shops);
            }
            break;
        case DialogKind::VaultRestore: {
            const Config c = m_link.CurrentConfig();
            if (!m_vault || m_dialog.game < 0 || m_dialog.game >= int(m_vault_saves.size())) break;
            const Vault::User user = m_vault_users.empty() ? Vault::User{} : m_vault_users[m_vault_user];
            if (!m_dialog.text.empty()) {
                // a copy of the SD card (9A)
                m_vault->StartRestoreSd(user, m_vault_kind, m_vault_saves[m_dialog.game].app_id, m_dialog.text, TrM().before_restore);
            } else if (c.Linked()) {
                m_vault->StartRestore(c.url, c.key, user, m_vault_saves[m_dialog.game].app_id, m_dialog.save, TrM().before_restore, m_vault_kind);
            }
            break;
        }
        case DialogKind::PreCheck:
            // the user goes on: what needs a newer firmware is installed all the same
            for (auto& j : m_dialog.jobs) j.firmware_ok = true;
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Share:
            m_prefs.share_installed = 1;
            m_prefs.Save();
            m_inventory_sent = ~0u;
            break;
        case DialogKind::Verify: {
            std::vector<InstallJob> repairs;
            if (RepairJobs(m_queue->GetVerify(), repairs)) {
                AddJobs(std::move(repairs));
            }
            break;
        }
        case DialogKind::Undo:
            if (!m_queue->StartUndo(m_dialog.undo)) {
                Notice(Tr().undo_wait_queue);
            }
            break;
        case DialogKind::ClearHistory:
            History(HistoryPath()).Clear();
            m_history.clear();
            m_history_version = ~0u;
            m_installed_focus = 0;
            break;
        default: ConfirmToolDialog(); break;
    }
}

void App::DrawSaver(NVGcontext* vg) {
    if (!m_saver_on) {
        return;
    }
    // the screen goes dark; only a thin line of the install and how to light it again remain
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.88f));
    nvgFill(vg);
    const auto live = m_queue->GetLive();
    NVGcolor dim = m_theme.work;
    dim.a = 0.6f;
    if (live.running) {
        ui::Progress(vg, m_theme, kWidth / 2 - 200, 352, 400, 6, JobFraction(live), dim, m_time);
    }
    ui::Text(vg, kWidth / 2, 384, 16, nvgRGBAf(1, 1, 1, 0.45f), Tr().saver_text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

} // namespace nxdock
