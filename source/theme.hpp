// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <nanovg.h>

namespace nxdock {

// Colours of the chosen design: progress in the NXPress blue, red only for errors.
// Install stages keep their own colours: network blue, decompress violet,
// write orange, check green.
struct Theme {
    NVGcolor bg, panel, panel2, line, text, muted;
    NVGcolor work, ok, warn, err;
    NVGcolor violet, orange, cyan;
    NVGcolor on_work; // text on a work-blue surface
    bool dark;
};

inline NVGcolor Hex(unsigned rgb, float a = 1.f) {
    return nvgRGBAf(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a);
}

inline Theme DarkTheme() {
    return {Hex(0x0f1218), Hex(0x171c24), Hex(0x1e2430), Hex(0x2a3240), Hex(0xe8edf3), Hex(0x8d99aa),
            Hex(0x3db7ea), Hex(0x5fd08f), Hex(0xf5b25c), Hex(0xff6b6b),
            Hex(0x9d86ff), Hex(0xf0a24a), Hex(0x22bfe3), Hex(0xffffff), true};
}

inline Theme LightTheme() {
    return {Hex(0xeef1f5), Hex(0xffffff), Hex(0xf3f5f8), Hex(0xdde2e9), Hex(0x18202a), Hex(0x5d6878),
            Hex(0x0a6fb0), Hex(0x187a41), Hex(0x8a5000), Hex(0xc62828),
            Hex(0x6a4ae0), Hex(0xb86a10), Hex(0x0a7d96), Hex(0xffffff), false};
}

} // namespace nxdock
