// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// What the web asked of the queue (22A): pause, resume, or cancel a job. A cancel is always asked
// about on the screen first.
struct QueueCommand {
    std::string id, action, game;
    int job = 0;
};

// Parses the answer of POST /install/queue: {"commands": [...]}. Tested on the computer.
std::vector<QueueCommand> ParseQueueCommands(const std::string& json);

// Tells the library app what the queue does, on a thread of its own (22A), and that it ended
// (21A). The newest state is sent at most every two seconds; what the web asked comes back in
// the answer.
class QueueReport {
public:
    QueueReport();
    ~QueueReport();
    void SetLink(const std::string& url, const std::string& key);
    // The state of the queue as JSON; only the newest waiting one is sent.
    void Live(std::string json);
    // The end of the queue, for the message to the phone; sent once.
    void Done(std::string json);
    std::vector<QueueCommand> TakeCommands();

private:
    void Run();
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;
    std::string m_url, m_key, m_live, m_done;
    std::vector<QueueCommand> m_commands;
};

} // namespace nxdock
