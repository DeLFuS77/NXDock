// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// How the texts are kept. Each text is one entry in a file of source/texts, with its name and every
// language together:
//
//     TEXT(queue_empty,
//          .en = "The queue is empty",
//          .es = "La cola está vacía",
//          ...)
//
// To add a text, add its entry to the file of its area (strings_*.inc for Tr(), more_*.inc for
// TrM(), tools_*.inc for TrT()); nothing else changes. The languages go in the order of
// LanguageName (en es fr de it pt nl ru ja ko zh); one left out is taken from English. The tables
// of every language are made while compiling, so nothing is copied or filled when NXDock starts.
#pragma once

namespace nxdock::texts {

// One text in every language, in the order of LanguageName.
struct Langs {
    const char *en, *es, *fr, *de, *it, *pt, *nl, *ru, *ja, *ko, *zh;
};

constexpr int kLanguages = 11;

// The text in a language, or in English when that language lacks it.
constexpr const char* Pick(int language, const Langs& t) {
    const char* const all[kLanguages] = {t.en, t.es, t.fr, t.de, t.it, t.pt, t.nl, t.ru, t.ja, t.ko, t.zh};
    const char* text = language >= 0 && language < kLanguages ? all[language] : nullptr;
    return text ? text : t.en ? t.en : "";
}

} // namespace nxdock::texts

// The fields of a struct of texts, one for each entry of a file of source/texts.
#define NXDOCK_TEXT_FIELD(name, ...) const char* name;
// One language's value of an entry, inside a struct of texts made for `language`.
#define NXDOCK_TEXT_VALUE(name, ...) .name = ::nxdock::texts::Pick(language, ::nxdock::texts::Langs{__VA_ARGS__}),
