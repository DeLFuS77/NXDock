// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Shops on the computer: a plain JSON index, the same packed as Tinfoil and Ownfoil pack it
// (zstd and zlib, with and without the session key area), an encrypted one told apart, relative
// addresses made whole, names from the part after #, and the files grouped as games.
#include "shop.hpp"

#include <zlib.h>
#include <zstd.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string Pack(unsigned char flag, const std::string& data, bool session) {
    std::string out = "TINFOIL";
    out += char(flag);
    if (session) out += std::string(0x100, '\0');
    unsigned long long n = data.size();
    for (int i = 0; i < 8; i++) out += char((n >> (8 * i)) & 0xFF);
    return out + data;
}

int main() {
    const std::string json = R"({"files":[
      {"url":"/api/get_game/1#Game%20One%20[0100000000010000][v0].nsp","size":1000},
      {"url":"files/Game One [0100000000010800][v65536].nsz","size":200},
      {"url":"https://cdn.example/dlc.nsp#Game One - Extra [0100000000011001][v0].nsp","size":50},
      {"url":"Solo.xci","size":7},
      {"url":"readme.txt","size":1}],
      "directories":["more/"], "success":"hello"})";

    std::string out;
    CHECK(UnpackIndex("  " + json, out) == ShopError::None && out.find("files") != std::string::npos);
    std::vector<std::string> dirs;
    auto files = ParseIndex(json, "http://shop:8465/", &dirs);
    CHECK(files.size() == 4);
    CHECK(dirs.size() == 1 && dirs[0] == "http://shop:8465/more/");
    if (files.size() == 4) {
        CHECK(files[0].url == "http://shop:8465/api/get_game/1");
        CHECK(files[0].name == "Game One [0100000000010000][v0].nsp" && files[0].title_id == 0x0100000000010000ull);
        CHECK(files[1].url == "http://shop:8465/files/Game One [0100000000010800][v65536].nsz" && files[1].version == 65536);
        CHECK(files[2].url == "https://cdn.example/dlc.nsp");
        CHECK(files[3].title_id == 0);
    }
    const auto games = ShopGames(files);
    CHECK(games.size() == 2);
    if (games.size() == 2) {
        CHECK(games[0].name == "Game One" && games[0].files.size() == 3 && games[0].title_id == 0x0100000000010000ull);
        int upd = 0, dlc = 0;
        for (const auto& f : games[0].files) {
            upd += f.kind == CatalogFile::Kind::Update;
            dlc += f.kind == CatalogFile::Kind::Dlc;
        }
        CHECK(upd == 1 && dlc == 1);
        CHECK(games[1].name == "Solo" && games[1].files.size() == 1);
    }

    // packed as the shops pack it
    std::string z(ZSTD_compressBound(json.size()), '\0');
    z.resize(ZSTD_compress(z.data(), z.size(), json.data(), json.size(), 3));
    for (const bool session : {true, false}) {
        CHECK(UnpackIndex(Pack(0x0E, z, session), out) == ShopError::None && out == json);
    }
    uLongf zl = compressBound(uLong(json.size()));
    std::string zz(zl, '\0');
    compress(reinterpret_cast<Bytef*>(zz.data()), &zl, reinterpret_cast<const Bytef*>(json.data()), uLong(json.size()));
    zz.resize(zl);
    CHECK(UnpackIndex(Pack(0x0D, zz, true), out) == ShopError::None && out == json);
    CHECK(UnpackIndex(Pack(0x00, json, true), out) == ShopError::None && out == json);
    // encrypted: told apart, never opened
    CHECK(UnpackIndex(Pack(0xFE, z, true), out) == ShopError::Encrypted);
    CHECK(UnpackIndex("<html>index of</html>", out) == ShopError::NotIndex);
    CHECK(UnpackIndex(Pack(0x0E, "broken", true), out) == ShopError::NotIndex);

    // the list of shops, saved and read back; the key joins user and password
    const std::string path = "/tmp/shops_test.ini";
    CHECK(SaveShops(path, {{"Home", "http://a/", "me", "secret"}, {"Open", "https://b/", "", ""}}));
    const auto shops = LoadShops(path);
    CHECK(shops.size() == 2 && shops[0].pass == "secret" && shops[1].user.empty());
    CHECK(ShopKey(shops[0]) == std::string("me\x1fsecret") && ShopKey(shops[1]).empty());

    if (failures) {
        std::printf("shop_test: %d failures\n", failures);
        return 1;
    }
    std::printf("shop_test: ok\n");
    return 0;
}
