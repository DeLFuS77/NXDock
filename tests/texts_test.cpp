// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The texts of source/texts in every language: none empty, and each with the same % fields as the
// English one, in the same order (a missing or extra %s would read the wrong value, or crash). The
// notice of Code and license keeps the name of NXDock, who makes it and its license as they are.
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using namespace nxdock;

int g_failed = 0;

// The conversions of a printf format, in order: "%s %02d:%.1f" gives "sdf". A percent sign in
// words ("10 % or more", "2%～8%") is no conversion.
std::string Fields(const char* text) {
    std::string out;
    for (const char* p = text; *p; p++) {
        if (*p != '%') continue;
        const char* q = p + 1;
        while (*q && std::strchr("-+#0123456789.*lhz", *q)) q++;
        if (*q && std::strchr("sdiufFeEgGxXcp", *q)) {
            out += *q;
            p = q;
        } else if (*q == '%') {
            p = q;
        }
    }
    return out;
}

// The texts of every struct are kept as const char*, one after the other.
template <class S> void Check(const char* what, const S& english, const S& other, const char* language) {
    const auto* en = reinterpret_cast<const char* const*>(&english);
    const auto* tr = reinterpret_cast<const char* const*>(&other);
    for (size_t i = 0; i < sizeof(S) / sizeof(const char*); i++) {
        if (!tr[i] || !*tr[i]) {
            std::printf("FAIL %s %s #%zu is empty\n", what, language, i);
            g_failed++;
        } else if (Fields(en[i]) != Fields(tr[i])) {
            std::printf("FAIL %s %s #%zu: \"%s\" and \"%s\" differ in their %% fields\n", what, language, i, en[i], tr[i]);
            g_failed++;
        }
    }
}

} // namespace

int main() {
    const More english_more = TrM();
    const Tools english_tools = TrT();
    UseLanguage(0);
    const Strings english = Tr();
    for (int l = 0; l < LanguageCount(); l++) {
        UseLanguage(l);
        Check("Tr", english, Tr(), LanguageName(l));
        Check("TrM", english_more, TrM(), LanguageName(l));
        Check("TrT", english_tools, TrT(), LanguageName(l));
        // Settings › Help › Code and license, which a modified version keeps (TRADEMARKS.md)
        if (std::string(TrT().lic_sub).rfind("NXDock © 2026 DeLFuS77 · GPL-3.0 ", 0) != 0) {
            std::printf("FAIL %s: Code and license says \"%s\"\n", LanguageName(l), TrT().lic_sub);
            g_failed++;
        }
    }
    std::printf(g_failed ? "%d failed\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
