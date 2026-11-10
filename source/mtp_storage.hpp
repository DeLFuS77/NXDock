// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "mtp_proto.hpp"

namespace nxdock::mtp {

// A folder as a storage: the SD card on the console (sdmc:/), a folder on the computer in the
// tests. Files are written as they come; one that breaks off is removed.
class FolderStorage final : public Storage {
public:
    FolderStorage(std::string root, std::string description) : m_root(std::move(root)), m_description(std::move(description)) {}
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
};

} // namespace nxdock::mtp
