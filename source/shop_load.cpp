// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "shop.hpp"

#include "http.hpp"

#include <algorithm>

namespace nxdock {

ShopLoader::~ShopLoader() {
    if (m_thread.joinable()) m_thread.join();
}

bool ShopLoader::Busy() {
    std::lock_guard lock(m_mutex);
    return m_busy;
}

bool ShopLoader::Take(ShopError& error, std::vector<ShopFile>& files) {
    std::lock_guard lock(m_mutex);
    if (!m_done) return false;
    m_done = false;
    error = m_error;
    files = std::move(m_files);
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
        std::vector<std::string> todo = {shop.url}, seen;
        // the index, and the directories it points to (one level deeper at most)
        for (size_t i = 0; i < todo.size() && i < 32; i++) {
            if (std::find(seen.begin(), seen.end(), todo[i]) != seen.end()) continue;
            seen.push_back(todo[i]);
            std::string body;
            const auto r = http::Receive(todo[i], key, 64ull * 1024 * 1024, [&body](const char* d, size_t n) {
                body.append(d, n);
                return true;
            });
            if (r.status == 401 || r.status == 403) {
                if (i == 0) error = ShopError::Auth;
                continue;
            }
            if (r.status != 200) {
                if (i == 0) error = ShopError::Network;
                continue;
            }
            std::string json;
            const ShopError e = UnpackIndex(body, json);
            if (e != ShopError::None) {
                if (i == 0) error = e;
                continue;
            }
            std::vector<std::string> dirs;
            auto more = ParseIndex(json, todo[i], i == 0 ? &dirs : nullptr);
            files.insert(files.end(), more.begin(), more.end());
            todo.insert(todo.end(), dirs.begin(), dirs.end());
        }
        if (error == ShopError::None && files.empty()) error = ShopError::Empty;
        std::lock_guard lock(m_mutex);
        m_error = files.empty() ? error : ShopError::None;
        m_files = std::move(files);
        m_done = true;
        m_busy = false;
    });
}

} // namespace nxdock
