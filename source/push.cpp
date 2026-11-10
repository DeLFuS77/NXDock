// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "push.hpp"

#include "http.hpp"

#include <chrono>
#include <utility>

namespace nxdock {

Push::Push() {
    m_thread = std::thread([this] { Run(); });
}

Push::~Push() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    m_thread.join();
}

void Push::SetLink(const std::string& url, const std::string& key) {
    {
        std::lock_guard lock(m_mutex);
        if (url == m_url && key == m_key) {
            return;
        }
        m_url = url;
        m_key = key;
    }
    m_cv.notify_all();
}

std::vector<PushItem> Push::Take() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_items, {});
}

void Push::Answer(const std::string& id, bool accepted) {
    {
        std::lock_guard lock(m_mutex);
        m_posts.push_back({"/install/push/answer", "{\"id\":" + http::JsonQuote(id) + ",\"accepted\":" + (accepted ? "true" : "false") + "}"});
    }
    m_cv.notify_all();
}

void Push::SendInventory(std::string json) {
    {
        std::lock_guard lock(m_mutex);
        // only the newest inventory matters
        std::erase_if(m_posts, [](const auto& p) { return p.first == "/install/inventory"; });
        m_posts.push_back({"/install/inventory", std::move(json)});
    }
    m_cv.notify_all();
}

void Push::Run() {
    std::unique_lock lock(m_mutex);
    int pause = 0; // seconds to wait after the app could not answer
    while (!m_quit) {
        if (m_url.empty()) {
            m_cv.wait(lock);
            continue;
        }
        const std::string url = m_url, key = m_key;
        // what to tell the app first
        while (!m_posts.empty() && !m_quit) {
            const auto post = m_posts.front();
            m_posts.pop_front();
            lock.unlock();
            const auto r = http::PostJson(url + post.first, post.second, key, 8000);
            lock.lock();
            if (r.status == 0 && post.first == "/install/inventory" && m_posts.empty()) {
                m_posts.push_back(post); // the network is gone: send it when it comes back
                break;
            }
        }
        if (m_quit) {
            break;
        }
        if (pause > 0) {
            m_cv.wait_for(lock, std::chrono::seconds(pause));
            pause = 0;
            continue;
        }
        lock.unlock();
        // a short wait in the app: it answers at once when something is sent
        const auto r = http::Get(url + "/install/push/wait?timeout=5", key, 9000);
        lock.lock();
        if (r.status == 200) {
            std::vector<PushItem> items;
            if (ParsePush(r.body, items) && m_url == url) {
                for (auto& i : items) m_items.push_back(std::move(i));
            }
        } else if (r.status == 204) {
            // nothing came: ask again
        } else if (r.status == 404 || r.status == 401 || r.status == 403) {
            pause = 60; // an app without it, or the key is not taken now
        } else {
            pause = 5;  // no answer: the network or the app is away
        }
    }
}

} // namespace nxdock
