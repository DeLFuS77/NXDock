// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Installing from the game card (14A of the visual test of groups 3 and 4): while a card is in,
// More shows a tile «Game card» with its cover and name, among the ways to install without the
// library; it opens the usual install screen, the SD card first, with what the card holds (its
// game, and the update and DLC it brings) marked when the console lacks it or has it older. It
// installs through yati from the secure partition of the card, read as an NSP (space.cpp); a card
// taken out stops its install and says so.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace nxdock {

namespace {

constexpr float kCardCover = 300, kCardRow = 46;

std::string CardIcon(unsigned long long app) {
    char path[24];
    std::snprintf(path, sizeof(path), "ns:%016llX", app);
    return path;
}

// A content of the card next to the console: installed (or newer there), newer than what is
// installed, or missing.
bool CardWanted(const ConsoleHas& has, const InstalledApp::Content& c) {
    const FileState st = MatchFile(has, c.id, c.version).state;
    return st == FileState::Unknown || st == FileState::ForGame;
}

} // namespace

void App::FollowCard() {
    // what is read of a card comes in
    if (m_card_read.valid() && m_card_read.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        GameCard card = m_card_read.get();
        if (card.ok && m_card_in) {
            // the tiles after it move one place on: the one in focus stays the same tool
            if (!m_card.ok && m_tab == Tab::More && m_more == MoreScreen::Tiles && m_tile >= 4) m_tile++;
            m_card = std::move(card);
            m_card_at = m_time;
        } else if (m_more == MoreScreen::Card) {
            Notice(TrM().gc_failed);
            m_more = MoreScreen::Tiles;
        }
    }
    // a card put in or taken out, looked at now and then while More is open or it installs
    bool card_jobs = false;
    if (m_queue) {
        for (const auto& j : m_queue->Jobs()) {
            card_jobs = card_jobs || (j.origin == InstallJob::Origin::Card &&
                                      (j.state == InstallJob::State::Running || j.state == InstallJob::State::Waiting));
        }
    }
    if ((m_tab != Tab::More && !card_jobs) || m_time - m_card_polled < 1.5) return;
    m_card_polled = m_time;
    const bool in = GameCardInserted();
    if (in == m_card_in) return;
    m_card_in = in;
    if (in) {
        if (!m_card_read.valid()) m_card_read = std::async(std::launch::async, [] { return ReadGameCard(); });
        return;
    }
    // taken out: its tile goes, its screen closes, and an install from it stops (yati says so)
    const bool had = m_card.ok;
    m_card = GameCard{};
    if (had && m_tab == Tab::More && m_more == MoreScreen::Tiles && m_tile > 4) m_tile--;
    if (m_more == MoreScreen::Card) {
        m_more = MoreScreen::Tiles;
        if (!card_jobs) Notice(TrM().gc_gone);
    }
    if (card_jobs) Notice(TrM().gc_pulled);
}

void App::OpenCard() {
    if (!m_card.ok) {
        Notice(TrM().gc_gone);
        return;
    }
    m_more = MoreScreen::Card;
    m_card_nand = false; // the SD card, unless chosen for this install
    m_card_pick.assign(m_card.contents.size(), 1);
    m_card_matched = false;
    m_card_focus = int(m_card.contents.size()); // what is missing is marked: Install in focus
    m_has_polled = -100;
}

void App::DrawCardTile(NVGcontext* vg, float x, float y, float w, float h) {
    const Theme& t = m_theme;
    const int image = m_covers.Get(CardIcon(m_card.app));
    ui::Cover(vg, t, x, y, w, PlaceholderColor(m_card.name), Initial(m_card.name).c_str(), nullptr, false, false, 12, image);
    // a small card on its corner
    ui::FillRound(vg, x + w - 18, y + h - 20, 26, 20, 4, t.work);
    ui::FillRound(vg, x + w - 13, y + h - 16, 16, 4, 1, t.on_work);
    // "inserted", in green
    const More& m = TrM();
    const float tw = ui::TextWidth(vg, 12, m.gc_inserted) + 16;
    NVGcolor bg = t.ok;
    bg.a = 0.2f;
    ui::FillRound(vg, x + w + 18, y + 5, tw, 22, 11, bg);
    ui::Text(vg, x + w + 18 + tw / 2, y + 16, 12, t.ok, m.gc_inserted, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

void App::DrawCard(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    CheckSdSpace();
    FollowConsoleHas();
    const int n = int(m_card.contents.size());
    if (m_card_pick.size() != size_t(n)) m_card_pick.assign(size_t(n), 1);
    if (!m_card_matched && m_has_ready) {
        // marked: what the console lacks, or has older
        for (int i = 0; i < n; i++) m_card_pick[size_t(i)] = CardWanted(m_has, m_card.contents[size_t(i)]);
        m_card_matched = true;
    }
    const float k = ui::EaseOutBack(ui::Appear(m_more_at, 0.4f));
    // the cover on the left, "Game card" under it, and what happens if it is taken out
    const float cs = kCardCover * (0.9f + 0.1f * k), cx = 48 + (kCardCover - cs) / 2, cy = 100 + (kCardCover - cs) / 2;
    nvgGlobalAlpha(vg, std::min(1.f, ui::Appear(m_more_at, 0.3f) * 1.3f));
    ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(m_card.name), Initial(m_card.name).c_str(), nullptr, false, false, 16,
              m_covers.Get(CardIcon(m_card.app)));
    {
        const float bw = ui::TextWidth(vg, 14, m.gc_tile) + 46;
        NVGcolor bg = t.work;
        bg.a = 0.16f;
        ui::FillRound(vg, 48, 418, bw, 34, 17, bg);
        ui::FillRound(vg, 62, 428, 18, 14, 3, t.work);
        ui::Text(vg, 88, 435, 14, t.text, m.gc_tile);
        draw::Box(vg, 48, 470, kCardCover, 13, t.muted, m.gc_note);
    }
    nvgGlobalAlpha(vg, 1.f);
    // its name, where it comes from, and what it holds
    const float rx = 48 + kCardCover + 44, rw = kWidth - rx - 48;
    nvgSave(vg);
    nvgScissor(vg, rx, 96, rw, 60);
    ui::Title(vg, rx, 126, 36, t.text, m_card.name.c_str());
    nvgRestore(vg);
    unsigned version = 0;
    int dlc = 0;
    for (const auto& c : m_card.contents) {
        if (c.type != NcmContentMetaType_AddOnContent) version = std::max(version, c.version);
        else dlc++;
    }
    std::string meta = std::string(m.gc_from) + " \xC2\xB7 " + VersionText(version);
    if (dlc) meta += " \xC2\xB7 " + std::to_string(dlc) + " DLC";
    ui::Text(vg, rx, 162, 15, t.muted, meta.c_str());
    u64 picked = 0;
    for (int i = 0; i < n; i++) {
        if (m_card_pick[size_t(i)]) picked += m_card.contents[size_t(i)].bytes;
    }
    // the SD card without room (12A): its button in focus when it appears
    if (NoRoomAppears(2, picked, m_card_nand)) m_card_focus = n + 1;
    const bool noroom = NoRoomShown(picked, m_card_nand);
    if (!noroom && m_card_focus > n) m_card_focus = n;
    const float ly = 190, list_h = float(std::min(n, 5)) * kCardRow + 12;
    ui::Glass(vg, t, rx, ly, rw, list_h, 12);
    for (int i = 0; i < n && i < 5; i++) {
        const auto& c = m_card.contents[size_t(i)];
        const float y = ly + 6 + float(i) * kCardRow;
        const float in = ui::EaseOut(ui::Appear(m_more_at + 0.05 * i, 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, (1 - in) * 20, 0);
        nvgGlobalAlpha(vg, in);
        if (i == m_card_focus) {
            ui::FillRound(vg, rx + 6, y, rw - 12, kCardRow - 4, 8, t.panel2);
            ui::Focus(vg, t, rx + 6, y, rw - 12, kCardRow - 4, 8, m_time);
        }
        AddHit(rx + 6, y - 1, rw - 12, kCardRow, HitKind::CardRow, i);
        draw::TickBox(vg, t, rx + 18, y + 21, m_card_pick[size_t(i)], "card:" + std::to_string(i));
        const std::string label = ContentLabel(c, m_card.app);
        nvgSave(vg);
        nvgIntersectScissor(vg, rx + 56, y, rw - 56 - 300, kCardRow);
        const float tx = ui::Text(vg, rx + 56, y + 21, 17, t.text, label.c_str());
        nvgRestore(vg);
        // next to the console: installed, older there, or newer than what it has
        const FileMatch match = m_has_ready ? MatchFile(m_has, c.id, c.version) : FileMatch{};
        const char* tag = nullptr;
        NVGcolor col = t.ok;
        if (match.state == FileState::Installed) tag = s.installed_tag;
        else if (match.state == FileState::ForGame && match.installed) tag = m.gc_newer;
        else if (match.state == FileState::Older) {
            tag = s.installed_tag;
            col = t.muted;
        }
        if (tag) {
            const float tw = ui::TextWidth(vg, 12, tag) + 14, px = std::min(tx + 10, rx + rw - 300);
            ui::StrokeRound(vg, px, y + 10, tw, 22, 6, col, 1);
            ui::Text(vg, px + tw / 2, y + 21, 12, col, tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        ui::Text(vg, rx + rw - 18, y + 21, 15, t.muted, (std::string(m.gc_tile) + " \xC2\xB7 " + draw::Size(c.bytes)).c_str(),
                 NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
    }
    // where, in large: the SD card unless chosen for this install
    const float cards_y = ly + list_h + 16;
    DestCards(vg, rx, cards_y, rw, 64, m_card_nand, picked, HitKind::CardDest);
    const float strip = noroom ? 60.f : 0.f;
    if (noroom) DrawNoRoom(vg, rx, cards_y + 72, rw, picked, m_card_focus == n + 1, 2);
    const float dy = cards_y + 64 + 18 + strip;
    char install[160];
    std::snprintf(install, sizeof(install), m_card_nand ? m.dc_install_nand_fmt : m.dc_install_sd_fmt, draw::Size(picked).c_str());
    const float bw = ui::TextWidth(vg, 20, install) + 64, bx = kWidth - 48 - bw;
    if (m_card_nand) draw::Box(vg, rx, dy - 2, std::max(120.f, bx - rx - 20), 14, t.orange, m.dc_once);
    if (m_card_focus == n) ui::Focus(vg, t, bx, dy - 4, bw, 52, 12, m_time);
    nvgGlobalAlpha(vg, picked > 0 ? 1.f : 0.45f);
    ui::FillRound(vg, bx, dy - 4, bw, 52, 12, t.work);
    ui::Text(vg, bx + bw / 2, dy + 22, 20, t.on_work, install, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgGlobalAlpha(vg, 1.f);
    AddHit(bx, dy - 4, bw, 52, HitKind::CardInstall, n);
    static const std::string where_glyphs = std::string(ui::glyph::ZL) + " " + ui::glyph::ZR;
    std::vector<ui::Hint> hints = {{ui::glyph::A, s.select, HidNpadButton_A}, {where_glyphs.c_str(), m.dc_switch, HidNpadButton_ZR},
                                   {ui::glyph::B, s.back, HidNpadButton_B}};
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateCard(u64 down, u64 held) {
    if (down & HidNpadButton_B) {
        m_more = MoreScreen::Tiles;
        return;
    }
    const int n = int(m_card.contents.size());
    if (!m_card.ok || n == 0) return;
    u64 picked = 0;
    for (int i = 0; i < n && i < int(m_card_pick.size()); i++) {
        if (m_card_pick[size_t(i)]) picked += m_card.contents[size_t(i)].bytes;
    }
    const bool noroom = NoRoomShown(picked, m_card_nand);
    const int use = n + 1;
    if (m_card_focus == use && !noroom) m_card_focus = n;
    // the rows, the button of the SD card without room, then Install
    if (Repeat(down, held, HidNpadButton_AnyUp)) {
        if (m_card_focus == n && noroom) m_card_focus = use;
        else if (m_card_focus == use) m_card_focus = n - 1;
        else if (m_card_focus > 0) m_card_focus--;
    }
    if (Repeat(down, held, HidNpadButton_AnyDown)) {
        if (m_card_focus == n - 1 && noroom) m_card_focus = use;
        else if (m_card_focus == use) m_card_focus = n;
        else if (m_card_focus < n) m_card_focus++;
    }
    if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) m_card_nand = !m_card_nand; // for this install only
    if (down & HidNpadButton_A) {
        if (m_card_focus == use) {
            UseNandOnce(2);
            m_card_focus = n;
        } else if (m_card_focus < n) {
            m_card_pick[size_t(m_card_focus)] ^= 1;
        } else {
            InstallCard();
        }
    }
}

void App::InstallCard() {
    const More& m = TrM();
    std::vector<InstallJob> jobs;
    for (size_t i = 0; i < m_card.contents.size() && i < m_card_pick.size(); i++) {
        if (!m_card_pick[i]) continue;
        const auto& c = m_card.contents[i];
        InstallJob job;
        job.game = m_card.name;
        job.label = std::string(m.gc_tile) + " \xC2\xB7 " + ContentLabel(c, m_card.app);
        job.format = "nsp";
        job.origin = InstallJob::Origin::Card;
        job.kind = c.type == NcmContentMetaType_Application ? CatalogFile::Kind::Game
                   : c.type == NcmContentMetaType_Patch     ? CatalogFile::Kind::Update
                                                            : CatalogFile::Kind::Dlc;
        job.title_id = c.id;
        job.version = c.version;
        job.size = c.bytes;
        job.nand = m_card_nand;
        job.cover = CardIcon(m_card.app);
        job.color_key = m_card.name;
        job.file_id = "card:" + CardIcon(c.id).substr(3);
        jobs.push_back(std::move(job));
    }
    if (jobs.empty()) {
        Notice(Tr().nothing_picked);
        return;
    }
    QueueLocal(std::move(jobs)); // the internal memory asks first, as everywhere
    m_card_nand = false;         // the next one goes to the SD card again
}

} // namespace nxdock
