// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "mtp_proto.hpp"

#include <cstdio>
#include <functional>

namespace nxdock::mtp {

// A folder as a storage: the SD card on the console (sdmc:/), a folder on the computer in the
// tests. Files are written as they come; one that breaks off is removed. `open_out`, when given,
// opens each new file to be written with its size (the SD card: a big file on FAT32, 19).
class FolderStorage final : public Storage {
public:
    using OpenOut = std::function<FILE*(const std::string& path, uint64_t size)>;
    FolderStorage(std::string root, std::string description, OpenOut open_out = {})
        : m_root(std::move(root)), m_description(std::move(description)), m_open_out(std::move(open_out)) {}
    std::string Description() const override { return m_description; }
    void Space(uint64_t& total, uint64_t& free) override;
    bool List(const std::string& dir, std::vector<Entry>& out) override;
    bool Stat(const std::string& path, Entry& out) override;
    std::unique_ptr<Source> Open(const std::string& path) override;
    std::unique_ptr<Sink> Create(const std::string& path, uint64_t size, bool& busy) override;
    bool MakeDir(const std::string& path) override;
    bool Remove(const std::string& path, bool dir) override;
    bool Rename(const std::string& from, const std::string& to) override;

private:
    std::string Full(const std::string& path) const;
    std::string m_root, m_description;
    OpenOut m_open_out;
};

// A name a computer takes for a file or folder: no / \ : * ? " < > | or control characters, no
// spaces or dots at its end.
std::string CleanName(const std::string& name);

// A path inside a storage that stays inside it: parts between "/", none empty, "." or "..", no
// "\" or ":" (17: the folder of mods of a game is written to).
bool SafePath(const std::string& path);

// The folder of mods and cheats of each game in the games installed (17), mapped to
// atmosphere/contents/<title ID>/.
inline constexpr const char* kModsFolder = "Mods & Cheats";
enum class ModsPath {
    No,     // not in a folder of mods (the game folder, an NSP)
    Root,   // "<game>/Mods & Cheats" itself
    Inside, // under it: `inner` is the path inside
    Bad,    // under it, but leaving it
};
ModsPath SplitModsPath(const std::string& path, std::string& game, std::string& inner);

// The name of the zip of a save in the saves (8A, 18): "<game> · <who>.zip", cleaned.
std::string SaveZipName(const std::string& game, const std::string& who);
// A zip dropped on a game in the saves: "<game>/<file>.zip", one level down, nothing else.
bool SaveDrop(const std::string& path, std::string& game, std::string& file);

} // namespace nxdock::mtp
