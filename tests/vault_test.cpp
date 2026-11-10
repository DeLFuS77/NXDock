// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The vault of saves on the computer: a zip written by NXDock is read back byte for byte (also
// by Python's zipfile), a deflated zip made by Python is read, damaged and unsafe names are
// refused, and the list of the library app is parsed.
#include "vault.hpp"
#include "zip.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static std::string ReadAll(zip::Reader& r, const zip::Entry& e, bool* ok) {
    std::string out;
    *ok = r.Extract(e, [&](const void* d, size_t n) {
        out.append(static_cast<const char*>(d), n);
        return true;
    });
    return out;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "/tmp";
    const std::string path = dir + "/vault_test.zip";
    std::string big(700000, '\0');
    for (size_t i = 0; i < big.size(); i++) big[i] = char((i * 7919) % 251);
    const std::string small = "hello save";
    {
        zip::Writer w;
        CHECK(w.Open(path));
        CHECK(w.AddFolder("data"));
        CHECK(w.AddFile("data/slot1.bin", big.size(), [&](uint64_t off, void* buf, size_t n) {
            std::memcpy(buf, big.data() + off, n);
            return true;
        }));
        CHECK(w.AddFile("meta.txt", small.size(), [&](uint64_t off, void* buf, size_t n) {
            std::memcpy(buf, small.data() + off, n);
            return true;
        }));
        CHECK(w.Close());
    }
    {
        zip::Reader r;
        CHECK(r.Open(path));
        CHECK(r.Entries().size() == 3);
        if (r.Entries().size() == 3) {
            CHECK(r.Entries()[0].Folder());
            bool ok = false;
            CHECK(ReadAll(r, r.Entries()[1], &ok) == big && ok);
            CHECK(ReadAll(r, r.Entries()[2], &ok) == small && ok);
        }
    }
    // the same zip, as another program reads it
    CHECK(std::system(("python3 -c \"import zipfile,sys; z=zipfile.ZipFile('" + path + "'); assert z.testzip() is None; assert z.read('meta.txt')==b'hello save'\"").c_str()) == 0);

    // a deflated zip made by Python (as mod packages are)
    const std::string deflated = dir + "/vault_deflate.zip";
    CHECK(std::system(("python3 -c \"import zipfile; z=zipfile.ZipFile('" + deflated +
                       "','w',zipfile.ZIP_DEFLATED); z.writestr('romfs/a.txt','abc'*10000); z.writestr('exefs/b.bin',bytes(range(256))*50); z.close()\"").c_str()) == 0);
    {
        zip::Reader r;
        CHECK(r.Open(deflated));
        CHECK(r.Entries().size() == 2);
        if (r.Entries().size() == 2) {
            bool ok = false;
            CHECK(ReadAll(r, r.Entries()[0], &ok).size() == 30000 && ok);
            const std::string b = ReadAll(r, r.Entries()[1], &ok);
            CHECK(ok && b.size() == 12800 && (unsigned char)b[255] == 255);
            // a damaged CRC is caught
            zip::Entry bad = r.Entries()[0];
            bad.crc ^= 1;
            ReadAll(r, bad, &ok);
            CHECK(!ok);
        }
    }

    CHECK(zip::SafeName("data/slot1.bin"));
    CHECK(!zip::SafeName("/etc/x"));
    CHECK(!zip::SafeName("a/../../b"));
    CHECK(!zip::SafeName("C:/x"));
    CHECK(!zip::SafeName("a\\b"));
    CHECK(!zip::SafeName(""));

    // the list of the library app
    const auto saves = ParseSaves(R"({"saves":[
      {"title_id":"0100000000010000","game":"Game","id":"aaa","when":100,"note":"old","version":0,"account":"Bob","console":"Switch","size":10,"pinned":false},
      {"title_id":"0100000000010000","game":"Game","id":"bbb","when":300,"note":"","version":65536,"account":"Bob","console":"Switch","size":12,"pinned":true},
      {"title_id":"0100000000020000","game":"","id":"ccc","when":200,"note":"x","version":0,"account":"Ann","console":"OLED","size":5,"pinned":false},
      {"title_id":"","id":"bad"}]})");
    CHECK(saves.size() == 3);
    if (saves.size() == 3) {
        CHECK(saves[0].id == "bbb" && saves[0].version == 65536 && saves[0].pinned);
        CHECK(saves[1].id == "ccc" && saves[1].console == "OLED");
        CHECK(saves[2].note == "old");
    }
    CHECK(ParseSaves("not json").empty());
    CHECK(TitleHex(0x0100000000010000ull) == "0100000000010000");

    if (failures) {
        std::printf("vault_test: %d failures\n", failures);
        return 1;
    }
    std::printf("vault_test: ok\n");
    return 0;
}
