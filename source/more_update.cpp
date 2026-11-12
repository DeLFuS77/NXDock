// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock updating itself (5A of the visual test of the new tools, 11/11): at start, at most once a
// day and only when turned on, a notice when a newer version is out; in Settings › NXDock, looking
// for it, its news, and updating after a question: downloaded, checked with the signature of the
// project, put in place with the one running kept, and NXDock opened again. Going back to the
// version before is a row of the same group.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <ctime>

namespace nxdock {

void App::FollowSelfUpdate() {
    if (!m_updater) m_updater = std::make_unique<Updater>();
    if (m_time - m_update_old_at > 3) {
        m_update_old_at = m_time;
        m_update_has_old = Updater::HasOld(m_home.nro);
    }
    // at start, once the wizard is done, at most once a day: only a notice
    if (!m_update_started && m_ui.wizard_done && !m_wizard && m_time > 6) {
        m_update_started = true;
        const long long now = std::time(nullptr);
        if (m_ui.self_update_notice && Updater::HasKeys() && now - m_ui.self_update_checked > 20 * 3600) {
            m_update_silent = true;
            m_updater->Check();
            m_ui.self_update_checked = now;
            m_ui.Save();
        }
    }
    const Updater::View v = m_updater->Snapshot();
    if (m_update_silent && v.stage != Updater::Stage::Checking) {
        m_update_silent = false;
        if (v.stage == Updater::Stage::Newer) {
            char text[160];
            std::snprintf(text, sizeof(text), TrT().up_out_fmt, v.version.c_str());
            Notice(text);
        }
    }
    if (v.stage == Updater::Stage::Done) {
        // a moment to read it, then NXDock opens again with the new version
        if (m_update_done_at < 0) {
            m_update_done_at = m_time;
        } else if (m_time - m_update_done_at > 1.5 && !m_quit) {
            if (envHasNextLoad()) {
                envSetNextLoad(m_home.nro.c_str(), m_home.nro.c_str());
                m_quit = true;
            } else if (m_dialog.kind == DialogKind::SelfUpdate) {
                m_dialog = Dialog{};
                NoticeOk(TrT().up_done);
            }
        }
    }
}

void App::AskSelfUpdate() {
    if (!m_updater) m_updater = std::make_unique<Updater>();
    if (!Updater::HasKeys()) {
        Notice(TrT().up_err_keys);
        return;
    }
    m_update_silent = false;
    const auto stage = m_updater->Snapshot().stage;
    if (stage != Updater::Stage::Newer && stage != Updater::Stage::Downloading && stage != Updater::Stage::Done) m_updater->Check();
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::SelfUpdate;
    m_dialog.focus = 1;
}

bool App::UpdaterDialogText(ToolDialog& d) {
    const Tools& tt = TrT();
    char text[300];
    if (m_dialog.kind == DialogKind::SelfBack) {
        d.title = tt.up_back_title;
        d.body = tt.up_back_body;
        d.primary = tt.up_back_go;
        return true;
    }
    if (m_dialog.kind != DialogKind::SelfUpdate || !m_updater) return false;
    const Updater::View v = m_updater->Snapshot();
    switch (v.stage) {
        case Updater::Stage::Idle:
        case Updater::Stage::Checking:
            d.title = tt.up_checking;
            d.bar = true;
            d.running = true;
            d.frac = 1.f;
            d.single = true;
            break;
        case Updater::Stage::Newer:
            std::snprintf(text, sizeof(text), tt.up_title_fmt, v.version.c_str());
            d.title = text;
            std::snprintf(text, sizeof(text), tt.up_body_fmt, NXDOCK_VERSION);
            d.body = std::string(text) + (v.notes.empty() ? "" : "\n" + v.notes);
            std::snprintf(text, sizeof(text), tt.up_note_fmt, draw::Size(v.size).c_str());
            d.note = text;
            d.primary = tt.up_go;
            d.cancel = tt.up_later;
            if (m_queue && m_queue->Busy()) {
                d.error = tt.up_busy;
                d.can = false;
            }
            break;
        case Updater::Stage::Latest:
            d.title = tt.up_latest_t;
            std::snprintf(text, sizeof(text), tt.up_latest_fmt, NXDOCK_VERSION);
            d.body = text;
            d.single = true;
            break;
        case Updater::Stage::Downloading:
            std::snprintf(text, sizeof(text), tt.up_title_fmt, v.version.c_str());
            d.title = std::string(tt.up_downloading) + " · " + text;
            d.bar = true;
            d.running = true;
            d.frac = v.size ? float(double(v.done) / double(v.size)) : 0.f;
            d.bar_text = draw::Size(v.done) + " / " + draw::Size(v.size);
            d.single = true;
            break;
        case Updater::Stage::Done:
            d.title = tt.up_done;
            d.bar = true;
            d.running = false;
            d.frac = 1.f;
            d.single = true;
            break;
        case Updater::Stage::Failed: {
            d.title = tt.up_failed_t;
            const char* why = tt.up_err_network;
            switch (v.error) {
                case Updater::Error::NoRelease: why = tt.up_err_none; break;
                case Updater::Error::NoKeys: why = tt.up_err_keys; break;
                case Updater::Error::Signature: why = tt.up_err_sig; break;
                case Updater::Error::Sum: why = tt.up_err_sum; break;
                case Updater::Error::Write: why = tt.up_err_write; break;
                default: break;
            }
            d.body = why;
            d.single = true;
            break;
        }
    }
    return true;
}

bool App::UpdaterDialogInput(u64 down) {
    if (m_dialog.kind != DialogKind::SelfUpdate || !m_updater) return false;
    // Update: the question stays, with the download
    if ((down & HidNpadButton_A) && m_dialog.focus == 1 && m_updater->Snapshot().stage == Updater::Stage::Newer) {
        ConfirmUpdater();
        return true;
    }
    return false;
}

void App::ConfirmUpdater() {
    if (m_dialog.kind == DialogKind::SelfBack) {
        if (!Updater::RollBack(m_home.nro)) {
            Notice(TrT().up_back_failed);
        } else if (envHasNextLoad()) {
            envSetNextLoad(m_home.nro.c_str(), m_home.nro.c_str());
            m_quit = true;
        } else {
            NoticeOk(TrT().up_done);
        }
        return;
    }
    if (m_dialog.kind != DialogKind::SelfUpdate || !m_updater) return;
    if (m_queue && m_queue->Busy()) {
        Notice(TrT().up_busy);
        return;
    }
    m_update_done_at = -100;
    m_updater->Install(m_home.nro);
}

} // namespace nxdock
