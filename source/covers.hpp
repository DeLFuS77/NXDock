// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <nanovg.h>
#include <atomic>
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
    bool Fetch(const std::string& path, std::vector<unsigned char>& bytes);

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
    // images taken off the screen and the frame they were dropped in: the GPU may still be drawing
    // a frame that uses them, so they are freed a few frames later
    std::deque<std::pair<int, unsigned long>> m_trash;
    std::atomic<bool> m_stop{false};
};

} // namespace nxdock
