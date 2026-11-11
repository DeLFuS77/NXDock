// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Checks a minisign signature (https://jedisct1.github.io/minisign/), as tools/minisign makes
// them for the releases: Ed25519 over the BLAKE2b-512 of the file ("ED"), or over the file itself
// ("Ed"), with a trusted comment signed as well. NXDock checks with it a new version of itself
// before it puts it in place (5A of the visual test of the new tools).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxdock::minisig {

struct Key {
    uint8_t id[8];
    uint8_t pk[32];
};

// The keys of a text: minisign public keys in base64 ("Ed", the ID and the key), one per line or
// separated by spaces; anything else is left out.
std::vector<Key> ParseKeys(const std::string& text);

// Whether `sig` (the text of a .minisig) is a signature of one of `keys` over `data`; its trusted
// comment goes to `comment`.
bool Verify(const std::string& data, const std::string& sig, const std::vector<Key>& keys, std::string& comment);

// Standard base64; false on anything else.
bool Base64(const std::string& text, std::vector<uint8_t>& out);

} // namespace nxdock::minisig
