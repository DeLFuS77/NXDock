// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "covers.hpp"

#include <sys/stat.h>
#include <cstdio>
#include <algorithm>
#include <functional>
#include "http.hpp"
#include <switch.h>
#include <cstdlib>
#include <memory>
#include <iterator>

// stb_image (public domain / MIT, by Sean Barrett), only for the JPEG and PNG covers the app gives.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stb_image.h>
#pragma GCC diagnostic pop

namespace nxdock {
namespace {

constexpr int kWorkers = 2;
constexpr int kUploadsPerFrame = 3;
// covers kept on the GPU at once (288 x 288, ~330 KB each): a screen and a half. They are slots made once
// and filled again: creating and freeing images while scrolling fast broke the memory pool of nanovg-deko3d
constexpr size_t kMaxImages = 64;
// covers waiting to be fetched: scrolling fast through a large library keeps only the newest
constexpr size_t kMaxWanted = 48;
// every cover is cut to a square (it is drawn as one) and scaled to this side, whatever its size
constexpr int kMaxSide = 288;
// frames a slot must not have been drawn in before it takes another cover: the GPU may still be drawing it
constexpr unsigned long kReuseFrames = 4;
constexpr const char* kCacheDir = "sdmc:/switch/NXDock/covers";

// The file a cover is kept in: its address hashed, so any name of the app is a safe file name.
std::string CachePath(const std::string& path) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s/%016zx.img", kCacheDir, std::hash<std::string>{}(path));
    return name;
}

bool ReadFile(const std::string& file, std::vector<unsigned char>& bytes) {
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && size < 8 * 1024 * 1024;
    if (ok) {
        bytes.resize(size_t(size));
        ok = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    }
    std::fclose(f);
    return ok;
}

void WriteFile(const std::string& file, const std::vector<unsigned char>& bytes) {
    const std::string temp = file + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) == 0 && ok) {
        std::rename(temp.c_str(), file.c_str());
    } else {
        std::remove(temp.c_str());
    }
}

} // namespace

void Covers::Start(const std::string& base_url, const std::string& key) {
    Stop();
    m_url = base_url;
    m_key = key;
    m_stop = false;
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/NXDock", 0777);
    mkdir(kCacheDir, 0777);
    for (int i = 0; i < kWorkers; i++) {
        m_threads.emplace_back(&Covers::Work, this);
    }
}

void Covers::Stop() {
    m_stop = true;
    m_wake.notify_all();
    for (auto& t : m_threads) {
        t.join();
    }
    m_threads.clear();
    std::lock_guard lock(m_mutex);
    m_wanted.clear();
    // what was asked and not ready is asked again by the next workers
    for (auto it = m_asked.begin(); it != m_asked.end();) {
        it = m_images.count(*it) ? std::next(it) : m_asked.erase(it);
    }
}

int Covers::Get(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
    std::lock_guard lock(m_mutex);
    const auto it = m_images.find(path);
    if (it != m_images.end()) {
        if (it->second) m_used[path] = m_frame;
        return it->second;
    }
    if (m_threads.empty() && path.rfind("ns:", 0) == 0) {
        // the icons of the console need no library: the workers start for them too
        m_stop = false;
        for (int i = 0; i < kWorkers; i++) m_threads.emplace_back(&Covers::Work, this);
    }
    if (!m_asked.count(path) && !m_threads.empty()) {
        m_asked.insert(path);
        // the newest asked first: what is on screen now
        m_wanted.push_front(path);
        while (m_wanted.size() > kMaxWanted) {
            // gone from the screen before it was fetched: asked again if it comes back
            m_asked.erase(m_wanted.back());
            m_wanted.pop_back();
        }
        m_wake.notify_one();
    }
    return 0;
}

void Covers::Upload(NVGcontext* vg) {
    {
        std::lock_guard lock(m_mutex);
        m_frame++;
    }
    for (int i = 0; i < kUploadsPerFrame; i++) {
        Decoded d;
        int slot = 0;
        {
            std::lock_guard lock(m_mutex);
            if (m_decoded.empty()) return;
            if (!m_free.empty()) {
                slot = m_free.back();
                m_free.pop_back();
            } else if (m_slots.size() < kMaxImages) {
                slot = -1; // a new slot
            } else {
                // the slot not drawn for longest, if it has not been drawn for a few frames
                auto oldest = m_used.end();
                for (auto it = m_used.begin(); it != m_used.end(); ++it) {
                    if (oldest == m_used.end() || it->second < oldest->second) oldest = it;
                }
                if (oldest == m_used.end() || oldest->second + kReuseFrames > m_frame) return;
                slot = m_images[oldest->first];
                m_images.erase(oldest->first);
                m_asked.erase(oldest->first);
                m_used.erase(oldest);
            }
            d = std::move(m_decoded.front());
            m_decoded.pop_front();
        }
        if (slot < 0) {
            slot = nvgCreateImageRGBA(vg, kMaxSide, kMaxSide, 0, d.rgba.data());
            if (!slot) return;
            m_slots.push_back(slot);
        } else {
            nvgUpdateImage(vg, slot, d.rgba.data());
        }
        std::lock_guard lock(m_mutex);
        if (const auto old = m_images.find(d.path); old != m_images.end() && old->second && old->second != slot) {
            // decoded twice: the older slot is free again
            m_used.erase(d.path);
            m_free.push_back(old->second);
        }
        m_images[d.path] = slot;
        m_used[d.path] = m_frame;
    }
}

void Covers::Free(NVGcontext* vg) {
    // the slots stay, to be filled with the covers of the next library (on leaving, the renderer frees them)
    (void)vg;
    std::lock_guard lock(m_mutex);
    m_images.clear();
    m_asked.clear();
    m_used.clear();
    m_decoded.clear(); // decoded for the library just left
    m_free = m_slots;
}

bool Covers::Fetch(const std::string& path, std::vector<unsigned char>& bytes) {
    // the icon a game has on the console (In the console, of the Installed tab)
    if (path.rfind("ns:", 0) == 0) {
        const u64 app = std::strtoull(path.c_str() + 3, nullptr, 16);
        if (!app || R_FAILED(nsInitialize())) return false;
        auto control = std::make_unique<NsApplicationControlData>();
        bool ok = false;
        for (const auto source : {NsApplicationControlSource_CacheOnly, NsApplicationControlSource_Storage}) {
            u64 size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(source, app, control.get(), sizeof(*control), &size)) && size > sizeof(control->nacp)) {
                const size_t n = size_t(std::min<u64>(size - sizeof(control->nacp), sizeof(control->icon)));
                bytes.assign(control->icon, control->icon + n);
                ok = true;
                break;
            }
        }
        nsExit();
        return ok;
    }
    const std::string cached = CachePath(path);
    if (ReadFile(cached, bytes)) {
        return true;
    }
    // a path of the app: "/…", not "//host" nor "@host", which would take the key elsewhere
    if (path.empty() || path[0] != '/' || path.rfind("//", 0) == 0 || path.find('@') != std::string::npos || path.find('\\') != std::string::npos) {
        return false;
    }
    const auto r = http::GetLarge(m_url + path, m_key, 10000, 4 * 1024 * 1024);
    if (r.status != 200 || r.body.empty()) {
        return false;
    }
    bytes.assign(r.body.begin(), r.body.end());
    WriteFile(cached, bytes);
    return true;
}

void Covers::Work() {
    while (true) {
        std::string path;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stop || !m_wanted.empty(); });
            if (m_stop) return;
            path = m_wanted.front();
            m_wanted.pop_front();
        }
        std::vector<unsigned char> bytes;
        Decoded d;
        d.path = path;
        int w = 0, h = 0, comp = 0;
        // a small file may say it is a huge picture: those are left out before decoding
        if (Fetch(path, bytes) && stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &comp) && w > 0 && h > 0 && w <= 4096 &&
            h <= 4096) {
            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 4);
            if (pixels) {
                // the centre square (covers are drawn as squares), scaled to kMaxSide: each new pixel is the
                // average of the source pixels it covers
                const int side = std::min(w, h);
                const int ox = (w - side) / 2, oy = (h - side) / 2;
                d.width = d.height = kMaxSide;
                d.rgba.resize(size_t(kMaxSide) * kMaxSide * 4);
                for (int y = 0; y < kMaxSide; y++) {
                    const int y0 = oy + y * side / kMaxSide;
                    const int y1 = std::max(y0 + 1, oy + (y + 1) * side / kMaxSide);
                    for (int x = 0; x < kMaxSide; x++) {
                        const int x0 = ox + x * side / kMaxSide;
                        const int x1 = std::max(x0 + 1, ox + (x + 1) * side / kMaxSide);
                        unsigned sum[4] = {0, 0, 0, 0};
                        for (int sy = y0; sy < y1; sy++) {
                            const unsigned char* row = pixels + (size_t(sy) * w + size_t(x0)) * 4;
                            for (int sx = 0; sx < x1 - x0; sx++) {
                                for (int c = 0; c < 4; c++) sum[c] += row[sx * 4 + c];
                            }
                        }
                        const unsigned n = unsigned((y1 - y0) * (x1 - x0));
                        unsigned char* out = d.rgba.data() + (size_t(y) * kMaxSide + x) * 4;
                        for (int c = 0; c < 4; c++) out[c] = static_cast<unsigned char>(sum[c] / n);
                    }
                }
                stbi_image_free(pixels);
            }
        }
        std::lock_guard lock(m_mutex);
        if (d.rgba.empty()) {
            // no cover: remembered as none, the placeholder stays
            m_images[path] = 0;
        } else {
            m_decoded.push_back(std::move(d));
        }
    }
}

} // namespace nxdock
