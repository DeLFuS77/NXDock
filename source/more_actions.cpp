// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The menu of actions of + on every screen that has more than one thing to do (1A of the visual
// test of the same buttons everywhere, 14/11): the page of a game in the library, In the console
// (the list and the page of a game), This session, the history, the queue, the shops, the games to
// archive and the games to compress. As in Files and Saves, − opens it too, it rises from the + of
// the bar, and every row with a button of its own shows that button at its right (2A), so the
// shortcuts are learnt from it. Deleting is always the first row and always asks first.
#include "app.hpp"

#include "console_order.hpp"
#include "file_match.hpp"
#include "mtp.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <algorithm>
#include <cstdio>

namespace nxdock {

std::vector<InstallJob> QueueAsShown(std::vector<InstallJob> jobs); // app.cpp
bool CanUndo(const SessionItem& item);                              // app.cpp
std::vector<InstalledApp::Content> ConsoleItems(const InstalledApp& a); // more_console.cpp
void FileActionIcon(NVGcontext* vg, NVGcolor c, int action, float x, float cy); // more_files.cpp

namespace {

constexpr float kActW = 470, kActRow = 56;

// What a row does when it has no button of its own (kButton: as its button)
enum : int { kButton, kRemovePicked, kRemoveMarked, kMarkAll, kUnmarkAll, kMissing, kShortcuts, kSize, kScreenOff, kShopRemove, kVersions, kClearHistory,
             kOrphans, kSort, kPlay, kCheck, kRepair, kInstallBase, kTicketsDelete, kTicketsOrphans, kInvert, kRandom, kViewWrap, kViewEnd,
             kViewTop, kViewClose, kUpdateShop, kUsb3, kCompress, kCompressAll, kCzGo, kCzFilter, kCzUnqueue, kSigpatchInfo, kSigpatchOff,
             kSendReport, kHidden, kHomeBack, kSortBase = 100 };

// The pictures: 0 to 9 those of the menu of files (install, list, copy, cut, paste, cross, rename,
// folder, i, bin); then a box ticked, an empty box, pause, play, the screen off, again, a shield,
// a card, an arrow, a plus, a funnel, a grid, a spanner, a box half filled, a die, lines wrapped, arrows to
// the end and to the top, two arrows that press together (compress).
enum : int { kIconInstall = 0, kIconList = 1, kIconCross = 5, kIconEdit = 6, kIconInfo = 8, kIconBin = 9, kIconTick = 20, kIconBox,
             kIconPause, kIconPlay, kIconScreen, kIconAgain, kIconShield, kIconCard, kIconOpen, kIconPlus, kIconFunnel, kIconGrid, kIconMods,
             kIconInvert, kIconDie, kIconWrap, kIconEnd, kIconTop, kIconPress };

void Icon(NVGcontext* vg, NVGcolor c, int icon, float x, float cy) {
    if (icon < 20) {
        FileActionIcon(vg, c, icon, x, cy);
        return;
    }
    nvgStrokeColor(vg, c);
    nvgFillColor(vg, c);
    nvgStrokeWidth(vg, 2.2f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    switch (icon) {
        case kIconTick:
            ui::StrokeRound(vg, x + 1, cy - 9, 18, 18, 4, c, 2.2f);
            nvgMoveTo(vg, x + 5, cy);
            nvgLineTo(vg, x + 9, cy + 4);
            nvgLineTo(vg, x + 15, cy - 4);
            nvgStroke(vg);
            break;
        case kIconBox:
            ui::StrokeRound(vg, x + 1, cy - 9, 18, 18, 4, c, 2.2f);
            break;
        case kIconPause:
            ui::FillRound(vg, x + 4, cy - 8, 4, 16, 1.5f, c);
            ui::FillRound(vg, x + 12, cy - 8, 4, 16, 1.5f, c);
            break;
        case kIconPlay:
            nvgMoveTo(vg, x + 5, cy - 9);
            nvgLineTo(vg, x + 17, cy);
            nvgLineTo(vg, x + 5, cy + 9);
            nvgClosePath(vg);
            nvgFill(vg);
            break;
        case kIconScreen:
            ui::StrokeRound(vg, x, cy - 8, 20, 14, 3, c, 2.2f);
            nvgMoveTo(vg, x + 6, cy + 10);
            nvgLineTo(vg, x + 14, cy + 10);
            nvgStroke(vg);
            break;
        case kIconAgain:
            nvgArc(vg, x + 10, cy, 8, -2.6f, 1.9f, NVG_CW);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 1, cy - 10);
            nvgLineTo(vg, x + 3, cy - 3);
            nvgLineTo(vg, x + 9, cy - 6);
            nvgStroke(vg);
            break;
        case kIconShield:
            nvgMoveTo(vg, x + 10, cy - 10);
            nvgLineTo(vg, x + 18, cy - 6);
            nvgLineTo(vg, x + 17, cy + 3);
            nvgLineTo(vg, x + 10, cy + 10);
            nvgLineTo(vg, x + 3, cy + 3);
            nvgLineTo(vg, x + 2, cy - 6);
            nvgClosePath(vg);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 6, cy);
            nvgLineTo(vg, x + 9, cy + 3);
            nvgLineTo(vg, x + 14, cy - 3);
            nvgStroke(vg);
            break;
        case kIconCard:
            nvgMoveTo(vg, x + 3, cy - 10);
            nvgLineTo(vg, x + 13, cy - 10);
            nvgLineTo(vg, x + 17, cy - 6);
            nvgLineTo(vg, x + 17, cy + 10);
            nvgLineTo(vg, x + 3, cy + 10);
            nvgClosePath(vg);
            nvgStroke(vg);
            break;
        case kIconOpen:
            nvgMoveTo(vg, x + 2, cy);
            nvgLineTo(vg, x + 16, cy);
            nvgMoveTo(vg, x + 10, cy - 6);
            nvgLineTo(vg, x + 16, cy);
            nvgLineTo(vg, x + 10, cy + 6);
            nvgStroke(vg);
            break;
        case kIconPlus:
            nvgMoveTo(vg, x + 10, cy - 8);
            nvgLineTo(vg, x + 10, cy + 8);
            nvgMoveTo(vg, x + 2, cy);
            nvgLineTo(vg, x + 18, cy);
            nvgStroke(vg);
            break;
        case kIconFunnel:
            nvgMoveTo(vg, x + 1, cy - 8);
            nvgLineTo(vg, x + 19, cy - 8);
            nvgLineTo(vg, x + 12, cy);
            nvgLineTo(vg, x + 12, cy + 9);
            nvgLineTo(vg, x + 8, cy + 6);
            nvgLineTo(vg, x + 8, cy);
            nvgClosePath(vg);
            nvgStroke(vg);
            break;
        case kIconGrid:
            for (int i = 0; i < 4; i++) ui::StrokeRound(vg, x + 1 + (i % 2) * 10, cy - 9 + (i / 2) * 10, 8, 8, 2, c, 2.f);
            break;
        case kIconMods:
            nvgCircle(vg, x + 13, cy - 4, 5);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 9, cy);
            nvgLineTo(vg, x + 2, cy + 8);
            nvgStroke(vg);
            break;
        case kIconInvert:
            ui::StrokeRound(vg, x + 1, cy - 9, 18, 18, 4, c, 2.2f);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 4, cy + 6);
            nvgLineTo(vg, x + 16, cy - 6);
            nvgLineTo(vg, x + 16, cy + 6);
            nvgClosePath(vg);
            nvgFill(vg);
            break;
        case kIconPress:
            // two arrows pressing a bar from above and below
            nvgMoveTo(vg, x + 10, cy - 10);
            nvgLineTo(vg, x + 10, cy - 3);
            nvgMoveTo(vg, x + 6, cy - 6.5f);
            nvgLineTo(vg, x + 10, cy - 2.5f);
            nvgLineTo(vg, x + 14, cy - 6.5f);
            nvgMoveTo(vg, x + 10, cy + 10);
            nvgLineTo(vg, x + 10, cy + 3);
            nvgMoveTo(vg, x + 6, cy + 6.5f);
            nvgLineTo(vg, x + 10, cy + 2.5f);
            nvgLineTo(vg, x + 14, cy + 6.5f);
            nvgMoveTo(vg, x + 1, cy);
            nvgLineTo(vg, x + 19, cy);
            nvgStroke(vg);
            break;
        case kIconDie:
            ui::StrokeRound(vg, x + 1, cy - 9, 18, 18, 4, c, 2.2f);
            for (int k = 0; k < 3; k++) {
                nvgBeginPath(vg);
                nvgCircle(vg, x + 5.5f + 4.5f * float(k), cy - 4.5f + 4.5f * float(k), 1.8f);
                nvgFill(vg);
            }
            break;
        case kIconWrap:
            nvgMoveTo(vg, x + 1, cy - 7);
            nvgLineTo(vg, x + 19, cy - 7);
            nvgMoveTo(vg, x + 1, cy);
            nvgLineTo(vg, x + 15, cy);
            nvgArc(vg, x + 15, cy + 4, 4, -1.5708f, 1.5708f, NVG_CW);
            nvgLineTo(vg, x + 9, cy + 8);
            nvgMoveTo(vg, x + 12, cy + 5);
            nvgLineTo(vg, x + 9, cy + 8);
            nvgLineTo(vg, x + 12, cy + 11);
            nvgStroke(vg);
            break;
        case kIconEnd:
        case kIconTop: {
            const float d = icon == kIconEnd ? 1.f : -1.f;
            nvgMoveTo(vg, x + 10, cy - 8 * d);
            nvgLineTo(vg, x + 10, cy + 5 * d);
            nvgMoveTo(vg, x + 4, cy);
            nvgLineTo(vg, x + 10, cy + 6 * d);
            nvgLineTo(vg, x + 16, cy);
            nvgMoveTo(vg, x + 2, cy + 10 * d);
            nvgLineTo(vg, x + 18, cy + 10 * d);
            nvgStroke(vg);
            break;
        }
        default: break;
    }
}

const char* SortLabel(const More& m, int sort) {
    switch (sort) {
        case kSortSize: return m.so_size;
        case kSortLast: return m.so_last;
        case kSortTime: return m.so_time;
        case kSortWhere: return m.so_where;
        default: return m.so_name;
    }
}

const char* GlyphOf(u64 button) {
    switch (button) {
        case HidNpadButton_A: return ui::glyph::A;
        case HidNpadButton_X: return ui::glyph::X;
        case HidNpadButton_Y: return ui::glyph::Y;
        case HidNpadButton_ZR: return ui::glyph::ZR;
        case HidNpadButton_ZL: return ui::glyph::ZL;
        case HidNpadButton_B: return ui::glyph::B;
        default: return nullptr;
    }
}

} // namespace

App::ActScreen App::CurrentActScreen() {
    if (m_detail && GridTab()) {
        return m_detail_game >= 0 && m_detail_game < int(m_games.size()) ? ActScreen::Detail : ActScreen::None;
    }
    switch (m_tab) {
        case Tab::Queue:
            return m_queue && !QueueJobs().empty() ? ActScreen::Queue : ActScreen::None;
        case Tab::Installed:
            if (m_installed_view == 1) return m_queue ? ActScreen::Session : ActScreen::None;
            if (m_installed_view == 2) return ActScreen::History;
            if (!m_space) return ActScreen::None;
            return m_console_app ? ActScreen::ConsoleGame : ActScreen::ConsoleList;
        case Tab::Shops:
            return m_shop_active ? ActScreen::None : ActScreen::Shops;
        case Tab::More:
            if (m_more == MoreScreen::Shops) return ActScreen::Shops;
            if (m_more == MoreScreen::Viewer) return ActScreen::Viewer; // the text viewer (20A)
            if (m_more == MoreScreen::Mtp || m_more == MoreScreen::Usb) return ActScreen::Arrival; // USB 3.0 there
            if (m_more == MoreScreen::Space && m_space) {
                if (m_space_mode == 1 && m_pick_kind == 0) return ActScreen::SpacePick;
                if (m_space_mode == 4) return ActScreen::Tickets;
                if (m_space_mode == 3 && m_compress) return ActScreen::Compress; // also while it works (15/11)
            }
            return ActScreen::None;
        default:
            return ActScreen::None;
    }
}

bool App::ConsoleRemoveTakesGame() {
    // the saves matter when the game itself goes: several games, or the game in what is marked
    if (!m_dialog.ids.empty()) return true;
    return std::any_of(m_console_remove.begin(), m_console_remove.end(),
                       [](const InstalledApp::Content& c) { return c.type == NcmContentMetaType_Application; });
}

std::vector<App::ActRow> App::ActRows(ActScreen screen, std::string& title) {
    const auto& s = Tr();
    const More& m = TrM();
    const Tools& tt = TrT();
    std::vector<ActRow> rows;
    title = tt.fm_actions;
    char text[160];
    switch (screen) {
        case ActScreen::ConsoleList: {
            const auto v = ConsoleView(m_space->Snapshot());
            unsigned long long bytes = 0;
            int picked = 0, can = 0;
            for (const auto& a : v.apps) {
                if (m_console_picked.count(a.app_id)) {
                    bytes += a.Bytes();
                    picked++;
                }
                if (!a.archived && !a.contents.empty()) can++;
            }
            if (picked) {
                std::snprintf(text, sizeof(text), m.ic_picked_fmt, picked, draw::Size(bytes).c_str());
                title = text;
                std::snprintf(text, sizeof(text), m.ic_rm_picked_fmt, int(m_console_picked.size()));
                rows.push_back({kRemovePicked, text, draw::Size(bytes), true, 0, true, kIconBin});
            }
            if (!v.apps.empty()) {
                const auto& a = v.apps[size_t(std::clamp(m_console_focus, 0, int(v.apps.size()) - 1))];
                rows.push_back({kButton, m.ac_mark, {}, !a.archived, HidNpadButton_Y, false, m_console_picked.count(a.app_id) ? kIconTick : kIconBox});
                rows.push_back({kMarkAll, m.ac_mark_all, {}, can > picked, 0, false, kIconTick});
                rows.push_back({kUnmarkAll, m.ac_unmark, {}, !m_console_picked.empty(), 0, false, kIconBox});
                rows.push_back({kInvert, m.ac_invert, {}, can > 0, 0, false, kIconInvert}); // 16A
                // the game in focus, compressed from here (14/11)
                if (CanCompress(a)) rows.push_back({kCompress, m.cz_menu_row, {}, true, 0, false, kIconPress});
            }
            rows.push_back({kMissing, m_console_missing ? tt.ms_hint_all : tt.ms_hint_filter, {}, true, 0, false, kIconFunnel});
            // the shortcuts of homebrew on the HOME menu, hidden unless asked; kept for the next time
            rows.push_back({kShortcuts, m_ui.show_shortcuts ? m.ic_hide_shortcuts : m.ic_show_shortcuts, {}, true, 0, false, kIconFunnel});
            // every game, in Compress games (14/11)
            rows.push_back({kCompressAll, m.cz_menu_all, {}, true, 0, false, kIconPress});
            // those without their game (5C), when there are some
            {
                const auto all = m_space->Snapshot();
                const int orphans = int(std::count_if(all.apps.begin(), all.apps.end(), WithoutGame));
                if (orphans || m_console_orphans) {
                    std::snprintf(text, sizeof(text), "%d", orphans);
                    rows.push_back({kOrphans, m_console_orphans ? tt.ms_hint_all : m.og_filter, m_console_orphans ? std::string{} : text, true, 0, false,
                                    kIconFunnel});
                }
            }
            // the games the HOME menu does not show (1A), when there are some: put back, or only them
            {
                const auto all = m_space->Snapshot();
                const int hidden = int(std::count_if(all.apps.begin(), all.apps.end(), NotOnHome));
                if (hidden) {
                    std::snprintf(text, sizeof(text), "%d", hidden);
                    rows.push_back({kHomeBack, m.hb_back, text, !m_space->Busy(), 0, false, kIconAgain});
                }
                if (hidden || m_console_hidden) {
                    rows.push_back({kHidden, m_console_hidden ? tt.ms_hint_all : m.hb_filter, {}, true, 0, false, kIconFunnel});
                }
            }
            // a game at random (21A of groups 3 and 4)
            {
                const auto all = m_space->Snapshot();
                const bool any = std::any_of(all.apps.begin(), all.apps.end(), [this](const InstalledApp& a) { return ConsoleCanPlay(a); });
                rows.push_back({kRandom, m.rg_row, {}, any, 0, false, kIconDie});
            }
            // the order (4A): its five choices open in a menu of their own
            rows.push_back({kSort, m.so_sort, SortLabel(m, m_ui.console_sort), true, 0, false, kIconList});
            const char* sizes[] = {m.ic_size_list, m.ic_size_mid, m.ic_size_big};
            rows.push_back({kSize, m.ic_size, sizes[std::clamp(int(m_ui.console_size), 0, 2)], true, 0, false, kIconGrid});
            break;
        }
        case ActScreen::ConsoleGame: {
            const auto v = m_space->Snapshot();
            const InstalledApp* found = nullptr;
            for (const auto& a : v.apps) {
                if (a.app_id == m_console_app) found = &a;
            }
            if (!found) break;
            title = found->name;
            const auto items = ConsoleItems(*found);
            if (m_console_marks.size() != items.size()) m_console_marks.assign(items.size(), 0);
            int marked = 0;
            unsigned long long bytes = 0;
            for (size_t i = 0; i < items.size(); i++) {
                if (!m_console_marks[i]) continue;
                marked++;
                bytes += items[i].bytes;
            }
            const bool orphan = WithoutGame(*found);
            // play, check and repair first (1A, 2A), deleting last, as the visual test shows
            if (ConsoleCanPlay(*found)) rows.push_back({kPlay, m.pl_play, {}, true, 0, false, kIconPlay});
            if (!orphan && !found->archived) {
                const bool running = m_queue && m_queue->GetVerify().running;
                rows.push_back({kCheck, m.ck_check, draw::Size(found->Bytes()), !running, 0, false, kIconShield});
                const char* block = ConsoleRepairBlock(m_console_app);
                bool bad = false;
                if (m_queue && m_console_check == m_console_app) {
                    const auto cv = m_queue->GetVerify();
                    if (cv.done) {
                        for (const auto& c : cv.result) bad = bad || !c.bad.empty();
                    }
                }
                rows.push_back({kRepair, m.ck_repair, block ? block : std::string{}, !block && bad, 0, false, kIconAgain});
            }
            if (CanCompress(*found)) rows.push_back({kCompress, m.cz_menu_row, {}, true, 0, false, kIconPress}); // 14/11
            if (orphan) {
                rows.push_back({kInstallBase, m.og_install, {}, LibraryBase(m_console_app), HidNpadButton_X, false, kIconInstall});
            } else if (LibraryGame(m_console_app)) {
                rows.push_back({kButton, m.ic_from_library, {}, true, HidNpadButton_X, false, kIconInstall});
            }
            if (!items.empty()) {
                rows.push_back({kMarkAll, m.ac_mark_all, {}, marked < int(items.size()), 0, false, kIconTick});
                if (marked) rows.push_back({kUnmarkAll, m.ac_unmark, {}, true, 0, false, kIconBox});
                rows.push_back({kInvert, m.ac_invert, {}, true, 0, false, kIconInvert});
                rows.push_back({kButton, tt.cd_hint, {}, true, HidNpadButton_A, false, kIconInfo});
            }
            if (orphan) rows.push_back({kRemoveMarked, m.og_remove, draw::Size(found->Bytes()), true, 0, true, kIconBin});
            else rows.push_back({kRemoveMarked, m.ic_remove_marked, marked ? draw::Size(bytes) : std::string{}, marked > 0, 0, true, kIconBin});
            break;
        }
        case ActScreen::Session: {
            const auto items = m_queue->Session();
            if (items.empty()) break;
            const auto& item = items[size_t(std::clamp(m_installed_focus, 0, int(items.size()) - 1))];
            title = item.game;
            rows.push_back({kButton, s.remove_this, {}, CanUndo(item), HidNpadButton_X, true, kIconBin});
            rows.push_back({kButton, s.undo_all, {}, std::any_of(items.begin(), items.end(), CanUndo), HidNpadButton_Y, true, kIconBin});
            break;
        }
        case ActScreen::History:
            if (m_history.empty()) break;
            rows.push_back({kClearHistory, s.clear_history, {}, true, 0, true, kIconBin});
            rows.push_back({kButton, s.details, {}, true, HidNpadButton_A, false, kIconInfo});
            break;
        case ActScreen::Queue: {
            const auto jobs = QueueAsShown(m_queue->Jobs());
            if (jobs.empty()) break;
            const InstallJob& j = jobs[size_t(std::clamp(m_queue_focus, 0, int(jobs.size()) - 1))];
            title = j.game + (j.label.empty() ? "" : " \xC2\xB7 " + j.label);
            const bool running = j.state == InstallJob::State::Running;
            rows.push_back({kButton, running ? s.cancel : s.remove, {}, true, HidNpadButton_X, true, kIconCross});
            if (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled) {
                rows.push_back({kButton, s.retry, {}, true, HidNpadButton_A, false, kIconAgain});
            }
            // its error card has the button of a report (4A): here too
            const bool error_card = (j.state == InstallJob::State::Failed && !j.error.empty()) ||
                                    (j.state == InstallJob::State::Done && !j.merge_error.empty());
            if (error_card && CanSendReport()) rows.push_back({kSendReport, m.rp_button, {}, !m_lib_report_running, 0, false, kIconOpen});
            const bool paused = m_queue->Paused();
            rows.push_back({kButton, paused ? m.qw_go_on : m.qw_pause, {}, paused || m_queue->Waiting() > 0, HidNpadButton_ZR, false,
                            paused ? kIconPlay : kIconPause});
            rows.push_back({kButton, s.clear_done, {}, true, HidNpadButton_Y, false, kIconBin});
            const bool installing = std::any_of(jobs.begin(), jobs.end(), [](const InstallJob& x) { return x.state == InstallJob::State::Running; });
            rows.push_back({kScreenOff, m.so_button, {}, installing, 0, false, kIconScreen});
            break;
        }
        case ActScreen::Shops:
            if (m_shop_focus < int(m_shops.size())) {
                title = m_shops[size_t(m_shop_focus)].name;
                rows.push_back({kButton, m.open, {}, true, HidNpadButton_A, false, kIconOpen});
                rows.push_back({kButton, m.sh_edit, {}, true, HidNpadButton_Y, false, kIconEdit});
                rows.push_back({kUpdateShop, m.uh_row, {}, true, 0, false, kIconAgain}); // 15A
            }
            rows.push_back({kButton, m.sh_add, {}, true, HidNpadButton_X, false, kIconPlus});
            if (m_shop_focus < int(m_shops.size())) rows.push_back({kShopRemove, m.sh_remove, {}, true, 0, true, kIconBin});
            break;
        case ActScreen::Detail: {
            const Game& g = m_games[size_t(m_detail_game)];
            title = g.name;
            if (g.installed.base && !m_shop_active && !VersionRows(g).empty()) rows.push_back({kVersions, m.ver_hint, {}, true, 0, false, kIconList});
            if (g.installed.base || !g.installed.dlc.empty()) rows.push_back({kButton, s.verify, {}, true, HidNpadButton_Y, false, kIconShield});
            if (g.installed.base && !m_shop_active) rows.push_back({kButton, m.t_mods, {}, true, HidNpadButton_X, false, kIconMods});
            rows.push_back({kButton, m.dc_switch, m_to_nand ? s.internal : s.sd_card, true, HidNpadButton_ZR, false, kIconCard});
            if (SigpatchShown()) {
                // no sigpatches found (5B): what they are, and the warning off (Settings turns it on)
                rows.push_back({kSigpatchInfo, m.sg_menu_info, {}, true, SigpatchByY(g) ? u64(HidNpadButton_Y) : 0, false, kIconInfo});
                rows.push_back({kSigpatchOff, m.sg_menu_off, {}, true, 0, false, kIconCross});
            }
            break;
        }
        case ActScreen::SpacePick: {
            if (m_pick_rows.empty()) break;
            const int marked = int(std::count(m_pick_marks.begin(), m_pick_marks.end(), true));
            const bool on = m_pick_marks[size_t(std::clamp(m_pick_focus, 0, int(m_pick_marks.size()) - 1))];
            rows.push_back({kButton, m.ac_mark, {}, true, HidNpadButton_Y, false, on ? kIconTick : kIconBox});
            rows.push_back({kMarkAll, m.ac_mark_all, {}, marked < int(m_pick_marks.size()), 0, false, kIconTick});
            rows.push_back({kUnmarkAll, m.ac_unmark, {}, marked > 0, 0, false, kIconBox});
            rows.push_back({kInvert, m.ac_invert, {}, true, 0, false, kIconInvert});
            rows.push_back({kButton, m.archive_go, {}, marked > 0, HidNpadButton_X, false, kIconList});
            break;
        }
        case ActScreen::Compress: {
            // compress the marked or the one in focus, the marks, the queue, the filter (1A, 4A)
            const auto c = m_compress->Snapshot();
            const auto v = m_space->Snapshot();
            const auto visible = CompressVisible(v, c);
            const bool busy = c.running || !m_compress_queue.empty();
            const auto it = c.games.find(m_compress_app);
            const bool stops = it != c.games.end() && it->second.why == GameEstimate::Why::Stops;
            int markable = 0, picked = 0;
            unsigned long long bytes = 0;
            for (int r : visible) {
                const auto& a = v.apps[size_t(r)];
                if (CompressMarkable(c, a.app_id)) markable++;
                if (m_compress_picked.count(a.app_id)) {
                    picked++;
                    bytes += a.Bytes();
                }
            }
            if (picked) {
                std::snprintf(text, sizeof(text), m.cz_marked_fmt, picked, draw::Size(bytes).c_str());
                title = text;
                std::snprintf(text, sizeof(text), m.cz_go_marked_fmt, picked);
                rows.push_back({kCzGo, text, draw::Size(bytes), true, 0, false, kIconPress});
            } else if (!visible.empty() && CompressMarkable(c, m_compress_app)) {
                rows.push_back({kCzGo, m.cz_menu_row, {}, true, 0, false, kIconPress});
            }
            if (!visible.empty()) {
                const bool on = m_compress_picked.count(m_compress_app) > 0;
                rows.push_back({kButton, m.ac_mark, {}, on || CompressMarkable(c, m_compress_app), HidNpadButton_Y, false, on ? kIconTick : kIconBox});
                rows.push_back({kMarkAll, m.ac_mark_all, {}, markable > picked, 0, false, kIconTick});
                rows.push_back({kUnmarkAll, m.ac_unmark, {}, picked > 0, 0, false, kIconBox});
                rows.push_back({kInvert, m.ac_invert, {}, markable > 0, 0, false, kIconInvert});
            }
            if (std::find(m_compress_queue.begin(), m_compress_queue.end(), m_compress_app) != m_compress_queue.end()) {
                rows.push_back({kCzUnqueue, m.cz_unqueue, {}, true, 0, false, kIconCross});
            }
            if (busy) {
                rows.push_back({kButton, m.cz_stop, {}, true, HidNpadButton_X, true, kIconCross});
            } else if (stops) {
                rows.push_back({kButton, m.cz_remove_update, {}, true, HidNpadButton_X, true, kIconBin});
                rows.push_back({kButton, m.cz_fix_original, {}, true, HidNpadButton_A, false, kIconInstall});
            } else if (!visible.empty()) {
                rows.push_back({kButton, m.open, {}, true, HidNpadButton_A, false, kIconOpen});
            }
            rows.push_back({kCzFilter, m_compress_all ? m.cz_only_some : m.cz_show_all, {}, true, 0, false, kIconFunnel});
            break;
        }
        case ActScreen::ConsoleSort:
            // the five orders, the one in use with "now" (4A)
            title = m.so_title;
            for (int i = 0; i < kSortCount; i++) {
                rows.push_back({kSortBase + i, SortLabel(m, i), i == m_ui.console_sort ? std::string(m.so_now) : std::string{}, true, 0, false,
                                i == m_ui.console_sort ? kIconTick : kIconBox});
            }
            break;
        case ActScreen::Viewer: {
            // the text viewer (20A): wrap, the end, the top, close
            const auto& s2 = Tr();
            title = m_text_name;
            rows.push_back({kViewWrap, m.tv_wrap, m_ui.text_wrap ? s2.yes_word : s2.no_word, true, 0, false, kIconWrap});
            rows.push_back({kViewEnd, m.tv_end, {}, true, 0, false, kIconEnd});
            rows.push_back({kViewTop, m.tv_top, {}, true, 0, false, kIconTop});
            rows.push_back({kViewClose, s2.close, {}, true, HidNpadButton_B, false, kIconCross});
            break;
        }
        case ActScreen::Arrival: {
            // MTP and NS-USBloader: where it goes, USB 3.0 once the cable is in (USB A), stop
            const bool mtp = m_more == MoreScreen::Mtp;
            title = mtp ? m.t_mtp : m.t_usb;
            if (mtp) rows.push_back({kButton, MtpSnapshot().nand_allowed ? m.mtp_sd_only : m.allow_nand, {}, true, HidNpadButton_X, false, kIconCard});
            else rows.push_back({kButton, m.dc_switch, m_files_nand ? s.internal : s.sd_card, true, HidNpadButton_ZR, false, kIconCard});
            if (Usb3ChipKind() != kU3None) rows.push_back({kUsb3, tt.u3_row, Usb3RowText(), true, 0, Usb3ChipKind() == kU3Pending, kIconInfo});
            rows.push_back({kButton, m.stop_back, {}, true, HidNpadButton_B, false, kIconCross});
            break;
        }
        case ActScreen::Tickets: {
            // the tickets of More › Space (6A)
            int marked = 0, orphans = 0, orphans_marked = 0;
            for (size_t i = 0; i < m_tickets.size(); i++) {
                marked += m_ticket_marks[i] != 0;
                orphans += m_tickets[i].orphan;
                orphans_marked += m_tickets[i].orphan && m_ticket_marks[i];
            }
            std::snprintf(text, sizeof(text), m.tk_sub_fmt, int(m_tickets.size()), orphans);
            title = text;
            std::snprintf(text, sizeof(text), "%d", marked);
            rows.push_back({kTicketsDelete, m.tk_delete, marked ? std::string(text) : std::string{}, marked > 0, 0, true, kIconBin});
            std::snprintf(text, sizeof(text), "%d", orphans);
            rows.push_back({kTicketsOrphans, m.tk_mark_orphans, text, orphans > orphans_marked, 0, false, kIconTick});
            if (!m_tickets.empty()) {
                rows.push_back({kMarkAll, m.ac_mark_all, {}, marked < int(m_tickets.size()), 0, false, kIconTick});
                rows.push_back({kUnmarkAll, m.ac_unmark, {}, marked > 0, 0, false, kIconBox});
                rows.push_back({kInvert, m.ac_invert, {}, true, 0, false, kIconInvert});
            }
            break;
        }
        default: break;
    }
    return rows;
}

void App::OpenActions(ActScreen screen) {
    std::string title;
    const auto rows = ActRows(screen, title);
    if (rows.empty()) return;
    m_act = true;
    m_act_screen = screen;
    m_act_at = m_time; // it rises from the + and its rows come in
    m_act_focus = 0;
    while (m_act_focus < int(rows.size()) - 1 && !rows[size_t(m_act_focus)].enabled) m_act_focus++;
}

void App::UpdateActions(u64 down, u64 held) {
    std::string title;
    const auto rows = ActRows(m_act_screen, title);
    const int n = int(rows.size());
    if (n == 0 || (down & (HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus))) {
        m_act = false;
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyUp)) m_act_focus = (m_act_focus + n - 1) % n;
    if (Repeat(down, held, HidNpadButton_AnyDown)) m_act_focus = (m_act_focus + 1) % n;
    m_act_focus = std::clamp(m_act_focus, 0, n - 1);
    if ((down & HidNpadButton_A) && rows[size_t(m_act_focus)].enabled) ActDo(rows[size_t(m_act_focus)]);
}

void App::ActPress(u64 button) {
    // the row does what its button does on that screen
    switch (m_act_screen) {
        case ActScreen::Detail: UpdateDetail(button, 0); break;
        case ActScreen::ConsoleList:
        case ActScreen::ConsoleGame: UpdateConsole(button, 0); break;
        case ActScreen::Session:
        case ActScreen::History: UpdateInstalled(button, 0); break;
        case ActScreen::Queue: UpdateQueue(button, 0); break;
        case ActScreen::Shops: UpdateShops(button, 0); break;
        case ActScreen::SpacePick:
        case ActScreen::Compress: UpdateSpace(button, 0); break;
        case ActScreen::Viewer: UpdateViewer(button, 0); break;
        case ActScreen::Arrival: UpdateArrival(button, 0); break;
        default: break;
    }
}

void App::ActDo(const ActRow& row) {
    m_act = false;
    switch (row.id) {
        case kButton: ActPress(row.button); break;
        case kRemovePicked:
            if (m_space) AskConsoleRemoveMany(ConsoleView(m_space->Snapshot()));
            break;
        case kRemoveMarked: AskConsoleRemoveMarked(); break;
        case kMarkAll:
            if (m_act_screen == ActScreen::ConsoleList) {
                for (const auto& a : ConsoleView(m_space->Snapshot()).apps) {
                    if (!a.archived && !a.contents.empty()) m_console_picked.insert(a.app_id);
                }
            } else if (m_act_screen == ActScreen::ConsoleGame) {
                std::fill(m_console_marks.begin(), m_console_marks.end(), 1);
            } else if (m_act_screen == ActScreen::SpacePick) {
                std::fill(m_pick_marks.begin(), m_pick_marks.end(), true);
            } else if (m_act_screen == ActScreen::Tickets) {
                std::fill(m_ticket_marks.begin(), m_ticket_marks.end(), 1);
            } else if (m_act_screen == ActScreen::Compress) {
                for (const auto app : CompressMarkableApps()) m_compress_picked.insert(app);
            }
            break;
        case kUnmarkAll:
            if (m_act_screen == ActScreen::ConsoleList) m_console_picked.clear();
            else if (m_act_screen == ActScreen::ConsoleGame) std::fill(m_console_marks.begin(), m_console_marks.end(), 0);
            else if (m_act_screen == ActScreen::SpacePick) std::fill(m_pick_marks.begin(), m_pick_marks.end(), false);
            else if (m_act_screen == ActScreen::Tickets) std::fill(m_ticket_marks.begin(), m_ticket_marks.end(), 0);
            else if (m_act_screen == ActScreen::Compress) m_compress_picked.clear();
            break;
        case kInvert: {
            // what was marked is not, and the rest is (16A)
            int on = 0;
            if (m_act_screen == ActScreen::ConsoleList) {
                std::vector<unsigned long long> can;
                for (const auto& a : ConsoleView(m_space->Snapshot()).apps) {
                    if (!a.archived && !a.contents.empty()) can.push_back(a.app_id);
                }
                on = InvertMarks(m_console_picked, can);
            } else if (m_act_screen == ActScreen::ConsoleGame) {
                on = InvertMarks(m_console_marks);
            } else if (m_act_screen == ActScreen::SpacePick) {
                on = InvertMarks(m_pick_marks);
            } else if (m_act_screen == ActScreen::Tickets) {
                on = InvertMarks(m_ticket_marks);
            } else if (m_act_screen == ActScreen::Compress) {
                on = InvertMarks(m_compress_picked, CompressMarkableApps());
            }
            char text[160];
            std::snprintf(text, sizeof(text), TrM().ac_inverted_fmt, on);
            Notice(text);
            break;
        }
        case kRandom: RollRandom(); break; // 21A
        case kCompressAll: CompressFromConsole(0); break; // 14/11
        case kCzGo:
            // the marked, or the one in focus: one question, then into the queue (1A)
            if (!m_compress_picked.empty()) {
                const auto apps = CompressMarkableApps();
                std::vector<unsigned long long> go;
                for (const auto app : apps) {
                    if (m_compress_picked.count(app)) go.push_back(app);
                }
                AskCompress(go);
            } else {
                AskCompress({m_compress_app});
            }
            break;
        case kCzFilter: m_compress_all = !m_compress_all; break; // 4A
        case kCzUnqueue: std::erase(m_compress_queue, m_compress_app); break;
        case kCompress: {
            // the game in focus of the list, or the one whose page is open
            unsigned long long app = m_console_app;
            if (m_act_screen == ActScreen::ConsoleList) {
                const auto v = ConsoleView(m_space->Snapshot());
                if (!v.apps.empty()) app = v.apps[size_t(std::clamp(m_console_focus, 0, int(v.apps.size()) - 1))].app_id;
            }
            CompressFromConsole(app);
            break;
        }
        case kSigpatchInfo: OpenSigpatchInfo(); break; // 5B
        case kSigpatchOff: SigpatchesOff(); break;
        case kSendReport: SendReport(); break; // 4A
        case kUpdateShop: UpdateFromShop(m_shop_focus); break; // 15A
        case kUsb3: OpenUsb3(); break;                         // USB A
        case kViewWrap:
            m_ui.text_wrap = !m_ui.text_wrap;
            m_ui.Save();
            m_text_cols = 0; // the rows made again
            break;
        case kViewEnd: m_text_top = 1 << 30; break; // clamped when drawn
        case kViewTop: m_text_top = 0; break;
        case kViewClose: m_more = MoreScreen::Files; break;
        case kHomeBack: AskHomeBack(); break;
        case kHidden:
            // only the games the HOME menu does not show (1A)
            m_console_hidden = !m_console_hidden;
            m_console_missing = false;
            m_console_orphans = false;
            m_console_focus = 0;
            m_console_list_at = m_time;
            break;
        case kOrphans:
            // only the updates and DLC without their game (5C)
            m_console_orphans = !m_console_orphans;
            m_console_missing = false;
            m_console_hidden = false;
            m_console_focus = 0;
            m_console_list_at = m_time;
            break;
        case kSort: OpenActions(ActScreen::ConsoleSort); break;
        case kPlay: PlayConsoleGame(false); break;
        case kCheck: CheckConsoleGame(); break;
        case kRepair: RepairConsoleGame(); break;
        case kInstallBase: InstallConsoleBase(); break;
        case kTicketsDelete: AskDeleteTickets(); break;
        case kTicketsOrphans:
            for (size_t i = 0; i < m_tickets.size(); i++) {
                if (m_tickets[i].orphan) m_ticket_marks[i] = 1;
            }
            break;
        case kMissing:
            // the filter of what is missing (6A)
            m_console_missing = !m_console_missing;
            m_console_orphans = false;
            m_console_hidden = false;
            m_console_focus = 0;
            m_console_list_at = m_time;
            break;
        case kShortcuts:
            m_ui.show_shortcuts = !m_ui.show_shortcuts;
            m_ui.Save();
            m_console_focus = 0;
            m_console_list_at = m_time;
            // nothing hidden stays marked
            if (!m_ui.show_shortcuts && m_space) {
                for (const auto& a : m_space->Snapshot().apps) {
                    if (IsShortcut(a)) m_console_picked.erase(a.app_id);
                }
            }
            break;
        case kSize:
            // the size: list, medium covers, large covers; kept for the next time
            m_ui.console_size = (m_ui.console_size + 1) % 3;
            m_ui.Save();
            break;
        case kScreenOff: AskScreenOff(); break; // the screen off while it installs (11A)
        case kShopRemove:
            OpenDialog(DialogKind::ShopRemove);
            m_dialog.entry = m_shop_focus;
            break;
        case kVersions: OpenVersions(); break; // another version of the update (20A)
        case kClearHistory:
            OpenDialog(DialogKind::ClearHistory);
            break;
        default:
            if (row.id >= kSortBase && row.id < int(kSortBase) + int(kSortCount)) {
                // a new order: kept for the next time, and the rows come in again (4A)
                m_ui.console_sort = row.id - kSortBase;
                m_ui.Save();
                m_console_focus = 0;
                m_console_list_at = m_time;
            }
            break;
    }
}

void App::DrawActions(NVGcontext* vg) {
    const Theme& t = m_theme;
    std::string title;
    const auto rows = ActRows(m_act_screen, title);
    if (rows.empty()) return;
    m_act_focus = std::clamp(m_act_focus, 0, int(rows.size()) - 1);
    // what is behind goes a little dark; a tap there closes it
    nvgBeginPath(vg);
    nvgRect(vg, 0, 73, kWidth, kHeight - 73 - 56);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.3f * ui::EaseOut(ui::Appear(m_act_at, 0.2f))));
    nvgFill(vg);
    AddHit(0, 73, kWidth, kHeight - 73 - 56, HitKind::ActBack);
    // above the bar, growing out of the + that opened it (1A)
    const float h = 52 + float(rows.size()) * kActRow + 14, x = kWidth - 24 - kActW, y = std::max(84.f, kHeight - 56 - 12 - h);
    nvgSave(vg);
    const float alpha = GrowFromPlus(vg, m_act_at, x, y, kActW, h);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 8, y + 6, kActW + 16, h + 16, 20);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, kActW, h, 16, 36, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, kActW, h, 16);
    AddHit(x, y, kActW, h, HitKind::Button); // the panel itself does nothing
    nvgSave(vg);
    nvgScissor(vg, x + 22, y + 8, kActW - 44, 40);
    ui::Text(vg, x + 22, y + 28, 15, t.muted, title.c_str());
    nvgRestore(vg);
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& r = rows[i];
        const float ry = y + 50 + float(i) * kActRow, rc = ry + (kActRow - 4) / 2;
        nvgSave(vg);
        const float ra = RowComesIn(vg, m_act_at, int(i), alpha);
        if (int(i) == m_act_focus) {
            ui::FillRound(vg, x + 8, ry, kActW - 16, kActRow - 4, 10, t.panel2);
            ui::Focus(vg, t, x + 8, ry, kActW - 16, kActRow - 4, 10, m_time);
        }
        AddHit(x + 8, ry, kActW - 16, kActRow, HitKind::ActRow, int(i));
        nvgGlobalAlpha(vg, (r.enabled ? 1.f : 0.45f) * ra);
        Icon(vg, r.enabled ? (r.danger ? t.warn : t.work) : t.muted, r.icon, x + 24, rc);
        float right = x + kActW - 24;
        if (const char* g = GlyphOf(r.button)) {
            // the button that does the same, so it is learnt (2A)
            ui::Symbol(vg, t, right - 12, rc, 22, g, t.muted, r.button);
            right -= 36;
        }
        if (!r.right.empty()) {
            ui::Text(vg, right, rc, 14, t.muted, r.right.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            right -= ui::TextWidth(vg, 14, r.right.c_str()) + 12;
        }
        nvgSave(vg);
        nvgScissor(vg, x + 58, ry, std::max(40.f, right - x - 58 - 8), kActRow);
        ui::Text(vg, x + 58, rc, 17, r.enabled ? t.text : t.muted, r.label.c_str());
        nvgRestore(vg);
        nvgRestore(vg);
    }
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, Tr().choose, HidNpadButton_A}, {ui::glyph::B, Tr().close, HidNpadButton_B}}, rects);
    AddHints(rects);
}

} // namespace nxdock
