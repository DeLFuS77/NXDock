// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "queue_report.hpp"

#include "http.hpp"

#include <chrono>
#include <utility>
#include <yyjson.h>

namespace nxdock {

std::vector<QueueCommand> ParseQueueCommands(const std::string& json) {
    std::vector<QueueCommand> out;
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return out;
    yyjson_val* list = yyjson_obj_get(yyjson_doc_get_root(doc), "commands");
    size_t i, n;
    yyjson_val* item;
    yyjson_arr_foreach(list, i, n, item) {
        QueueCommand c;
        const char* id = yyjson_get_str(yyjson_obj_get(item, "id"));
        const char* action = yyjson_get_str(yyjson_obj_get(item, "action"));
        const char* game = yyjson_get_str(yyjson_obj_get(item, "game"));
        c.id = id ? id : "";
        c.action = action ? action : "";
        c.game = game ? game : "";
        c.job = int(yyjson_get_int(yyjson_obj_get(item, "job")));
        if (c.action == "pause" || c.action == "resume" || (c.action == "cancel" && c.job > 0)) out.push_back(std::move(c));
    }
    yyjson_doc_free(doc);
    return out;
}

QueueReport::QueueReport() {
    m_thread = std::thread([this] { Run(); });
}

QueueReport::~QueueReport() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
    }
    m_cv.notify_all();
    m_thread.join();
}

void QueueReport::SetLink(const std::string& url, const std::string& key) {
    std::lock_guard lock(m_mutex);
    m_url = url;
    m_key = key;
}

void QueueReport::Live(std::string json) {
    {
        std::lock_guard lock(m_mutex);
        m_live = std::move(json);
    }
    m_cv.notify_all();
}

void QueueReport::Done(std::string json) {
    {
        std::lock_guard lock(m_mutex);
        m_done = std::move(json);
    }
    m_cv.notify_all();
}

std::vector<QueueCommand> QueueReport::TakeCommands() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_commands, {});
}

void QueueReport::Run() {
    std::unique_lock lock(m_mutex);
    while (!m_quit) {
        m_cv.wait(lock, [this] { return m_quit || ((!m_live.empty() || !m_done.empty()) && !m_url.empty()); });
        if (m_quit) break;
        const std::string url = m_url, key = m_key;
        const std::string live = std::exchange(m_live, {}), done = std::exchange(m_done, {});
        lock.unlock();
        std::vector<QueueCommand> commands;
        if (!live.empty()) {
            const auto r = http::PostJson(url + "/install/queue", live, key, 6000);
            if (r.status == 200) commands = ParseQueueCommands(r.body);
        }
        if (!done.empty()) http::PostJson(url + "/install/queue/done", done, key, 10000);
        lock.lock();
        for (auto& c : commands) m_commands.push_back(std::move(c));
        // at most every two seconds
        m_cv.wait_for(lock, std::chrono::seconds(2), [this] { return m_quit; });
    }
}

} // namespace nxdock
