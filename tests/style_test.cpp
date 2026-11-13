// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The styles of Settings › Look, on the computer: NXDock is the theme it always was, every style
// reads well in light and in dark, green, orange and red never change, the change of style starts
// and ends on its two styles and every language names them.
#include "theme.hpp"
#include "i18n_more.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace nxdock {
int g_lang = 0;
int CurrentLanguage() { return g_lang; }
} // namespace nxdock

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static bool Same(NVGcolor a, NVGcolor b) {
    return std::fabs(a.r - b.r) < 1e-6f && std::fabs(a.g - b.g) < 1e-6f && std::fabs(a.b - b.b) < 1e-6f && std::fabs(a.a - b.a) < 1e-6f;
}

static float Lum(NVGcolor c) {
    const auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

static float Contrast(NVGcolor a, NVGcolor b) {
    float x = Lum(a), y = Lum(b);
    if (x < y) std::swap(x, y);
    return (x + 0.05f) / (y + 0.05f);
}

int main() {
    for (int light = 0; light < 2; light++) {
        const Theme base = light ? LightTheme() : DarkTheme();
        // NXDock is the theme of before
        const Theme own = StyleTheme(kStyleNXDock, light);
        NVGcolor Theme::*all[] = {&Theme::bg, &Theme::panel, &Theme::panel2, &Theme::line, &Theme::text, &Theme::muted, &Theme::work,
                                  &Theme::ok, &Theme::warn, &Theme::err, &Theme::violet, &Theme::orange, &Theme::cyan, &Theme::on_work};
        for (auto m : all) CHECK(Same(own.*m, base.*m));
        CHECK(Same(own.glow2, base.violet));
        for (int s = 0; s < kStyles; s++) {
            const Theme t = StyleTheme(s, light);
            CHECK(t.style == s && t.dark == !light);
            // what says something keeps its colour in every style
            CHECK(Same(t.ok, base.ok) && Same(t.warn, base.warn) && Same(t.err, base.err));
            CHECK(Same(t.violet, base.violet) && Same(t.orange, base.orange) && Same(t.cyan, base.cyan));
            // it reads well: text on the background and on panels, text on buttons, the main colour
            CHECK(Contrast(t.text, t.bg) >= 7);
            CHECK(Contrast(t.text, t.panel) >= 7);
            CHECK(Contrast(t.muted, t.panel) >= 4.5f);
            // (the white on the blue of NXDock is as it always was; the new styles read better)
            if (s != kStyleNXDock) CHECK(Contrast(t.on_work, t.work) >= 4.5f);
            CHECK(Contrast(t.work, t.bg) >= 3);
            if (s != kStyleNXDock) CHECK(!Same(t.work, own.work));
        }
        // the change of style starts and ends on its two styles
        const Theme a = StyleTheme(kStyleRuneStone, light), b = StyleTheme(kStyleHuntress, light);
        const Theme from = MixTheme(a, b, 0), to = MixTheme(a, b, 1), half = MixTheme(a, b, 0.5f);
        CHECK(Same(from.work, a.work) && Same(from.bg, a.bg) && from.style == kStyleRuneStone);
        CHECK(Same(to.work, b.work) && Same(to.ornament, b.ornament) && to.style == kStyleHuntress);
        CHECK(!Same(half.work, a.work) && !Same(half.work, b.work));
    }
    // a style out of range is NXDock
    CHECK(StyleTheme(9, false).style == kStyleNXDock && StyleTheme(-1, true).style == kStyleNXDock);
    // every language names the row and the styles, with a line under it
    for (g_lang = 0; g_lang < 11; g_lang++) {
        const More& m = TrM();
        for (const char* s : {m.style_row, m.style_hint, m.style_rune, m.style_vamp, m.style_hunt}) {
            CHECK(s && std::strlen(s) > 0);
        }
    }
    g_lang = 1;
    CHECK(std::strcmp(TrM().style_row, "Estilo") == 0 && std::strcmp(TrM().style_hunt, "Cazadora") == 0);
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("style_test: ok\n");
    return 0;
}
