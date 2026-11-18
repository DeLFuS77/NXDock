// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// A game the library asks to remove (2bA of the visual test of 18/11). What arrives from
// /install/push/wait with "action":"remove" is asked about one by one, over the screen shown,
// with Keep it in focus; never while the queue or Compress games is at work. Remove takes it off
// as In the console does (its saves copied first as Settings › Saves says, its bar, its notice,
// its lines in the history), and then the library is answered: removed, failed, or missing when
// the game is not there (then nothing is asked).
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

// The game itself on the console now (not archived, not only its update or DLC); null when not.
const InstalledApp* GameOnConsole(const Space::View& v, unsigned long long app) {
    if (std::find(v.removed.begin(), v.removed.end(), app) != v.removed.end()) return nullptr;
    for (const auto& a : v.apps) {
        if (a.app_id != app || a.archived) continue;
        for (const auto& c : a.contents) {
            if (c.type == NcmContentMetaType_Application) return &a;
        }
    }
    return nullptr;
}

} // namespace

bool App::CompressAtWork() const {
    return m_compress && (m_compress->Progress().running || !m_compress_queue.empty());
}

std::string App::LibraryName() {
    const auto link = m_link.Snapshot();
    return !link.app_name.empty() ? link.app_name : std::string("Switch Library Manager Web");
}

void App::FollowRemoveAsks() {
    if (m_remove_asks.empty() || m_push_rm.on) return; // the next once the one at work is gone
    // after the queue, and never while a game is compressed: asked once they end
    if ((m_queue && (m_queue->Busy() || m_queue->Undoing())) || CompressAtWork()) return;
    if (!m_space) m_space = std::make_unique<Space>();
    if (m_space->Busy()) return;
    const PushItem p = m_remove_asks.front();
    // what the console has now, read again for each: the game may have gone meanwhile
    if (m_remove_scanned != p.id) {
        if (m_space->StartScan()) {
            m_remove_scanned = p.id;
            if (m_queue) m_console_scanned_for = m_queue->Version();
        }
        return;
    }
    const auto v = m_space->Snapshot();
    if (!v.scanned) return;
    m_remove_asks.pop_front();
    const InstalledApp* game = GameOnConsole(v, p.title_id);
    if (!game) {
        m_push->Answer(p.id, false, "missing"); // not there (any more): nothing to ask
        return;
    }
    OpenDialog(DialogKind::PushRemove);
    m_dialog.push = {p.id};
    m_dialog.ids = {p.title_id};
    m_dialog.text = game->name.empty() ? p.game : game->name;
    m_dialog.from = LibraryName();
    m_dialog.focus = 0; // Keep it, first
}

void App::ConfirmPushRemove() {
    const std::string id = m_dialog.push.empty() ? std::string() : m_dialog.push.front();
    const unsigned long long app = m_dialog.ids.empty() ? 0 : m_dialog.ids.front();
    const std::string name = m_dialog.text;
    m_dialog = Dialog{};
    if (id.empty() || !app || !m_space) return;
    if ((m_queue && (m_queue->Busy() || m_queue->Undoing())) || CompressAtWork() || m_space->Busy()) {
        // something began while it was asked: asked again once it ends
        PushItem again;
        again.id = id;
        again.action = "remove";
        again.title_id = app;
        again.game = name;
        m_remove_asks.push_front(again);
        m_remove_scanned.clear();
        Notice(TrM().cz_busy);
        return;
    }
    const auto v = m_space->Snapshot();
    if (!GameOnConsole(v, app)) {
        m_push->Answer(id, false, "missing");
        return;
    }
    // as In the console takes a game off: its saves first when Settings › Saves says so, its mods
    // and tickets stay
    if (!RemoveWholeGames({app}, m_ui.save_copy ? CopySavesFirst() : nullptr, {})) {
        m_push->Answer(id, true, "failed");
        return;
    }
    m_push_rm = PushRemoving{id, app, v.finished, true};
    // to In the console, where it is seen going: the game dims, the bar of its progress shows, and
    // the notice and the history when it ends, as for any removal
    m_tab = Tab::Installed;
    m_installed_view = 0;
    m_console_app = 0;
    m_console_list_at = m_time;
}

void App::FollowPushRemoving() {
    if (!m_push_rm.on || !m_space) return;
    const auto v = m_space->Snapshot();
    if (v.finished == m_push_rm.finished || v.task == Space::Task::Remove) return; // at work
    // ended: gone when the removal took it off; a copy of the saves that failed stops it
    const bool gone = std::find(v.removed_done.begin(), v.removed_done.end(), m_push_rm.app) != v.removed_done.end();
    m_push->Answer(m_push_rm.id, true, gone ? "removed" : "failed");
    m_push_rm = PushRemoving{};
    // its notice, its lines in the history and the inventory sent again (as after any removal)
    // also when another screen is shown now; In the console does it itself
    if (!(m_tab == Tab::Installed && m_installed_view == 0)) {
        const int mode = m_space_mode; // a screen of Space and dumps stays as it was
        FollowSpace();
        m_space_mode = mode;
    }
}

} // namespace nxdock
