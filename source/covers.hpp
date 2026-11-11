// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <nanovg.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// The covers of the library: downloaded once and kept on the SD card, decoded on threads of their
// own so the interface never waits, and handed to the GPU a few per frame.
class Covers {
public:
    void Start(const std::string& base_url, const std::string& key);
    void Stop();
    // The image of a cover, or 0 while it is not ready (asking for it puts it in the line).
    int Get(const std::string& path);
    // True while a cover is still on its way (not yet fetched, or fetched again after a failure).
    bool Loading(const std::string& path);
    // Makes the decoded covers into images; call once per frame on the drawing thread.
    void Upload(NVGcontext* vg);
    void Free(NVGcontext* vg);

private:
    struct Decoded {
        std::string path;
        int width = 0, height = 0;
        std::vector<unsigned char> rgba;
    };
    void Work();
    bool Fetch(const std::string& path, std::vector<unsigned char>& bytes, bool& retry);

    std::string m_url, m_key;
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::string> m_wanted;
    std::set<std::string> m_asked;
    std::deque<Decoded> m_decoded;
    std::map<std::string, int> m_images;
    // the frame each image was last drawn in: in a library of thousands, the ones not seen for
    // longest are freed when there are too many
    std::map<std::string, unsigned long> m_used;
    unsigned long m_frame = 0;
    // the images on the GPU, made once and filled with one cover after another; the ones holding none
    std::vector<int> m_slots, m_free;
    // covers whose download failed, and when they may be asked again
    std::map<std::string, std::chrono::steady_clock::time_point> m_retry;
    std::atomic<bool> m_stop{false};
};

} // namespace nxdock
