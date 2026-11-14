// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Installed: what this session installed (and undoing it) and the history.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

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
            ui::Text(vg, 134, y + 47, 14, t.muted, (i.label + " · " + AsciiUpper(i.format)).c_str());
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
    hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus}); // Undo all among them, as its button (Y)
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
            ui::Text(vg, c_name, cy + 12, 13, t.muted, (e.label + " · " + AsciiUpper(e.format)).c_str());
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
                {{ui::glyph::A, s.details, HidNpadButton_A}, {ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus},
                 {ViewGlyphs(), s.view_switch, HidNpadButton_ZR}, {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::AskUndo(std::vector<int> jobs) {
    if (m_queue->Busy() || m_queue->Undoing()) {
        Notice(Tr().undo_wait_queue);
        return;
    }
    OpenDialog(DialogKind::Undo);
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
            OpenDialog(DialogKind::HistoryInfo);
            m_dialog.entry = m_installed_focus;
            m_dialog.single = true;
        }
    }
}

bool CanUndo(const SessionItem& item) { return Undoable(item); }

} // namespace nxdock
