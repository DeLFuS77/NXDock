// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Shops of the kind of Tinfoil and Ownfoil (9A): a list of shops in More › Shops; opened, a shop
// is shown as another library, with its name in the top bar, and B goes back to the library app.
// The user and the password are typed on the console's keyboard and kept on the SD card, as the
// console key is; they are never shown.
#include "app.hpp"

#include "draw.hpp"
#include "errors.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <algorithm>

namespace nxdock {
namespace {

// The keyboard of the console; `secret` hides what is typed. True unless cancelled.
bool AskField(const char* guide, const std::string& initial, bool secret, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, 500);
    if (secret) {
        swkbdConfigSetPasswordFlag(&kbd, 1);
    } else {
        swkbdConfigSetInitialText(&kbd, initial.c_str());
    }
    char text[512] = {};
    const Result rc = swkbdShow(&kbd, text, sizeof(text));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = text;
    return true;
}

std::string Host(const std::string& url) {
    const auto scheme = url.find("://");
    const auto start = scheme == std::string::npos ? 0 : scheme + 3;
    const auto end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

void App::OpenShops() {
    m_shops = LoadShops(ShopsPath());
    m_shop_focus = std::clamp(m_shop_focus, 0, std::max(0, int(m_shops.size()) - 1));
    if (!m_shop_loader) m_shop_loader = std::make_unique<ShopLoader>();
    m_more = MoreScreen::Shops;
}

bool App::EditShop(ShopConfig& shop, bool fresh) {
    const More& m = TrM();
    std::string url = fresh ? "http://" : shop.url;
    if (!AskField(m.sh_guide_url, url, false, url) || url.size() < 8) return false;
    std::string name = shop.name.empty() ? Host(url) : shop.name;
    if (!AskField(m.sh_guide_name, name, false, name)) return false;
    std::string user = shop.user;
    if (!AskField(m.sh_guide_user, user, false, user)) return false;
    std::string pass;
    // the password is typed again each time; empty keeps none
    if (!AskField(m.sh_guide_pass, "", true, pass)) return false;
    shop = {name.empty() ? Host(url) : name, url, user, pass};
    return true;
}

void App::FollowShop() {
    if (!m_shop_loader) return;
    ShopError error;
    std::vector<ShopFile> files;
    if (!m_shop_loader->Take(error, files)) return;
    const More& m = TrM();
    if (error != ShopError::None) {
        ErrorText e;
        switch (error) {
            case ShopError::Encrypted: e = {m.e_shop_enc_t, m.e_shop_enc_w, m.e_shop_enc_d, {}}; break;
            case ShopError::Auth: e = {m.e_shop_auth_t, {}, m.e_shop_auth_d, {}}; break;
            case ShopError::Network: e = {m.e_network_t, m.e_network_w, m.e_network_d, {}}; break;
            case ShopError::Empty: e = {m.e_shop_empty, {}, m.e_shop_bad_d, {}}; break;
            default: e = {m.e_shop_bad_t, m.e_shop_bad_w, m.e_shop_bad_d, {}}; break;
        }
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultError;
        m_dialog.single = true;
        m_dialog.from = PackError(e);
        m_dialog.text = m.t_shops;
        return;
    }
    if (m_shop_opening < 0 || m_shop_opening >= int(m_shops.size())) return;
    // the shop as the library: its games, with what the console has of them
    const auto& shop = m_shops[size_t(m_shop_opening)];
    m_shop_active = true;
    m_shop_name = shop.name;
    m_shop_key = ShopKey(shop);
    m_games = ShopGames(files);
    if (m_queue) {
        const auto installed = m_queue->Installed();
        for (auto& g : m_games) {
            const auto it = installed.find(g.title_id);
            g.installed = it != installed.end() ? it->second : InstalledState{};
        }
    }
    m_detail = false;
    m_focus = 0;
    m_scroll = m_scroll_target = 0;
    RebuildView();
    m_more = MoreScreen::Tiles;
    m_tab = Tab::Library;
}

void App::LeaveShop() {
    if (!m_shop_active) return;
    m_shop_active = false;
    m_shop_key.clear();
    m_games = m_catalog.Games();
    m_games_version = m_catalog.Version();
    if (m_queue) {
        const auto installed = m_queue->Installed();
        for (auto& g : m_games) {
            const auto it = installed.find(g.title_id);
            g.installed = it != installed.end() ? it->second : InstalledState{};
        }
    }
    m_detail = false;
    m_focus = 0;
    RebuildView();
}

void App::DrawShops(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    FollowShop();
    ui::Text(vg, 48, 104, 22, t.text, m.t_shops);
    draw::Box(vg, 48, 124, kWidth - 96, 14, t.muted, m.sh_note);
    const int rows = int(m_shops.size()) + 1; // the last row adds one
    m_shop_focus = std::clamp(m_shop_focus, 0, rows - 1);
    for (int i = 0; i < rows && i < 8; i++) {
        const float y = 160 + i * 64;
        draw::Panel(vg, t, 48, y, kWidth - 96, 56, 12);
        if (i == m_shop_focus) ui::Focus(vg, t, 48, y, kWidth - 96, 56, 12, m_time);
        AddHit(48, y, kWidth - 96, 56, HitKind::ShopRow, i);
        if (i == int(m_shops.size())) {
            ui::Text(vg, 76, y + 28, 22, t.work, "+", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            ui::Text(vg, 100, y + 20, 17, t.text, m.sh_add);
            ui::Text(vg, 100, y + 40, 13, t.muted, m.sh_add_sub);
            continue;
        }
        const auto& sh = m_shops[size_t(i)];
        DrawTileIcon(vg, 3, 62, y + 28);
        ui::Text(vg, 110, y + 20, 17, t.text, sh.name.c_str());
        ui::Text(vg, 110, y + 40, 13, t.muted, Host(sh.url).c_str());
        if (m_shop_loader && m_shop_loader->Busy() && m_shop_opening == i) {
            ui::Spinner(vg, kWidth - 90, y + 28, 9, t.work, m_time);
            ui::Text(vg, kWidth - 110, y + 28, 14, t.muted, m.sh_loading, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
    }
    if (m_shops.empty()) draw::Box(vg, 48, 240, kWidth - 96, 15, t.muted, m.sh_none);
    std::vector<ui::Hint> hints;
    if (m_shop_focus < int(m_shops.size())) {
        hints.push_back({ui::glyph::A, m.open, HidNpadButton_A});
        hints.push_back({ui::glyph::Y, m.sh_edit, HidNpadButton_Y});
        hints.push_back({ui::glyph::Minus, m.sh_remove, HidNpadButton_Minus});
    } else {
        hints.push_back({ui::glyph::A, m.sh_add, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::X, m.sh_add, HidNpadButton_X});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateShops(u64 down, u64 held) {
    const int rows = int(m_shops.size()) + 1;
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_shop_focus > 0) m_shop_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_shop_focus < rows - 1) m_shop_focus++;
    if (down & HidNpadButton_B) {
        m_more = MoreScreen::Tiles;
        return;
    }
    const bool on_add = m_shop_focus == int(m_shops.size());
    if ((down & HidNpadButton_X) || (on_add && (down & HidNpadButton_A))) {
        ShopConfig shop;
        if (EditShop(shop, true)) {
            m_shops.push_back(shop);
            SaveShops(ShopsPath(), m_shops);
            m_shop_focus = int(m_shops.size()) - 1;
        }
        return;
    }
    if (on_add) return;
    if (down & HidNpadButton_Y) {
        ShopConfig shop = m_shops[size_t(m_shop_focus)];
        if (EditShop(shop, false)) {
            m_shops[size_t(m_shop_focus)] = shop;
            SaveShops(ShopsPath(), m_shops);
        }
        return;
    }
    if (down & HidNpadButton_Minus) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::ShopRemove;
        m_dialog.entry = m_shop_focus;
        return;
    }
    if ((down & HidNpadButton_A) && m_shop_loader && !m_shop_loader->Busy()) {
        m_shop_opening = m_shop_focus;
        m_shop_loader->Start(m_shops[size_t(m_shop_focus)]);
    }
}

} // namespace nxdock
