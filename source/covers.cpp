// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "covers.hpp"

#include <sys/stat.h>
#include <cstdio>
#include <functional>
#include "http.hpp"

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
}

int Covers::Get(const std::string& path) {
    if (path.empty()) {
        return 0;
    }
    std::lock_guard lock(m_mutex);
    const auto it = m_images.find(path);
    if (it != m_images.end()) {
        return it->second;
    }
    if (!m_asked.count(path) && !m_threads.empty()) {
        m_asked.insert(path);
        // the newest asked first: what is on screen now
        m_wanted.push_front(path);
        m_wake.notify_one();
    }
    return 0;
}

void Covers::Upload(NVGcontext* vg) {
    for (int i = 0; i < kUploadsPerFrame; i++) {
        Decoded d;
        {
            std::lock_guard lock(m_mutex);
            if (m_decoded.empty()) return;
            d = std::move(m_decoded.front());
            m_decoded.pop_front();
        }
        const int image = nvgCreateImageRGBA(vg, d.width, d.height, 0, d.rgba.data());
        std::lock_guard lock(m_mutex);
        m_images[d.path] = image;
    }
}

void Covers::Free(NVGcontext* vg) {
    std::lock_guard lock(m_mutex);
    for (const auto& [path, image] : m_images) {
        if (image) nvgDeleteImage(vg, image);
    }
    m_images.clear();
    m_asked.clear();
}

bool Covers::Fetch(const std::string& path, std::vector<unsigned char>& bytes) {
    const std::string cached = CachePath(path);
    if (ReadFile(cached, bytes)) {
        return true;
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
        if (Fetch(path, bytes)) {
            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &d.width, &d.height, &channels, 4);
            if (pixels) {
                d.rgba.assign(pixels, pixels + size_t(d.width) * d.height * 4);
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
