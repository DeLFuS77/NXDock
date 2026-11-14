// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "i18n_table.hpp"

namespace nxdock {

// The first texts of NXDock, in source/texts/strings_*.inc.
struct Strings {
#define TEXT NXDOCK_TEXT_FIELD
#include "texts/strings.inc"
#undef TEXT
};

// Picks the language of the console: English, Spanish, French, German, Italian, Portuguese,
// Dutch, Russian, Japanese, Korean or Chinese, as the library app; English for any other.
const Strings& Tr();
void InitStrings();

// The languages NXDock speaks, to choose another one than the console's (the first-run wizard).
int LanguageCount();
const char* LanguageName(int index); // in its own language: English, Español, …
int ConsoleLanguage();               // the index the console's language gives
int CurrentLanguage();
void UseLanguage(int index);         // -1: the console's

} // namespace nxdock
