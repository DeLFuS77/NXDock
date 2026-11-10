// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "app.hpp"

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

// The format shown on a cover: that of the game, or of what there is.
std::string FormatOf(const Game& game) {
    const CatalogFile* base = game.Base();
    if (base) return Upper(base->format);
    return game.files.empty() ? "" : Upper(game.files.front().format);
}

// Library grid layout.
constexpr int kColumns = 7;
constexpr float kGridX = 36, kGridTop = 150, kGridGap = 20;
constexpr float kCoverSize = (kWidth - 2 * kGridX - kGridGap * (kColumns - 1)) / kColumns;
constexpr float kRowPitch = kCoverSize + kGridGap;
constexpr float kGridViewH = 2 * kRowPitch - kGridGap; // two full rows in sight
constexpr float kGridMargin = 10;                      // room for the focus glow, less than the gap

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
enum SettingRow { kRowTheme, kRowLibrary, kRowForget, kRowShare, kRowAddress, kRowKey, kRowSpeed, kSettingRows };

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
    m_queue = std::make_unique<InstallQueue>();
    m_queue->SetOptions(m_prefs.connections, m_prefs.download_test);
    m_push = std::make_unique<Push>();
    m_safety = std::make_unique<Safety>();
    m_time = Now();
    return true;
}

void App::Exit() {
    // a job running is cancelled and cleaned up before the network goes
    m_queue.reset();
    m_push.reset();
    m_speed.Cancel();
    m_link.Stop();
    m_covers.Stop();
    m_covers.Free(m_gfx.vg());
    m_gfx.Exit();
    net::Exit();
}

void App::ActivateSetting(int row) {
    const auto& s = Tr();
    const Config current = m_link.CurrentConfig();
    switch (row) {
        case kRowTheme:
            m_theme = m_theme.dark ? LightTheme() : DarkTheme();
            m_link.SaveTheme(!m_theme.dark);
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
    if (down & HidNpadButton_Y) {
        m_link.SetRemember(!v.remember);
    }
}

bool App::Repeat(u64 down, u64 held, u64 mask) {
    if (down & mask) {
        m_held_dir = down & mask;
        m_next_repeat = m_time + 0.35;
        return true;
    }
    if ((held & m_held_dir) && (m_held_dir & mask) && m_time >= m_next_repeat) {
        m_next_repeat = m_time + 0.09;
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
    if (state.count > 0) {
        const float x = state.touches[0].x, y = state.touches[0].y;
        if (!m_touching) {
            m_touching = true;
            m_dragging = false;
            m_touch_x0 = x;
            m_touch_y0 = y;
            m_drag_scroll0 = m_scroll;
        }
        m_touch_x = x;
        m_touch_y = y;
        const bool in_grid = m_tab == Tab::Library && m_touch_y0 >= kGridTop - 10 && m_touch_y0 < kGridTop + kGridViewH;
        if (!m_dragging && std::fabs(y - m_touch_y0) > kDragSlop && in_grid) {
            m_dragging = true;
        }
        if (m_dragging) {
            // The list follows the finger.
            m_scroll = m_scroll_target = std::clamp(m_drag_scroll0 - (y - m_touch_y0), 0.f, MaxScroll());
        }
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
            case HitKind::Dest: m_to_nand = it->index == 1; break;
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
            case HitKind::DialogBack: down |= HidNpadButton_B; break; // as B: each question answers its own way
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
    if (m_detail && m_tab == Tab::Library) {
        if (m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
            m_detail = false;
        } else {
            UpdateDetail(down, held);
            return;
        }
    }
    if (down & HidNpadButton_L) {
        m_tab = Tab((int(m_tab) + 3) % 4);
    }
    if (down & HidNpadButton_R) {
        m_tab = Tab((int(m_tab) + 1) % 4);
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
        case Tab::Settings:
            if (Repeat(down, held, up) && m_setting > 0) m_setting--;
            if (Repeat(down, held, dn) && m_setting < kSettingRows - 1) m_setting++;
            if (down & HidNpadButton_A) ActivateSetting(m_setting);
            if (down & HidNpadButton_X) ActivateSetting(kRowSpeed);
            break;
        default:
            break;
    }
    if ((down & HidNpadButton_B) && m_tab != Tab::Library) {
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
    ReadTouch(down);
    Update(down, padGetButtons(&m_pad));
    FollowLink();
    FollowQueue();
    FollowPush();
    CheckSdSpace();

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

void App::AddHints(const std::vector<ui::HitRect>& rects) {
    for (const auto& r : rects) {
        m_hits.push_back({r, HitKind::Button, 0});
    }
}

void App::Draw(NVGcontext* vg) {
    m_hits.clear();
    m_hint_rects.clear();
    Background(vg, m_theme);
    ui::HitRect tabs[4];
    int queued = 0;
    if (m_queue) {
        queued = m_queue->Waiting() + (m_queue->GetLive().running ? 1 : 0);
    }
    ui::TopBar(vg, m_theme, int(m_tab), queued, tabs, m_sd_free.c_str());
    for (int i = 0; i < 4; i++) {
        m_hits.push_back({tabs[i], HitKind::Tab, i});
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
        case Tab::Settings: DrawSettings(vg); break;
    }
    AddHints(m_hint_rects);
    if (m_dialog.kind != DialogKind::None) {
        DrawDialog(vg);
    }
    if (m_time < m_notice_until && !m_notice.empty()) {
        // a short notice above the hint bar
        const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48;
        const float x = (kWidth - w) / 2, y = 600;
        nvgGlobalAlpha(vg, float(std::min(1.0, (m_notice_until - m_time) * 3)));
        ui::FillRound(vg, x, y, w, 44, 22, m_theme.panel2);
        ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, 43, 22, m_theme.work, 1.5f);
        ui::Text(vg, x + w / 2, y + 22, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    DrawTouchFeedback(vg);
}

void App::DrawTouchFeedback(NVGcontext* vg) {
    // While a finger rests on something, light it up so it is clear what will be pressed.
    if (!m_touching || m_dragging) {
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
    if (m_catalog.Version() != m_games_version) {
        m_games_version = m_catalog.Version();
        m_games = m_catalog.Games();
        RebuildView();
    }
}

void App::RebuildView() {
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
    m_focus = std::clamp(m_focus, 0, std::max(0, int(m_view.size()) - 1));
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
    KeepFocusVisible();
}

void App::UpdateLibrary(u64 down, u64 held) {
    const u64 left = HidNpadButton_AnyLeft, right = HidNpadButton_AnyRight;
    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;
    const int count = int(m_view.size());
    const int before = m_focus;
    if (Repeat(down, held, left) && m_focus % kColumns > 0) m_focus--;
    if (Repeat(down, held, right) && m_focus % kColumns < kColumns - 1 && m_focus + 1 < count) m_focus++;
    if (Repeat(down, held, up) && m_focus >= kColumns) m_focus -= kColumns;
    if (Repeat(down, held, dn) && m_focus + kColumns < count) m_focus += kColumns;
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
        auto jobs = PendingUpdates();
        if (!jobs.empty()) {
            // every update goes where its game is; the question lists where, the internal memory too
            m_sd_checked = -100;
            CheckSdSpace();
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::UpdateAll;
            m_dialog.jobs = std::move(jobs);
        }
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

void App::OpenDetail(int view_index) {
    if (view_index < 0 || view_index >= int(m_view.size())) {
        return;
    }
    m_detail = true;
    m_detail_game = m_view[view_index];
    m_detail_focus = 0;
    m_to_nand = false;
    // what is missing is marked: the game if not installed, a newer update, the DLC not installed
    const Game& g = m_games[m_detail_game];
    m_detail_pick.assign(g.files.size(), 0);
    const CatalogFile* newest = g.NewestUpdate();
    for (size_t i = 0; i < g.files.size(); i++) {
        const CatalogFile& f = g.files[i];
        switch (f.kind) {
            case CatalogFile::Kind::Game: m_detail_pick[i] = !g.installed.base; break;
            case CatalogFile::Kind::Update: m_detail_pick[i] = &f == newest && f.version > g.installed.patch; break;
            case CatalogFile::Kind::Dlc: m_detail_pick[i] = !g.installed.dlc.count(f.title_id); break;
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
    if (down & (HidNpadButton_L | HidNpadButton_ZL)) m_to_nand = false;
    if (down & (HidNpadButton_R | HidNpadButton_ZR)) m_to_nand = true;
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

    // Grid of covers, clipped to its viewport and scrolled.
    nvgSave(vg);
    nvgScissor(vg, 0, kGridTop - kGridMargin, kWidth, kGridViewH + 2 * kGridMargin);
    for (int i = 0; i < int(m_view.size()); i++) {
        const float cx = kGridX + (i % kColumns) * (kCoverSize + kGridGap);
        const float cy = kGridTop + (i / kColumns) * kRowPitch - m_scroll;
        if (cy + kCoverSize < kGridTop - kGridMargin || cy > kGridTop + kGridViewH + kGridMargin) {
            continue;
        }
        if (i == m_focus) {
            ui::Focus(vg, t, cx, cy, kCoverSize, kCoverSize, 10, m_time);
        }
        const Game& g = m_games[m_view[i]];
        const std::string format = FormatOf(g);
        ui::Cover(vg, t, cx, cy, kCoverSize, PlaceholderColor(g.key), Initial(g.name).c_str(), format.c_str(),
                  g.installed.base, g.UpdateAvailable(), 10, m_covers.Get(g.cover));
        // Only the visible part of a cover can be tapped.
        const float top = std::max(cy, kGridTop - kGridMargin), bottom = std::min(cy + kCoverSize, kGridTop + kGridViewH + kGridMargin);
        if (bottom > top) {
            AddHit(cx - kGridGap / 2, top, kCoverSize + kGridGap, bottom - top, HitKind::Cover, i);
        }
    }
    nvgRestore(vg);
    if (m_view.empty()) {
        ui::Text(vg, kWidth / 2, kGridTop + kGridViewH / 2, 18, t.muted, s.empty_filter, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // Scroll indicator when there are more rows than fit.
    if (MaxScroll() > 0) {
        const float track_h = kGridViewH;
        const float thumb_h = track_h * kGridViewH / (kGridViewH + MaxScroll());
        const float thumb_y = kGridTop + (track_h - thumb_h) * (m_scroll / MaxScroll());
        NVGcolor c = t.muted;
        c.a = 0.45f;
        ui::FillRound(vg, kWidth - 16, thumb_y, 4, thumb_h, 2, c);
    }

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
    const size_t updates = PendingUpdates().size();
    if (updates) {
        m_update_hint = std::string(s.update_all) + " · " + std::to_string(updates);
        hints.push_back({ui::glyph::Minus, m_update_hint.c_str(), HidNpadButton_Minus});
    }
    hints.push_back({ui::glyph::Plus, s.exit, HidNpadButton_Plus});
    ui::HintBar(vg, t, where.c_str(), hints, m_hint_rects);
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
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx + 4, cy + 14, cs - 8, cs, 14);
    nvgFillPaint(vg, nvgBoxGradient(vg, cx, cy + 14, cs, cs, 14, 30, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 14, m_covers.Get(g.cover));

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
    if (g.installed.base) {
        facts.push_back({s.on_console, g.installed.patch ? "v" + std::to_string(g.installed.patch) : std::string("v0"), t.ok});
    } else {
        facts.push_back({s.on_console, "—", t.muted});
    }
    float fx = rx;
    const float fy = cy + 62;
    for (const auto& f : facts) {
        const float w = std::max(ui::TextWidth(vg, 14, f.label), ui::TextWidth(vg, 17, f.value.c_str())) + 28;
        ui::FillRound(vg, fx, fy, w, 56, 10, Hex(0, t.dark ? 0.25f : 0.04f));
        ui::StrokeRound(vg, fx + .5f, fy + .5f, w - 1, 55, 10, t.line, 1);
        ui::Text(vg, fx + 14, fy + 18, 14, t.muted, f.label);
        ui::Text(vg, fx + 14, fy + 39, 17, f.color, f.value.c_str());
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
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
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
    const u32 hos = hosversionGet();
    const u32 console = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    int picked = 0;
    std::vector<InstallJob> jobs;
    std::string firmware_note;
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
            if (f.required_system && (f.required_system & 0xFFFF0000) > console) {
                char text[160];
                std::snprintf(text, sizeof(text), s.needs_firmware, label.c_str(), FirmwareText(f.required_system).c_str());
                firmware_note = text;
                continue;
            }
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
    if (!firmware_note.empty()) {
        Notice(firmware_note);
        return;
    }
    if (jobs.empty()) {
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
    job.cover = g.cover;
    job.color_key = g.key;
    job.file_id = f.id;
    job.url = c.url + f.url;
    job.key = c.key;
    job.format = f.format;
    job.kind = f.kind;
    job.title_id = f.title_id;
    job.size = f.size;
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
    if (m_dialog.kind != DialogKind::None) {
        return;
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
    if (live.phase >= 3) {
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
        if (live.phase >= 3) {
            std::snprintf(eta, sizeof(eta), "%s…", s.registering);
        } else if (live.total == 0) {
            std::snprintf(eta, sizeof(eta), "%s… · %s", s.preparing, Duration(live.elapsed).c_str());
        } else {
            const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
            std::snprintf(eta, sizeof(eta), "%s / %s%s%s", SizeText(live.written).c_str(), SizeText(live.total).c_str(),
                          left >= 0 ? (std::string(" · ") + s.left + " ").c_str() : "", left >= 0 ? Duration(left).c_str() : "");
        }
        ui::Text(vg, cx, py + 70, 16, t.muted, eta);
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
    const float list_top = y, list_bottom = 640;
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
        if (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled) {
            // why, in place of the figures
            nvgSave(vg);
            nvgScissor(vg, 730, y, 340, 44);
            ui::Text(vg, 1070, y + 22, 14, j.state == InstallJob::State::Failed ? t.err : t.muted, j.error.c_str(),
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
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, m_prefs.download_test ? s.download_test : (running ? s.no_sleep : s.queue_done_hint), hints, m_hint_rects);
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
        {s.library, library, linked},
        {s.forget, "", false},
        {s.share_row, m_prefs.share_installed == 1 ? s.yes_word : s.no_word, m_prefs.share_installed == 1},
        {s.manual_address, address, !m_manual_url.empty()},
        {s.manual_key, linked ? s.key_saved : s.not_set, linked},
        {s.speed_test, running ? s.cancel : s.start, linked},
    };
    const float x = 48, w = 540, h = 62, gap = 10;
    float y = 96;
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
        AddHit(x, y, w, h, HitKind::SettingsRow, i);
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

    const float cy = top + 150;
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

    // What happens now.
    const float sy = cy + bh + 40;
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
            ui::Text(vg, 134, y + 47, 14, t.err, i.error.c_str()); // why it could not be removed
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
            ui::Text(vg, c_name, cy + 12, 13, t.err, e.error.c_str());
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
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::DialogBack);

    struct Line {
        std::string text, tag, right;
        NVGcolor color;
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
                    error = InstallErrorText(v.rc, 0, 0, -1);
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
        case DialogKind::Share:
            title = s.share_title;
            body = s.share_body;
            primary = s.share_yes;
            cancel_label = s.no_word;
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
    const float err_h = error.empty() ? 0 : TextBoxHeight(vg, iw, 15, error.c_str()) + 14;
    h += body_h + list_h + (bar ? 64 : 0) + (verify_bar ? 54 : 0) + err_h + note_h + 60 + pad - 8;
    const float x = (kWidth - w) / 2, y = std::max(80.f, (kHeight - 56 - h) / 2);
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
            if (i > 0) {
                nvgBeginPath(vg);
                nvgRect(vg, x + pad + 14, ly - 20, iw - 28, 1);
                nvgFillColor(vg, t.line);
                nvgFill(vg);
            }
            const float rw = ui::TextWidth(vg, 15, l.right.c_str());
            float tx = x + pad + 16;
            nvgSave(vg);
            nvgScissor(vg, tx, ly - 20, iw - 32 - rw - 140, 40);
            tx = ui::Text(vg, tx, ly, 17, m_dialog.kind == DialogKind::HistoryInfo ? t.muted : t.text, l.text.c_str());
            const bool colored = m_dialog.kind == DialogKind::HistoryInfo || m_dialog.kind == DialogKind::Verify;
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
        TextBox(vg, x + pad, cy, iw, 15, t.err, error.c_str());
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
    if (!info) buttons.push_back({ui::glyph::A, primary, true, 1});
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const float bw = ui::TextWidth(vg, 18, it->label.c_str()) + 78;
        bx -= bw;
        const bool enabled = !it->primary || can;
        nvgGlobalAlpha(vg, enabled ? 1.f : 0.4f);
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
        ui::Text(vg, bx + 22, by + bh / 2 + 1, 24, fg, it->glyph, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, bx + 56, by + bh / 2, 18, fg, it->label.c_str());
        nvgGlobalAlpha(vg, 1.f);
        if (enabled) {
            AddHit(bx, by, bw, bh, HitKind::DialogButton, it->index);
        }
        bx -= 14;
    }

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
    if (!info) {
        if (Repeat(down, held, HidNpadButton_AnyLeft)) m_dialog.focus = 0;
        if (Repeat(down, held, HidNpadButton_AnyRight)) m_dialog.focus = 1; // a disabled action falls back when drawn
    }
    const bool declined = (down & HidNpadButton_B) || ((down & HidNpadButton_A) && m_dialog.focus == 0);
    if (declined) {
        if (m_dialog.kind == DialogKind::Push) {
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
        case DialogKind::Push:
            for (const auto& id : m_dialog.push) m_push->Answer(id, true);
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Risk:
            m_risk_ok = true;
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

} // namespace nxdock
