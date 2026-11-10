// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Check the SD card (17A of the visual test of phases 16 to 23), from Space and dumps. On the
// left, what the check does and the choice between quick and full, each with how long it takes;
// on the right, the free space of the card as a grid that fills as it is written (blue) and read
// back (green), and turns red where the card fails or ends. The result says it plainly: a good
// card, a fake one and how much it really holds, or the parts that fail.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "sdcheck_nx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nxdock {
namespace {

std::string Took(double seconds) {
    char text[32];
    const int sec = int(seconds + 0.5);
    if (sec >= 3600) std::snprintf(text, sizeof(text), "%d h %02d min", sec / 3600, (sec / 60) % 60);
    else if (sec >= 60) std::snprintf(text, sizeof(text), "%d min", std::max(1, (sec + 30) / 60));
    else std::snprintf(text, sizeof(text), "%d s", sec);
    return text;
}

constexpr int kCols = 20, kRows = 12, kCells = kCols * kRows;

} // namespace

void App::OpenSdCheck() {
    if (!m_sdcheck) {
        m_sdcheck = std::make_unique<SdChecker>();
        SdChecker::CleanLeftovers();
    }
    SdChecker::Space(m_sdc_free, m_sdc_total);
    m_sd_opened = m_time;
    m_space_mode = 2;
}

void App::DrawSdCheck(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const auto v = m_sdcheck->Snapshot();
    const float lx = 48, rx = 664, cw = 568;
    const float in = ui::EaseOut(ui::Appear(m_sd_opened, 0.35));
    char text[400];

    // what the free space of the card is, for the grid and the times
    const long long free_b = v.running || v.done ? v.free_bytes : m_sdc_free;
    const long long total_b = v.running || v.done ? v.total_bytes : m_sdc_total;
    const long long margin = static_cast<long long>(kSdMargin);
    const double usable = free_b > margin ? double(free_b - margin) : 0;
    const double used_before = total_b > free_b && free_b >= 0 ? double(total_b - free_b) : 0;

    // ---- the left: what it does, the choice, or the result
    nvgSave(vg);
    nvgTranslate(vg, -24 * (1 - in), 0);
    nvgGlobalAlpha(vg, in);
    ui::Text(vg, lx, 116, 26, t.text, m.sd_title);
    const bool result = v.done && !v.running && v.result.kind != SdCheckResult::Kind::None;
    if (!v.running && !result) {
        float y = 142;
        y += draw::Box(vg, lx, y, cw, 16, t.muted, m.sd_explain) + 12;
        // the risk on a fake card, said plainly, with a small sign
        nvgBeginPath(vg);
        nvgCircle(vg, lx + 9, y + 10, 9);
        nvgStrokeColor(vg, t.warn);
        nvgStrokeWidth(vg, 1.6f);
        nvgStroke(vg);
        ui::Text(vg, lx + 9, y + 10, 13, t.warn, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        y += draw::Box(vg, lx + 28, y, cw - 28, 14, t.muted, m.sd_risk) + 22;
        // quick or full, each with how long it takes
        for (int i = 0; i < 2; i++) {
            const float bx = lx + i * (cw / 2 + 6), bw = cw / 2 - 6, bh = 104;
            const bool on = m_sd_choice == i;
            draw::Panel(vg, t, bx, y, bw, bh, 14);
            if (on) {
                NVGcolor fill = t.work;
                fill.a = 0.12f;
                ui::FillRound(vg, bx, y, bw, bh, 14, fill);
                ui::Focus(vg, t, bx, y, bw, bh, 14, m_time);
            }
            AddHit(bx, y, bw, bh, HitKind::SdChoice, i);
            ui::Text(vg, bx + 18, y + 28, 19, on ? t.work : t.text, i == 0 ? m.sd_quick : m.sd_full);
            const double secs = SdCheckEstimate(free_b > 0 ? u64(free_b) : 0, i == 0, 0, 0);
            std::snprintf(text, sizeof(text), i == 0 ? m.sd_quick_sub : m.sd_full_sub, Took(secs).c_str());
            draw::Box(vg, bx + 18, y + 46, bw - 36, 14, t.muted, text);
        }
        y += 104 + 24;
        // the button
        const float bw = 260, bh = 52;
        ui::FillRound(vg, lx, y, bw, bh, 14, t.work);
        AddHit(lx, y, bw, bh, HitKind::SdChoice, 2);
        ui::Symbol(vg, t, lx + 30, y + bh / 2, 22, ui::glyph::A, t.on_work, HidNpadButton_A);
        ui::Text(vg, lx + 52, y + bh / 2, 18, t.on_work, m.sd_start, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    } else if (v.running) {
        const char* what = v.phase <= 1 ? m.sd_writing : v.phase == 2 ? m.sd_reading : m.sd_deleting;
        ui::Text(vg, lx, 160, 18, t.text, what);
        const float frac = v.bytes ? float(double(v.bytes_done) / double(v.bytes)) : 0.f;
        ui::Progress(vg, t, lx, 184, cw, 12, frac, t.work, m_time);
        const double rate = v.phase <= 1 ? v.write_mb : v.read_mb;
        const double left = rate > 0.5 && v.bytes ? double(v.bytes - std::min(v.bytes, v.bytes_done)) / (rate * 1048576.0) : -1;
        std::snprintf(text, sizeof(text), "%d %% · %s%s", int(frac * 100), Took(v.elapsed).c_str(), left >= 0 ? " · " : "");
        std::string line = text;
        if (left >= 0) {
            char l[64];
            std::snprintf(l, sizeof(l), m.sd_left, Took(left).c_str());
            line += l;
        }
        ui::Text(vg, lx, 222, 15, t.muted, line.c_str());
        // the two speeds, as lanes
        const float lw = (cw - 14) / 2;
        for (int i = 0; i < 2; i++) {
            const float x = lx + i * (lw + 14), y = 252;
            ui::FillRound(vg, x, y, lw, 96, 12, t.panel2);
            ui::StrokeRound(vg, x + .5f, y + .5f, lw - 1, 95, 12, t.line, 1);
            const NVGcolor c = i == 0 ? t.work : t.ok;
            nvgBeginPath(vg);
            nvgCircle(vg, x + 18, y + 22, 5);
            nvgFillColor(vg, c);
            nvgFill(vg);
            ui::Text(vg, x + 30, y + 22, 14, t.muted, i == 0 ? m.sd_write_speed : m.sd_read_speed);
            const double mb = i == 0 ? v.write_mb : v.read_mb;
            std::snprintf(text, sizeof(text), mb > 0.05 ? "%.1f" : "—", mb);
            const float vw = ui::Text(vg, x + 16, y + 56, 28, t.text, text) - (x + 16);
            ui::Text(vg, x + 16 + vw + 6, y + 58, 14, t.muted, "MB/s");
        }
        draw::Box(vg, lx, 372, cw, 14, t.muted, m.sd_keep_on);
    } else {
        // the result
        const auto& r = v.result;
        const bool good = r.kind == SdCheckResult::Kind::Good;
        const bool wrong = r.kind == SdCheckResult::Kind::Fake || r.kind == SdCheckResult::Kind::Bad || r.kind == SdCheckResult::Kind::Failed;
        const NVGcolor c = good ? t.ok : wrong ? t.err : t.muted;
        const float pop = ui::EaseOutBack(ui::Appear(m_sd_result_at, 0.45));
        const float cx = lx + 44, cy = 196;
        NVGcolor halo = c;
        halo.a = 0.16f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 40 * pop);
        nvgFillColor(vg, halo);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 28 * pop);
        nvgFillColor(vg, c);
        nvgFill(vg);
        if (good) {
            ui::CheckMark(vg, cx, cy, 15 * pop, t.on_work, 3.2f);
        } else {
            ui::Text(vg, cx, cy + 1, 30 * pop, t.on_work, wrong ? "!" : "i", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        std::string title, body;
        char a[64], b[64];
        switch (r.kind) {
            case SdCheckResult::Kind::Good:
                title = m.sd_good_t;
                std::snprintf(text, sizeof(text), m.sd_good_d, draw::Size(r.checked).c_str());
                body = text;
                break;
            case SdCheckResult::Kind::Fake:
                std::snprintf(text, sizeof(text), m.sd_fake_t, draw::Size(r.real).c_str());
                title = text;
                std::snprintf(a, sizeof(a), "%s", draw::Size(u64(std::max(0LL, total_b))).c_str());
                std::snprintf(b, sizeof(b), "%s", draw::Size(r.real).c_str());
                std::snprintf(text, sizeof(text), m.sd_fake_d, a, b);
                body = text;
                break;
            case SdCheckResult::Kind::Bad:
                title = m.sd_bad_t;
                std::snprintf(a, sizeof(a), "%s", draw::Size(r.bad).c_str());
                std::snprintf(b, sizeof(b), "%s", draw::Size(r.first_bad).c_str());
                std::snprintf(text, sizeof(text), m.sd_bad_d, a, b);
                body = text;
                break;
            case SdCheckResult::Kind::TooFull:
                title = m.sd_full_t;
                body = m.sd_full_d;
                break;
            case SdCheckResult::Kind::Stopped:
                title = m.sd_stopped_t;
                body = m.sd_stopped;
                break;
            default:
                title = m.sd_failed_t;
                body = m.sd_failed_d;
                break;
        }
        nvgSave(vg);
        nvgScissor(vg, lx + 100, 160, cw - 100, 80);
        draw::Box(vg, lx + 100, 172, cw - 100, 22, good ? t.text : c, title.c_str());
        nvgRestore(vg);
        float y = 256;
        y += draw::Box(vg, lx, y, cw, 16, t.text, body.c_str()) + 18;
        if (r.write_mb > 0 || r.read_mb > 0) {
            std::snprintf(text, sizeof(text), m.sd_speeds_fmt, r.write_mb, r.read_mb, Took(r.seconds).c_str());
            ui::Text(vg, lx, y + 8, 15, t.muted, text);
            y += 34;
        }
        // again, with the same buttons
        const float bw = 260, bh = 52;
        ui::FillRound(vg, lx, y + 10, bw, bh, 14, t.panel2);
        ui::StrokeRound(vg, lx + .5f, y + 10.5f, bw - 1, bh - 1, 14, t.line, 1);
        AddHit(lx, y + 10, bw, bh, HitKind::SdChoice, 3);
        ui::Symbol(vg, t, lx + 30, y + 10 + bh / 2, 22, ui::glyph::A, t.text, HidNpadButton_A);
        ui::Text(vg, lx + 52, y + 10 + bh / 2, 18, t.text, m.sd_again, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);

    // ---- the right: the card, as a grid of its free space
    draw::Panel(vg, t, rx, 92, cw, 470, 16);
    if (free_b >= 0 && total_b > 0) {
        std::snprintf(text, sizeof(text), m.sd_free_fmt, draw::Size(u64(free_b)).c_str(), draw::Size(u64(total_b)).c_str());
        draw::Section(vg, t, rx + 22, 120, text);
    }
    const float gx = rx + 22, gy = 140, gw = cw - 44, gh = 340;
    const float cell = std::min((gw - (kCols - 1) * 5) / kCols, (gh - (kRows - 1) * 5) / kRows);
    // how far the writing and the reading got, and where the card fails
    float wrote = 0, read = 0, bad_from = 2, bad_at = -1;
    if (v.running) {
        const double half = v.bytes ? double(v.bytes) / 2 : 1;
        if (v.phase <= 1) wrote = float(double(v.bytes_done) / half);
        else {
            wrote = 1;
            read = float((double(v.bytes_done) - half) / half);
        }
    } else if (result) {
        const auto& r = v.result;
        if (r.kind == SdCheckResult::Kind::Good) wrote = read = 1;
        if (r.kind == SdCheckResult::Kind::Fake && usable > 0) {
            wrote = read = 1;
            bad_from = float(std::clamp((double(r.real) - used_before) / usable, 0.0, 1.0));
        }
        if (r.kind == SdCheckResult::Kind::Bad && usable > 0) {
            wrote = read = 1;
            bad_at = float(double(r.first_bad) / usable);
        }
    }
    const float reveal = ui::Still() ? 1.f : std::min(1.f, float(m_time - m_sd_result_at) * 2.5f);
    for (int i = 0; i < kCells; i++) {
        const int col = i % kCols, row = i / kCols;
        const float x = gx + col * (cell + 5), y = gy + row * (cell + 5);
        const float at = float(i) / kCells, next = float(i + 1) / kCells;
        NVGcolor c = t.panel2;
        if (next <= wrote) {
            c = t.work;
            c.a = 0.55f;
        }
        if (next <= read) c = t.ok;
        const bool now = v.running && ((v.phase <= 1 && at <= wrote && wrote < next) || (v.phase == 2 && at <= read && read < next));
        if (now) {
            c = v.phase <= 1 ? t.work : t.ok;
            c.a = 0.55f + 0.45f * float(0.5 + 0.5 * std::sin(m_time * 8));
        }
        if (result && (at >= bad_from || (bad_at >= at && bad_at < next))) {
            // the red spreads from where the card fails (16A of the result)
            const float spread = (at - std::min(bad_from, bad_at >= 0 ? bad_at : 2.f)) * 3;
            if (spread <= reveal * 3) c = t.err;
        }
        ui::FillRound(vg, x, y, cell, cell, 4, c);
    }
    // the key of the colours
    const struct { NVGcolor c; const char* label; } key[] = {{t.panel2, m.sd_key_free}, {t.work, m.sd_key_written}, {t.ok, m.sd_key_ok}, {t.err, m.sd_key_bad}};
    float kx = gx;
    for (const auto& k : key) {
        NVGcolor c = k.c;
        if (&k == &key[1]) c.a = 0.55f;
        ui::FillRound(vg, kx, 512, 14, 14, 3, c);
        kx = ui::Text(vg, kx + 22, 519, 14, t.muted, k.label) + 22;
    }

    std::vector<ui::Hint> hints;
    if (v.running) {
        hints.push_back({ui::glyph::X, m.sd_stop, HidNpadButton_X});
    } else if (result) {
        hints.push_back({ui::glyph::A, m.sd_again, HidNpadButton_A});
    } else {
        hints.push_back({ui::glyph::DPad, s.choose, HidNpadButton_AnyLeft});
        hints.push_back({ui::glyph::A, m.sd_start, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateSdCheck(u64 down, u64 held) {
    const auto v = m_sdcheck->Snapshot();
    if (v.done && !v.running && m_sd_seen_done == false) {
        m_sd_seen_done = true;
        m_sd_result_at = m_time;
        m_sd_checked = -100; // the free space changes back
        if (m_ui.sounds) m_sound.Play(synth::Effect::Finish);
    }
    if (down & HidNpadButton_B) {
        m_space_mode = 0; // a check that runs goes on; the row says how far it got
        return;
    }
    if (v.running) {
        if (down & HidNpadButton_X) m_sdcheck->Stop();
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_sd_choice = 0;
    if (Repeat(down, held, HidNpadButton_AnyRight)) m_sd_choice = 1;
    if (down & HidNpadButton_A) {
        if (m_sdcheck->Start(m_sd_choice == 0)) { // 0 quick, 1 full
            m_sd_seen_done = false;
        }
    }
}

} // namespace nxdock
