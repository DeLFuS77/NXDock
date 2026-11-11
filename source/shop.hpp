// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "catalog.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// Shops of the kind of Tinfoil and Ownfoil (9A): an index of files, plain JSON or packed in a
// "TINFOIL" header with zstd or zlib, opened as another library. An encrypted index is told
// apart and explained: it is encrypted with Tinfoil's key, which only Tinfoil has.
struct ShopConfig {
    std::string name, url, user, pass; // the password is typed on the console and kept on the SD
    std::string pin; // https: the key of the shop's certificate, kept the first time it opens
};

std::vector<ShopConfig> LoadShops(const std::string& path);
bool SaveShops(const std::string& path, const std::vector<ShopConfig>& shops);
const char* ShopsPath(); // sdmc:/config/NXDock/shops.ini

struct ShopFile {
    std::string url, name;
    unsigned long long size = 0;
    unsigned long long title_id = 0;
    unsigned version = 0;
};

// KeyChanged: an https shop answered with another key than the one kept the first time
enum class ShopError { None, Encrypted, NotIndex, Auth, Network, Empty, KeyChanged };

// The bytes of an index to its JSON (unpacking a TINFOIL header); Encrypted or NotIndex.
ShopError UnpackIndex(const std::string& bytes, std::string& json);
// The files of the JSON, with their addresses made whole against `base`, and the directories
// it points to.
std::vector<ShopFile> ParseIndex(const std::string& json, const std::string& base, std::vector<std::string>* directories = nullptr);
// The files as games of a library: the game, its updates and DLC together.
std::vector<Game> ShopGames(const std::vector<ShopFile>& files);
// The user and password of a shop as one key for the install core (a unit separator between).
std::string ShopKey(const ShopConfig& shop);

// Reads a shop on a thread.
class ShopLoader {
public:
    ~ShopLoader();
    void Start(const ShopConfig& shop);
    bool Busy();
    // once, when it ends; `pin` the key of an https shop read the first time (to keep), or empty
    bool Take(ShopError& error, std::vector<ShopFile>& files, std::string& pin);

private:
    std::thread m_thread;
    std::mutex m_mutex;
    bool m_busy = false, m_done = false;
    ShopError m_error = ShopError::None;
    std::string m_pin;
    std::vector<ShopFile> m_files;
};

} // namespace nxdock
