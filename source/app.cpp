// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include "i18n.hpp"
#include "net.hpp"

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
enum SettingRow { kRowTheme, kRowLibrary, kRowForget, kRowAddress, kRowKey, kRowSpeed, kSettingRows };

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
    m_time = Now();
    return true;
}

void App::Exit() {
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
        case kRowSpeed:
            if (m_speed.Running()) {
                m_speed.Cancel();
            } else if (current.Linked()) {
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
        }
        return;
    }
}

void App::Update(u64 down, u64 held) {
    if (down & HidNpadButton_Plus) {
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
    ui::TopBar(vg, m_theme, int(m_tab), 0, tabs, m_sd_free.c_str());
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
        case Tab::Settings: DrawSettings(vg); break;
        default: DrawComingSoon(vg); break;
    }
    AddHints(m_hint_rects);
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
            Notice(Tr().install_soon);
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
    const float qy = 556;
    Panel(vg, t, kGridX, qy, kWidth - 2 * kGridX, 54, 12);
    AddHit(kGridX, qy, kWidth - 2 * kGridX, 54, HitKind::QueueStrip);
    const float qw = ui::Text(vg, 58, qy + 27, 16, t.muted, s.queue_empty) - 58;
    ui::Text(vg, 58 + qw + 12, qy + 27, 15, t.muted, s.queue_empty_hint);
    ui::Text(vg, 1222, qy + 28, 24, t.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    ui::HintBar(vg, t, where.c_str(),
                {{ui::glyph::A, s.view, HidNpadButton_A}, {ui::glyph::ZR, s.filter, HidNpadButton_ZR},
                 {ui::glyph::Y, s.search, HidNpadButton_Y}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                m_hint_rects);
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
    if (m_sd_free_bytes >= 0 && !m_to_nand) {
        char line[128];
        const u64 left = u64(m_sd_free_bytes) > picked_size ? u64(m_sd_free_bytes) - picked_size : 0;
        std::snprintf(line, sizeof(line), s.space_line, m_sd_free.c_str(), SizeText(left).c_str());
        ui::Text(vg, rx, dy + 72, 15, t.muted, line);
    }

    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.select, HidNpadButton_A}, {ui::glyph::L, s.sd_card, HidNpadButton_L},
                 {ui::glyph::R, s.internal, HidNpadButton_R}, {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawQueue(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    // nothing is installed yet: the queue fills from the install step
    const bool has_jobs = false;
    if (!has_jobs) {
        ui::Text(vg, kWidth / 2, 320, 22, t.text, s.queue_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::Text(vg, kWidth / 2, 356, 16, t.muted, s.queue_empty_hint, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    const float frac = float(std::fmod(m_time * 0.02, 1.0));

    // The game being installed, with its four stages.
    const float px = 48, py = 98, pw = kWidth - 96, ph = 236;
    Panel(vg, t, px, py, pw, ph, 16);
    ui::Cover(vg, t, px + 24, py + 24, 120, 0x2e7d6b, "N", nullptr, false, false, 10);
    const float cx = px + 168;
    const float nw = ui::Text(vg, cx, py + 40, 24, t.text, "Nebula Racers") - cx;
    ui::Text(vg, cx + nw + 14, py + 41, 16, t.muted, s.base_update_sd);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d %%", int(frac * 100));
    ui::Text(vg, px + pw - 24, py + 40, 26, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    char eta[96];
    const int left_s = int((1 - frac) * 240);
    std::snprintf(eta, sizeof(eta), "%.1f / 7.0 GB · %s %d min %02d s", frac * 7.0, s.left, left_s / 60, left_s % 60);
    ui::Text(vg, cx, py + 70, 16, t.muted, eta);
    ui::Progress(vg, t, cx, py + 88, pw - 168 - 24, 12, frac, t.work, m_time);

    struct Lane { const char* title; NVGcolor color; const char* value; const char* unit; float meter; };
    char v_net[16], v_dec[16], v_wr[16], v_hash[16], dec_title[64], hash_unit[32];
    const float wob = float(std::sin(m_time * 1.7));
    std::snprintf(v_net, sizeof(v_net), "%d", 86 + int(wob * 6));
    std::snprintf(v_dec, sizeof(v_dec), "%d", 142 + int(wob * 9));
    std::snprintf(v_wr, sizeof(v_wr), "%d", 91 + int(wob * 4));
    const int parts = std::min(1 + int(frac * 5), 5);
    std::snprintf(v_hash, sizeof(v_hash), "%d", parts);
    std::snprintf(dec_title, sizeof(dec_title), "%s · 3 %s", s.decompress, s.threads);
    std::snprintf(hash_unit, sizeof(hash_unit), "%s 5", s.parts_of);
    const Lane lanes[] = {
        {s.network, t.work, v_net, "MB/s", 0.72f + wob * 0.05f},
        {dec_title, t.violet, v_dec, "MB/s", -1},
        {s.write_sd, t.orange, v_wr, "MB/s", 0.64f - wob * 0.05f},
        {s.check_sha, t.ok, v_hash, hash_unit, parts / 5.f},
    };
    const float lw = (pw - 168 - 24 - 3 * 14) / 4;
    for (int i = 0; i < 4; i++) {
        const float lx = cx + i * (lw + 14), ly = py + 118;
        ui::FillRound(vg, lx, ly, lw, 92, 12, t.panel2);
        ui::StrokeRound(vg, lx + .5f, ly + .5f, lw - 1, 91, 12, t.line, 1);
        nvgBeginPath(vg);
        nvgCircle(vg, lx + 18, ly + 20, 5);
        nvgFillColor(vg, lanes[i].color);
        nvgFill(vg);
        ui::Text(vg, lx + 30, ly + 20, 14, t.muted, lanes[i].title);
        const float vw = ui::Text(vg, lx + 14, ly + 48, 22, t.text, lanes[i].value) - (lx + 14);
        ui::Text(vg, lx + 14 + vw + 6, ly + 50, 14, t.muted, lanes[i].unit);
        if (lanes[i].meter >= 0) {
            NVGcolor track = t.text;
            track.a = 0.10f;
            ui::FillRound(vg, lx + 14, ly + 70, lw - 28, 6, 3, track);
            ui::FillRound(vg, lx + 14, ly + 70, (lw - 28) * lanes[i].meter, 6, 3, lanes[i].color);
        } else {
            // One segment per decompression thread; the fourth core is kept free.
            const float sw = (lw - 28 - 12) / 4;
            for (int k = 0; k < 4; k++) {
                NVGcolor c = lanes[i].color;
                c.a = k < 3 ? 1.f : 0.25f;
                ui::FillRound(vg, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 3, c);
            }
        }
    }

    // Waiting and done.
    float y = py + ph + 26;
    ui::Text(vg, 62, y, 13, t.muted, s.in_queue);
    y += 22;
    struct Row { const char* name; const char* initial; unsigned color; const char* info; };
    const Row rows[] = {
        {"Midnight Rhythm", "M", 0x6a4ae0, "XCZ · 3.2 GB · SD"},
        {"Secret Garden · DLC", "S", 0xb8406e, "NSZ · 380 MB · SD"},
    };
    for (const auto& r : rows) {
        nvgGlobalAlpha(vg, 0.55f);
        ui::Cover(vg, t, 62, y, 44, r.color, r.initial, nullptr, false, false, 8);
        ui::Text(vg, 122, y + 22, 17, t.text, r.name);
        ui::Text(vg, 1010, y + 22, 15, t.muted, r.info, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, 1218, y + 22, 15, t.muted, s.waiting, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
        y += 54;
    }
    y += 12;
    ui::Text(vg, 62, y + 8, 13, t.muted, s.done);
    y += 30;
    ui::Cover(vg, t, 62, y, 44, 0x3a6fc8, "F", nullptr, false, false, 8);
    ui::Text(vg, 122, y + 22, 17, t.text, "Frozen Peaks");
    ui::Text(vg, 1010, y + 22, 15, t.muted, "NSP · 1.9 GB · SD · 42 s", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    const float cw = ui::TextWidth(vg, 15, s.checked);
    ui::Text(vg, 1218, y + 22, 15, t.ok, s.checked, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgBeginPath(vg);
    nvgCircle(vg, 1218 - cw - 14, y + 22, 9);
    nvgFillColor(vg, t.ok);
    nvgFill(vg);
    ui::CheckMark(vg, 1218 - cw - 14, y + 22, 10, t.panel, 2.2f);

    ui::HintBar(vg, t, s.no_sleep,
                {{ui::glyph::Y, s.pause, HidNpadButton_Y}, {ui::glyph::X, s.remove, HidNpadButton_X},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
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
        {s.library, library, linked},
        {s.forget, "", false},
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
        const bool dim = (i == kRowForget || i == kRowSpeed) && !linked;
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

void App::DrawComingSoon(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    ui::Spinner(vg, kWidth / 2, 330, 18, t.work, m_time);
    ui::Text(vg, kWidth / 2, 384, 20, t.muted, s.coming_soon, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
}

} // namespace nxdock
