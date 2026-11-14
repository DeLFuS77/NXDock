// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "i18n_tools.hpp"

#include "i18n.hpp"

namespace nxdock {
namespace {

// The texts of one language, made while compiling.
constexpr Tools Make(int language) {
    return Tools{
#define TEXT NXDOCK_TEXT_VALUE
#include "texts/tools.inc"
#undef TEXT
    };
}

// In the order of LanguageName.
constexpr Tools kAll[] = {Make(0), Make(1), Make(2), Make(3), Make(4), Make(5), Make(6), Make(7), Make(8), Make(9), Make(10)};
constexpr int kCount = int(sizeof(kAll) / sizeof(kAll[0]));
static_assert(kCount == texts::kLanguages);

} // namespace

const Tools& TrT() {
    const int l = CurrentLanguage();
    return kAll[l >= 0 && l < kCount ? l : 0];
}

} // namespace nxdock
