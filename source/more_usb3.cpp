// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// USB 3.0 in More › From the computer (MTP) and More › NS-USBloader (USB A of the visual test of
// 14/11). The console starts its USB at 2.0; Atmosphère turns 3.0 on at the next start with a line
// in system_settings.ini. Once the cable is in, a chip says the speed it runs at: "can go faster"
// at 2.0 with the setting off, blue at 3.0, orange when the file says on and the console still
// waits for its reboot. A or a tap opens a sheet with what it gives, the risk (Bluetooth controllers
// and 2.4 GHz WiFi) and how it is turned on, the steps by hand, and the button that does it: a copy
// of the file first, the line changed in place, then "Reboot now", which waits for the queue.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "mtp.hpp"
#include "usb_tinfoil.hpp"

#include <algorithm>
#include <cstdio>

namespace nxdock {

namespace {

// what a button of the sheet does
enum : int { kClose, kTurnOn, kTurnOff, kUndo, kReboot };

struct SheetButton {
    const char* glyph;
    const char* label;
    u64 mask;
    int action;
    bool primary;
};

// The state the sheet speaks of: 0 off, 1 on, 2 on in the file but not yet (a reboot), 3 off in the
// file but still on.
int SheetState(const Usb30State& s, int speed) {
    const bool active = s.active == 1 || (s.active < 0 && speed == 3);
    if (s.FileOn() && !active) return 2;
    if (s.file == 0 && active) return 3;
    return active ? 1 : 0;
}

std::vector<SheetButton> SheetButtons(int state) {
    const Tools& tt = TrT();
    switch (state) {
        case 0: return {{ui::glyph::B, tt.u3_not_now, HidNpadButton_B, kClose, false}, {ui::glyph::A, tt.u3_turn_on, 0, kTurnOn, true}};
        case 1: return {{ui::glyph::X, tt.u3_turn_off, HidNpadButton_X, kTurnOff, false}, {ui::glyph::A, Tr().close, 0, kClose, true}};
        default:
            return {{ui::glyph::X, tt.u3_undo, HidNpadButton_X, kUndo, false},
                    {ui::glyph::B, tt.u3_not_now, HidNpadButton_B, kClose, false},
                    {ui::glyph::A, tt.u3_reboot_now, 0, kReboot, true}};
    }
}

// the first button in focus: the one that changes nothing
int SheetFocus(int state) { return state == 0 ? 0 : 1; }

float Wrapped(NVGcontext* vg, float w, float size, const char* text) { return draw::BoxHeight(vg, w, size, text); }

} // namespace

void App::FollowUsb3(bool connected) {
    if (!m_usb3_read) {
        m_usb3 = ReadUsb30(); // the file and the setting in use, once a screen opens
        m_usb3_read = true;
    }
    if (!connected) {
        m_usb_speed = 0;
    } else if (m_time - m_usb_speed_at > (m_usb_speed ? 1.0 : 0.25)) {
        m_usb_speed_at = m_time;
        m_usb_speed = UsbSpeedNow();
    }
    const int kind = Usb3ChipKind();
    if (kind != m_usb3_shown) {
        m_usb3_shown = kind;
        m_usb3_at = m_time; // it comes in again with its new face
    }
}

int App::Usb3ChipKind() const {
    if (m_usb_speed == 0) return kU3None;
    if (m_usb_speed == 3) return kU3Fast;
    if (m_usb3.FileOn() && m_usb3.active != 1) return kU3Pending;
    if (m_usb3.ActiveOn()) return kU3Cable; // on, but this cable or port runs at 2.0
    return kU3Slow;
}

const char* App::Usb3RowText() const {
    const Tools& tt = TrT();
    switch (Usb3ChipKind()) {
        case kU3Fast: return tt.u3_r_fast;
        case kU3Pending: return tt.u3_r_pending;
        case kU3Cable: return tt.u3_r_cable;
        default: return tt.u3_r_slow;
    }
}

namespace {

std::string ChipLabel(const char* full, bool short_form) {
    std::string s = full;
    if (short_form) {
        // only what comes before " · ": the colour and the mark say the rest
        const size_t cut = s.find(" \xC2\xB7 ");
        if (cut != std::string::npos) s.erase(cut);
    }
    return s;
}

} // namespace

const char* App::Usb3ChipText() const {
    const Tools& tt = TrT();
    switch (Usb3ChipKind()) {
        case kU3Fast: return tt.u3_chip_fast;
        case kU3Pending: return tt.u3_chip_pending;
        case kU3Cable: return tt.u3_chip_cable;
        default: return tt.u3_chip_slow;
    }
}

float App::Usb3ChipWidth(NVGcontext* vg, bool short_form) const {
    return ui::TextWidth(vg, 15, ChipLabel(Usb3ChipText(), short_form).c_str()) + 78;
}

void App::DrawUsb3Chip(NVGcontext* vg, float x, float y, bool focused, bool short_form) {
    const Theme& t = m_theme;
    const int kind = Usb3ChipKind();
    if (kind == kU3None) return;
    const std::string label = ChipLabel(Usb3ChipText(), short_form);
    const float w = Usb3ChipWidth(vg, short_form), h = 44;
    const NVGcolor col = kind == kU3Pending ? t.warn : t.work;
    const bool strong = kind == kU3Fast || kind == kU3Pending;
    // it pops in when the cable comes, and when it changes
    const float in = ui::Appear(m_usb3_at, 0.45f), pop = ui::EaseOutBack(in);
    nvgSave(vg);
    const float cx = x + w / 2, cy = y + h / 2, sc = 0.6f + 0.4f * pop;
    nvgTranslate(vg, cx, cy);
    nvgScale(vg, sc, sc);
    nvgTranslate(vg, -cx, -cy);
    nvgGlobalAlpha(vg, std::min(1.f, in * 4));
    ui::FillRound(vg, x, y, w, h, 22, t.panel2);
    NVGcolor fill = col;
    fill.a = strong ? 0.2f : 0.08f;
    ui::FillRound(vg, x, y, w, h, 22, fill);
    NVGcolor edge = col;
    edge.a = strong ? 0.65f : 0.45f;
    ui::StrokeRound(vg, x + .75f, y + .75f, w - 1.5f, h - 1.5f, 22, edge, 1.5f);
    if (focused) ui::Focus(vg, t, x, y, w, h, 22, m_time);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 20, cy, 5);
    nvgFillColor(vg, col);
    nvgFill(vg);
    ui::Text(vg, x + 34, cy, 15, strong ? col : t.text, label.c_str());
    // the mark that says it opens something
    nvgBeginPath(vg);
    nvgCircle(vg, x + w - 22, cy, 9);
    nvgStrokeColor(vg, col);
    nvgStrokeWidth(vg, 1.8f);
    nvgStroke(vg);
    ui::Text(vg, x + w - 22, cy + 1, 13, col, "i", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    // a ring once it has landed
    const double age = m_time - m_usb3_at;
    if (ui::Lively() && age > 0.35 && age < 0.85) {
        NVGcolor ring = col;
        ring.a = float(1 - std::abs(age - 0.6) / 0.25);
        ui::StrokeRound(vg, x - 6, y - 6, w + 12, h + 12, 28, ring, 3);
    }
    nvgRestore(vg);
    AddHit(x, y - 2, w, h + 4, HitKind::UsbChip);
}

void App::OpenUsb3() {
    m_usb3 = ReadUsb30(); // as it is now
    m_usb3_read = true;
    m_usb3_error.clear();
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Usb3;
    m_dialog.focus = SheetFocus(SheetState(m_usb3, m_usb_speed));
}

void App::Usb3Do(int action) {
    const Tools& tt = TrT();
    switch (action) {
        case kTurnOn:
        case kTurnOff: {
            const bool on = action == kTurnOn;
            bool backed = false;
            if (!WriteUsb30(on, backed)) {
                m_usb3_error = tt.u3_failed;
                return;
            }
            m_usb3 = ReadUsb30();
            m_usb3_backed = backed;
            m_usb3_written_on = on;
            // done: reboot now or later, the focus on now (that was asked for)
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Usb3Reboot;
            m_dialog.focus = 1;
            return;
        }
        case kUndo: {
            // the file as the setting in use says
            bool backed = false;
            const bool active = m_usb3.ActiveOn() || (m_usb3.active < 0 && m_usb_speed == 3);
            if (!WriteUsb30(active, backed)) {
                m_usb3_error = tt.u3_failed;
                return;
            }
            m_usb3 = ReadUsb30();
            m_dialog = Dialog{};
            NoticeOk(tt.u3_undone);
            return;
        }
        case kReboot:
            m_dialog = Dialog{};
            RebootWhenIdle();
            return;
        default: m_dialog = Dialog{}; return;
    }
}

void App::UpdateUsb3Sheet(u64 down, u64 held) {
    const auto buttons = SheetButtons(SheetState(m_usb3, m_usb_speed));
    const int n = int(buttons.size());
    if (Repeat(down, held, HidNpadButton_AnyLeft) && m_dialog.focus > 0) m_dialog.focus--;
    if (Repeat(down, held, HidNpadButton_AnyRight) && m_dialog.focus < n - 1) m_dialog.focus++;
    m_dialog.focus = std::clamp(m_dialog.focus, 0, n - 1);
    if (down & HidNpadButton_A) {
        Usb3Do(buttons[size_t(m_dialog.focus)].action);
        return;
    }
    for (const auto& b : buttons) {
        if (b.mask && (down & b.mask)) {
            Usb3Do(b.action);
            return;
        }
    }
    if (down & HidNpadButton_B) m_dialog = Dialog{};
}

void App::DrawUsb3Sheet(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const Tools& tt = TrT();
    const int state = SheetState(m_usb3, m_usb_speed);
    const auto buttons = SheetButtons(state);
    // everything behind goes dark; a tap there closes it
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f * ui::EaseOut(ui::Appear(m_dialog_at, 0.2f))));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::DialogBack);

    // what it gives, the risk, and how (or what is waiting)
    const char* how = state == 0 ? tt.u3_how_off : state == 1 ? tt.u3_how_on : state == 2 ? tt.u3_until : tt.u3_until_off;
    const char* lines[3] = {tt.u3_gives, tt.u3_risk, how};
    const float w = 940, pad = 28, tw = w - 2 * pad - 52;
    float lines_h = 0, line_h[3];
    for (int i = 0; i < 3; i++) {
        line_h[i] = std::max(26.f, Wrapped(vg, tw, 17, lines[i]));
        lines_h += line_h[i] + 12;
    }
    const float manual_h = 164, err_h = m_usb3_error.empty() ? 0 : Wrapped(vg, w - 2 * pad, 15, m_usb3_error.c_str()) + 14;
    const float h = 86 + lines_h + 8 + manual_h + 18 + err_h + 56 + pad;
    const float x = (kWidth - w) / 2, y = std::max(80.f, (kHeight - 56 - h) / 2);
    // it grows out of the middle with a soft spring
    const float pop = ui::EaseOutBack(ui::Appear(m_dialog_at, 0.32f));
    nvgSave(vg);
    nvgTranslate(vg, x + w / 2, y + h / 2);
    nvgScale(vg, 0.88f + 0.12f * pop, 0.88f + 0.12f * pop);
    nvgTranslate(vg, -(x + w / 2), -(y + h / 2));
    const float a = std::min(1.f, pop * 1.4f);
    nvgGlobalAlpha(vg, a);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 10, y + 6, w + 20, h + 20, 22);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, w, h, 18, 40, nvgRGBAf(0, 0, 0, 0.5f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, w, h, 18);
    AddHit(x, y, w, h, HitKind::Button); // the panel itself does nothing

    // the title, and what it is now
    NVGcolor ring = t.work;
    ring.a = 0.18f;
    nvgBeginPath(vg);
    nvgCircle(vg, x + pad + 22, y + 46, 20);
    nvgFillColor(vg, ring);
    nvgFill(vg);
    ui::Text(vg, x + pad + 22, y + 47, 24, t.work, "i", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    ui::Title(vg, x + pad + 56, y + 46, 24, t.text, tt.u3_title);
    {
        const bool fast = m_usb_speed == 3;
        const char* now = state == 2 ? tt.u3_now_on_pending : state == 3 ? tt.u3_now_off_pending : fast ? tt.u3_now_30 : tt.u3_now_20;
        const NVGcolor col = state >= 2 ? t.warn : fast ? t.work : t.muted;
        const float pw = ui::TextWidth(vg, 14, now) + 28, px = x + w - pad - pw;
        NVGcolor fill = col;
        fill.a = 0.16f;
        ui::FillRound(vg, px, y + 32, pw, 28, 14, fill);
        ui::Text(vg, px + pw / 2, y + 46, 14, col, now, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    // the three lines come in one after another
    float ly = y + 86;
    for (int i = 0; i < 3; i++) {
        const float in = ui::EaseOut(ui::Appear(m_dialog_at + 0.14 + 0.08 * i, 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, (1 - in) * 24, 0);
        nvgGlobalAlpha(vg, a * in);
        const float mx = x + pad + 22, my = ly + 13;
        if (i == 1) {
            // the risk, with an orange triangle
            nvgBeginPath(vg);
            nvgMoveTo(vg, mx, my - 11);
            nvgLineTo(vg, mx + 12, my + 10);
            nvgLineTo(vg, mx - 12, my + 10);
            nvgClosePath(vg);
            nvgFillColor(vg, t.warn);
            nvgFill(vg);
            ui::Text(vg, mx, my + 3, 13, t.panel, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        } else {
            nvgBeginPath(vg);
            nvgCircle(vg, mx, my, 6);
            nvgFillColor(vg, i == 2 && state >= 2 ? t.warn : t.work);
            nvgFill(vg);
        }
        draw::Box(vg, x + pad + 52, ly + 2, tw, 17, t.text, lines[i]);
        nvgRestore(vg);
        ly += line_h[i] + 12;
    }
    // the steps by hand, the two lines apart
    {
        const float in = ui::EaseOut(ui::Appear(m_dialog_at + 0.4, 0.35f));
        const float by = ly + 8 + (1 - in) * 14, bx = x + pad, bw = w - 2 * pad;
        nvgSave(vg);
        nvgGlobalAlpha(vg, a * in);
        ui::FillRound(vg, bx, by, bw, manual_h, 14, t.panel);
        ui::StrokeRound(vg, bx + .5f, by + .5f, bw - 1, manual_h - 1, 14, t.line, 1);
        ui::Text(vg, bx + 24, by + 28, 14, t.muted, tt.u3_manual);
        const float code_w = 360, code_x = bx + bw - 24 - code_w, code_y = by + 46;
        const char* steps[3] = {tt.u3_step1, tt.u3_step2, tt.u3_step3};
        nvgSave(vg);
        nvgIntersectScissor(vg, bx + 16, by, code_x - bx - 28, manual_h);
        for (int i = 0; i < 3; i++) ui::Text(vg, bx + 24, by + 62 + 30 * i, 15, t.text, steps[i]);
        nvgRestore(vg);
        ui::FillRound(vg, code_x, code_y, code_w, 76, 10, t.bg);
        ui::StrokeRound(vg, code_x + .5f, code_y + .5f, code_w - 1, 75, 10, t.line, 1);
        ui::Text(vg, code_x + 18, code_y + 24, 16, t.muted, "[usb]");
        ui::Text(vg, code_x + 18, code_y + 52, 16, t.text, "usb30_force_enabled = u8!0x1");
        nvgRestore(vg);
        ly += 8 + manual_h + 18;
    }
    if (!m_usb3_error.empty()) {
        draw::Box(vg, x + pad, ly, w - 2 * pad, 15, t.err, m_usb3_error.c_str());
        ly += err_h;
    }
    // the buttons, as wide as they can be; the one in focus with its ring
    {
        const float in = ui::EaseOut(ui::Appear(m_dialog_at + 0.3, 0.3f));
        const float gap = 12, by = y + h - pad - 56;
        const float bw = (w - 2 * pad - gap * float(buttons.size() - 1)) / float(buttons.size());
        m_dialog.focus = std::clamp(m_dialog.focus, 0, int(buttons.size()) - 1);
        for (size_t i = 0; i < buttons.size(); i++) {
            const auto& b = buttons[i];
            const float bx = x + pad + float(i) * (bw + gap);
            nvgGlobalAlpha(vg, a * in);
            if (int(i) == m_dialog.focus) ui::Focus(vg, t, bx, by, bw, 56, 14, m_time);
            if (b.primary) {
                ui::FillRound(vg, bx, by, bw, 56, 14, t.work);
            } else {
                ui::FillRound(vg, bx, by, bw, 56, 14, t.panel2);
                ui::StrokeRound(vg, bx + 1, by + 1, bw - 2, 54, 14, t.line, 2);
            }
            const NVGcolor fg = b.primary ? t.on_work : t.text;
            const float lw = ui::TextWidth(vg, 17, b.label) + 34, lx = bx + std::max(12.f, (bw - lw) / 2);
            ui::Symbol(vg, t, lx + 12, by + 28, 22, b.glyph, fg, b.mask ? b.mask : u64(HidNpadButton_A));
            nvgSave(vg);
            nvgIntersectScissor(vg, lx + 30, by, bx + bw - lx - 36, 56);
            ui::Text(vg, lx + 32, by + 28, 17, fg, b.label);
            nvgRestore(vg);
            AddHit(bx, by, bw, 56, HitKind::DialogButton, int(i));
        }
    }
    nvgGlobalAlpha(vg, 1.f);
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.choose, HidNpadButton_A}, {ui::glyph::B, state == 1 ? s.close : tt.u3_not_now, HidNpadButton_B}}, rects);
    AddHints(rects);
}

// ---- the reboot: now, or once nothing is being installed ----

void App::RebootWhenIdle() {
    const bool busy = (m_queue && (m_queue->Busy() || m_queue->Undoing())) || m_fwork || m_usb_active ||
                      (m_vault && m_vault->Busy()) || (m_space && m_space->Busy());
    if (busy) {
        m_reboot_waiting = true;
        Notice(TrT().u3_waiting);
        m_notice_until = m_time + 5;
        return;
    }
    m_reboot_waiting = false;
    if (!RebootConsole()) Notice(TrT().u3_reboot_failed);
}

void App::FollowReboot() {
    if (!m_reboot_waiting || m_dialog.kind != DialogKind::None) return;
    const bool busy = (m_queue && (m_queue->Busy() || m_queue->Undoing())) || m_fwork || m_usb_active ||
                      (m_vault && m_vault->Busy()) || (m_space && m_space->Busy());
    if (!busy) RebootWhenIdle();
}

} // namespace nxdock
