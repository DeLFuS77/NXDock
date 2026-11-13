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
    // the style chosen in Settings › Look (Style below): the second light of the background and
    // what adorns (frames, embers, titles); they never say anything, red stays for errors
    NVGcolor glow2 = {}, ornament = {};
    int style = 0;
};

// The styles NXDock can wear, as the colours of NXPress: its own and three with a scene.
enum Style { kStyleNXDock = 0, kStyleRuneStone = 1, kStyleVampire = 2, kStyleHuntress = 3, kStyles = 4 };

inline NVGcolor Hex(unsigned rgb, float a = 1.f) {
    return nvgRGBAf(((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a);
}

inline Theme DarkTheme() {
    return {Hex(0x0f1218), Hex(0x171c24), Hex(0x1e2430), Hex(0x2a3240), Hex(0xe8edf3), Hex(0x8d99aa),
            Hex(0x3db7ea), Hex(0x5fd08f), Hex(0xf5b25c), Hex(0xff6b6b),
            Hex(0x9d86ff), Hex(0xf0a24a), Hex(0x22bfe3), Hex(0xffffff), true, Hex(0x9d86ff), Hex(0x3db7ea), kStyleNXDock};
}

inline Theme LightTheme() {
    return {Hex(0xeef1f5), Hex(0xffffff), Hex(0xf3f5f8), Hex(0xdde2e9), Hex(0x18202a), Hex(0x5d6878),
            Hex(0x0a6fb0), Hex(0x187a41), Hex(0x8a5000), Hex(0xc62828),
            Hex(0x6a4ae0), Hex(0xb86a10), Hex(0x0a7d96), Hex(0xffffff), false, Hex(0x6a4ae0), Hex(0x0a6fb0), kStyleNXDock};
}

// A style in the light or the dark theme. The background, the panels, the lines, the text and
// the main colour (focus, buttons, progress) change; green, orange, red and the colours of the
// install stages keep what they say.
inline Theme StyleTheme(int style, bool light) {
    Theme t = light ? LightTheme() : DarkTheme();
    struct Colours { unsigned bg, panel, panel2, line, text, muted, work, on_work, glow2, ornament; };
    static constexpr Colours kDark[kStyles] = {
        {0x0f1218, 0x171c24, 0x1e2430, 0x2a3240, 0xe8edf3, 0x8d99aa, 0x3db7ea, 0xffffff, 0x9d86ff, 0x3db7ea},
        {0x0b1316, 0x101b1f, 0x152429, 0x1f3d45, 0xe2f3f5, 0x8fb8be, 0x4fe3ff, 0x021216, 0xd9a441, 0x4fe3ff},
        {0x070a12, 0x0b0f1c, 0x111728, 0x24304f, 0xe6eaf6, 0x9ca8c8, 0xcdd9ff, 0x0b1020, 0xa3172f, 0xa3172f},
        {0x05070c, 0x0a0d14, 0x10151f, 0x26303c, 0xe6eef0, 0x93a3a8, 0x9cff3a, 0x0a1400, 0xe2621c, 0xe2621c},
    };
    static constexpr Colours kLight[kStyles] = {
        {0xeef1f5, 0xffffff, 0xf3f5f8, 0xdde2e9, 0x18202a, 0x5d6878, 0x0a6fb0, 0xffffff, 0x6a4ae0, 0x0a6fb0},
        {0xebe2cc, 0xf6f0e1, 0xefe6d2, 0xd3c4a0, 0x2a2418, 0x6b5f45, 0x0c6f86, 0xffffff, 0xa87a1e, 0x0c6f86},
        {0xcfd3e2, 0xdde1ec, 0xd5d9e7, 0xa9b0c8, 0x141a2c, 0x3f4866, 0x26336e, 0xffffff, 0x8c1b33, 0x8c1b33},
        {0xc9cfcf, 0xd8dddc, 0xd0d6d5, 0xa7b0af, 0x141a1a, 0x3f4a4a, 0x2f6408, 0xffffff, 0x9a3a16, 0x9a3a16},
    };
    if (style < 0 || style >= kStyles) style = kStyleNXDock;
    const Colours& c = (light ? kLight : kDark)[style];
    t.bg = Hex(c.bg);
    t.panel = Hex(c.panel);
    t.panel2 = Hex(c.panel2);
    t.line = Hex(c.line);
    t.text = Hex(c.text);
    t.muted = Hex(c.muted);
    t.work = Hex(c.work);
    t.on_work = Hex(c.on_work);
    t.glow2 = Hex(c.glow2);
    t.ornament = Hex(c.ornament);
    t.style = style;
    return t;
}

// Between two themes, for the change of style: every colour on the way, the rest of the one
// that is coming once past the middle.
inline Theme MixTheme(const Theme& a, const Theme& b, float k) {
    Theme t = k < 0.5f ? a : b;
    NVGcolor Theme::*colours[] = {&Theme::bg, &Theme::panel, &Theme::panel2, &Theme::line, &Theme::text, &Theme::muted,
                                  &Theme::work, &Theme::ok, &Theme::warn, &Theme::err, &Theme::violet, &Theme::orange,
                                  &Theme::cyan, &Theme::on_work, &Theme::glow2, &Theme::ornament};
    for (auto member : colours) {
        t.*member = nvgLerpRGBA(a.*member, b.*member, k);
    }
    return t;
}

} // namespace nxdock
