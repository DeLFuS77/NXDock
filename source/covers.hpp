// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <nanovg.h>
#include <array>
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
    // A small soft picture of a cover (kThumb a side, RGBA), for the light behind the game in focus
    // (1A); false while the cover has not been read.
    static constexpr int kThumb = 24;
    bool Thumb(const std::string& path, unsigned char* out);
    // Makes the decoded covers into images; call once per frame on the drawing thread. How many.
    int Upload(NVGcontext* vg);
    void Free(NVGcontext* vg);

private:
    struct Decoded {
        std::string path;
        int width = 0, height = 0;
        std::vector<unsigned char> rgba;
    };
    static constexpr int kNetWorkers = 3;
    void StartWorkers();
    // net < 0: a thread reading the SD card; otherwise the number of a downloading thread
    void Work(int net);
    bool ReadLocal(const std::string& path, std::vector<unsigned char>& bytes);
    bool Download(const std::string& path, std::vector<unsigned char>& bytes, bool& retry, const std::atomic<bool>* cancel);

    std::string m_url, m_key;
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_net_wake;
    // covers to download (not on the card), the one each downloading thread is on, and its stop
    std::deque<std::string> m_net;
    std::array<std::string, kNetWorkers> m_net_path;
    std::array<std::atomic<bool>, kNetWorkers> m_net_cancel{};
    // the frame each cover was last asked for (on screen): downloads of ones scrolled past are dropped
    std::map<std::string, unsigned long> m_seen;
    std::deque<std::string> m_wanted;
    std::set<std::string> m_asked;
    std::deque<Decoded> m_decoded;
    std::map<std::string, int> m_images;
    // the small pictures (kThumb a side), the oldest dropped past kMaxThumbs (about 2 KB each)
    static constexpr size_t kMaxThumbs = 256;
    std::map<std::string, std::vector<unsigned char>> m_thumbs;
    std::deque<std::string> m_thumb_order;
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
