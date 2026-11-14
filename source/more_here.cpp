// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the console has, read once for the explorer (13A of the visual test of groups 3 and 4),
// and "Update installed games from here" (15A): in the + of a folder of the explorer or of a
// shop, the updates and DLC found there newer than what is installed (and the DLC not installed)
// of the games of the console, in a list with all ticked, to the usual queue with the SD card.
// What is older or installed already is left alone; nothing is read from inside a file.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <algorithm>
#include <cstdio>

namespace nxdock {

void App::FollowConsoleHas() {
    if (m_time - m_has_polled < 0.5) return;
    m_has_polled = m_time;
    if (!m_space) m_space = std::make_unique<Space>();
    // read again when the queue has ended a job, as In the console does
    const unsigned version = m_queue ? m_queue->Version() : 0;
    if (m_console_scanned_for != version && !m_space->Busy() && m_space->StartScan()) m_console_scanned_for = version;
    if ((m_has_ready && m_has_for == m_console_scanned_for) || m_space->Busy()) return;
    const auto v = m_space->Snapshot();
    if (!v.scanned) return;
    m_has = HasOf(v.apps);
    if (!m_has_ready) m_has_at = m_time; // the tags come in now
    m_has_ready = true;
    m_has_for = m_console_scanned_for;
}

void App::UpdateFromFolder(const std::string& folder) {
    // the games of this folder and its subfolders, by their names
    const auto files = fileops::GamesUnder({folder});
    m_here_jobs.clear();
    for (const auto& g : files) {
        InstallJob job;
        job.game = g.name;
        job.label = g.parts.size() > 1 ? std::string(TrM().t_files) + " · " + std::to_string(g.parts.size()) : TrM().t_files;
        job.format = g.format;
        job.origin = InstallJob::Origin::File;
        job.resumable = true;
        job.parts = g.parts;
        job.file_id = "file:" + g.parts.front();
        job.size = g.size;
        job.color_key = g.name;
        u64 id = 0;
        u32 v = 0;
        if (TitleFromName(g.name, id, v)) {
            job.title_id = id;
            job.version = v;
        }
        m_here_jobs.push_back(std::move(job));
    }
    char where[200];
    std::snprintf(where, sizeof(where), TrM().uh_where_fmt, int(files.size()));
    m_here_where = where;
    m_here_waiting = true;
    OpenDialog(DialogKind::UpdateHere);
    m_has_polled = -100;
    FollowUpdateHere();
}

void App::UpdateFromShop(int shop) {
    if (shop < 0 || shop >= int(m_shops.size())) return;
    // the shop opens as always; once its games are there, the list (FollowShop)
    m_update_shop = shop;
    OpenShopAt(shop);
    if (m_shop_opening != shop) {
        m_update_shop = -1;
        Notice(TrM().sh_loading);
    }
}

void App::FollowUpdateHere() {
    // the shop asked for has opened: its updates and DLC
    if (m_update_shop >= 0 && m_shop_active && m_shop_opening == m_update_shop && m_dialog.kind == DialogKind::None) {
        m_update_shop = -1;
        m_here_jobs.clear();
        for (const auto& g : m_games) {
            for (const auto& f : g.files) {
                if (f.kind == CatalogFile::Kind::Game) continue;
                InstallJob job = JobFor(g, f);
                job.title_id = f.title_id;
                job.version = f.version;
                m_here_jobs.push_back(std::move(job));
            }
        }
        m_here_where = m_shop_name;
        m_here_waiting = true;
        OpenDialog(DialogKind::UpdateHere);
        m_has_polled = -100;
    }
    if (!m_here_waiting) return;
    if (m_dialog.kind != DialogKind::UpdateHere) {
        m_here_waiting = false; // closed before it was ready
        return;
    }
    FollowConsoleHas();
    if (!m_has_ready || m_has_for != m_console_scanned_for) return;
    // the newest of each title the console wants, all ticked; to the SD card, as everywhere
    std::vector<FoundFile> found;
    for (const auto& j : m_here_jobs) found.push_back({j.title_id, j.version});
    const UpdatePick pick = PickUpdates(m_has, found);
    m_dialog.jobs.clear();
    for (const size_t i : pick.chosen) {
        InstallJob job = m_here_jobs[i];
        job.nand = false;
        job.kind = TitleKind(job.title_id) == 1 ? CatalogFile::Kind::Update : CatalogFile::Kind::Dlc;
        m_dialog.jobs.push_back(std::move(job));
    }
    m_dialog.marks.assign(m_dialog.jobs.size(), 1);
    m_dialog.size = (unsigned long long)pick.left;
    m_dialog.text = m_here_where;
    m_dialog.entry = 0;
    m_dialog.focus = m_dialog.jobs.empty() ? 0 : 1;
    m_dialog.single = m_dialog.jobs.empty();
    m_dialog_at = m_time; // the list comes in
    m_here_waiting = false;
    m_here_jobs.clear();
}

} // namespace nxdock
