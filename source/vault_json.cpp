// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "vault.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cstdio>

namespace nxdock {
namespace {

std::string Str(yyjson_val* obj, const char* key) {
    const char* s = yyjson_get_str(yyjson_obj_get(obj, key));
    return s ? s : "";
}

unsigned long long Num(yyjson_val* obj, const char* key) {
    yyjson_val* v = yyjson_obj_get(obj, key);
    if (yyjson_is_uint(v)) return yyjson_get_uint(v);
    if (yyjson_is_sint(v)) return (unsigned long long)std::max<long long>(0, yyjson_get_sint(v));
    if (yyjson_is_real(v)) return (unsigned long long)std::max(0.0, yyjson_get_real(v));
    return 0;
}

} // namespace

std::string TitleHex(unsigned long long id) {
    char t[20];
    std::snprintf(t, sizeof(t), "%016llX", id);
    return t;
}

std::vector<LibrarySave> ParseSaves(const std::string& json) {
    std::vector<LibrarySave> out;
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return out;
    yyjson_val* list = yyjson_obj_get(yyjson_doc_get_root(doc), "saves");
    size_t i, n;
    yyjson_val* item;
    yyjson_arr_foreach(list, i, n, item) {
        LibrarySave s;
        s.title_id = Str(item, "title_id");
        for (auto& c : s.title_id) c = char(c >= 'a' && c <= 'f' ? c - 32 : c);
        s.game = Str(item, "game");
        s.id = Str(item, "id");
        s.note = Str(item, "note");
        s.account = Str(item, "account");
        s.console = Str(item, "console");
        s.when = (long long)Num(item, "when");
        s.version = unsigned(Num(item, "version"));
        s.size = Num(item, "size");
        s.pinned = yyjson_get_bool(yyjson_obj_get(item, "pinned"));
        if (!s.title_id.empty() && !s.id.empty()) out.push_back(std::move(s));
    }
    yyjson_doc_free(doc);
    std::stable_sort(out.begin(), out.end(), [](const LibrarySave& a, const LibrarySave& b) { return a.when > b.when; });
    return out;
}

} // namespace nxdock
