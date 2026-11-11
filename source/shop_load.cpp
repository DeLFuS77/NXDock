// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "shop.hpp"

#include "http.hpp"
#include "tls_pin.hpp"

#include <new>

#include <algorithm>

namespace nxdock {

ShopLoader::~ShopLoader() {
    if (m_thread.joinable()) m_thread.join();
}

bool ShopLoader::Busy() {
    std::lock_guard lock(m_mutex);
    return m_busy;
}

bool ShopLoader::Take(ShopError& error, std::vector<ShopFile>& files, std::string& pin) {
    std::lock_guard lock(m_mutex);
    if (!m_done) return false;
    m_done = false;
    error = m_error;
    files = std::move(m_files);
    pin = std::move(m_pin);
    return true;
}

void ShopLoader::Start(const ShopConfig& shop) {
    if (Busy()) return;
    if (m_thread.joinable()) m_thread.join();
    m_busy = true;
    m_thread = std::thread([this, shop] {
        const std::string key = ShopKey(shop);
        std::vector<ShopFile> files;
        ShopError error = ShopError::None;
        // https: the key of the shop's certificate, read and kept the first time (there is no
        // authority on the console to check it against); from then on another key is refused
        std::string learned;
        if (shop.url.rfind("https://", 0) == 0) {
            std::string pin = shop.pin, why;
            if (pin.empty()) {
                pin = learned = tls::ReadPinAt(shop.url, why);
            }
            if (pin.empty()) {
                std::lock_guard lock(m_mutex);
                m_error = ShopError::Network;
                m_done = true;
                m_busy = false;
                return;
            }
            tls::SetSitePin(shop.url, pin);
        }
        std::vector<std::string> todo = {shop.url}, seen;
        // the index, and the directories it points to (one level deeper at most)
        for (size_t i = 0; i < todo.size() && i < 32; i++) {
            if (std::find(seen.begin(), seen.end(), todo[i]) != seen.end()) continue;
            seen.push_back(todo[i]);
            std::string body;
            // the user and password of the shop go only to the shop's own server
            const std::string send = http::SameOrigin(todo[i], shop.url) ? key : "";
            const auto r = http::Receive(todo[i], send, 16ull * 1024 * 1024, [&body](const char* d, size_t n) {
                body.append(d, n);
                return true;
            });
            if (r.status == 401 || r.status == 403) {
                if (i == 0) error = ShopError::Auth;
                continue;
            }
            // curl refused the certificate: not the key kept the first time
            if (r.status == 0 && r.error.find("pinned") != std::string::npos) {
                if (i == 0) error = ShopError::KeyChanged;
                continue;
            }
            if (r.status != 200) {
                if (i == 0) error = ShopError::Network;
                continue;
            }
            std::string json;
            ShopError e = ShopError::NotIndex;
            // an index too large for the memory is a shop that cannot be read, not a crash
            try {
                e = UnpackIndex(body, json);
            } catch (const std::bad_alloc&) {
                json.clear();
            }
            if (e != ShopError::None) {
                if (i == 0) error = e;
                continue;
            }
            std::vector<std::string> dirs;
            std::vector<ShopFile> more;
            try {
                more = ParseIndex(json, todo[i], i == 0 ? &dirs : nullptr);
            } catch (const std::bad_alloc&) {
                if (i == 0) error = ShopError::NotIndex;
                continue;
            }
            files.insert(files.end(), more.begin(), more.end());
            todo.insert(todo.end(), dirs.begin(), dirs.end());
        }
        if (error == ShopError::None && files.empty()) error = ShopError::Empty;
        std::lock_guard lock(m_mutex);
        m_error = files.empty() ? error : ShopError::None;
        m_files = std::move(files);
        // kept only when the shop answered with it
        if (m_error == ShopError::None) m_pin = learned;
        m_done = true;
        m_busy = false;
    });
}

} // namespace nxdock
