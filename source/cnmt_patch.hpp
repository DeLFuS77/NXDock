// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The CNMT of a game with one of its contents changed (24A, "Compress games"): the record of the
// content in the .cnmt file gets its new id, hash and size, and the hash table of the section (a
// hierarchical SHA-256 over the PFS0 that holds the .cnmt) is made again, with its master hash.
// It works on the decrypted section; the hash is given (SHA-256 on the console). Plain C++,
// tested on the computer.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace nxdock {

struct CnmtLayers {
    uint32_t block_size = 0;
    uint64_t hash_offset = 0, hash_size = 0; // the hash table, after the start of the section
    uint64_t pfs0_offset = 0, pfs0_size = 0; // the PFS0
};

using HashFn = std::function<void(const uint8_t* data, size_t size, uint8_t out[32])>;

// Changes the record of old_id in the .cnmt of the section; false when there is none, or the
// section is not as expected. `master` gets the new master hash.
bool PatchCnmt(std::vector<uint8_t>& section, const CnmtLayers& layers, const uint8_t old_id[16], const uint8_t new_id[16],
               const uint8_t new_hash[32], uint64_t new_size, const HashFn& hash, uint8_t master[32]);

} // namespace nxdock
