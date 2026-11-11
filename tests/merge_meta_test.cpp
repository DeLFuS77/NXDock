// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The CNMT of a game merged with its update (25D, merge_meta): one Application of the version of
// the update, with its contents and the merged program, no delta fragments, no required
// application version and the firmware of the LZ4 layer; its meta NCA (a PFS0 hashed in blocks),
// read back; the record of the database of ncm. (Against real games: the CNMTs of four games and
// their updates read and written again the same, and the meta NCA of one of them made again byte
// for byte.)
#include "merge_meta.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

extern "C" unsigned char* SHA256(const unsigned char* d, size_t n, unsigned char* md);

using namespace nxdock;
using Bytes = std::vector<uint8_t>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

namespace {

void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }
void Put64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, 8); }
uint32_t Get32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint64_t Get64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

mergemeta::Content Make(uint8_t type, uint8_t seed, uint64_t size) {
    mergemeta::Content c;
    c.hash.fill(seed);
    c.id.fill(uint8_t(seed + 1));
    c.size = size;
    c.type = type;
    return c;
}

} // namespace

int main() {
    const mergemeta::HashFn sha = [](const uint8_t* d, size_t n, uint8_t out[32]) { SHA256(d, n, out); };
    const uint64_t app = 0x0100ABCD12340000ull;

    // the base: an Application v0 with its program, control and legal information
    mergemeta::Cnmt base;
    base.title_id = app;
    base.version = 0;
    base.type = mergemeta::kTypeApplication;
    base.header[0x14] = 0;
    Put32(base.header.data() + 0x18, 0x0C000000);
    base.extended.assign(0x10, 0);
    Put64(base.extended.data(), app | 0x800);
    Put32(base.extended.data() + 8, 5u << 26); // 5.0.0
    Put32(base.extended.data() + 0xC, 0x10000);
    base.contents = {Make(1, 10, 3ull << 30), Make(3, 20, 900000), Make(5, 30, 200000)};
    base.digest.fill(0x5A);
    // the update: a Patch v196608 with its program, control, a document, delta fragments and history
    mergemeta::Cnmt upd;
    upd.title_id = app | 0x800;
    upd.version = 196608;
    upd.type = mergemeta::kTypePatch;
    Put32(upd.header.data() + 0x18, 0x40000000);
    upd.extended.assign(0x18, 0);
    Put64(upd.extended.data(), app);
    Put32(upd.extended.data() + 8, 14u << 26); // 14.0.0
    upd.contents = {Make(1, 40, 1ull << 30), Make(3, 50, 950000), Make(4, 60, 11000000), Make(6, 70, 5000), Make(6, 80, 6000), Make(5, 90, 210000)};
    upd.extended_data.assign(1684, 0x33);
    Put32(upd.extended.data() + 0xC, 1684);

    // 1. written and read back the same, with the history of the update
    {
        const Bytes b = mergemeta::WriteCnmt(upd);
        mergemeta::Cnmt back;
        CHECK(mergemeta::ParseCnmt(b.data(), b.size(), back));
        CHECK(back.version == upd.version && back.contents.size() == 6 && back.extended_data.size() == 1684 && mergemeta::WriteCnmt(back) == b);
        CHECK(back.contents[0].size == (1ull << 30) && back.contents[3].type == 6);
    }

    // 2. merged: the version and contents of the update, the merged program, no delta fragments
    mergemeta::Content program = Make(1, 100, 700000000);
    mergemeta::Cnmt merged;
    CHECK(mergemeta::MergedCnmt(base, upd, program, merged));
    CHECK(merged.type == mergemeta::kTypeApplication && merged.title_id == app && merged.version == 196608);
    CHECK(merged.contents.size() == 4 && merged.contents[0].id == program.id && merged.contents[0].size == 700000000);
    bool delta = false;
    for (const auto& c : merged.contents) delta |= c.type == mergemeta::kContentDeltaFragment;
    CHECK(!delta && merged.extended_data.empty());
    CHECK(Get64(merged.extended.data()) == (app | 0x800));               // its patch id, as the base's
    CHECK(Get32(merged.extended.data() + 8) == (14u << 26));             // the higher firmware
    CHECK(Get32(merged.extended.data() + 0xC) == 0);                     // no required version
    CHECK(Get32(merged.header.data() + 0x18) == 0x40000000);
    // an old firmware raised to the LZ4 layer's
    {
        mergemeta::Cnmt u2 = upd, m2;
        Put32(u2.extended.data() + 8, 3u << 26);
        CHECK(mergemeta::MergedCnmt(base, u2, program, m2) && Get32(m2.extended.data() + 8) == mergemeta::kFirmware12);
    }
    // the update of another game, or two of the same type: refused
    {
        mergemeta::Cnmt other = upd, m2;
        Put64(other.extended.data(), app + 0x1000);
        CHECK(!mergemeta::MergedCnmt(base, other, program, m2));
        CHECK(!mergemeta::MergedCnmt(base, base, program, m2));
    }

    // 3. its meta NCA, from the header of the base's: read back, hashes right
    {
        Bytes model(0xC00, 0);
        std::memcpy(model.data() + 0x200, "NCA3", 4);
        model[0x205] = 1; // meta
        std::memset(model.data() + 0x300, 0x77, 0x40); // the key area stays
        uint8_t* fsh = model.data() + 0x400;
        fsh[0] = 2;
        fsh[2] = 1; // PFS0
        fsh[3] = 2; // HierarchicalSha256
        fsh[4] = 3; // AES-CTR
        Put32(fsh + 0x28, 0x1000);
        fsh[0x140] = 0x42;
        Bytes header, section;
        CHECK(mergemeta::MetaNca(model.data(), merged, sha, header, section));
        CHECK(header.size() == 0xC00 && section.size() % 0x200 == 0 && Get64(header.data() + 0x208) == 0xC00 + section.size());
        CHECK(Get32(header.data() + 0x240) == 6 && Get32(header.data() + 0x244) == (0xC00 + section.size()) / 0x200);
        CHECK(!std::memcmp(header.data() + 0x300, model.data() + 0x300, 0x40) && header[0x400 + 0x140] == 0x42);
        uint8_t d[32];
        SHA256(header.data() + 0x400, 0x200, d);
        CHECK(!std::memcmp(d, header.data() + 0x280, 32));
        const uint8_t* nf = header.data() + 0x400;
        const uint64_t hs = Get64(nf + 0x38), po = Get64(nf + 0x40), ps = Get64(nf + 0x48);
        SHA256(section.data(), hs, d);
        CHECK(!std::memcmp(d, nf + 8, 32));
        SHA256(section.data() + po, ps, d); // one block of 0x1000: its hash is the table's first
        CHECK(ps <= 0x1000 && !std::memcmp(d, section.data(), 32));
        Bytes cnmt;
        mergemeta::Cnmt back;
        CHECK(mergemeta::CnmtFromSection(section, nf, cnmt) && mergemeta::ParseCnmt(cnmt.data(), cnmt.size(), back));
        CHECK(back.version == 196608 && back.contents.size() == 4 && mergemeta::WriteCnmt(back) == mergemeta::WriteCnmt(merged));
        const char* name = reinterpret_cast<const char*>(section.data() + po + 0x28);
        CHECK(!std::strcmp(name, "Application_0100abcd12340000.cnmt"));
        // a model that is not a meta NCA
        model[0x205] = 0;
        CHECK(!mergemeta::MetaNca(model.data(), merged, sha, header, section));
    }

    // 4. the record of the database: header, extended header, the meta NCA first, then the rest
    {
        std::array<uint8_t, 16> mid;
        mid.fill(0xEE);
        const Bytes r = mergemeta::DbRecord(merged, mid, 0x400 + 0xC00);
        CHECK(r.size() == 8 + 0x10 + 5 * 0x18);
        CHECK(r[0] == 0x10 && r[2] == 5 && r[4] == 0);
        const uint8_t* first = r.data() + 8 + 0x10;
        CHECK(first[0] == 0xEE && Get32(first + 0x10) == 0x1000 && first[0x16] == mergemeta::kContentMeta);
        const uint8_t* prog = first + 0x18;
        CHECK(!std::memcmp(prog, program.id.data(), 16) && Get32(prog + 0x10) == 700000000u && prog[0x16] == mergemeta::kContentProgram);
    }

    if (failures) std::printf("%d failed\n", failures);
    else std::printf("merge_meta: all passed\n");
    return failures ? 1 : 0;
}
