// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "i18n.hpp"

#include <switch.h>

namespace nxdock {
namespace {

// The texts of one language, made while compiling.
constexpr Strings Make(int language) {
    return Strings{
#define TEXT NXDOCK_TEXT_VALUE
#include "texts/strings.inc"
#undef TEXT
    };
}

// In the order of LanguageName.
constexpr Strings kAll[] = {Make(0), Make(1), Make(2), Make(3), Make(4), Make(5), Make(6), Make(7), Make(8), Make(9), Make(10)};
constexpr int kCount = int(sizeof(kAll) / sizeof(kAll[0]));
static_assert(kCount == texts::kLanguages);

const char* const kNames[] = {"English", "Español", "Français", "Deutsch", "Italiano", "Português",
                              "Nederlands", "Русский", "日本語", "한국어", "中文"};

const Strings* g_strings = &kAll[0];
int g_console = 0;

} // namespace

void InitStrings() {
    u64 code = 0;
    SetLanguage lang{};
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &lang))) {
            switch (lang) {
                case SetLanguage_ES: case SetLanguage_ES419: g_console = 1; break;
                case SetLanguage_FR: case SetLanguage_FRCA: g_console = 2; break;
                case SetLanguage_DE: g_console = 3; break;
                case SetLanguage_IT: g_console = 4; break;
                case SetLanguage_PT: case SetLanguage_PTBR: g_console = 5; break;
                case SetLanguage_NL: g_console = 6; break;
                case SetLanguage_RU: g_console = 7; break;
                case SetLanguage_JA: g_console = 8; break;
                case SetLanguage_KO: g_console = 9; break;
                case SetLanguage_ZHCN: case SetLanguage_ZHHANS:
                case SetLanguage_ZHTW: case SetLanguage_ZHHANT: g_console = 10; break;
                default: break; // English
            }
        }
        setExit();
    }
    g_strings = &kAll[g_console];
}

int LanguageCount() {
    return kCount;
}

const char* LanguageName(int index) {
    return index >= 0 && index < LanguageCount() ? kNames[index] : kNames[0];
}

int ConsoleLanguage() {
    return g_console;
}

int CurrentLanguage() {
    return int(g_strings - kAll);
}

void UseLanguage(int index) {
    g_strings = &kAll[index >= 0 && index < LanguageCount() ? index : g_console];
}

const Strings& Tr() {
    return *g_strings;
}

} // namespace nxdock
