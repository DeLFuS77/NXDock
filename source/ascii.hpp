// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Letters A-Z in lower or upper case; anything else (digits, signs, UTF-8) is left as it is.
#pragma once

#include <string>

namespace nxdock {

inline std::string AsciiLower(std::string text) {
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return text;
}

inline std::string AsciiUpper(std::string text) {
    for (auto& c : text) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return text;
}

} // namespace nxdock
