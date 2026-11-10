// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Space and dumps (8A of the visual test of phases 9 to 15): the SD card and the internal memory
// as bars, and six actions, each with what it frees. Archive, move and dump choose from a list
// of what is installed; every action asks first, the internal memory always.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "sdcheck_nx.hpp"

#include <algorithm>
#include <ctime>

namespace nxdock {
namespace {

std::string Clean(std::string s) {
    for (auto& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = ' ';
    }
    return s;
}

void Bar(NVGcontext* vg, const Theme& t, float x, float y, float w, long long free_b, long long total_b) {
    ui::FillRound(vg, x, y, w, 18, 9, t.panel2);
    if (total_b > 0 && free_b >= 0) {
        const float used = float(double(total_b - free_b) / double(total_b));
        NVGcolor c = t.muted;
        c.a = 0.45f;
        ui::FillRound(vg, x, y, std::max(18.f, w * used), 18, 9, c);
    }
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, 17, 9, t.line, 1);
}

} // namespace

unsigned long long App::QueueNeedsOnSd() {
    if (!m_queue) return 0;
    unsigned long long need = 0;
    for (const auto& j : m_queue->Jobs()) {
        if (!j.nand && (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running)) need += j.size + 64ull * 1024 * 1024;
    }
    return need;
}

void App::OpenSpace() {
    if (!m_space) m_space = std::make_unique<Space>();
    m_space->StartScan();
    m_space_mode = 0;
    m_more = MoreScreen::Space;
}

void App::OpenPicker(int kind) {
    const auto v = m_space->Snapshot();
    m_pick_kind = kind;
    m_pick_rows.clear();
    for (int i = 0; i < int(v.apps.size()); i++) {
        const auto& a = v.apps[size_t(i)];
        if (a.archived) continue;
        if (kind == 2) {
            for (int c = 0; c < int(a.contents.size()); c++) m_pick_rows.push_back({i, c});
        } else {
            m_pick_rows.push_back({i, -1});
        }
    }
    m_pick_marks.assign(m_pick_rows.size(), 0);
    m_pick_focus = m_pick_first = 0;
    m_list_at = m_time;
    m_glide_list = {};
    m_plan_head.clear();
    m_space_mode = 1;
}

void App::OpenPlanner() {
    const More& m = TrM();
    const auto v = m_space->Snapshot();
    const unsigned long long need = QueueNeedsOnSd();
    const long long free_b = v.storage.sd_free;
    if (free_b < 0 || need <= u64(free_b)) {
        Notice(m.a_plan_fits);
        return;
    }
    const unsigned long long missing = need - u64(free_b);
    OpenPicker(0);
    const auto plan = PlanArchive(v.apps, missing, false);
    for (size_t r = 0; r < m_pick_rows.size(); r++) {
        if (std::find(plan.begin(), plan.end(), m_pick_rows[r].first) != plan.end()) m_pick_marks[r] = 1;
    }
    char head[200];
    std::snprintf(head, sizeof(head), m.plan_head_fmt, draw::Size(need).c_str(), draw::Size(missing).c_str());
    m_plan_head = head;
}

void App::FollowSpace() {
    if (!m_space) return;
    const auto v = m_space->Snapshot();
    if (v.finished == m_space_finished) return;
    m_space_finished = v.finished;
    if (v.last == Space::Task::Scan) return;
    const More& m = TrM();
    if (!v.error.empty()) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultError;
        m_dialog.single = true;
        m_dialog.from = v.error;
        m_dialog.text = m.t_space;
        return;
    }
    switch (v.last) {
        case Space::Task::Clean: Notice(m.done_clean); break;
        case Space::Task::Archive: Notice(m.done_archive); break;
        case Space::Task::Move: Notice(m.done_move); break;
        case Space::Task::Dump: Notice(m.done_dump); break;
        default: break;
    }
    m_space_mode = 0;
    m_sd_checked = -100;
}

void App::DrawSpace(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    FollowSpace();
    const auto v = m_space ? m_space->Snapshot() : Space::View{};
    if (m_space_mode == 1) {
        DrawPicker(vg, v);
        return;
    }
    if (m_space_mode == 2 && m_sdcheck) {
        DrawSdCheck(vg);
        return;
    }
    if (m_space_mode == 3 && m_compress) {
        DrawCompress(vg);
        return;
    }
    const float lx = 48, rx = 664, cw = 568;
    char line[200];
    std::snprintf(line, sizeof(line), m.sp_sd_fmt, v.storage.sd_free >= 0 ? draw::Size(u64(v.storage.sd_free)).c_str() : "?",
                  v.storage.sd_total >= 0 ? draw::Size(u64(v.storage.sd_total)).c_str() : "?");
    draw::Section(vg, t, lx, 100, line);
    Bar(vg, t, lx, 114, cw, v.storage.sd_free, v.storage.sd_total);
    std::snprintf(line, sizeof(line), m.sp_nand_fmt, v.storage.nand_free >= 0 ? draw::Size(u64(v.storage.nand_free)).c_str() : "?",
                  v.storage.nand_total >= 0 ? draw::Size(u64(v.storage.nand_total)).c_str() : "?");
    draw::Section(vg, t, lx, 154, line);
    Bar(vg, t, lx, 168, cw, v.storage.nand_free, v.storage.nand_total);

    // the six actions, each with what it frees or needs
    const unsigned long long need = QueueNeedsOnSd();
    std::string sub[8], right[8];
    char text[200];
    std::snprintf(text, sizeof(text), m.cl_sub_fmt, v.leftovers.contents, v.leftovers.placeholders, v.leftovers.addons, v.leftovers.tickets);
    sub[0] = v.scanned ? text : m.scanning;
    right[0] = v.scanned ? draw::Size(v.leftovers.bytes) : "";
    sub[1] = m.a_archive_sub;
    right[1] = m.choose_word;
    sub[2] = m.a_move_sub;
    right[2] = m.choose_word;
    if (v.storage.sd_free >= 0 && need > u64(v.storage.sd_free)) {
        std::snprintf(text, sizeof(text), m.a_plan_sub_fmt, draw::Size(need).c_str(), draw::Size(need - u64(v.storage.sd_free)).c_str());
        sub[3] = text;
    } else {
        sub[3] = m.a_plan_fits;
    }
    right[3] = "";
    sub[4] = v.card ? m.a_card_sub : m.a_card_none;
    sub[5] = m.a_dump_sub;
    right[5] = m.choose_word;
    const auto sd = m_sdcheck ? m_sdcheck->Snapshot() : SdChecker::View{};
    if (sd.running) {
        std::snprintf(text, sizeof(text), m.sd_running_fmt, sd.bytes ? int(100.0 * double(sd.bytes_done) / double(sd.bytes)) : 0);
        sub[6] = text;
    } else {
        sub[6] = m.sd_action_sub;
    }
    sub[7] = m.cz_action_sub;
    right[7] = m.cz_tag;
    const char* titles[8] = {m.a_clean, m.a_archive, m.a_move, m.a_plan, m.a_card, m.a_dump, m.sd_action, m.cz_action};
    m_space_focus = std::clamp(m_space_focus, 0, 7);
    for (int i = 0; i < 8; i++) {
        // three and "Compress games" on the left, four on the right
        const bool left = i < 3 || i == 7;
        const float x = left ? lx : rx, y = left ? 222 + (i == 7 ? 3 : i) * 78 : 100 + (i - 3) * 78;
        draw::Panel(vg, t, x, y, cw, 66, 12);
        if (i == m_space_focus) ui::Focus(vg, t, x, y, cw, 66, 12, m_time);
        AddHit(x, y, cw, 66, HitKind::SpaceAction, i);
        const bool off = (i == 4 && !v.card) || (i == 0 && v.scanned && !v.leftovers.Any());
        nvgGlobalAlpha(vg, off ? 0.5f : 1.f);
        DrawTileIcon(vg, i == 0 ? 6 : i == 1 ? 4 : i == 2 ? 5 : i == 3 ? 5 : i == 4 ? 0 : i == 5 ? 1 : i == 6 ? 0 : 5, x + 18, y + 33);
        ui::Text(vg, x + 70, y + 24, 17, t.text, titles[i]);
        nvgSave(vg);
        nvgScissor(vg, x + 70, y + 34, cw - 190, 30);
        ui::Text(vg, x + 70, y + 46, 14, t.muted, sub[i].c_str());
        nvgRestore(vg);
        ui::Text(vg, x + cw - 18, y + 33, 15, t.muted, right[i].c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    draw::Box(vg, rx, 412, cw, 14, t.muted, m.dumps_note);

    // the console being read: how far (a long while with many games)
    if ((v.task == Space::Task::Scan && v.total) || v.names_total) {
        const bool names = v.task != Space::Task::Scan;
        draw::Panel(vg, t, lx, 572, kWidth - 96, 70, 12);
        std::snprintf(text, sizeof(text), names ? m.sp_names_fmt : m.sp_reading_fmt, names ? v.names_done : int(v.done),
                      names ? v.names_total : int(v.total));
        ui::Spinner(vg, lx + 30, 596, 9, t.work, m_time);
        ui::Text(vg, lx + 50, 596, 15, t.muted, text);
        const float frac = names ? float(v.names_done) / float(std::max(1, v.names_total)) : float(double(v.done) / double(v.total));
        ui::Progress(vg, t, lx + 22, 620, kWidth - 140, 10, frac, t.work, m_time);
    }
    // the work going on
    if (v.task != Space::Task::None && v.task != Space::Task::Scan) {
        const char* what = v.task == Space::Task::Clean     ? m.st_cleaning
                           : v.task == Space::Task::Archive ? m.st_archiving
                           : v.task == Space::Task::Move    ? m.st_moving
                                                            : m.st_dumping;
        draw::Panel(vg, t, lx, 548, kWidth - 96, 104, 12);
        ui::Text(vg, lx + 22, 570, 17, t.text, what);
        if (!v.current.empty()) ui::Text(vg, lx + 22, 594, 14, t.muted, v.current.c_str());
        const float frac = v.total ? float(double(v.done) / double(v.total)) : 0.f;
        ui::Progress(vg, t, lx + 22, 612, kWidth - 140, 12, frac, t.work, m_time);
        if (v.total) {
            std::snprintf(text, sizeof(text), "%s / %s", draw::Size(v.done).c_str(), draw::Size(v.total).c_str());
            ui::Text(vg, kWidth - 70, 640, 14, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
    }
    std::vector<ui::Hint> hints = {{ui::glyph::A, m.open, HidNpadButton_A}};
    if (v.task == Space::Task::Dump) hints.push_back({ui::glyph::X, s.cancel, HidNpadButton_X});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::DrawPicker(NVGcontext* vg, const Space::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const char* title = m_pick_kind == 0 ? m.a_archive : m_pick_kind == 1 ? m.a_move : m.a_dump;
    ui::Text(vg, 48, 104, 22, t.text, title);
    if (!m_plan_head.empty()) ui::Text(vg, 48, 134, 15, t.warn, m_plan_head.c_str());
    if (m_pick_rows.empty()) ui::Text(vg, 48, 170, 15, t.muted, m.no_installed);
    const float top = 156, pitch = 52;
    const int rows = 8;
    m_pick_focus = std::clamp(m_pick_focus, 0, std::max(0, int(m_pick_rows.size()) - 1));
    if (m_pick_focus < m_pick_first) m_pick_first = m_pick_focus;
    if (m_pick_focus >= m_pick_first + rows) m_pick_first = m_pick_focus - rows + 1;
    unsigned long long marked = 0;
    if (!m_pick_rows.empty()) {
        const float fy = m_glide_list.Step(top + (m_pick_focus - m_pick_first) * pitch);
        ui::FillRound(vg, 40, fy, kWidth - 80, 46, 10, t.panel2);
        ui::Focus(vg, t, 40, fy, kWidth - 80, 46, 10, m_time);
    }
    for (int r = 0; r < rows && m_pick_first + r < int(m_pick_rows.size()); r++) {
        const int i = m_pick_first + r;
        const auto& a = v.apps[size_t(m_pick_rows[size_t(i)].first)];
        const float y = top + r * pitch, cy = y + 23;
        AddHit(40, y, kWidth - 80, 46, HitKind::PickRow, i);
        float x = 56;
        if (m_pick_kind == 0) {
            draw::TickBox(vg, t, x, cy, m_pick_marks[size_t(i)], "pick:" + std::to_string(a.app_id));
            x += 40;
        }
        std::string name = a.name, where, size;
        if (m_pick_kind == 2) {
            const auto& c = a.contents[size_t(m_pick_rows[size_t(i)].second)];
            name += c.type == NcmContentMetaType_Patch          ? std::string(" · ") + s.update_word + " v" + std::to_string(c.version)
                    : c.type == NcmContentMetaType_AddOnContent ? std::string(" · ") + s.dlc_prefix + std::to_string(c.id & 0xFFF)
                                                                : std::string(" · ") + m.game_word;
            where = c.nand ? s.internal_short : "SD";
            size = draw::Size(c.bytes);
        } else {
            where = a.nand_bytes && a.sd_bytes ? std::string("SD + ") + s.internal_short : a.nand_bytes ? s.internal_short : "SD";
            size = draw::Size(a.Bytes());
        }
        nvgSave(vg);
        nvgScissor(vg, x, y, kWidth - x - 480, 46);
        ui::Text(vg, x, cy, 16, t.text, name.c_str());
        nvgRestore(vg);
        if (m_pick_kind == 0) {
            // when it was last played: the planner archives the least used first
            char idle[64];
            if (a.idle_minutes < 0) std::snprintf(idle, sizeof(idle), "%s", m.idle_never);
            else if (a.idle_minutes < 60 * 24) std::snprintf(idle, sizeof(idle), "%s", m.idle_today);
            else std::snprintf(idle, sizeof(idle), m.idle_days_fmt, int(a.idle_minutes / (60 * 24)));
            ui::Text(vg, kWidth - 330, cy, 14, t.muted, idle, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        ui::Text(vg, kWidth - 200, cy, 15, t.muted, where.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, kWidth - 60, cy, 15, t.muted, size.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    for (size_t i = 0; i < m_pick_rows.size(); i++) {
        if (m_pick_marks[i]) marked += v.apps[size_t(m_pick_rows[i].first)].sd_bytes + v.apps[size_t(m_pick_rows[i].first)].nand_bytes;
    }
    std::vector<ui::Hint> hints;
    if (m_pick_kind == 0) {
        hints.push_back({ui::glyph::Y, m.mark, HidNpadButton_Y});
        hints.push_back({ui::glyph::X, m.archive_go, HidNpadButton_X});
    } else {
        hints.push_back({ui::glyph::A, m_pick_kind == 1 ? m.move_go : m.dump_go, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    char left[96];
    std::snprintf(left, sizeof(left), "%s", m_pick_kind == 0 ? draw::Size(marked).c_str() : "");
    ui::HintBar(vg, t, m_pick_kind == 0 ? left : nullptr, hints, m_hint_rects);
}

void App::UpdateSpace(u64 down, u64 held) {
    const More& m = TrM();
    if (!m_space) return;
    const auto v = m_space->Snapshot();
    if (m_space_mode == 1) {
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_pick_focus > 0) m_pick_focus--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_pick_focus < int(m_pick_rows.size()) - 1) m_pick_focus++;
        if (down & HidNpadButton_B) {
            m_space_mode = 0;
            return;
        }
        if (m_pick_rows.empty() || m_space->Busy()) return;
        const auto row = m_pick_rows[size_t(m_pick_focus)];
        const auto& a = v.apps[size_t(row.first)];
        if (m_pick_kind == 0) {
            if (down & (HidNpadButton_Y | HidNpadButton_A)) m_pick_marks[size_t(m_pick_focus)] = !m_pick_marks[size_t(m_pick_focus)];
            if (down & HidNpadButton_X) {
                m_dialog = Dialog{};
                m_dialog.kind = DialogKind::SpaceArchive;
                unsigned long long bytes = 0;
                for (size_t i = 0; i < m_pick_rows.size(); i++) {
                    if (!m_pick_marks[i]) continue;
                    const auto& p = v.apps[size_t(m_pick_rows[i].first)];
                    m_dialog.ids.push_back(p.app_id);
                    bytes += p.Bytes();
                }
                if (m_dialog.ids.empty()) {
                    m_dialog.kind = DialogKind::None;
                    Notice(Tr().nothing_picked);
                    return;
                }
                m_dialog.text = draw::Size(bytes);
            }
            return;
        }
        if (!(down & HidNpadButton_A)) return;
        m_dialog = Dialog{};
        m_dialog.text = a.name;
        if (m_pick_kind == 1) {
            m_dialog.kind = DialogKind::SpaceMove;
            m_dialog.ids = {a.app_id};
            // where most of it is decides the way; the internal memory always asks (this window)
            m_dialog.to_nand = a.sd_bytes >= a.nand_bytes;
            m_dialog.size = a.Bytes();
        } else {
            const auto& c = a.contents[size_t(row.second)];
            m_dialog.kind = DialogKind::SpaceDump;
            m_dialog.ids = {c.id, c.version, c.type, c.nand ? 1ull : 0ull};
            char tid[24];
            std::snprintf(tid, sizeof(tid), "%016llX", c.id);
            m_dialog.from = Clean(a.name) + " [" + tid + "][v" + std::to_string(c.version) + "].nsp";
        }
        return;
    }
    // the actions: three on the left, three on the right
    if (m_space_mode == 2 && m_sdcheck) {
        UpdateSdCheck(down, held);
        return;
    }
    if (m_space_mode == 3 && m_compress) {
        UpdateCompress(down, held);
        return;
    }
    // the actions: four on the left (0, 1, 2, 7), four on the right (3 to 6)
    {
        static constexpr int kLeft[] = {0, 1, 2, 7}, kRight[] = {3, 4, 5, 6};
        const bool right_col = m_space_focus >= 3 && m_space_focus <= 6;
        const int* col = right_col ? kRight : kLeft;
        int row = 0;
        for (int k = 0; k < 4; k++) {
            if (col[k] == m_space_focus) row = k;
        }
        if (Repeat(down, held, HidNpadButton_AnyUp) && row > 0) m_space_focus = col[row - 1];
        if (Repeat(down, held, HidNpadButton_AnyDown) && row < 3) m_space_focus = col[row + 1];
        if (Repeat(down, held, HidNpadButton_AnyLeft) && right_col) m_space_focus = kLeft[row];
        if (Repeat(down, held, HidNpadButton_AnyRight) && !right_col) m_space_focus = kRight[row];
    }
    if (down & HidNpadButton_B) {
        m_more = MoreScreen::Tiles;
        return;
    }
    if ((down & HidNpadButton_X) && v.task == Space::Task::Dump) {
        m_space->Cancel();
        return;
    }
    if ((down & HidNpadButton_A) && m_space_focus == 6) {
        OpenSdCheck();
        return;
    }
    if ((down & HidNpadButton_A) && m_space_focus == 7) {
        OpenCompress(); // it opens at once; the list comes when the scan ends
        return;
    }
    if ((down & HidNpadButton_A) && !v.scanned) {
        Notice(m.scanning); // never silent: the console is still being read
        return;
    }
    if (!(down & HidNpadButton_A) || !v.scanned) return;
    if (m_space->Busy()) {
        Notice(m.st_dumping);
        return;
    }
    const Config c = m_link.CurrentConfig();
    switch (m_space_focus) {
        case 0:
            if (!v.leftovers.Any()) return;
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::SpaceClean;
            break;
        case 1: OpenPicker(0); break;
        case 2: OpenPicker(1); break;
        case 3: OpenPlanner(); break;
        case 4:
            if (!v.card) return;
            if (!c.Linked()) {
                Notice(m.need_link);
                return;
            }
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::SpaceCard;
            break;
        case 5:
            if (!c.Linked()) {
                Notice(m.need_link);
                return;
            }
            OpenPicker(2);
            break;
    }
}

// The questions of this screen (drawn by DrawDialog, confirmed by ConfirmDialog).
void App::SpaceDialogText(std::string& title, std::string& body, std::string& primary) {
    const More& m = TrM();
    const auto& s = Tr();
    const auto v = m_space ? m_space->Snapshot() : Space::View{};
    char text[600];
    switch (m_dialog.kind) {
        case DialogKind::SpaceClean:
            title = m.clean_title;
            std::snprintf(text, sizeof(text), m.cl_body_fmt, draw::Size(v.leftovers.bytes).c_str(), v.leftovers.contents, v.leftovers.placeholders,
                          v.leftovers.addons, v.leftovers.tickets);
            body = text;
            primary = m.clean_go;
            break;
        case DialogKind::SpaceArchive:
            std::snprintf(text, sizeof(text), m.archive_title_fmt, int(m_dialog.ids.size()));
            title = text;
            std::snprintf(text, sizeof(text), m.archive_body_fmt, m_dialog.text.c_str());
            body = text;
            primary = m.archive_go;
            break;
        case DialogKind::SpaceMove:
            std::snprintf(text, sizeof(text), m.move_title_fmt, m_dialog.text.c_str());
            title = text;
            std::snprintf(text, sizeof(text), m.move_body_fmt, m_dialog.to_nand ? s.sd_card : s.internal,
                          m_dialog.to_nand ? s.internal : s.sd_card, draw::Size(m_dialog.size).c_str());
            body = text;
            if (m_dialog.to_nand) body += std::string("\n\n") + s.nand_body;
            primary = m.move_go;
            break;
        case DialogKind::SpaceCard:
            title = m.card_title;
            body = m.card_body;
            primary = m.dump_go;
            break;
        case DialogKind::SpaceDump:
            std::snprintf(text, sizeof(text), m.dump_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m.dump_body;
            primary = m.dump_go;
            break;
        default: break;
    }
}

void App::ConfirmSpaceDialog() {
    if (!m_space) return;
    const Config c = m_link.CurrentConfig();
    switch (m_dialog.kind) {
        case DialogKind::SpaceClean: m_space->StartClean(); break;
        case DialogKind::SpaceArchive:
            m_space->StartArchive(m_dialog.ids);
            m_space_mode = 0;
            break;
        case DialogKind::SpaceMove:
            m_space->StartMove(m_dialog.ids[0], m_dialog.to_nand);
            m_space_mode = 0;
            break;
        case DialogKind::SpaceCard: {
            const time_t now = time(nullptr);
            const struct tm* lt = localtime(&now);
            char name[64];
            std::snprintf(name, sizeof(name), "Game card %04d-%02d-%02d %02d%02d.nsp", lt ? lt->tm_year + 1900 : 2026, lt ? lt->tm_mon + 1 : 1,
                          lt ? lt->tm_mday : 1, lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
            m_space->StartCardDump(c.url, c.key, name);
            break;
        }
        case DialogKind::SpaceDump:
            m_space->StartDump(c.url, c.key, m_dialog.ids[0], unsigned(m_dialog.ids[1]), (unsigned char)m_dialog.ids[2], m_dialog.ids[3] != 0,
                               m_dialog.from);
            m_space_mode = 0;
            break;
        default: break;
    }
}

} // namespace nxdock
