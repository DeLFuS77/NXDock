// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "catalog.hpp"

namespace nxdock {

// Talks to the library app in the background while NXDock is linked:
//  - waits for what is sent from the web ("Send to console"), which the interface then asks
//    about before installing anything;
//  - tells the app whether a sent file was accepted;
//  - sends the inventory of what is installed, only when the user said yes.
// Each wait lasts a few seconds, so leaving NXDock is never held up for long.
class Push {
public:
    Push();
    ~Push();

    // The library in use; empty url: not linked (nothing is asked).
    void SetLink(const std::string& url, const std::string& key);
    // What arrived since the last call.
    std::vector<PushItem> Take();
    void Answer(const std::string& id, bool accepted);
    // The inventory to send, or off to delete it in the app.
    void SendInventory(std::string json);

private:
    void Run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;
    std::string m_url, m_key;
    std::vector<PushItem> m_items;
    std::deque<std::pair<std::string, std::string>> m_posts; // path, body
};

} // namespace nxdock
