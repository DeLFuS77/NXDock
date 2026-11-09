// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "yati/source/base.hpp"
#include "ui/progress_box.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// A file of the library app (/install/f/{id}/{name}) read with HTTP Range requests, for the
// install core.
//  - Inside the range the core says it reads in order (an NCA), it fetches ahead in large
//    pieces over several connections at once: on WiFi with losses, several TCP streams fill the
//    air time much better than one.
//  - Outside it (the PFS0 header, tickets, the cnmt), it reads what is asked, rounded up.
//  - A piece that breaks off is asked again from where it stopped, waiting for the network to
//    come back for a while, so a short drop costs nothing.
//  - The key goes as in http.cpp: the password of basic authentication.
class HttpSource final : public sphaira::yati::source::Base {
public:
    struct Options {
        int connections = 4;
        size_t piece = 8 * 1024 * 1024; // bytes per request ahead
        int ahead = 2;                  // pieces ahead per connection
        int give_up_seconds = 90;       // without one byte arriving
    };

    HttpSource(std::string url, std::string key, Options options, sphaira::ui::ProgressBox* progress);
    ~HttpSource() override;

    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    void HintRange(s64 off, s64 size) override;
    void SignalCancel() override;

    // Asks the app for the size (a one-byte Range request). Set before installing.
    Result Open();
    s64 Size() const { return m_size; }

private:
    struct Piece {
        s64 index = 0;
        std::vector<unsigned char> data;
        size_t want = 0;   // bytes this piece holds
        size_t filled = 0; // bytes arrived
        enum class State { Queued, Fetching, Ready, Failed } state = State::Queued;
        Result rc = 0;
        std::atomic<bool> dropped{false}; // no longer wanted: the fetch stops
    };
    using PiecePtr = std::shared_ptr<Piece>;

    void Worker();
    PiecePtr NextQueued();
    // Fetches [off, off + size) into dst, retrying from where it stopped.
    Result Fetch(void* curl, unsigned char* dst, s64 off, size_t size, Piece* piece);
    // One request; returns the bytes it got, and why it stopped.
    Result FetchOnce(void* curl, unsigned char* dst, s64 off, size_t size, size_t& got, Piece* piece, bool& retry);
    void Schedule(s64 first_index);
    Result ReadDirect(void* buf, s64 off, s64 size);
    bool Stopping() const;

    std::string m_url, m_key;
    Options m_opt;
    sphaira::ui::ProgressBox* m_progress;
    s64 m_size = -1;

    std::mutex m_mutex;
    std::condition_variable m_cv;     // pieces queued, the source closing
    std::condition_variable m_filled; // bytes arrived in a piece
    std::map<s64, PiecePtr> m_pieces;
    s64 m_hint_off = 0, m_hint_end = 0;
    std::atomic<bool> m_quit{false};
    std::vector<std::thread> m_workers;

    // small reads outside the hint: one cached block
    std::vector<unsigned char> m_small;
    s64 m_small_off = -1;
    void* m_direct = nullptr; // the curl handle of the reading thread
};

// The connections to use, from the last speed test (0: never run) and the mode.
int ConnectionsFor(int tested_best, bool applet);

} // namespace nxdock
