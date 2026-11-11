// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "minisig.hpp"

#include <cstring>
#include <sstream>

extern "C" {
#include "monocypher-ed25519.h"
#include "monocypher.h"
}

namespace nxdock::minisig {
namespace {

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    for (const char c : text) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        if (line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

} // namespace

bool Base64(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0, pad = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') {
            pad++;
            continue;
        } else return false;
        if (pad) return false; // letters after the padding
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
    }
    return pad <= 2 && (text.size() % 4 == 0 || pad == 0);
}

std::vector<Key> ParseKeys(const std::string& text) {
    std::vector<Key> keys;
    std::istringstream words(text);
    std::string word;
    while (words >> word) {
        std::vector<uint8_t> data;
        if (!Base64(word, data) || data.size() != 2 + 8 + 32 || data[0] != 'E' || data[1] != 'd') continue;
        Key k;
        std::memcpy(k.id, data.data() + 2, 8);
        std::memcpy(k.pk, data.data() + 10, 32);
        keys.push_back(k);
    }
    return keys;
}

bool Verify(const std::string& data, const std::string& sig, const std::vector<Key>& keys, std::string& comment) {
    comment.clear();
    const auto lines = Lines(sig);
    if (lines.size() < 4 || lines[0].rfind("untrusted comment:", 0) != 0 || lines[2].rfind("trusted comment: ", 0) != 0) return false;
    std::vector<uint8_t> head, global;
    if (!Base64(Trim(lines[1]), head) || head.size() != 2 + 8 + 64) return false;
    if (!Base64(Trim(lines[3]), global) || global.size() != 64) return false;
    const std::string trusted = lines[2].substr(std::strlen("trusted comment: "));
    const bool hashed = head[0] == 'E' && head[1] == 'D';
    if (!hashed && !(head[0] == 'E' && head[1] == 'd')) return false;
    for (const auto& key : keys) {
        if (std::memcmp(key.id, head.data() + 2, 8) != 0) continue;
        const uint8_t* signature = head.data() + 10;
        int bad;
        if (hashed) {
            uint8_t sum[64];
            crypto_blake2b(sum, sizeof(sum), reinterpret_cast<const uint8_t*>(data.data()), data.size());
            bad = crypto_ed25519_check(signature, key.pk, sum, sizeof(sum));
        } else {
            bad = crypto_ed25519_check(signature, key.pk, reinterpret_cast<const uint8_t*>(data.data()), data.size());
        }
        if (bad) return false;
        // the trusted comment, signed with the signature of the file
        std::string both(reinterpret_cast<const char*>(signature), 64);
        both += trusted;
        if (crypto_ed25519_check(global.data(), key.pk, reinterpret_cast<const uint8_t*>(both.data()), both.size())) return false;
        comment = trusted;
        return true;
    }
    return false;
}

} // namespace nxdock::minisig
