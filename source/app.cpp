// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "app.hpp"

#include "errors.hpp"
#include "mtp.hpp"
#include "usb_disks.hpp"
#include "usb_tinfoil.hpp"
#include "i18n_more.hpp"
#include "mods.hpp"
#include "resume.hpp"
#include "http.hpp"
#include <yyjson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <ctime>
#include <string>
#include "i18n.hpp"
#include "net.hpp"
#include "uninstall.hpp"

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

// Library grid layout.
constexpr int kColumns = 7;
constexpr float kGridX = 36, kGridTop = 150, kGridGap = 20;
constexpr float kCoverSize = (kWidth - 2 * kGridX - kGridGap * (kColumns - 1)) / kColumns;
constexpr float kRowPitch = kCoverSize + kGridGap;
constexpr float kGridViewH = 2 * kRowPitch - kGridGap; // two full rows in sight
constexpr float kGridMargin = 10;                      // room for the focus glow, less than the gap
// The A-Z bar on the right edge: as tall as the grid, 48 px wide for a finger.
constexpr float kAzTop = kGridTop - 6, kAzHeight = kGridViewH + 12;
constexpr float kAzX = 1251, kAzW = 26, kAzTouchX = kWidth - 48;

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
    ui::FillRound(vg, x, y, w, h, r, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, t.line, 1);
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

// Settings: theme, library, forget it, address and key by hand (advanced), speed test.
enum SettingRow { kRowTheme, kRowSounds, kRowLibrary, kRowForget, kRowShare, kRowWizard, kRowScreenOff, kRowAnim, kRowSaveCopy, kRowNotifyDone, kRowAddress, kRowKey, kRowSpeed, kSettingRows };

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

std::string Clip(std::string text, size_t max) {
    if (text.size() > max) {
        text = text.substr(0, max - 1) + "…";
    }
    return text;
}

} // namespace

bool App::Init() {
    InitStrings();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
    net::Init();
    const Config config = Config::Load();
    m_theme = config.light ? LightTheme() : DarkTheme();
    if (!m_gfx.Init()) {
        return false;
    }
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
    m_psm = R_SUCCEEDED(psmInitialize());
    m_sound.Start();
    InitRumble();
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
            case 0: std::snprintf(note, sizeof(note), m.sc_note_update, ("v" + std::to_string(to)).c_str()); break;
            case 1: std::snprintf(note, sizeof(note), "%s", m.sc_note_repair); break;
            case 3: std::snprintf(note, sizeof(note), m.sc_note_back, ("v" + std::to_string(to)).c_str()); break;
            default: std::snprintf(note, sizeof(note), "%s", m.sc_note_remove); break;
        }
        return CopySavesBefore(c.url, c.key, app, current, note, progress);
    });
    m_push = std::make_unique<Push>();
    m_safety = std::make_unique<Safety>();
    m_sysinfo = ReadSystemInfo();
    m_time = Now();
    m_last_input = m_time;
    if (!m_ui.wizard_done) {
        OpenWizard(); // only the first time; repeated from Settings
    }
    return true;
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
    if (m_lbl) lblExit();
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

void App::ActivateSetting(int row) {
    const auto& s = Tr();
    const Config current = m_link.CurrentConfig();
    switch (row) {
        case kRowTheme:
            m_theme = m_theme.dark ? LightTheme() : DarkTheme();
            m_link.SaveTheme(!m_theme.dark);
            break;
        case kRowSounds:
            m_ui.sounds = !m_ui.sounds;
            m_ui.Save();
            break;
        case kRowWizard:
            OpenWizard();
            break;
        case kRowScreenOff:
            m_ui.screen_off_auto = !m_ui.screen_off_auto;
            m_ui.Save();
            break;
        case kRowSaveCopy:
            m_ui.save_copy = !m_ui.save_copy;
            m_ui.Save();
            break;
        case kRowNotifyDone:
            m_ui.notify_done = !m_ui.notify_done;
            m_ui.Save();
            break;
        case kRowAnim:
            m_ui.anim = (m_ui.anim + 1) % 3;
            ui::SetMotion(m_ui.anim);
            m_ui.Save();
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
                m_speed.Cancel();
                m_link.Forget();
                m_tab = Tab::Library;
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
    if (down & HidNpadButton_Minus) {
        AskScreenOff();
        return;
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
    const int rows = (int(m_view.size()) + kColumns - 1) / kColumns;
    return std::max(0.f, rows * kRowPitch - kGridGap - kGridViewH);
}

void App::KeepFocusVisible() {
    const float row_top = (m_focus / kColumns) * kRowPitch;
    if (row_top < m_scroll_target) {
        m_scroll_target = row_top;
    } else if (row_top + kCoverSize > m_scroll_target + kGridViewH) {
        m_scroll_target = row_top + kCoverSize - kGridViewH;
    }
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
}

void App::ReadTouch(u64& down) {
    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
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
            m_touch_x0 = x;
            m_touch_y0 = y;
            m_drag_scroll0 = m_scroll;
            // the A-Z bar on the right edge: tapped or dragged along, it jumps to the letter
            m_az_touch = AzBarShown() && m_dialog.kind == DialogKind::None && !m_menu && x >= kAzTouchX &&
                         y >= kAzTop - 8 && y < kAzTop + kAzHeight + 8;
        }
        m_touch_x = x;
        m_touch_y = y;
        if (m_az_touch) {
            const int slot = std::clamp(int((y - kAzTop) / (kAzHeight / 27)), 0, 26);
            JumpToLetter(slot, 0);
            return;
        }
        const bool in_grid = m_tab == Tab::Library && m_touch_y0 >= kGridTop - 10 && m_touch_y0 < kGridTop + kGridViewH;
        if (!m_dragging && std::fabs(y - m_touch_y0) > kDragSlop && in_grid) {
            m_dragging = true;
        }
        if (m_dragging) {
            // The list follows the finger.
            m_scroll = m_scroll_target = std::clamp(m_drag_scroll0 - (y - m_touch_y0), 0.f, MaxScroll());
        }
    } else if (m_touching && m_az_touch) {
        m_touching = m_az_touch = false;
    } else if (m_touching) {
        m_touching = false;
        const bool moved = std::fabs(m_touch_x - m_touch_x0) > kDragSlop || std::fabs(m_touch_y - m_touch_y0) > kDragSlop;
        if (!m_dragging && !moved) {
            Tap(m_touch_x, m_touch_y, down);
        }
        m_dragging = false;
    }
}

void App::Tap(float x, float y, u64& down) {
    // Last drawn on top wins: walk backwards.
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->rect.Contains(x, y)) {
            continue;
        }
        switch (it->kind) {
            case HitKind::Button: down |= it->rect.button; break;
            case HitKind::Tab: m_tab = Tab(it->index); break;
            case HitKind::Chip:
                m_filter = it->index;
                RebuildView();
                break;
            case HitKind::Cover:
                // First tap marks it, a second tap opens it (as A does).
                if (m_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_focus = it->index;
                    KeepFocusVisible();
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
            case HitKind::PairRow:
                if (m_pair_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_pair_focus = it->index;
                }
                break;
            case HitKind::Remember: down |= HidNpadButton_Y; break;
            case HitKind::DetailRow:
            case HitKind::Install:
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
                }
                break;
            case HitKind::InstalledRow:
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
        }
        return;
    }
}

void App::Update(u64 down, u64 held) {
    if (m_dialog.kind != DialogKind::None) {
        UpdateDialog(down, held);
        return;
    }
    if (down & HidNpadButton_Plus) {
        // leaving stops an install (nothing is left half done): the second + leaves
        if (m_queue && m_queue->Busy() && m_time - m_quit_asked > 4) {
            m_quit_asked = m_time;
            Notice(Tr().leave_while_installing);
            return;
        }
        m_quit = true;
        return;
    }
    if (m_wizard) {
        UpdateWizard(down, held);
        return;
    }
    if (m_detail && m_tab == Tab::Library) {
        if (m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
            m_detail = false;
        } else {
            UpdateDetail(down, held);
            return;
        }
    }
    if (m_menu) {
        if (m_tab == Tab::Library) {
            UpdateMenu(down, held);
            return;
        }
        m_menu = false;
    }
    // inside a tool of the More tab the shoulders are its own; B leaves the tool first
    const bool in_tool = m_tab == Tab::More && m_more != MoreScreen::Tiles;
    if ((down & HidNpadButton_L) && !in_tool) {
        m_tab = Tab((int(m_tab) + kTabs - 1) % kTabs);
    }
    if ((down & HidNpadButton_R) && !in_tool) {
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
    const double now = Now();
    const float dt = float(std::min(0.1, now - m_time));
    m_time = now;

    padUpdate(&m_pad);
    u64 down = padGetButtonsDown(&m_pad);
    const u64 held = padGetButtons(&m_pad);
    // the clock of the animations, and the buttons whose symbols pulse
    ui::Tick(m_time);
    ui::NotePress(down);
    if (m_dialog.kind != m_dialog_drawn) {
        m_dialog_drawn = m_dialog.kind;
        m_dialog_at = m_time;
    }
    HidTouchScreenState touch{};
    hidGetTouchScreenStates(&touch, 1);
    const bool new_touch = touch.count > 0 && !m_touching && !m_wake_touch;
    if (down || held || touch.count > 0) {
        m_last_input = m_time;
    }
    if (FollowScreenOff(down, new_touch)) {
        // the input that lit the screen, or stopped the countdown, does nothing else
    } else if ((m_saver_on || TourActive()) && (down || new_touch)) {
        // the input that lights the screen again, or ends the walk, does nothing else
        m_saver_on = false;
        if (TourActive()) EndTour();
        m_wake_touch = new_touch;
        down = 0;
    }
    const u64 focus_before = FocusSignature();
    ReadTouch(down);
    Update(down, held);
    PlaySounds(down, focus_before);
    FollowTour();
    // saver mode: dark after a minute without input while something installs
    if (m_queue && !m_queue->Busy()) {
        m_saver_on = false;
    } else if (m_ui.saver && !m_wizard && m_time - m_last_input > 60) {
        m_saver_on = true;
    }
    FollowLink();
    FollowQueue();
    FollowFinish();
    FollowUsb();
    FollowPush();
    FollowQueueAsk();
    FollowQueueWeb();
    CheckSdSpace();
    CheckBattery();

    // Ease the grid toward where it should be (frame-rate independent).
    if (!m_dragging) {
        m_scroll += (m_scroll_target - m_scroll) * (1.f - std::exp(-dt * 14.f));
        if (std::fabs(m_scroll_target - m_scroll) < 0.5f) m_scroll = m_scroll_target;
    }

    m_gfx.CheckResize();
    NVGcontext* vg = m_gfx.BeginFrame();
    m_covers.Upload(vg);
    Draw(vg);
    m_gfx.EndFrame();
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
    Background(vg, m_theme);
    // the slow lights behind, paused while installing on little battery
    if (!(m_queue && m_queue->Busy() && m_battery >= 0 && m_battery < 20 && !m_charging)) ui::Aurora(vg, m_theme);
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
    ui::TopBar(vg, m_theme, int(m_tab), queued, tabs, m_sd_free.c_str(), m_battery, m_charging);
    if (m_shop_active && m_tab == Tab::Library) {
        // which shop the library shows
        char badge[160];
        std::snprintf(badge, sizeof(badge), TrM().sh_badge, m_shop_name.c_str());
        const float bw = std::min(260.f, ui::TextWidth(vg, 14, badge) + 28);
        const float bx = tabs[kTabs - 1].x + tabs[kTabs - 1].w + 30;
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
        // the content comes in from the side of the new tab (L wraps from the first to the last)
        const int from = int(m_shown_tab), to = int(m_tab);
        m_tab_dir = ((to - from + kTabs) % kTabs) <= 2 ? 1.f : -1.f;
        m_shown_tab = m_tab;
        m_tab_at = m_time;
    }
    if (m_detail != m_shown_detail) {
        m_shown_detail = m_detail;
        m_detail_at = m_time;
    }
    const float tab_e = float(std::min(1.0, (m_time - m_tab_at) / 0.22));
    nvgSave(vg);
    if (tab_e < 1 && !ui::Still()) {
        const float k = 1 - tab_e;
        nvgTranslate(vg, 30 * m_tab_dir * k * k, 0);
    }
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
        case Tab::Queue: DrawQueue(vg); break;
        case Tab::Installed: DrawInstalled(vg); break;
        case Tab::More: DrawMore(vg); break;
        case Tab::Settings: DrawSettings(vg); break;
    }
    nvgRestore(vg);
    DrawTransitions(vg);
    DrawJumpLetter(vg);
    AddHints(m_hint_rects);
    if (m_menu && m_tab == Tab::Library && !m_detail) {
        DrawMenu(vg);
    }
    if (m_dialog.kind != DialogKind::None) {
        DrawDialog(vg);
    }
    if (m_time < m_notice_until && !m_notice.empty()) {
        // a short notice above the hint bar
        const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48;
        // it comes up from below with a little spring
        const float rise = ui::EaseOutBack(ui::Appear(m_notice_at, 0.35f));
        const float x = (kWidth - w) / 2, y = 600 + (1 - rise) * 34;
        nvgGlobalAlpha(vg, float(std::min(1.0, (m_notice_until - m_time) * 3)) * std::min(1.f, rise * 1.5f));
        ui::FillRound(vg, x, y, w, 44, 22, m_theme.panel2);
        ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, 43, 22, m_theme.work, 1.5f);
        ui::Text(vg, x + w / 2, y + 22, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    DrawFinish(vg);
    DrawTour(vg);
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
        veil(0.9f * (1 - tab_e));
    }
    const double age = m_time - m_detail_at;
    if (m_tab != Tab::Library || age >= 0.3 || m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
        return;
    }
    // the page of a game grows out of its cover, and goes back into it
    const float p = float(age / 0.3), e = 1 - (1 - p) * (1 - p) * (1 - p);
    veil(0.85f * (1 - e));
    const float gx = kGridX + (m_focus % kColumns) * (kCoverSize + kGridGap);
    const float gy = kGridTop + (m_focus / kColumns) * kRowPitch - m_scroll;
    const float dx = 48, dy = 72 + 34, ds = 330;
    const float k = m_detail ? e : 1 - e; // 0: in the grid, 1: on the page
    const float x = gx + (dx - gx) * k, y = gy + (dy - gy) * k, s = kCoverSize + (ds - kCoverSize) * k;
    const Game& g = m_games[m_detail_game];
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 10, s + 60, s + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x + 6, y + 12, s - 12, s, 14, 30, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, m_theme, x, y, s, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 10 + 4 * k,
              m_covers.Get(g.cover));
}

void App::InitRumble() {
    // the console in the hands, and player 1's controllers (two Joy-Con or a Pro Controller)
    m_vib_ok[0] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_handheld, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
    m_vib_ok[1] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_dual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
    m_vib_ok[2] = R_SUCCEEDED(hidInitializeVibrationDevices(m_vib_full, 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
}

void App::SendRumble(bool on) {
    // short and gentle: a quarter of the strength, at the frequencies the console uses for a tap
    HidVibrationValue v[2];
    for (auto& value : v) {
        value.amp_low = on ? 0.25f : 0.f;
        value.freq_low = 160.f;
        value.amp_high = on ? 0.20f : 0.f;
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
            Notice(text);
            m_notice_until = m_time + 5;
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

void App::DrawFinish(NVGcontext* vg) {
    const double t = m_time - m_finish_at;
    if (t < 0 || t > 1.4) {
        return;
    }
    // the glow: up in a quarter of a second, away in a little more than one
    const float glow = float(t < 0.25 ? t / 0.25 : std::max(0.0, 1 - (t - 0.25) / 1.15));
    NVGcolor edge = m_theme.work;
    edge.a = 0.5f * glow;
    NVGcolor clear = m_theme.work;
    clear.a = 0;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillPaint(vg, nvgBoxGradient(vg, 60, 60, kWidth - 120, kHeight - 120, 40, 220, clear, edge));
    nvgFill(vg);

    // the tick in a circle: it pops, rests, and goes
    const float scale = float(t < 0.18 ? 0.6 + 0.45 * (t / 0.18) : t < 0.3 ? 1.05 - 0.05 * ((t - 0.18) / 0.12) : 1.0);
    const float alpha = float(t < 0.12 ? t / 0.12 : t > 0.9 ? std::max(0.0, 1 - (t - 0.9) / 0.5) : 1.0);
    const float cx = kWidth / 2, cy = 340, r = 72 * scale;
    nvgGlobalAlpha(vg, alpha);
    NVGcolor halo = m_theme.work;
    halo.a = 0.45f;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r + 40);
    nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, r * 0.8f, r + 40, halo, clear));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, m_theme.work);
    nvgFill(vg);
    ui::CheckMark(vg, cx, cy + 2, 70 * scale, m_theme.on_work, 9 * scale);
    nvgGlobalAlpha(vg, 1.f);
}

void App::DrawTouchFeedback(NVGcontext* vg) {
    // While a finger rests on something, light it up so it is clear what will be pressed.
    if (!m_touching || m_dragging || m_az_touch) {
        return;
    }
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (it->rect.Contains(m_touch_x, m_touch_y)) {
            NVGcolor c = m_theme.work;
            c.a = 0.18f;
            ui::FillRound(vg, it->rect.x, it->rect.y, it->rect.w, it->rect.h, 10, c);
            return;
        }
    }
}

void App::Notice(const std::string& text) {
    m_notice = text;
    m_notice_at = m_time;
    m_notice_until = m_time + 3.5;
}

void App::CheckSdSpace() {
    if (m_time - m_sd_checked < 10) {
        return;
    }
    m_sd_checked = m_time;
    FsFileSystem fs;
    if (R_SUCCEEDED(fsOpenSdCardFileSystem(&fs))) {
        s64 free_bytes = 0;
        char root[FS_MAX_PATH] = "/";
        if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, root, &free_bytes))) {
            m_sd_free_bytes = free_bytes;
            m_sd_free = SizeText(u64(free_bytes));
        }
        fsFsClose(&fs);
    }
    StorageSpace(NcmStorageId_BuiltInUser, m_nand_free_bytes, m_nand_total_bytes);
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
        m_games = m_catalog.Games();
        if (!m_games.empty()) {
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
    const std::string keep = !m_keep_key.empty() ? m_keep_key : FocusedKey();
    m_keep_key.clear();
    m_view.clear();
    const std::string search = Lower(m_search);
    for (int i = 0; i < int(m_games.size()); i++) {
        const Game& g = m_games[i];
        bool keep = true;
        switch (m_filter) {
            case 1: keep = !g.installed.base; break;
            case 2: keep = g.UpdateAvailable() || g.MissingDlc(); break;
            case 3: keep = !g.Dlc().empty(); break;
            default: break;
        }
        if (keep && !search.empty()) {
            keep = Lower(g.name).find(search) != std::string::npos;
        }
        if (keep) {
            m_view.push_back(i);
        }
    }
    // the order: A-Z (with what starts with no letter first), size, or not installed first
    std::vector<std::string> names(m_games.size());
    std::vector<int> slots(m_games.size());
    for (int i : m_view) {
        names[i] = Lower(m_games[i].name);
        slots[i] = LetterSlot(m_games[i].name);
    }
    const auto by_name = [&](int a, int b) { return slots[a] != slots[b] ? slots[a] < slots[b] : names[a] < names[b]; };
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
        // B leaves the shop: the library app again
        LeaveShop();
        return;
    }
    if (Repeat(down, held, left) && m_focus % kColumns > 0) m_focus--;
    if (Repeat(down, held, right) && m_focus % kColumns < kColumns - 1 && m_focus + 1 < count) m_focus++;
    if (Repeat(down, held, up) && m_focus >= kColumns) m_focus -= kColumns;
    if (Repeat(down, held, dn) && m_focus + kColumns < count) m_focus += kColumns;
    if (count > 0) {
        const bool r_up = Repeat(down, held, HidNpadButton_StickRUp);
        const bool r_down = Repeat(down, held, HidNpadButton_StickRDown);
        if (r_up || r_down) {
            if (m_ui.sort == 0) {
                JumpToLetter(-1, r_down ? 1 : -1);
            } else {
                // other orders have no letters: a page (two rows)
                const int page = 2 * kColumns;
                m_focus = std::clamp(m_focus + (r_down ? page : -page), 0, count - 1);
            }
        }
    }
    if (m_focus != before) KeepFocusVisible();
    if (down & (HidNpadButton_ZL)) {
        m_filter = (m_filter + 3) % 4;
        RebuildView();
    }
    if (down & (HidNpadButton_ZR)) {
        m_filter = (m_filter + 1) % 4;
        RebuildView();
    }
    if ((down & HidNpadButton_A) && count > 0) {
        OpenDetail(m_focus);
    }
    if (down & HidNpadButton_Minus) {
        // Options (5A): the order and Update all
        m_menu = true;
        m_menu_focus = m_ui.sort;
    }
    if (down & HidNpadButton_Y) {
        std::string text;
        if (AskText(Tr().search_hint, m_search, false, text) || !m_search.empty()) {
            m_search = text;
            RebuildView();
        }
    }
    if (down & HidNpadButton_X) {
        if (m_catalog.GetState() == Catalog::State::Failed) {
            const Config c = m_link.CurrentConfig();
            m_catalog.Load(c.url, c.key);
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
    const int rows = PendingUpdates().empty() ? kSorts : kSorts + 1;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_menu_focus > 0) m_menu_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_menu_focus < rows - 1) m_menu_focus++;
    m_menu_focus = std::clamp(m_menu_focus, 0, rows - 1);
    if (down & HidNpadButton_A) {
        ActivateMenu(m_menu_focus);
    } else if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        m_menu = false;
    }
}

bool App::AzBarShown() const {
    return m_tab == Tab::Library && !m_detail && m_ui.sort == 0 && !m_view.empty() && MaxScroll() > 0;
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
        if (MaxScroll() > 0 && m_tab == Tab::Library && !m_view.empty()) {
            const float track_h = kGridViewH;
            const float thumb_h = track_h * kGridViewH / (kGridViewH + MaxScroll());
            const float thumb_y = kGridTop + (track_h - thumb_h) * (m_scroll / MaxScroll());
            NVGcolor c = t.muted;
            c.a = 0.45f;
            ui::FillRound(vg, kWidth - 16, thumb_y, 4, thumb_h, 2, c);
        }
        return;
    }
    bool present[27] = {};
    for (int v : m_view) present[LetterSlot(m_games[v].name)] = true;
    const int here = LetterSlot(m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]].name);
    NVGcolor back = t.panel2;
    back.a = m_az_touch ? 0.95f : 0.7f;
    ui::FillRound(vg, kAzX, kAzTop, kAzW, kAzHeight, kAzW / 2, back);
    if (m_az_touch) {
        ui::StrokeRound(vg, kAzX + .5f, kAzTop + .5f, kAzW - 1, kAzHeight - 1, kAzW / 2, t.work, 1.5f);
    }
    const float pitch = kAzHeight / 27;
    for (int i = 0; i < 27; i++) {
        const char label[2] = {i == 0 ? '#' : char('A' + i - 1), 0};
        const float y = kAzTop + pitch * (i + 0.5f);
        NVGcolor c = i == here ? t.work : t.muted;
        if (!present[i]) c.a = 0.35f;
        ui::Text(vg, kAzX + kAzW / 2, y, i == here ? 13 : 11, c, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    // the whole edge answers to the finger (see ReadTouch); nothing here is a tap target of its own
}

void App::DrawJumpLetter(NVGcontext* vg) {
    const double age = m_time - m_jump_at;
    const double hold = m_az_touch ? 1e9 : 0.6; // stays while the finger is on the bar
    if (m_jump_letter < 0 || age > hold + 0.25 || m_tab != Tab::Library || m_detail) {
        return;
    }
    const Theme& t = m_theme;
    const float alpha = float(age < hold ? 1.0 : 1 - (age - hold) / 0.25);
    const char label[2] = {m_jump_letter == 0 ? '#' : char('A' + m_jump_letter - 1), 0};
    const float s = 150, x = (kWidth - s) / 2, y = kGridTop + kGridViewH / 2 - s / 2;
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
    const size_t updates = PendingUpdates().size();
    const int rows = updates ? kSorts + 1 : kSorts;
    const float w = 440, rh = 56, pad = 10;
    const float h = pad * 2 + 34 + kSorts * rh + (updates ? rh + 13 : 0);
    const float x = kWidth - 36 - w, y = 664 - 14 - h;
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 20, w + 60, h + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, w, h, 16, 36, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    Panel(vg, t, x, y, w, h, 16);
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
        if (i < kSorts) {
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
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.choose, HidNpadButton_A}, {ui::glyph::B, s.close, HidNpadButton_B}}, rects);
    AddHints(rects);
}

void App::OpenDetail(int view_index) {
    if (view_index < 0 || view_index >= int(m_view.size())) {
        return;
    }
    m_detail = true;
    m_detail_game = m_view[view_index];
    m_detail_focus = 0;
    // where, as chosen in the wizard: the internal memory still asks every time (EnqueuePicked)
    m_to_nand = m_ui.dest == 1;
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
    if (down & (HidNpadButton_L | HidNpadButton_ZL)) {
        m_to_nand = false;
        m_dest_touched = true;
    }
    if (down & (HidNpadButton_R | HidNpadButton_ZR)) {
        m_to_nand = true;
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
        // loading, or it could not be read
        if (state == Catalog::State::Failed) {
            TextBox(vg, kWidth / 2 - 300, 300, 600, 18, t.warn, m_catalog.Error().c_str());
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, s.retry, HidNpadButton_X}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                        m_hint_rects);
        } else {
            ui::Spinner(vg, kWidth / 2, 330, 20, t.work, m_time);
            ui::Text(vg, kWidth / 2, 384, 20, t.muted, s.loading_library, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::Plus, s.exit, HidNpadButton_Plus}}, m_hint_rects);
        }
        return;
    }

    // Filters, with what each shows.
    int counts[4] = {int(m_games.size()), 0, 0, 0};
    for (const auto& g : m_games) {
        counts[1] += !g.installed.base;
        counts[2] += g.UpdateAvailable() || g.MissingDlc();
        counts[3] += !g.Dlc().empty();
    }
    const char* names[] = {s.all, s.not_installed, s.with_update, s.dlc};
    float x = kGridX;
    for (int i = 0; i < 4; i++) {
        char chip[64];
        std::snprintf(chip, sizeof(chip), "%s · %d", names[i], counts[i]);
        const float w = ui::Chip(vg, t, x, 92, chip, i == m_filter);
        AddHit(x - 4, 82, w + 8, 54, HitKind::Chip, i); // taller than drawn: easy to hit
        x += w + 10;
    }
    if (!m_search.empty()) {
        const std::string label = "“" + m_search + "”";
        ui::Chip(vg, t, x + 10, 92, label.c_str(), true);
    }
    // the order, on the right (changed in Options)
    const char* sorts[] = {s.sort_az, s.sort_size, s.sort_missing, s.sort_recent};
    ui::Text(vg, kWidth - kGridX, 109, 15, t.muted, sorts[std::clamp(m_ui.sort, 0, kSorts - 1)], NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    // Grid of covers, clipped to its viewport and scrolled.
    nvgSave(vg);
    nvgScissor(vg, 0, kGridTop - kGridMargin, kWidth, kGridViewH + 2 * kGridMargin);
    if (m_focus != m_lift_focus) {
        // the cover in focus lifts and leans a little toward where the focus came from
        if (m_lift_focus >= 0) {
            m_lift_dx = float((m_focus % kColumns) - (m_lift_focus % kColumns));
            m_lift_dy = float((m_focus / kColumns) - (m_lift_focus / kColumns));
        }
        m_lift_focus = m_focus;
        m_lift_at = m_time;
    }
    for (int i = 0; i < int(m_view.size()); i++) {
        const float cx = kGridX + (i % kColumns) * (kCoverSize + kGridGap);
        const float cy = kGridTop + (i / kColumns) * kRowPitch - m_scroll;
        if (cy + kCoverSize < kGridTop - kGridMargin || cy > kGridTop + kGridViewH + kGridMargin) {
            continue;
        }
        const Game& g = m_games[m_view[i]];
        // Only the visible part of a cover can be tapped.
        const float top = std::max(cy, kGridTop - kGridMargin), bottom = std::min(cy + kCoverSize, kGridTop + kGridViewH + kGridMargin);
        if (bottom > top) {
            AddHit(cx - kGridGap / 2, top, kCoverSize + kGridGap, bottom - top, HitKind::Cover, i);
        }
        if (i == m_focus) {
            continue; // drawn last, over its neighbours
        }
        const std::string format = FormatOf(g);
        ui::Cover(vg, t, cx, cy, kCoverSize, PlaceholderColor(g.key), Initial(g.name).c_str(), format.c_str(),
                  g.installed.base, g.UpdateAvailable(), 10, m_covers.Get(g.cover));
    }
    if (m_focus >= 0 && m_focus < int(m_view.size())) {
        // a little more room above and below for the lifted cover and its glow
        nvgScissor(vg, 0, kGridTop - kGridMargin - 10, kWidth, kGridViewH + 2 * kGridMargin + 20);
        const float cx = kGridX + (m_focus % kColumns) * (kCoverSize + kGridGap);
        const float cy = kGridTop + (m_focus / kColumns) * kRowPitch - m_scroll;
        const Game& g = m_games[m_view[m_focus]];
        DrawLiftedCover(vg, g, cx, cy, kCoverSize);
    }
    nvgRestore(vg);
    if (m_view.empty()) {
        ui::Text(vg, kWidth / 2, kGridTop + kGridViewH / 2, 18, t.muted, s.empty_filter, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // The A-Z bar, or the scroll indicator when the order has no letters.
    DrawAzBar(vg);

    // Name of the game in focus, with what matters of it.
    if (!m_view.empty()) {
        const Game& g = m_games[m_view[m_focus]];
        const float ny = kGridTop + kGridViewH + 22;
        nvgSave(vg);
        nvgScissor(vg, kGridX, ny - 20, 700, 40);
        const float nw = ui::Text(vg, kGridX, ny, 22, t.text, g.name.c_str()) - kGridX;
        nvgRestore(vg);
        std::string facts = FormatOf(g) + " · " + SizeText(g.SizeOfAll());
        if (!g.Dlc().empty()) facts += " · " + std::to_string(g.Dlc().size()) + " DLC";
        facts += " · ";
        facts += g.installed.base ? s.on_console : s.not_on_console;
        ui::Text(vg, kGridX + std::min(nw, 700.f) + 16, ny + 1, 16, t.muted, facts.c_str());
    }

    // Queue strip, always in sight; tapping it opens the queue.
    DrawQueueStrip(vg, 556);

    std::vector<ui::Hint> hints = {{ui::glyph::A, s.view, HidNpadButton_A}, {ui::glyph::ZR, s.filter, HidNpadButton_ZR},
                                   {ui::glyph::Y, s.search, HidNpadButton_Y}};
    // Options: the order, and Update all when there is something to update
    const size_t updates = PendingUpdates().size();
    m_update_hint = s.options;
    if (updates) {
        m_update_hint += std::string(" · ") + s.update_all + " " + std::to_string(updates);
    }
    hints.push_back({ui::glyph::Minus, m_update_hint.c_str(), HidNpadButton_Minus});
    hints.push_back({ui::glyph::Plus, s.exit, HidNpadButton_Plus});
    ui::HintBar(vg, t, where.c_str(), hints, m_hint_rects);
}

void App::DrawLiftedCover(NVGcontext* vg, const Game& g, float x, float y, float s) {
    const Theme& t = m_theme;
    const double age = m_time - m_lift_at;
    // up in a fifth of a second, slowing at the end
    const float p = float(std::min(1.0, age / 0.2));
    const float e = 1 - (1 - p) * (1 - p) * (1 - p);
    const float lift = 6 * e, scale = 1 + 0.05f * e;
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
    const std::string format = FormatOf(g);
    ui::Cover(vg, t, 0, 0, s, PlaceholderColor(g.key), Initial(g.name).c_str(), format.c_str(), g.installed.base,
              g.UpdateAvailable(), 10, m_covers.Get(g.cover));
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

    // the colour of the cover, blurred behind everything
    const NVGcolor tint = Hex(PlaceholderColor(g.key), t.dark ? 0.20f : 0.14f);
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, kWidth, bottom - top);
    nvgFillPaint(vg, nvgRadialGradient(vg, 300, 330, 40, 700, tint, Hex(0, 0)));
    nvgFill(vg);

    // the cover
    const float cs = 330, cx = 48, cy = top + 34;
    if (m_time - m_detail_at >= 0.3) { // while the page opens, the cover is still on its way (DrawTransitions)
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx + 4, cy + 14, cs - 8, cs, 14);
        nvgFillPaint(vg, nvgBoxGradient(vg, cx, cy + 14, cs, cs, 14, 30, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, m_covers.Get(g.cover));
    }

    // name and facts
    const float rx = cx + cs + 44, rw = kWidth - rx - 48;
    nvgSave(vg);
    nvgScissor(vg, rx, cy, rw, 60);
    ui::Text(vg, rx, cy + 26, 32, t.text, g.name.c_str());
    nvgRestore(vg);
    const CatalogFile* newest = g.NewestUpdate();
    u64 picked_size = 0;
    for (size_t i = 0; i < g.files.size(); i++) {
        if (m_detail_pick[i]) picked_size += g.files[i].size;
    }
    u32 firmware = 0;
    for (const auto& f : g.files) firmware = std::max(firmware, f.required_system);
    struct Fact { const char* label; std::string value; NVGcolor color; };
    std::vector<Fact> facts;
    if (newest) {
        facts.push_back({s.version, "v" + std::to_string(newest->version), t.text});
    }
    facts.push_back({s.format, FormatOf(g), t.text});
    facts.push_back({s.will_take, SizeText(picked_size), t.text});
    if (firmware) facts.push_back({s.firmware, FirmwareText(firmware) + " " + s.or_newer, t.text});
    const bool versions = g.installed.base && !m_shop_active && !VersionRows(g).empty();
    if (g.installed.base) {
        facts.push_back({s.on_console, g.installed.patch ? "v" + std::to_string(g.installed.patch) : std::string("v0"), t.ok});
    } else {
        facts.push_back({s.on_console, "—", t.muted});
    }
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

    // what to install: the game, the newest update, every DLC
    std::vector<int> rows;
    for (int i = 0; i < int(g.files.size()); i++) {
        if (g.files[i].kind != CatalogFile::Kind::Update || &g.files[i] == newest) rows.push_back(i);
    }
    float ly = fy + 76;
    const float lh = 46;
    const float list_h = std::min(float(rows.size()) * lh + 12, 250.f);
    ui::FillRound(vg, rx, ly, rw, list_h, 12, Hex(0, t.dark ? 0.22f : 0.03f));
    ui::StrokeRound(vg, rx + .5f, ly + .5f, rw - 1, list_h - 1, 12, t.line, 1);
    nvgSave(vg);
    nvgScissor(vg, rx, ly, rw, list_h);
    // keep the row in focus in sight
    const int visible = int((list_h - 12) / lh);
    const int first = std::clamp(m_detail_focus - visible + 1, 0, std::max(0, int(rows.size()) - visible));
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
            ui::CheckMark(vg, rx + 32, y + 21, 13, nvgRGB(255, 255, 255), 2.4f);
        } else {
            ui::StrokeRound(vg, rx + 20.5f, y + 9.5f, 23, 23, 6, t.muted, 1.5f);
        }
        std::string label;
        bool installed = false, fresh = false;
        switch (f.kind) {
            case CatalogFile::Kind::Game: label = s.base_game; installed = g.installed.base; break;
            case CatalogFile::Kind::Update:
                label = std::string(s.update_word) + " v" + std::to_string(f.version);
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

    // where, and Install
    const float dy = ly + list_h + 22;
    ui::Text(vg, rx, dy + 22, 16, t.muted, s.install_to);
    const float segx = rx + ui::TextWidth(vg, 16, s.install_to) + 16;
    const char* dests[] = {s.sd_card, s.internal};
    float sx = segx;
    const float segw0 = ui::TextWidth(vg, 16, dests[0]) + 36, segw1 = ui::TextWidth(vg, 16, dests[1]) + 36;
    ui::FillRound(vg, sx, dy, segw0 + segw1 + 8, 44, 10, t.panel2);
    for (int d = 0; d < 2; d++) {
        const float w = d == 0 ? segw0 : segw1;
        const bool on = (d == 1) == m_to_nand;
        if (on) ui::FillRound(vg, sx + 4, dy + 4, w, 36, 8, t.work);
        ui::Text(vg, sx + 4 + w / 2, dy + 22, 16, on ? t.on_work : t.muted, dests[d], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        AddHit(sx, dy - 6, w + 8, 56, HitKind::Dest, d);
        sx += w + 4;
    }
    char install[64];
    std::snprintf(install, sizeof(install), s.install_size, SizeText(picked_size).c_str());
    const float bw = ui::TextWidth(vg, 20, install) + 64, bx = kWidth - 48 - bw;
    const bool can = picked_size > 0;
    if (m_detail_focus == int(rows.size())) {
        ui::Focus(vg, t, bx, dy - 4, bw, 52, 12, m_time);
    }
    nvgGlobalAlpha(vg, can ? 1.f : 0.45f);
    ui::FillRound(vg, bx, dy - 4, bw, 52, 12, t.work);
    ui::Text(vg, bx + bw / 2, dy + 22, 20, t.on_work, install, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgGlobalAlpha(vg, 1.f);
    AddHit(bx, dy - 4, bw, 52, HitKind::Install, int(rows.size()));
    const s64 free_here = m_to_nand ? m_nand_free_bytes : m_sd_free_bytes;
    if (free_here >= 0) {
        char line[160];
        const u64 left = u64(free_here) > picked_size ? u64(free_here) - picked_size : 0;
        std::snprintf(line, sizeof(line), m_to_nand ? s.space_line_nand : s.space_line, SizeText(u64(free_here)).c_str(), SizeText(left).c_str());
        ui::Text(vg, rx, dy + 72, 15, u64(free_here) < picked_size ? t.err : t.muted, line);
    }

    std::vector<ui::Hint> hints = {{ui::glyph::A, s.select, HidNpadButton_A}, {ui::glyph::L, s.sd_card, HidNpadButton_L},
                                   {ui::glyph::R, s.internal, HidNpadButton_R}};
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
float JobFraction(const InstallQueue::Live& l) {
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
                case CatalogFile::Kind::Update: label = std::string(s.update_word) + " v" + std::to_string(f.version); break;
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
        return;
    }
    if (m_to_nand) {
        // never to the internal memory without asking: the question (1A), Cancel in focus
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand;
        m_dialog.jobs = std::move(jobs);
        return;
    }
    AddJobs(std::move(jobs));
}

InstallJob App::JobFor(const Game& g, const CatalogFile& f) {
    const auto& s = Tr();
    const Config c = m_link.CurrentConfig();
    InstallJob job;
    job.game = g.name;
    switch (f.kind) {
        case CatalogFile::Kind::Game: job.label = s.base_game; break;
        case CatalogFile::Kind::Update: job.label = std::string(s.update_word) + " v" + std::to_string(f.version); break;
        case CatalogFile::Kind::Dlc: job.label = f.dlc_name.empty() ? f.name : f.dlc_name; break;
    }
    job.version = f.version;
    job.resumable = !m_shop_active; // a journal while it installs (23A); a shop's key is not written down
    job.cover = g.cover;
    job.color_key = g.key;
    job.file_id = f.id;
    const bool whole = f.url.rfind("http://", 0) == 0 || f.url.rfind("https://", 0) == 0;
    job.url = whole ? f.url : c.url + f.url;
    job.key = m_shop_active ? m_shop_key : c.key;
    job.format = f.format;
    job.kind = f.kind;
    job.title_id = f.title_id;
    job.size = f.size;
    job.required_system = f.required_system;
    return job;
}

std::vector<InstallJob> App::PendingUpdates() {
    std::vector<InstallJob> jobs;
    const u32 hos = hosversionGet();
    const u32 console = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    for (const auto& g : m_games) {
        if (!g.UpdateAvailable()) {
            continue;
        }
        const CatalogFile* f = g.NewestUpdate();
        if (f->required_system && (f->required_system & 0xFFFF0000) > console) {
            continue; // the console cannot run it yet
        }
        InstallJob job = JobFor(g, *f);
        job.label = std::string(Tr().update_word) + " v" + std::to_string(g.installed.patch) + " → v" + std::to_string(f->version);
        job.nand = g.installed.base_nand;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

std::string App::ContentLabel(const Game& g, const NcmContentMetaKey& key) const {
    const auto& s = Tr();
    switch (key.type) {
        case NcmContentMetaType_Application: return s.base_game;
        case NcmContentMetaType_Patch: return std::string(s.update_word) + " v" + std::to_string(key.version);
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
                job.label = p.kind == "update" ? std::string(s.update_word) + " v" + std::to_string(p.version)
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

void App::AddJobs(std::vector<InstallJob> jobs) {
    const auto& s = Tr();
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
        Notice(s.added_to_queue);
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
    if (down & HidNpadButton_X) {
        m_queue->Remove(j.id); // a waiting one goes; the one installing is cancelled
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
        ui::Text(vg, 1222, qy + 28, 24, t.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
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
    if (live.copying_save) {
        std::snprintf(figures, sizeof(figures), "%s…", TrM().sc_copying);
    } else if (live.phase >= 3) {
        std::snprintf(figures, sizeof(figures), "%s…", s.registering);
    } else if (live.total == 0) {
        std::snprintf(figures, sizeof(figures), "%s… · %s %.0f MB/s", s.preparing, s.network, live.net_rate);
    } else {
        const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
        std::snprintf(figures, sizeof(figures), "%d %% · %s %.0f · %s %.0f · %s %.0f MB/s · %u/%u%s%s", int(frac * 100), s.network,
                      live.net_rate, s.decompress, live.ready_rate, it->nand ? s.internal_short : "SD", live.write_rate, live.checked, live.ncas,
                      left >= 0 ? " · " : "", left >= 0 ? (std::string(s.left) + " " + Duration(left)).c_str() : "");
    }
    const float fw = ui::TextWidth(vg, 15, figures);
    const float bar_x = 494, bar_end = 1196 - fw - 18;
    if (bar_end - bar_x > 40) {
        ui::Progress(vg, t, bar_x, qy + 23, bar_end - bar_x, 8, frac, t.work, m_time);
    }
    ui::Text(vg, 1196, qy + 27, 15, t.muted, figures, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Text(vg, 1222, qy + 28, 24, t.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
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
        const float cx = px + 168;
        nvgSave(vg);
        nvgScissor(vg, cx, py + 16, pw - 168 - 140, 48);
        const float nw = ui::Text(vg, cx, py + 40, 24, t.text, j.game.c_str()) - cx;
        const std::string sub = j.label + " · " + Upper(j.format) + " · " + (j.nand ? s.internal : "SD");
        ui::Text(vg, cx + nw + 14, py + 41, 16, t.muted, sub.c_str());
        nvgRestore(vg);

        const float frac = JobFraction(live);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d %%", int(frac * 100));
        ui::Text(vg, px + pw - 24, py + 40, 26, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        char eta[160];
        if (live.copying_save) {
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
        if (live.saved > 0) {
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
        ui::Progress(vg, t, cx, py + 88, pw - 168 - 24, 12, frac, t.work, m_time);

        struct Lane { std::string title; NVGcolor color; std::string value; std::string unit; float meter; int segments; };
        char v[32];
        std::vector<Lane> lanes;
        std::snprintf(v, sizeof(v), "%.0f", live.net_rate);
        lanes.push_back({s.network, t.work, v, "MB/s", live.source_size ? float(double(live.net) / double(live.source_size)) : 0.f, -1});
        if (live.threads == 0) {
            lanes.push_back({s.decompress, t.violet, "—", s.not_compressed, -1, 0});
        } else {
            std::snprintf(v, sizeof(v), "%.0f", live.ready_rate);
            std::string title = s.decompress;
            if (live.threads > 0) {
                title += " · " + std::to_string(live.threads) + " " + (live.threads == 1 ? s.thread_one : s.threads);
            }
            lanes.push_back({title, t.violet, v, "MB/s", -1, std::max(0, live.threads)});
        }
        std::snprintf(v, sizeof(v), "%.0f", live.write_rate);
        lanes.push_back({j.nand ? s.write_nand : s.write_sd, t.orange, v, "MB/s", live.total ? float(double(live.written) / double(live.total)) : 0.f, -1});
        std::snprintf(v, sizeof(v), "%u", live.checked);
        lanes.push_back({s.check_sha, t.ok, v, std::string(s.parts_of) + " " + std::to_string(live.ncas),
                         live.ncas ? float(live.checked) / float(live.ncas) : 0.f, -1});
        const float lw = (pw - 168 - 24 - 3 * 14) / 4;
        for (int i = 0; i < 4; i++) {
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
                NVGcolor track = t.text;
                track.a = 0.10f;
                ui::FillRound(vg, lx + 14, ly + 70, lw - 28, 6, 3, track);
                ui::FillRound(vg, lx + 14, ly + 70, (lw - 28) * std::clamp(lane.meter, 0.f, 1.f), 6, 3, lane.color);
            } else {
                // One segment per decompression thread; the fourth core is the system's.
                const float sw = (lw - 28 - 12) / 4;
                for (int k = 0; k < 4; k++) {
                    NVGcolor c = lane.color;
                    c.a = k < lane.segments ? 1.f : 0.25f;
                    ui::FillRound(vg, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 3, c);
                }
            }
        }
        y = py + ph + 26;
    }

    // Waiting, then done; the list scrolls to keep the row in focus in sight.
    const int first_row = running ? 1 : 0;
    // a failed row in focus shows what happened and what to do under the list (1A)
    const InstallJob& focused = rows[m_queue_focus];
    const bool card = focused.state == InstallJob::State::Failed && !focused.error.empty();
    const float card_h = card ? ErrorCard(vg, t, 48, 0, kWidth - 96, focused.error, false) : 0;
    const float list_top = y, list_bottom = card ? std::max(y + 100, 650 - card_h - 12) : 640;
    if (card) {
        ErrorCard(vg, t, 48, 650 - card_h, kWidth - 96, focused.error, true);
    }
    const float pitch = 54;
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
        nvgSave(vg);
        nvgScissor(vg, 122, y, 600, 44);
        ui::Text(vg, 122, y + 22, 17, t.text, FileLabel(j).c_str());
        nvgRestore(vg);
        std::string info = Upper(j.format) + " · " + SizeText(j.size) + " · " + (j.nand ? s.internal_short : "SD");
        if (j.state == InstallJob::State::Done) info += " · " + Duration(j.seconds);
        if (j.state == InstallJob::State::Done && j.saved > 0) {
            char saved[64];
            std::snprintf(saved, sizeof(saved), TrM().saved_fmt, SizeText(j.saved).c_str());
            info += std::string(" · ") + saved;
        }
        if (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled) {
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
    ui::HintBar(vg, t, paused ? TrM().qw_paused : m_prefs.download_test ? s.download_test : (running ? s.no_sleep : s.queue_done_hint), hints,
                m_hint_rects);
}

void App::DrawSettings(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const bool running = m_speed.Running();
    const Link::View link = m_link.Snapshot();
    const Config current = m_link.CurrentConfig();
    const bool linked = current.Linked();

    // Rows on the left: the usual ones first, the ones by hand last.
    std::string library = s.not_linked;
    if (linked) {
        const std::string host = current.url.substr(current.url.find("://") == std::string::npos ? 0 : current.url.find("://") + 3);
        library = Clip((current.name.empty() ? link.app_name : current.name) + " · " + host, 36);
    }
    const std::string address = !m_manual_url.empty() ? Clip(m_manual_url, 32) : s.not_set;
    struct Row { const char* label; std::string value; bool accent; };
    const Row rows[kSettingRows] = {
        {s.theme, t.dark ? s.dark : s.light, true},
        {s.sounds_row, m_ui.sounds ? s.yes_word : s.no_word, m_ui.sounds},
        {s.library, library, linked},
        {s.forget, "", false},
        {s.share_row, m_prefs.share_installed == 1 ? s.yes_word : s.no_word, m_prefs.share_installed == 1},
        {s.wiz_row, s.wiz_repeat, true},
        {TrM().so_auto_row, m_ui.screen_off_auto ? s.yes_word : s.no_word, m_ui.screen_off_auto},
        {TrM().anim_row, m_ui.anim == 2 ? TrM().anim_none : m_ui.anim == 1 ? TrM().anim_soft : TrM().anim_full, m_ui.anim != 2},
        {TrM().sc_row, m_ui.save_copy ? s.yes_word : s.no_word, m_ui.save_copy},
        {TrM().qw_notify_row, m_ui.notify_done ? s.yes_word : s.no_word, m_ui.notify_done},
        {s.manual_address, address, !m_manual_url.empty()},
        {s.manual_key, linked ? s.key_saved : s.not_set, linked},
        {s.speed_test, running ? s.cancel : s.start, linked},
    };
    const float x = 48, w = 540, h = 56, gap = 8;
    // the rows scroll when they do not fit, keeping the one in focus in sight
    const float view_top = 88, view_bottom = 652;
    const float list_h = float(int(kSettingRows)) * (h + gap) + 8 + 70;
    const float focus_top = m_setting * (h + gap) + (m_setting >= kRowAddress ? 8 : 0);
    float target = m_settings_scroll;
    if (focus_top - target < 0) target = focus_top;
    if (focus_top + h + 12 - target > view_bottom - view_top) target = focus_top + h + 12 - (view_bottom - view_top);
    if (m_setting == kSettingRows - 1) target = std::max(target, list_h - (view_bottom - view_top));
    m_settings_scroll = std::clamp(target, 0.f, std::max(0.f, list_h - (view_bottom - view_top)));
    nvgSave(vg);
    nvgScissor(vg, 0, view_top - 8, 600, view_bottom - view_top + 8);
    float y = view_top + 8 - m_settings_scroll;
    for (int i = 0; i < kSettingRows; i++) {
        if (i == kRowAddress) {
            y += 8; // the ones by hand apart
        }
        const bool dim = (i == kRowForget || i == kRowSpeed || i == kRowShare) && !linked;
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
        ui::Text(vg, x + 22, y + h / 2, 18, i == kRowForget ? t.warn : t.text, rows[i].label);
        ui::Text(vg, x + w - 22, y + h / 2, 16, rows[i].accent ? t.work : t.muted, rows[i].value.c_str(),
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
        y += h + gap;
    }
    if (linked && !link.enabled) {
        TextBox(vg, x, y + 4, w, 15, t.warn, s.link_off_warning);
    } else if (!linked) {
        TextBox(vg, x, y + 4, w, 15, t.muted, s.need_config);
    }
    nvgRestore(vg);

    DrawSpeedPanel(vg, 620, 96, kWidth - 620 - 48, 548);

    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.edit, HidNpadButton_A}, {ui::glyph::X, running ? s.cancel : s.speed_test, HidNpadButton_X},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawPairing(NVGcontext* vg, const Link::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    using State = Link::State;
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
        TextBox(vg, lx + 26, y + 6, lw - 52, 15, t.warn, v.error.c_str());
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
        TextBox(vg, rx + 26, sy - 10, rw - 52, 15, v.state == State::Failed ? t.err : t.warn, v.error.c_str());
    }

    // Remember: on by default; off, the link lasts until NXDock closes.
    const float my = top + ph - 70;
    AddHit(rx + 16, my - 8, rw - 32, 52, HitKind::Remember);
    ui::FillRound(vg, rx + 26, my, 30, 30, 8, v.remember ? t.work : t.panel2);
    if (!v.remember) {
        ui::StrokeRound(vg, rx + 26.5f, my + .5f, 29, 29, 8, t.muted, 1.5f);
    } else {
        ui::CheckMark(vg, rx + 41, my + 15, 16, nvgRGB(255, 255, 255), 2.6f);
    }
    ui::Text(vg, rx + 68, my + 15, 17, t.text, s.remember);
    ui::Text(vg, rx + rw - 26, my + 16, 22, t.muted, ui::glyph::Y, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    const bool code_shown = v.state == State::Waiting || v.state == State::Expired || v.state == State::Refused;
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.choose, HidNpadButton_A},
                 {ui::glyph::X, code_shown ? s.new_code : s.search_again, HidNpadButton_X},
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
        TextBox(vg, px, by, pw, 15, t.err, v.error.c_str());
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
    const char* names[] = {s.this_session, s.history};
    const int counts[] = {session, int(m_history.size())};
    float x = 48;
    for (int i = 0; i < 2; i++) {
        char chip[64];
        std::snprintf(chip, sizeof(chip), "%s · %d", names[i], counts[i]);
        const float w = ui::Chip(vg, t, x, 92, chip, i == m_installed_view);
        AddHit(x - 4, 82, w + 8, 54, HitKind::InstalledView, i);
        x += w + 10;
    }
    if (m_installed_view == 0) {
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
        m_installed_view ^= 1;
        m_installed_focus = 0;
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_installed_focus > 0) m_installed_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown)) m_installed_focus++; // clamped when drawn
    if (m_installed_view == 0) {
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
                Line l{std::string(s.update_word) + " v" + std::to_string(f.version), here ? s.installed_tag : "",
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
                std::snprintf(text, sizeof(text), m.ver_back_fmt, ("v" + std::to_string(chosen.version)).c_str());
                primary = text;
                note = m.ver_note;
            } else {
                std::snprintf(text, sizeof(text), m.ver_install_fmt, ("v" + std::to_string(chosen.version)).c_str());
                primary = text;
            }
            break;
        }
        case DialogKind::Downgrade: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.dg_title_fmt, m_dialog.jobs.empty() ? "" : ("v" + std::to_string(m_dialog.jobs[0].version)).c_str());
            title = text;
            std::snprintf(text, sizeof(text), m.dg_body_fmt, ("v" + std::to_string(m_dialog.size)).c_str(),
                          m_dialog.jobs.empty() ? "" : ("v" + std::to_string(m_dialog.jobs[0].version)).c_str());
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
            cancel_label = m.rs_discard;
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
    }

    // the layout, from the top
    const float w = 740, pad = 32, iw = w - 2 * pad;
    const int shown = std::min<int>(int(lines.size()), m_dialog.kind == DialogKind::HistoryInfo ? 8 : 5);
    const bool more = int(lines.size()) > shown;
    float h = pad + 40;
    const float body_h = body.empty() ? 0 : TextBoxHeight(vg, iw, 17, body.c_str()) + 16;
    const float note_h = note.empty() ? 0 : TextBoxHeight(vg, iw, 15, note.c_str()) + 14;
    const float list_h = lines.empty() ? 0 : shown * 40 + (more ? 30 : 0) + 12 + 16;
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
    Panel(vg, t, x, y, w, h, 18);
    // a tap on the panel itself does nothing
    AddHit(x, y, w, h, HitKind::Button);

    float cy = y + pad;
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
        const float lh = shown * 40 + (more ? 30 : 0) + 12;
        ui::FillRound(vg, x + pad, cy, iw, lh, 12, t.panel2);
        ui::StrokeRound(vg, x + pad + .5f, cy + .5f, iw - 1, lh - 1, 12, t.line, 1);
        for (int i = 0; i < shown; i++) {
            const Line& l = lines[i];
            const float ly = cy + 6 + i * 40 + 20;
            if (l.focus) {
                ui::FillRound(vg, x + pad + 4, ly - 18, iw - 8, 36, 8, t.panel);
                ui::StrokeRound(vg, x + pad + 4, ly - 18, iw - 8, 36, 8, t.work, 2.5f);
            }
            if (l.hit >= 0) AddHit(x + pad, ly - 20, iw, 40, HitKind::DialogLine, l.hit);
            if (i > 0) {
                nvgBeginPath(vg);
                nvgRect(vg, x + pad + 14, ly - 20, iw - 28, 1);
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
            nvgScissor(vg, tx, ly - 20, iw - 32 - rw - 140, 40);
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
            ui::Text(vg, x + pad + 16, cy + 6 + shown * 40 + 14, 15, t.muted, text);
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
            ui::Text(vg, x + pad, cy + bh / 2, 15, t.muted, "?");
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
    struct Button { const char* glyph; std::string label; bool primary; int index; };
    std::vector<Button> buttons;
    buttons.push_back({ui::glyph::B, info ? s.close : cancel_label, false, 0});
    if (m_dialog.kind == DialogKind::Where) {
        // the internal memory, which asks again with its own question
        buttons.push_back({ui::glyph::X, s.internal, false, 2});
    }
    if (!info) buttons.push_back({ui::glyph::A, primary, true, 1});
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const float bw = ui::TextWidth(vg, 18, it->label.c_str()) + 78;
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
        ui::Symbol(vg, t, bx + 34, by + bh / 2, 24, it->glyph, fg, it->primary ? HidNpadButton_A : HidNpadButton_B);
        ui::Text(vg, bx + 56, by + bh / 2, 18, fg, it->label.c_str());
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
    if (m_dialog.kind == DialogKind::Where) {
        // Cancel, Internal memory, SD card, from left to right
        static constexpr int kOrder[] = {0, 2, 1};
        int pos = m_dialog.focus == 2 ? 1 : m_dialog.focus == 1 ? 2 : 0;
        if (Repeat(down, held, HidNpadButton_AnyLeft) && pos > 0) pos--;
        if (Repeat(down, held, HidNpadButton_AnyRight) && pos < 2) pos++;
        m_dialog.focus = kOrder[pos];
        if ((down & HidNpadButton_X) || ((down & HidNpadButton_A) && m_dialog.focus == 2)) {
            auto jobs = std::move(m_dialog.jobs);
            for (auto& j : jobs) j.nand = true;
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Nand; // never to the internal memory without its question
            m_dialog.jobs = std::move(jobs);
            return;
        }
    } else if (!info) {
        if (Repeat(down, held, HidNpadButton_AnyLeft)) m_dialog.focus = 0;
        if (Repeat(down, held, HidNpadButton_AnyRight)) m_dialog.focus = 1; // a disabled action falls back when drawn
    }
    const bool declined = (down & HidNpadButton_B) || ((down & HidNpadButton_A) && m_dialog.focus == 0);
    if (declined) {
        if (m_dialog.kind == DialogKind::Resume) {
            // discarded: every placeholder of the journal goes, and the journal
            if (m_resume) DiscardResume(*m_resume);
            m_resume.reset();
            Notice(TrM().rs_discarded);
        } else if (m_dialog.kind == DialogKind::SaveCopyAsk || m_dialog.kind == DialogKind::ModsAsk) {
            m_queue->Answer(false);
        } else if (m_dialog.kind == DialogKind::Push) {
            for (const auto& id : m_dialog.push) m_push->Answer(id, false);
        } else if (m_dialog.kind == DialogKind::Share) {
            m_prefs.share_installed = 0;
            m_prefs.Save();
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
            m_queue->Remove(m_dialog.entry); // a waiting one goes; the one installing is cancelled
            break;
        case DialogKind::Resume:
            if (m_resume) AddJobs(ResumeJobs(*m_resume, m_link.CurrentConfig().key));
            m_resume.reset();
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
        case DialogKind::ShopRemove:
            if (m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size())) {
                m_shops.erase(m_shops.begin() + m_dialog.entry);
                SaveShops(ShopsPath(), m_shops);
            }
            break;
        case DialogKind::VaultRestore: {
            const Config c = m_link.CurrentConfig();
            if (m_vault && c.Linked() && m_dialog.game >= 0 && m_dialog.game < int(m_vault_saves.size()) && !m_vault_users.empty()) {
                m_vault->StartRestore(c.url, c.key, m_vault_users[m_vault_user], m_vault_saves[m_dialog.game].app_id, m_dialog.save,
                                      TrM().before_restore);
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
        default: break;
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
