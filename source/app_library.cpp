// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The library: its grid, filters and orders, the A-Z bar, the menu, the game in focus and the quick menu.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

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

// The orders of the library, in Options: A-Z, size, not installed first, recent.
constexpr int kSorts = 4;

// The filters of the library: all, not installed, installed, with an update, with DLC.
constexpr int kFilters = 5;

constexpr float kGridMargin = 10;                      // room for the focus glow, less than the gap

// The panel of the game in focus (2B), on the right of the covers.
constexpr float kPanelX = 884, kPanelW = kWidth - kPanelX - 36;

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

void App::KeepFocusVisible() {
    const float row_top = (m_focus / g_columns) * g_row_pitch;
    if (row_top < m_scroll_target) {
        m_scroll_target = row_top;
    } else if (row_top + g_cover > m_scroll_target + g_view_h) {
        m_scroll_target = row_top + g_cover - g_view_h;
    }
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
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
            m_lower_names[i] = AsciiLower(m_games[i].name);
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
    const std::string search = AsciiLower(m_search);
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
    if (down & HidNpadButton_Plus) {
        // Options (5A): the order and Update all; with updates to install, the focus is on it.
        // + opens the menus of every screen (1A); NXDock is left with HOME
        m_menu = true;
        m_menu_at = m_time;
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
    OpenDialog(DialogKind::UpdateAll);
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
    } else if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
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
    nvgSave(vg);
    const float alpha = GrowFromPlus(vg, m_menu_at, x, y, w, h);
    draw::Popup(vg, t, x, y, w, h, 16);
    AddHit(x, y, w, h, HitKind::Button); // a tap on the panel itself does nothing
    ui::Text(vg, x + 22, y + pad + 17, 14, t.muted, s.options);
    const char* sorts[] = {s.sort_az, s.sort_size, s.sort_missing, s.sort_recent};
    float ry = y + pad + 34;
    for (int i = 0; i < rows; i++) {
        nvgSave(vg);
        RowComesIn(vg, m_menu_at, i, alpha);
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
        nvgRestore(vg);
    }
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.choose, HidNpadButton_A}};
    if (updates) hints.push_back({ui::glyph::Minus, s.update_all, HidNpadButton_Minus});
    hints.push_back({ui::glyph::B, s.close, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, rects);
    AddHints(rects);
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
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, m.lb_reload, HidNpadButton_X}},
                        m_hint_rects);
            HomeToLeave(vg);
            return;
        }
        if (state == Catalog::State::Failed) {
            // the card of an error, as in the queue: what happened, what to do, the code small
            const std::string error = m_catalog.Error();
            const float cw = 700, ch = ErrorCard(vg, t, 0, 0, cw, error, false);
            ErrorCard(vg, t, (kWidth - cw) / 2, std::max(96.f, 360 - ch / 2), cw, error, true);
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, s.retry, HidNpadButton_X}},
                        m_hint_rects);
            HomeToLeave(vg);
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
            ui::HintBar(vg, t, where.c_str(), {{ui::glyph::X, s.cancel, HidNpadButton_X}},
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
    hints.push_back({ui::glyph::Plus, m_update_hint.c_str(), HidNpadButton_Plus});
    if (m_shop_active) hints.push_back({ui::glyph::B, TrM().sh_shops_hint, HidNpadButton_B});
    ui::HintBar(vg, t, status.c_str(), hints, m_hint_rects);
}

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
    const auto& jobs = QueueJobs();
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
                OpenDialog(DialogKind::Verify);
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

} // namespace nxdock
