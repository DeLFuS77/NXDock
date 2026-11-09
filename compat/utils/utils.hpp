// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for the parts of Sphaira's utils/utils.hpp the yati core uses.
#pragma once

#include "defines.hpp"
#include <cstdio>

namespace sphaira::utils {

struct HashStr {
    char str[0x21];
};

inline HashStr hexIdToStr(const void* id16) {
    HashStr r{};
    const u8* b = static_cast<const u8*>(id16);
    for (int i = 0; i < 0x10; i++) {
        std::snprintf(r.str + i * 2, 3, "%02x", b[i]);
    }
    return r;
}

inline HashStr hexIdToStr(FsRightsId id) { return hexIdToStr(&id); }
inline HashStr hexIdToStr(NcmRightsId id) { return hexIdToStr(&id); }
inline HashStr hexIdToStr(NcmContentId id) { return hexIdToStr(&id); }

template<typename T>
constexpr inline T AlignUp(T value, T align) {
    return (value + (align - 1)) & ~(align - 1);
}

template<typename T>
constexpr inline T AlignDown(T value, T align) {
    return value & ~(align - 1);
}

} // namespace sphaira::utils
