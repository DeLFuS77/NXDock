// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The report for the library (4A of the visual test of 18/11): the logs, the history and the
// crash reports, zipped and sent to Switch Library Manager Web (report.hpp) on a thread of its
// own, from Settings › Help and from the error cards of the queue and of Compress games. A
// notice spins meanwhile; then it says the size sent, or what went wrong and what to do.
#include "app_shared.hpp"
#include "log.hpp"

namespace nxdock {

using namespace appui;

namespace {

// The size of a report, as it is said when sent: 640 KB, 1.2 MB.
std::string ReportSize(unsigned long long bytes) {
    char text[32];
    if (bytes < 1024ull * 1024) std::snprintf(text, sizeof(text), "%llu KB", std::max(1ull, bytes / 1024));
    else std::snprintf(text, sizeof(text), "%.1f MB", double(bytes) / (1024.0 * 1024));
    return text;
}

} // namespace

bool App::CanSendReport() {
    return m_link.CurrentConfig().Linked();
}

void App::SendReport() {
    const More& m = TrM();
    if (m_lib_report_running) {
        NoticeBusy(m.rp_sending); // one at a time: the one going on is said again
        return;
    }
    const Config c = m_link.CurrentConfig();
    if (!c.Linked()) {
        NoticeError(m.rp_no_library); // and how to pair one
        return;
    }
    if (m_lib_report_thread.joinable()) m_lib_report_thread.join();
    m_lib_report_running = true;
    m_lib_report_ready = false;
    // what the console is and the files are read there too: the interface never waits
    const SystemInfo sys = m_sysinfo;
    m_lib_report_thread = std::thread([this, url = c.url, key = c.key, sys] {
        m_lib_report_result = report::Send(url, key, ReadConsoleFacts(sys));
        m_lib_report_ready = true;
    });
    NoticeBusy(m.rp_sending);
}

void App::FollowReport() {
    if (!m_lib_report_running || !m_lib_report_ready) return;
    m_lib_report_thread.join();
    m_lib_report_running = false;
    const More& m = TrM();
    const report::Result& r = m_lib_report_result;
    char text[300];
    switch (r.state) {
        case report::Result::State::Sent:
            std::snprintf(text, sizeof(text), m.rp_sent_fmt, ReportSize(r.bytes).c_str());
            NoticeOk(text);
            break;
        case report::Result::State::NoLibrary: NoticeError(m.rp_no_library); break;
        case report::Result::State::TooLarge:
            std::snprintf(text, sizeof(text), m.rp_too_large_fmt, ReportSize(r.bytes).c_str());
            NoticeError(text);
            break;
        case report::Result::State::Failed:
            if (r.status > 0) std::snprintf(text, sizeof(text), m.rp_failed_fmt, r.status);
            else std::snprintf(text, sizeof(text), "%s", r.error == "zip" ? m.rp_failed_zip : m.rp_failed_net);
            NoticeError(text);
            log_write("[NXDock] report: not sent (status %ld, %s)\n", r.status, r.error.c_str());
            break;
    }
}

float App::DrawReportButton(NVGcontext* vg, float right, float cy) {
    const Theme& t = m_theme;
    const More& m = TrM();
    // as the other buttons that are not the main one: the panel, its line, and its sign in blue;
    // while a report goes, a ring that spins in its place
    const float bh = 40, bw = ui::TextWidth(vg, 16, m.rp_button) + 60, bx = right - bw, by = cy - bh / 2;
    ui::FillRound(vg, bx, by, bw, bh, 10, t.panel2);
    ui::StrokeRound(vg, bx + 1, by + 1, bw - 2, bh - 2, 10, t.line, 2);
    const float ix = bx + 24;
    if (m_lib_report_running) {
        ui::Spinner(vg, ix, cy, 8, t.work, m_time);
    } else {
        // an arrow going up out of a tray
        nvgBeginPath(vg);
        nvgMoveTo(vg, ix, cy + 3);
        nvgLineTo(vg, ix, cy - 9);
        nvgMoveTo(vg, ix - 5, cy - 4);
        nvgLineTo(vg, ix, cy - 9);
        nvgLineTo(vg, ix + 5, cy - 4);
        nvgMoveTo(vg, ix - 8, cy + 1);
        nvgLineTo(vg, ix - 8, cy + 8);
        nvgLineTo(vg, ix + 8, cy + 8);
        nvgLineTo(vg, ix + 8, cy + 1);
        nvgStrokeColor(vg, t.work);
        nvgStrokeWidth(vg, 2.2f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    ui::Text(vg, bx + 44, cy, 16, m_lib_report_running ? t.muted : t.text, m.rp_button);
    // a finger: the whole button, 48 px high
    if (!m_lib_report_running) AddHit(bx, cy - 24, bw, 48, HitKind::SendReport);
    return bx;
}

} // namespace nxdock
