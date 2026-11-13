// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "compress_nx.hpp"

#include "config.hpp"
#include "http.hpp"

#include <cstring>

#include "cnmt_patch.hpp"
#include "errors.hpp"
#include "i18n_more.hpp"
#include "log.hpp"
#include "net.hpp"
#include "merge_game.hpp"
#include "merge_meta.hpp"
#include "merge_recipe.hpp"
#include "romfs_lz4.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <switch.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>

extern "C" void nxdock_log_open(const char* path);
extern "C" void nxdock_log_close();

namespace nxdock {
namespace {

namespace nca = sphaira::nca;
namespace keys = sphaira::keys;
namespace ncm = sphaira::ncm;
namespace ns = sphaira::ns;

// The LZ4 HC of Nintendo at level 9 (a hash chain of 256 tries): on real games 9 % to 21 % smaller
// than its fast level 0, and levels 12 and 16 gain under 1 % more for far more time (25A).
constexpr int kLevel = 9;
constexpr unsigned kThreads = 3;   // the cores a homebrew gets
constexpr uint32_t kMinGain = 64;  // a block must get this much smaller to be kept compressed
constexpr uint32_t kSample = 32;   // the analysis reads one block in 32
// A game Nintendo compressed is packed again when most of its blocks are of its fast level 0;
// those of its HC levels (5, 16) are as small as LZ4 gets.
constexpr double kRepackShare = 0.5;

std::string Hex(const void* p, size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = static_cast<const uint8_t*>(p)[i];
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

std::string Packed(const char* title, const char* todo, Result rc = 0, const std::string& what = {}) {
    ErrorText e;
    e.title = title;
    e.what = what;
    e.todo = todo;
    if (rc) e.code = ErrorCode(rc);
    return PackError(e);
}

struct Storage {
    NcmStorageId id;
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    bool ok = false;
    explicit Storage(NcmStorageId sid) : id(sid) {
        ok = R_SUCCEEDED(ncmOpenContentStorage(&cs, sid));
        if (ok && R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) {
            ncmContentStorageClose(&cs);
            ok = false;
        }
    }
    ~Storage() {
        if (!ok) return;
        ncmContentMetaDatabaseClose(&db);
        ncmContentStorageClose(&cs);
    }
};

// The game a title belongs to: its update is the game's ID with 0x800, its DLC the next 0x1000 block.
u64 GameOf(u64 id, u8 type) {
    if (type == NcmContentMetaType_Patch) return id & ~u64(0x800);
    if (type == NcmContentMetaType_AddOnContent) return (id - 0x1000) & ~u64(0xFFF);
    return id;
}

// The keys of one type in a database whose ID is in [lo, hi], and of the game `app` (0: any). The
// filter by game is done here: ncm does not filter updates and DLC by their game on every firmware,
// and then every game seemed to have the update or the DLC of another.
std::vector<NcmContentMetaKey> KeysOf(NcmContentMetaDatabase* db, u8 type, u64 app, u64 lo, u64 hi) {
    std::vector<NcmContentMetaKey> keys(64), out;
    s32 total = 0, written = 0;
    if (R_FAILED(ncmContentMetaDatabaseList(db, &total, &written, keys.data(), s32(keys.size()), NcmContentMetaType(type), 0, lo, hi,
                                            NcmContentInstallType_Full))) {
        return out;
    }
    if (total > written) {
        keys.resize(size_t(total));
        if (R_FAILED(ncmContentMetaDatabaseList(db, &total, &written, keys.data(), s32(keys.size()), NcmContentMetaType(type), 0, lo, hi,
                                                NcmContentInstallType_Full))) {
            return out;
        }
    }
    for (s32 i = 0; i < written && size_t(i) < keys.size(); i++) {
        const auto& k = keys[size_t(i)];
        if (k.type == type && k.id >= lo && k.id <= hi && (app == 0 || GameOf(k.id, k.type) == app)) out.push_back(k);
    }
    return out;
}

// The game in a storage: its key, its program and its CNMT.
struct GameContents {
    NcmContentMetaKey key{};
    NcmContentId program{}, meta{};
    bool has_meta = false; // other installers do not always list the CNMT
    s64 program_size = 0;
};

bool FindGame(Storage& s, u64 app, GameContents& g) {
    const auto found = KeysOf(&s.db, NcmContentMetaType_Application, app, app, app);
    if (found.empty()) return false;
    g.key = found[0];
    NcmContentInfo infos[16];
    s32 n = 0;
    if (R_FAILED(ncmContentMetaDatabaseListContentInfo(&s.db, &n, infos, 16, &g.key, 0))) return false;
    bool program = false, meta = false;
    for (s32 i = 0; i < n; i++) {
        if (infos[i].content_type == NcmContentType_Program && infos[i].id_offset == 0) {
            g.program = infos[i].content_id;
            program = true;
        }
        if (infos[i].content_type == NcmContentType_Meta) {
            g.meta = infos[i].content_id;
            meta = true;
        }
    }
    g.has_meta = meta;
    if (!program) return false;
    return R_SUCCEEDED(ncmContentStorageGetSizeFromContentId(&s.cs, &g.program_size, &g.program));
}

// The RomFS section an NCA can have compressed: IVFC, AES-CTR, not a patch, not sparse. -1 when
// none; `already` when it is compressed (by Nintendo): then it can be packed again.
int RomfsSection(const nca::Header& h, bool& already, bool& sparse_out) {
    already = false;
    sparse_out = false;
    for (int i = 0; i < NCA_SECTION_TOTAL; i++) {
        const auto& fh = h.fs_header[i];
        if (!h.fs_table[i].IsValid() || fh.fs_type != 0 || fh.hash_type != 3) continue;
        already = fh.compression_info.table_size != 0;
        bool sparse = false;
        for (u8 b : fh.spares_info) sparse |= b != 0;
        if (sparse) sparse_out = true;
        if (fh.encryption_type != nca::EncryptionType_AesCtr || fh.patch_info.indirect_size || sparse) continue;
        const u32 layers = fh.hash_data.integrity_meta_info.info_level_hash.max_layers;
        if (layers < 2 || layers > 7) continue;
        return i;
    }
    return -1;
}

nca::HierarchicalIntegrityVerificationLevelInformation DataLevel(const nca::FsHeader& fh) {
    const auto& info = fh.hash_data.integrity_meta_info.info_level_hash;
    return info.levels[info.max_layers - 2];
}

// Whether an update of the game is installed: it reads its base game by the places of the bytes
// stored in it, so a base game written again would break it (25A).
bool HasUpdate(u64 app) {
    for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
        NcmContentMetaDatabase db{};
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) continue;
        const bool found = !KeysOf(&db, NcmContentMetaType_Patch, app, app | 0x800, app | 0x800).empty();
        ncmContentMetaDatabaseClose(&db);
        if (found) return true;
    }
    return false;
}

// The version of the update of a game installed (0 without one), for its row.
unsigned UpdateVersion(u64 app) {
    unsigned version = 0;
    for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
        NcmContentMetaDatabase db{};
        if (R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) continue;
        for (const auto& key : KeysOf(&db, NcmContentMetaType_Patch, app, app | 0x800, app | 0x800)) {
            version = std::max(version, unsigned(key.version));
        }
        ncmContentMetaDatabaseClose(&db);
    }
    return version;
}

void Sha256(const u8* d, size_t n, u8 out[32]) { sha256CalculateHash(out, d, n); }

// The levels of the IVFC of a RomFS, from its FS header.
// The levels of the IVFC, empty when the header does not hold together (levels in order, one after
// another without wrapping, blocks of 512 B to 16 MiB): a damaged or made-up header is refused
// before its offsets are used to place anything.
std::vector<lz4pack::IvfcLevel> IvfcLevels(const nca::FsHeader& fh) {
    const auto& info = fh.hash_data.integrity_meta_info.info_level_hash;
    std::vector<lz4pack::IvfcLevel> lv;
    if (info.max_layers < 2 || info.max_layers > 7) return lv;
    u64 end = 0;
    for (u32 i = 0; i + 1 < info.max_layers; i++) {
        const auto& l = info.levels[i];
        if (l.block_size < 9 || l.block_size > 24 || l.logical_offset < end || l.hash_data_size > (u64(1) << 40) ||
            l.logical_offset > (u64(1) << 40)) {
            return {};
        }
        end = l.logical_offset + l.hash_data_size;
        lv.push_back({l.logical_offset, l.hash_data_size, l.block_size});
    }
    return lv;
}

Result ReadPlain(NcmContentStorage& cs, const NcmContentId& id, const u8 key[16], const nca::FsHeader& fh, u64 offset, u8* out, size_t size,
                 std::vector<u8>& work);

// The table of a RomFS Nintendo compressed, read decrypted.
bool ReadOldTable(NcmContentStorage& cs, const NcmContentId& id, const u8 key[16], const nca::FsHeader& fh, u64 data_start,
                  std::vector<lz4pack::Entry>& entries, u64& virt_end, std::vector<u8>& work) {
    const auto& ci = fh.compression_info;
    if (ci.table_size == 0 || ci.table_size > 256 * 1024 * 1024 || ci.table_header.magic != 0x52544B42) return false;
    std::vector<u8> table(size_t(ci.table_size));
    if (R_FAILED(ReadPlain(cs, id, key, fh, data_start + ci.table_offset, table.data(), table.size(), work))) return false;
    return lz4pack::ParseTable(table.data(), table.size(), ci.table_header.count, entries, virt_end);
}

void CtrAt(const nca::FsHeader& fh, u64 offset, u8 ctr[16]) {
    const u64 high = fh.section_ctr;
    for (int i = 0; i < 8; i++) ctr[i] = u8(high >> (8 * (7 - i)));
    const u64 low = offset >> 4;
    for (int i = 0; i < 8; i++) ctr[8 + i] = u8(low >> (8 * (7 - i)));
}

// Decrypted bytes of an installed NCA, at any place in a section.
Result ReadPlain(NcmContentStorage& cs, const NcmContentId& id, const u8 key[16], const nca::FsHeader& fh, u64 offset, u8* out, size_t size,
                 std::vector<u8>& work) {
    const u64 from = offset & ~u64(15);
    const size_t n = size_t(offset - from) + size;
    work.resize(n);
    R_TRY(ncmContentStorageReadContentIdFile(&cs, work.data(), n, &id, s64(from)));
    u8 ctr[16];
    CtrAt(fh, from, ctr);
    Aes128CtrContext ctx;
    aes128CtrContextCreate(&ctx, key, ctr);
    aes128CtrCrypt(&ctx, work.data(), work.data(), n);
    std::memcpy(out, work.data() + (offset - from), size);
    R_SUCCEED();
}

// Writes a placeholder from its start to its end, in order: hashed as it goes (its SHA-256 is
// the new content id) and encrypted with the section it is in.
class Writer {
public:
    Writer(NcmContentStorage& cs, const NcmPlaceHolderId& ph) : m_cs(cs), m_ph(ph) {
        sha256ContextCreate(&m_sha);
        m_buf.reserve(kChunk);
    }
    // the bytes from here on are encrypted with this section (null: as they are)
    void Encrypt(const nca::FsHeader* fh, const u8* key) {
        m_fh = fh;
        if (fh) {
            u8 ctr[16];
            CtrAt(*fh, m_at & ~u64(15), ctr);
            aes128CtrContextCreate(&m_ctx, key, ctr);
            // a start inside a block: the stream is moved on to it
            u8 skip[16] = {};
            if (m_at & 15) aes128CtrCrypt(&m_ctx, skip, skip, size_t(m_at & 15));
        }
    }
    Result Write(const u8* data, size_t size) {
        while (size) {
            const size_t n = std::min(size, kChunk - m_buf.size());
            const size_t at = m_buf.size();
            m_buf.insert(m_buf.end(), data, data + n);
            if (m_fh) aes128CtrCrypt(&m_ctx, m_buf.data() + at, m_buf.data() + at, n);
            data += n;
            size -= n;
            m_at += n;
            if (m_buf.size() == kChunk) R_TRY(Flush());
        }
        R_SUCCEED();
    }
    Result Zeros(u64 size) {
        static const u8 zeros[0x1000] = {};
        while (size) {
            const size_t n = size_t(std::min<u64>(size, sizeof(zeros)));
            R_TRY(Write(zeros, n));
            size -= n;
        }
        R_SUCCEED();
    }
    Result Flush() {
        if (m_buf.empty()) R_SUCCEED();
        sha256ContextUpdate(&m_sha, m_buf.data(), m_buf.size());
        R_TRY(ncmContentStorageWritePlaceHolder(&m_cs, &m_ph, s64(m_written), m_buf.data(), m_buf.size()));
        m_written += m_buf.size();
        m_buf.clear();
        R_SUCCEED();
    }
    u64 At() const { return m_at; }
    void Hash(u8 out[32]) { sha256ContextGetHash(&m_sha, out); }

private:
    static constexpr size_t kChunk = 4 * 1024 * 1024;
    NcmContentStorage& m_cs;
    NcmPlaceHolderId m_ph;
    Sha256Context m_sha{};
    std::vector<u8> m_buf;
    u64 m_at = 0, m_written = 0;
    const nca::FsHeader* m_fh = nullptr;
    Aes128CtrContext m_ctx{};
};

// Copies [from, to) of the old NCA as it is (same places, so the same encryption).
Result CopyRaw(NcmContentStorage& cs, const NcmContentId& id, Writer& w, u64 from, u64 to, std::vector<u8>& buf) {
    w.Encrypt(nullptr, nullptr);
    buf.resize(4 * 1024 * 1024);
    while (from < to) {
        const size_t n = size_t(std::min<u64>(buf.size(), to - from));
        R_TRY(ncmContentStorageReadContentIdFile(&cs, buf.data(), n, &id, s64(from)));
        R_TRY(w.Write(buf.data(), n));
        from += n;
    }
    R_SUCCEED();
}

// A placeholder registered as a content id other than the one it was made for: the folder of the
// new id is made first, by a placeholder of nothing made for it.
Result RegisterAs(NcmContentStorage& cs, const NcmContentId& id, const NcmPlaceHolderId& ph) {
    NcmPlaceHolderId tmp{};
    if (R_SUCCEEDED(ncmContentStorageGeneratePlaceHolderId(&cs, &tmp)) && R_SUCCEEDED(ncmContentStorageCreatePlaceHolder(&cs, &id, &tmp, 0))) {
        ncmContentStorageDeletePlaceHolder(&cs, &tmp);
    }
    return sphaira::ncm::Register(&cs, &id, &ph);
}

// ---- Merge and compress (25D): a game with its update installed becomes one Application ----

// The contents of a key of the database.
bool Contents(Storage& s, const NcmContentMetaKey& key, std::vector<NcmContentInfo>& out) {
    out.resize(64);
    s32 n = 0;
    if (R_FAILED(ncmContentMetaDatabaseListContentInfo(&s.db, &n, out.data(), s32(out.size()), &key, 0))) return false;
    out.resize(size_t(std::max(n, 0)));
    return true;
}

const NcmContentInfo* OfType(const std::vector<NcmContentInfo>& infos, u8 type) {
    for (const auto& i : infos) {
        if (i.content_type == type && i.id_offset == 0) return &i;
    }
    return nullptr;
}

// A base game and its update, in the same storage.
struct Pair {
    NcmContentMetaKey base{}, patch{};
    std::vector<NcmContentInfo> base_infos, patch_infos;
    NcmContentId base_program{}, patch_program{}, base_meta{}, patch_meta{};
    u64 before = 0; // what the two programs take now
};

bool FindPair(Storage& s, u64 app, Pair& p) {
    const auto bases = KeysOf(&s.db, NcmContentMetaType_Application, app, app, app);
    if (bases.empty()) {
        log_write("[NXDock] %016llX: no game in this storage\n", (unsigned long long)app);
        return false;
    }
    p.base = bases[0];
    const u64 patch_id = app | 0x800;
    const auto patches = KeysOf(&s.db, NcmContentMetaType_Patch, app, patch_id, patch_id);
    if (patches.empty()) {
        log_write("[NXDock] %016llX: no update in this storage\n", (unsigned long long)app);
        return false;
    }
    p.patch = patches[0];
    for (const auto& k : patches) {
        if (k.version > p.patch.version) p.patch = k;
    }
    if (!Contents(s, p.base, p.base_infos) || !Contents(s, p.patch, p.patch_infos)) return false;
    // a game of several programs is not merged: only the first is, and the others (patches that
    // read their own base) would be left without it
    for (const auto* list : {&p.base_infos, &p.patch_infos}) {
        for (const auto& i : *list) {
            if (i.content_type == NcmContentType_Program && i.id_offset != 0) {
                log_write("[NXDock] %016llX: a game of several programs is not merged\n", (unsigned long long)app);
                return false;
            }
        }
    }
    const auto* bp = OfType(p.base_infos, NcmContentType_Program);
    const auto* up = OfType(p.patch_infos, NcmContentType_Program);
    const auto* bm = OfType(p.base_infos, NcmContentType_Meta);
    const auto* um = OfType(p.patch_infos, NcmContentType_Meta);
    // the CNMTs are needed to make the new one
    if (!bp || !up || !bm || !um) {
        log_write("[NXDock] %016llX: contents not listed (program %d/%d, CNMT %d/%d of the game/update)\n", (unsigned long long)app, bp != nullptr,
                  up != nullptr, bm != nullptr, um != nullptr);
        return false;
    }
    p.base_program = bp->content_id;
    p.patch_program = up->content_id;
    p.base_meta = bm->content_id;
    p.patch_meta = um->content_id;
    s64 a = 0, b = 0;
    if (R_FAILED(ncmContentStorageGetSizeFromContentId(&s.cs, &a, &p.base_program)) ||
        R_FAILED(ncmContentStorageGetSizeFromContentId(&s.cs, &b, &p.patch_program))) {
        return false;
    }
    p.before = u64(a) + u64(b);
    return true;
}

// The RomFS section of a program NCA (IVFC, RomFS), whatever else it is (sparse, a patch).
int RomfsIndex(const nca::Header& h) {
    for (int i = 0; i < NCA_SECTION_TOTAL; i++) {
        const auto* fsh = reinterpret_cast<const u8*>(&h.fs_header[i]);
        if (h.fs_table[i].IsValid() && fsh[2] == 0 && fsh[3] == 3) return i;
    }
    return -1;
}

// What the merger reads: an NCA of the storage, in pieces the storage takes.
patchview::RawRead NcaReader(NcmContentStorage& cs, const NcmContentId& id) {
    return [&cs, id](u64 o, u8* b, size_t n) {
        while (n) {
            const size_t k = std::min<size_t>(n, 4 * 1024 * 1024);
            if (R_FAILED(ncmContentStorageReadContentIdFile(&cs, b, k, &id, s64(o)))) return false;
            o += k;
            b += k;
            n -= k;
        }
        return true;
    };
}

void CtrCrypt(const u8 key[16], const u8 ctr[16], u8* data, size_t n) {
    Aes128CtrContext ctx;
    aes128CtrContextCreate(&ctx, key, ctr);
    aes128CtrCrypt(&ctx, data, data, n);
}

patchview::NcaSection SectionOf(NcmContentStorage& cs, const NcmContentId& id, const nca::Header& h, int i, const u8 key[16]) {
    patchview::NcaSection s;
    s.raw = NcaReader(cs, id);
    s.ctr = CtrCrypt;
    std::memcpy(s.key.data(), key, 16);
    std::memcpy(s.fsh.data(), &h.fs_header[i], 0x200);
    s.start = h.fs_table[i].GetOffset();
    s.size = h.fs_table[i].GetOffsetEnd() - s.start;
    return s;
}

// The recipe of a merged game (25E): where the pieces of its base game lie in its RomFS, kept
// next to the list of compressed games, one file per game.
std::string RecipePath(u64 app) {
    char path[128];
    std::snprintf(path, sizeof(path), "sdmc:/switch/NXDock/recipes/%016llX.bin", (unsigned long long)app);
    return path;
}

// Every game NXDock merged keeps this mark, with or without a recipe: one whose RomFS Nintendo had
// compressed has no recipe (its update read the stored bytes of the base, which the merged game
// does not keep), and is still a merged game (its Undo brings its update back too).
std::string MergedPath(u64 app) { return RecipePath(app).substr(0, RecipePath(app).size() - 4) + ".merged"; }

bool Exists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

// A merged game a newer update can be merged over: it has its recipe (25E).
bool HasRecipe(u64 app) { return Exists(RecipePath(app)); }

std::string SparePath(u64 app) { return RecipePath(app).substr(0, RecipePath(app).size() - 4) + ".spare"; }

// Bytes of the base game of a merged game asked of the library (25E): the app on the computer reads
// them from the base it holds, decrypted as the update reads them (/install/base/). Every block is
// checked afterwards with the hashes of the new update. At most this many for one merge.
constexpr u64 kMaxFetched = 128ull << 20;

bool FetchBase(u64 app, u64 at, u64 size, std::vector<u8>& out) {
    const Config cfg = Config::Load();
    if (!cfg.Linked()) return false;
    out.clear();
    out.reserve(size_t(size));
    constexpr u64 kPiece = 16ull << 20;
    for (u64 done = 0; done < size;) {
        const u64 n = std::min(kPiece, size - done);
        char url[64];
        std::snprintf(url, sizeof(url), "/install/base/%016llX/%llu/%llu", (unsigned long long)app, (unsigned long long)(at + done),
                      (unsigned long long)n);
        const size_t before = out.size();
        const auto r = http::Receive(cfg.url + url, cfg.key, n, [&](const char* d, size_t k) {
            if (out.size() - before + k > n) return false;
            out.insert(out.end(), reinterpret_cast<const u8*>(d), reinterpret_cast<const u8*>(d) + k);
            return true;
        });
        if (r.status != 200 || out.size() - before != n) {
            log_write("[NXDock] merge: the library answered %ld for the base\n", r.status);
            return false;
        }
        done += n;
    }
    return true;
}

bool LoadRecipe(u64 app, recipe::Recipe& r) {
    FILE* f = std::fopen(RecipePath(app).c_str(), "rb");
    if (!f) return false;
    std::vector<u8> b;
    u8 chunk[4096];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0 && b.size() < 64 * 1024 * 1024) b.insert(b.end(), chunk, chunk + n);
    std::fclose(f);
    return recipe::Read(b, r);
}

bool SaveRecipe(u64 app, const recipe::Recipe& r) {
    mkdir("sdmc:/switch/NXDock/recipes", 0777);
    const std::string path = RecipePath(app), tmp = path + ".tmp";
    const auto b = recipe::Write(r);
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    if (std::fclose(f) != 0 || !ok) {
        std::remove(tmp.c_str());
        return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// The base game and its update opened for the merge. `why` set when they cannot be. A game NXDock
// merged before (its recipe says the version installed) is the base of a newer update: the base it
// was made from is made again from its RomFS by the recipe (25E).
struct Opened {
    nca::Header base_h{}, patch_h{};
    keys::KeyEntry base_key{}, patch_key{};
    bool from_merged = false;
    std::unique_ptr<patchview::Section> merged_section;
    patchview::Romfs merged_romfs;
    std::unique_ptr<recipe::RebuiltBase> rebuilt;
    recipe::Recipe recipe;
    // the spare kept beside it (what its recipe has not of the base), when it was little
    FILE* spare_file = nullptr;
    recipe::Spare spare;
    bool has_spare = false;
    // what neither has, asked of the library before the merge starts: by place in the base
    std::vector<std::pair<u64, std::vector<u8>>> fetched;
    u64 missing = 0; // bytes of the base the new update reads that neither has
    Opened() = default;
    Opened(const Opened&) = delete;
    Opened& operator=(const Opened&) = delete;
    ~Opened() {
        if (spare_file) std::fclose(spare_file);
    }
};

bool OpenMerger(Storage& s, const keys::Keys& k, const Pair& p, Opened& o, mergegame::Merger& m, GameEstimate::Why& why) {
    u8 raw[0xC00];
    why = GameEstimate::Why::Read;
    if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &p.base_program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, o.base_h))) {
        return false;
    }
    if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &p.patch_program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, o.patch_h))) {
        return false;
    }
    why = GameEstimate::Why::Key;
    nca::Header c1 = o.base_h, c2 = o.patch_h;
    if (R_FAILED(nca::GetDecryptedTitleKey(c1, k, o.base_key)) || R_FAILED(nca::GetDecryptedTitleKey(c2, k, o.patch_key))) return false;
    why = GameEstimate::Why::Read;
    const int bi = RomfsIndex(o.base_h), ui = RomfsIndex(o.patch_h);
    if (bi < 0 || ui < 0) return false;
    // merged before: the base made again from it
    if (LoadRecipe(p.base.id, o.recipe) && o.recipe.version == p.base.version) {
        std::string error;
        const auto sec = SectionOf(s.cs, p.base_program, o.base_h, bi, o.base_key.key);
        o.merged_section = patchview::OpenBase(sec, error);
        if (!o.merged_section || !o.merged_romfs.Open(o.merged_section.get(), sec.fsh.data(), Sha256, error)) {
            log_write("[NXDock] merge: the merged game: %s\n", error.c_str());
            return false;
        }
        patchview::Romfs* romfs = &o.merged_romfs;
        // what the recipe has not: from the spare when one was kept (the library, for the rest, later)
        o.spare_file = std::fopen(SparePath(p.base.id).c_str(), "rb");
        if (o.spare_file) {
            FILE* f = o.spare_file;
            fseeko(f, 0, SEEK_END);
            const u64 size = u64(ftello(f));
            o.has_spare = o.spare.Open([f](u64 at, u8* b, size_t n) { return fseeko(f, off_t(at), SEEK_SET) == 0 && std::fread(b, 1, n, f) == n; }, size);
            if (!o.has_spare) log_write("[NXDock] merge: the spare of the merged game is damaged\n");
        }
        recipe::Spare* spare = o.has_spare ? &o.spare : nullptr;
        auto* fetched = &o.fetched;
        o.rebuilt = std::make_unique<recipe::RebuiltBase>(o.recipe, [romfs](u64 at, u8* b, size_t n) { return romfs->Read(at, b, n); },
                                                          [spare, fetched](u64 at, u8* b, size_t n) {
                                                              if (spare && spare->Read(at, b, n)) return true;
                                                              for (const auto& [from, bytes] : *fetched) {
                                                                  if (at >= from && at + n <= from + bytes.size()) {
                                                                      std::memcpy(b, bytes.data() + (at - from), n);
                                                                      return true;
                                                                  }
                                                              }
                                                              return false;
                                                          });
        o.from_merged = true;
    }
    mergegame::Input in;
    if (o.from_merged) in.base_section = o.rebuilt.get();
    else in.base = SectionOf(s.cs, p.base_program, o.base_h, bi, o.base_key.key);
    in.update = SectionOf(s.cs, p.patch_program, o.patch_h, ui, o.patch_key.key);
    in.update_index = ui;
    in.update_header.assign(reinterpret_cast<const u8*>(&o.patch_h), reinterpret_cast<const u8*>(&o.patch_h) + 0xC00);
    in.update_raw = NcaReader(s.cs, p.patch_program);
    in.hash = Sha256;
    const u8* header_key = k.header_key;
    in.encrypt_header = [header_key](u8* h, size_t n) {
        std::vector<u8> plain(h, h + n);
        sphaira::crypto::cryptoAes128Xts(plain.data(), h, header_key, 0, 0x200, n, true);
    };
    in.level = kLevel;
    in.min_gain = kMinGain;
    in.threads = kThreads;
    std::string error;
    if (!m.Open(std::move(in), error)) {
        log_write("[NXDock] merge: %s\n", error.c_str());
        return false;
    }
    if (o.from_merged) {
        std::vector<patchview::IndirectEntry> entries;
        u64 end = 0;
        if (!m.Indirect(entries, end)) return false;
        o.missing = 0;
        std::vector<u8> probe;
        for (const auto& gap : recipe::Missing(o.recipe, entries, end)) {
            probe.resize(size_t(std::min<u64>(gap.second, 1 << 20)));
            bool there = o.has_spare;
            for (u64 at = 0; there && at < gap.second; at += probe.size()) {
                const size_t n = size_t(std::min<u64>(probe.size(), gap.second - at));
                there = o.spare.Read(gap.first + at, probe.data(), n);
            }
            if (there) continue;
            // asked of the library (the app on the computer reads them from the base it holds)
            std::vector<u8> bytes;
            if (o.missing + gap.second <= kMaxFetched && FetchBase(p.base.id, gap.first, gap.second, bytes)) {
                o.fetched.push_back({gap.first, std::move(bytes)});
            } else {
                log_write("[NXDock] merge: %llu bytes of the base at 0x%llx are not in the library\n", (unsigned long long)gap.second,
                          (unsigned long long)gap.first);
                o.fetched.clear();
                o.missing = UINT64_MAX;
                break;
            }
            o.missing += gap.second;
        }
        if (o.missing == UINT64_MAX) {
            // not in the merged game, nor kept beside it, nor in the library: nothing is changed
            why = GameEstimate::Why::Update;
            return false;
        }
        if (o.missing) log_write("[NXDock] merge: %llu bytes of the base asked of the library\n", (unsigned long long)o.missing);
    }
    why = GameEstimate::Why::None;
    return true;
}

// A content as it was registered, read back from the card: its SHA-256 must be its id. The hashes
// of the new data were made from what was handed to the card; this sees what the card kept, before
// the only good copy (the original) goes.
bool ReadBack(NcmContentStorage& cs, const NcmContentId& id, const std::atomic<bool>& stop) {
    s64 size = 0;
    if (R_FAILED(ncmContentStorageGetSizeFromContentId(&cs, &size, &id)) || size <= 0) return false;
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    std::vector<u8> buf(4 * 1024 * 1024);
    for (s64 at = 0; at < size;) {
        if (stop) return false;
        const size_t n = size_t(std::min<s64>(s64(buf.size()), size - at));
        if (R_FAILED(ncmContentStorageReadContentIdFile(&cs, buf.data(), n, &id, at))) return false;
        sha256ContextUpdate(&ctx, buf.data(), n);
        at += s64(n);
    }
    u8 hash[32];
    sha256ContextGetHash(&ctx, hash);
    return !std::memcmp(hash, &id, sizeof(id));
}

// The decrypted .cnmt of a meta NCA of the storage, with its header (to make another from it).
bool ReadCnmt(Storage& s, const keys::Keys& k, const NcmContentId& id, nca::Header& h, keys::KeyEntry& key, mergemeta::Cnmt& out) {
    s64 size = 0;
    if (R_FAILED(ncmContentStorageGetSizeFromContentId(&s.cs, &size, &id)) || size <= 0xC00 || size > 16 * 1024 * 1024) return false;
    std::vector<u8> nca_bytes(static_cast<size_t>(size));
    if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, nca_bytes.data(), nca_bytes.size(), &id, 0)) || R_FAILED(nca::DecryptHeader(nca_bytes.data(), k, h))) {
        return false;
    }
    nca::Header copy = h;
    if (R_FAILED(nca::GetDecryptedTitleKey(copy, k, key))) return false;
    const u64 st = h.fs_table[0].GetOffset(), en = h.fs_table[0].GetOffsetEnd();
    if (en > nca_bytes.size() || st >= en) return false;
    std::vector<u8> section(nca_bytes.begin() + std::ptrdiff_t(st), nca_bytes.begin() + std::ptrdiff_t(en));
    u8 ctr[16];
    CtrAt(h.fs_header[0], st, ctr);
    CtrCrypt(key.key, ctr, section.data(), section.size());
    std::vector<u8> cnmt;
    return mergemeta::CnmtFromSection(section, reinterpret_cast<const u8*>(&h.fs_header[0]), cnmt) && mergemeta::ParseCnmt(cnmt.data(), cnmt.size(), out);
}

// The whole RomFS of the game read by the file system of the console, as the game reads it: every
// block checked by the console against its hashes, and decompressed by it. False when a read
// fails; `could` false when the console would not open it for NXDock (then nothing is known).
bool CheckWithSystem(u64 app, const std::atomic<bool>& stop, const std::function<void(u64, u64)>& progress, bool& could) {
    FsStorage st{};
    could = R_SUCCEEDED(fsOpenDataStorageByProgramId(&st, app));
    if (!could) return true;
    s64 size = 0;
    bool ok = R_SUCCEEDED(fsStorageGetSize(&st, &size));
    std::vector<u8> buf(4 * 1024 * 1024);
    for (s64 o = 0; ok && o < size && !stop; o += s64(buf.size())) {
        const size_t n = size_t(std::min<s64>(s64(buf.size()), size - o));
        ok = R_SUCCEEDED(fsStorageRead(&st, o, buf.data(), n));
        progress(u64(o) + n, u64(size));
    }
    fsStorageClose(&st);
    return ok && !stop;
}

// A placeholder written whole from memory and registered under the SHA-256 of its bytes.
Result PutContent(NcmContentStorage& cs, const std::vector<u8>& bytes, NcmContentId& id, NcmPlaceHolderId& ph) {
    u8 hash[32];
    sha256CalculateHash(hash, bytes.data(), bytes.size());
    std::memcpy(&id, hash, sizeof(id));
    R_TRY(ncmContentStorageGeneratePlaceHolderId(&cs, &ph));
    R_TRY(ncmContentStorageCreatePlaceHolder(&cs, &id, &ph, s64(bytes.size())));
    if (const Result rc = ncmContentStorageWritePlaceHolder(&cs, &ph, 0, bytes.data(), bytes.size()); R_FAILED(rc)) {
        ncmContentStorageDeletePlaceHolder(&cs, &ph);
        return rc;
    }
    R_SUCCEED();
}

// The merge of one game (25D), in this order, so that a failure or a cut before the end leaves the
// game as it was (the base and its update installed and registered):
//  1. the merged program NCA measured, then written into a placeholder and checked against what
//     was measured; its CNMT made from those of the base and the update;
//  2. both registered under new ids, next to the old contents;
//  3. the database of ncm and the record of ns changed to the one Application, the console told the
//     version it has (no update asked for);
//  4. the whole RomFS read by the console's own file system: a failure puts the database and the
//     record back as they were and removes the new contents;
//  5. only then the contents of the base and the update not used any more are deleted.
void MergeOne(Storage& s, u64 app, const keys::Keys& k, const Pair& p, const std::atomic<bool>& stop,
              const std::function<void(int, u64, u64)>& progress, std::string& error, u64& saved, NcmContentId& new_program) {
    const More& m = TrM();
    const auto failed = [&](Result rc = 0) { error = stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d, rc); };
    Opened o;
    mergegame::Merger merger;
    GameEstimate::Why why{};
    if (!OpenMerger(s, k, p, o, merger, why)) {
        // a newer update over a merged game that needs its base game, which the library has not
        error = why == GameEstimate::Why::Update                  ? Packed(m.cz_base_t, m.cz_base_d)
                : why == GameEstimate::Why::Key && !k.from_file ? Packed(m.cz_keys_t, m.cz_keys_d)
                                                                  : Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        return;
    }
    std::string what;
    // 1. measured, then written
    const u64 virt = merger.RomfsSize();
    if (!merger.Measure([&](u64 d) { progress(1, d, virt); return !stop; }, what)) {
        log_write("[NXDock] merge %016llX: %s\n", (unsigned long long)app, what.c_str());
        return failed();
    }
    const u64 new_size = merger.NcaSize();
    if (new_size >= p.before) {
        error = Packed(m.cz_low_t, m.cz_low_d);
        return;
    }
    Result rc = 0;
    NcmPlaceHolderId ph{};
    if (R_FAILED(rc = ncmContentStorageGeneratePlaceHolderId(&s.cs, &ph)) ||
        R_FAILED(rc = ncmContentStorageCreatePlaceHolder(&s.cs, &p.patch_program, &ph, s64(new_size)))) {
        error = Packed(m.cz_room_t, m.cz_room_d, rc);
        return;
    }
    bool keep_ph = false;
    const auto drop_ph = [&] {
        if (!keep_ph) ncmContentStorageDeletePlaceHolder(&s.cs, &ph);
    };
    const std::string spare_tmp = SparePath(app) + ".tmp";
    bool spare_written = false;
    {
        Writer w(s.cs, ph);
        Result wrc = 0;
        const bool ok = merger.Write([&](const u8* d, size_t n) { return R_SUCCEEDED(wrc = w.Write(d, n)); },
                                     [&](u64 d) { progress(2, d, virt); return !stop; }, what);
        if (!ok || R_FAILED(wrc = w.Flush()) || w.At() != new_size) {
            log_write("[NXDock] merge %016llX: %s\n", (unsigned long long)app, what.c_str());
            drop_ph();
            return failed(wrc);
        }
        u8 hash[32];
        w.Hash(hash);
        std::memcpy(&new_program, hash, sizeof(new_program));
        // what the recipe will not have of the base, kept beside the merged game when it is
        // little (up to a tenth of it): a newer update then needs nothing from the library (25E)
        std::remove(spare_tmp.c_str());
        {
            std::vector<patchview::IndirectEntry> entries;
            u64 end = 0;
            if (!merger.RomfsCompressed() && merger.Indirect(entries, end)) {
                const auto rc = recipe::FromIndirect(entries, end, merger.DataOffset(), merger.DataSize(), merger.BaseSize(), p.patch.version);
                const auto left = recipe::Leftover(rc);
                u64 left_bytes = 0;
                for (const auto& l : left) left_bytes += l.second;
                const recipe::ReadFn from_base = [&merger](u64 at, u8* b, size_t n) { return merger.ReadBase(at, b, n); };
                const u64 estimate = recipe::EstimateSpare(left, from_base);
                if (estimate != UINT64_MAX && estimate <= new_size / 10) {
                    mkdir("sdmc:/switch/NXDock/recipes", 0777);
                    FILE* f = std::fopen(spare_tmp.c_str(), "wb");
                    u64 written = 0;
                    spare_written = f && recipe::WriteSpare(left, from_base, [f](const u8* d, size_t n) { return std::fwrite(d, 1, n, f) == n; },
                                                            [&](u64 d) { progress(3, d, left_bytes); return !stop; }, written);
                    if (f && std::fclose(f) != 0) spare_written = false;
                    if (!spare_written) std::remove(spare_tmp.c_str());
                    log_write("[NXDock] merge %016llX: spare of %llu bytes for %llu left of the base: %s\n", (unsigned long long)app,
                              (unsigned long long)written, (unsigned long long)left_bytes, spare_written ? "kept" : "not kept");
                } else {
                    log_write("[NXDock] merge %016llX: %llu bytes left of the base, too many to keep: a newer update may need the library\n",
                              (unsigned long long)app, (unsigned long long)left_bytes);
                }
            }
        }
        if (stop) {
            std::remove(spare_tmp.c_str());
            drop_ph();
            return failed();
        }
        // its CNMT: the base's, with the version and the contents of the update
        progress(3, 0, 1);
        nca::Header bmh, umh;
        keys::KeyEntry bmk, umk;
        mergemeta::Cnmt bc, uc, mc;
        if (!ReadCnmt(s, k, p.base_meta, bmh, bmk, bc) || !ReadCnmt(s, k, p.patch_meta, umh, umk, uc)) {
            drop_ph();
            return failed();
        }
        mergemeta::Content prog;
        for (const auto& c : uc.contents) {
            if (c.type == mergemeta::kContentProgram && c.id_offset == 0) prog = c;
        }
        std::memcpy(prog.hash.data(), hash, 32);
        std::memcpy(prog.id.data(), &new_program, 16);
        prog.size = new_size;
        std::vector<u8> mheader, msection;
        if (!mergemeta::MergedCnmt(bc, uc, prog, mc)) {
            drop_ph();
            return failed();
        }
        // no more of the system than the record of the update installed asks: an installer may
        // have lowered it so this console starts the game
        {
            std::vector<u8> rec(0x1000);
            u64 rec_size = 0;
            if (R_SUCCEEDED(ncmContentMetaDatabaseGet(&s.db, &p.patch, &rec_size, rec.data(), rec.size()))) {
                mergemeta::KeepRecordedSystem(mc, mergemeta::RecordedSystemVersion(rec.data(), size_t(std::min<u64>(rec_size, rec.size()))));
            }
        }
        if (!mergemeta::MetaNca(reinterpret_cast<const u8*>(&bmh), mc, Sha256, mheader, msection)) {
            drop_ph();
            return failed();
        }
        // the meta NCA: its section encrypted at 0xC00 with the key of the base's, its header with
        // the header key
        {
            u8 ctr[16];
            nca::FsHeader fh;
            std::memcpy(&fh, mheader.data() + 0x400, sizeof(fh));
            CtrAt(fh, 0xC00, ctr);
            CtrCrypt(bmk.key, ctr, msection.data(), msection.size());
        }
        std::vector<u8> meta_nca(0xC00);
        sphaira::crypto::cryptoAes128Xts(mheader.data(), meta_nca.data(), k.header_key, 0, 0x200, 0xC00, true);
        meta_nca.insert(meta_nca.end(), msection.begin(), msection.end());
        NcmContentId new_meta{};
        NcmPlaceHolderId mph{};
        if (R_FAILED(rc = PutContent(s.cs, meta_nca, new_meta, mph))) {
            drop_ph();
            return failed(rc);
        }

        // 2. both registered, next to the old contents
        if (R_FAILED(rc = RegisterAs(s.cs, new_program, ph))) {
            ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
            drop_ph();
            return failed(rc);
        }
        keep_ph = true;
        if (R_FAILED(rc = RegisterAs(s.cs, new_meta, mph))) {
            ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
            sphaira::ncm::Delete(&s.cs, &new_program);
            return failed(rc);
        }
        const auto remove_new = [&] {
            sphaira::ncm::Delete(&s.cs, &new_program);
            sphaira::ncm::Delete(&s.cs, &new_meta);
        };

        // 3. the database and the record: the one Application of the version of the update. What
        // was there is kept, to be put back.
        std::vector<u8> old_base(0x10000), old_patch(0x10000);
        u64 base_size = 0, patch_size = 0;
        if (R_FAILED(rc = ncmContentMetaDatabaseGet(&s.db, &p.base, &base_size, old_base.data(), old_base.size())) ||
            R_FAILED(rc = ncmContentMetaDatabaseGet(&s.db, &p.patch, &patch_size, old_patch.data(), old_patch.size()))) {
            remove_new();
            return failed(rc);
        }
        old_base.resize(size_t(base_size));
        old_patch.resize(size_t(patch_size));
        s64 meta_size = s64(meta_nca.size());
        std::array<u8, 16> meta_id;
        std::memcpy(meta_id.data(), &new_meta, 16);
        const std::vector<u8> record = mergemeta::DbRecord(mc, meta_id, u64(meta_size));
        NcmContentMetaKey key = p.base;
        key.version = mc.version;
        key.type = NcmContentMetaType_Application;
        key.install_type = NcmContentInstallType_Full;
        if (R_FAILED(rc = ns::Initialize())) {
            remove_new();
            return failed(rc);
        }
        std::vector<ncm::ContentStorageRecord> old_records;
        if (R_FAILED(rc = ns::GetApplicationRecords(app, old_records))) {
            ns::Exit();
            remove_new();
            return failed(rc);
        }
        const auto put_back = [&] {
            ncmContentMetaDatabaseRemove(&s.db, &key);
            ncmContentMetaDatabaseSet(&s.db, &p.base, old_base.data(), old_base.size());
            ncmContentMetaDatabaseSet(&s.db, &p.patch, old_patch.data(), old_patch.size());
            ncmContentMetaDatabaseCommit(&s.db);
            ns::DeleteApplicationRecord(app);
            if (!old_records.empty()) {
                ns::PushApplicationRecord(app, old_records.data(), u32(old_records.size()));
                ns::SetLowestLaunchVersion(app, old_records);
            }
            ns::InvalidateApplicationControlCache(app);
            remove_new();
        };
        std::vector<ncm::ContentStorageRecord> records;
        for (const auto& r : old_records) {
            const bool ours = r.storage_id == s.id && r.key.id == p.base.id && (r.key.type == p.base.type || r.key.type == p.patch.type);
            const bool patch = r.storage_id == s.id && r.key.id == p.patch.id && r.key.type == p.patch.type;
            if (!ours && !patch) records.push_back(r);
        }
        ncm::ContentStorageRecord merged{};
        merged.key = key;
        merged.storage_id = u8(s.id);
        records.insert(records.begin(), merged);
        bool db_ok = R_SUCCEEDED(rc = ncmContentMetaDatabaseRemove(&s.db, &p.patch));
        if (db_ok && key.version != p.base.version) db_ok = R_SUCCEEDED(rc = ncmContentMetaDatabaseRemove(&s.db, &p.base));
        if (db_ok) db_ok = R_SUCCEEDED(rc = ncmContentMetaDatabaseSet(&s.db, &key, record.data(), record.size()));
        if (db_ok) db_ok = R_SUCCEEDED(rc = ncmContentMetaDatabaseCommit(&s.db));
        if (db_ok) db_ok = R_SUCCEEDED(rc = ns::DeleteApplicationRecord(app));
        if (db_ok) db_ok = R_SUCCEEDED(rc = ns::PushApplicationRecord(app, records.data(), u32(records.size())));
        if (!db_ok) {
            put_back();
            ns::Exit();
            return failed(rc);
        }
        ns::SetLowestLaunchVersion(app, records);
        ns::InvalidateApplicationControlCache(app);

        // 4. read by the console as the game reads it
        bool could = false;
        if (!CheckWithSystem(app, stop, [&](u64 d, u64 t) { progress(3, d, t); }, could)) {
            log_write("[NXDock] merge %016llX: the console could not read it, put back\n", (unsigned long long)app);
            put_back();
            ns::Exit();
            return failed();
        }
        if (!could) {
            // the console would not open it for NXDock: what the card kept is read back whole
            log_write("[NXDock] merge %016llX: not read by the console (no access), read back from the card\n", (unsigned long long)app);
            if (!ReadBack(s.cs, new_program, stop)) {
                log_write("[NXDock] merge %016llX: the new copy did not read back right, put back\n", (unsigned long long)app);
                put_back();
                ns::Exit();
                return failed();
            }
        }
        ns::Exit();

        // 5. the old contents not used any more
        const auto used = [&](const NcmContentId& id) {
            if (!std::memcmp(&id, &new_program, sizeof(id)) || !std::memcmp(&id, &new_meta, sizeof(id))) return true;
            for (const auto& c : mc.contents) {
                if (!std::memcmp(c.id.data(), &id, sizeof(id))) return true;
            }
            return false;
        };
        for (const auto* list : {&p.base_infos, &p.patch_infos}) {
            for (const auto& i : *list) {
                if (!used(i.content_id)) sphaira::ncm::Delete(&s.cs, &i.content_id);
            }
        }
    }
    // the recipe, for the next update (25E): the places of the base the update read, found in the
    // RomFS of the merged game. Without it, a newer update needs the base from the library.
    {
        std::vector<patchview::IndirectEntry> entries;
        u64 end = 0;
        if (!merger.RomfsCompressed() && merger.Indirect(entries, end)) {
            const auto rc = recipe::FromIndirect(entries, end, merger.DataOffset(), merger.DataSize(), merger.BaseSize(), p.patch.version);
            if (!SaveRecipe(app, rc)) log_write("[NXDock] merge %016llX: the recipe was not saved\n", (unsigned long long)app);
        } else {
            std::remove(RecipePath(app).c_str());
        }
        // the spare of the version merged before goes; the new one takes its place
        if (o.spare_file) {
            std::fclose(o.spare_file);
            o.spare_file = nullptr;
            o.has_spare = false;
        }
        std::remove(SparePath(app).c_str());
        if (spare_written && std::rename(spare_tmp.c_str(), SparePath(app).c_str()) != 0) std::remove(spare_tmp.c_str());
        // merged, with a recipe or not
        mkdir("sdmc:/switch/NXDock/recipes", 0777);
        if (FILE* f = std::fopen(MergedPath(app).c_str(), "wb")) std::fclose(f);
    }
    saved = p.before - new_size;
    log_write("[NXDock] merged %016llX v%u: %llu bytes saved\n", (unsigned long long)app, p.patch.version, (unsigned long long)saved);
}

// Compresses the RomFS of one content in place (the program of a game, or the data of a DLC): the
// NCA written again with the LZ4 layer, its CNMT made again, both registered and the record of the
// title changed; only then do the old ones go. `error` empty when it went well; `saved` what it
// saved, `out` the new content.
void CompressNca(Storage& s, const keys::Keys& k, GameContents& g, const std::atomic<bool>& stop,
                 const std::function<void(int, u64, u64)>& progress, std::string& error, u64& saved, NcmContentId& out) {
    const More& m = TrM();
    Result rc = 0;
    // 1. the program NCA and its RomFS
    u8 raw[0xC00];
    nca::Header h;
    if (R_FAILED(rc = ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(rc = nca::DecryptHeader(raw, k, h))) {
        error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
        return;
    }
    bool already = false, sparse = false;
    const int r = RomfsSection(h, already, sparse);
    if (r < 0) {
        error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        return;
    }
    nca::Header copy = h;
    keys::KeyEntry key;
    if (R_FAILED(rc = nca::GetDecryptedTitleKey(copy, k, key))) {
        error = k.from_file ? Packed(m.cz_failed_t, m.cz_failed_d, rc) : Packed(m.cz_keys_t, m.cz_keys_d);
        return;
    }
    const nca::FsHeader fh = h.fs_header[r];
    const u64 start = h.fs_table[r].GetOffset(), old_end = h.fs_table[r].GetOffsetEnd();
    const u64 data_at = DataLevel(fh).logical_offset;
    u64 virt = DataLevel(fh).hash_data_size;
    // the levels of a header that does not hold together are never used to place anything
    if (start > old_end || IvfcLevels(fh).empty() || data_at > old_end - start || virt > (u64(1) << 40) || DataLevel(fh).block_size < 9 ||
        DataLevel(fh).block_size > 24) {
        error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        return;
    }
    std::vector<u8> work;
    // the data level as it is stored now
    const lz4pack::ReadFn raw_read = [&](u64 o, u8* b, size_t n) {
        return !stop && R_SUCCEEDED(ReadPlain(s.cs, g.program, key.key, fh, start + data_at + o, b, n, work));
    };
    // compressed by Nintendo (with its fast level): read decompressed through its table
    std::vector<lz4pack::Entry> old;
    u64 old_virt_end = 0;
    if (already) {
        if (!ReadOldTable(s.cs, g.program, key.key, fh, start + data_at, old, old_virt_end, work) ||
            lz4pack::ShareAtLevel(old, 0) < kRepackShare) {
            error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
            return;
        }
        virt = old_virt_end;
    }
    const lz4pack::ReadFn read = already ? lz4pack::ReadFn([&](u64 o, u8* b, size_t n) {
        return lz4pack::Unpack(old, old_virt_end, raw_read, o, b, n);
    }) : raw_read;
    // 2. measured: what the compressed data level takes, and the hashes of it, from which
    // the IVFC is made again (it hashes the data level as it is stored)
    const u32 data_block = 1u << DataLevel(fh).block_size;
    lz4pack::BlockHasher hasher(data_block, Sha256);
    lz4pack::Packed sizes;
    if (!lz4pack::Pack(virt, kLevel, kMinGain, read, [&](u64, const u8* d, size_t n) { hasher.Feed(d, n); return true; },
                       [&](u64 d) { progress(1, d, virt); return !stop; }, sizes, kThreads)) {
        error = stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d);
        return;
    }
    const auto table = lz4pack::BuildTable(sizes.entries, virt);
    const u64 stored = sizes.table_offset + table.size();
    hasher.Feed(table.data(), table.size());
    const std::vector<u8> data_hashes = hasher.Finish();
    std::vector<lz4pack::IvfcLevel> lv = IvfcLevels(fh);
    if (lv.empty() || data_at > old_end - start) {
        error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        return;
    }
    lv.back().size = stored;
    std::vector<std::vector<u8>> levels;
    u8 master[32];
    if (lv.back().offset != data_at || !lz4pack::BuildIvfc(data_hashes, lv, Sha256, levels, master)) {
        error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        return;
    }
    const u64 new_end = start + ((data_at + stored + 0x3FFF) & ~u64(0x3FFF));
    if (new_end >= old_end) {
        error = Packed(m.cz_low_t, m.cz_low_d);
        return;
    }
    const s64 shift = s64(new_end) - s64(old_end);
    // 3. the new header: the sections after the RomFS move up, the RomFS says what it holds
    nca::Header nh = h;
    for (int i = 0; i < NCA_SECTION_TOTAL; i++) {
        if (!nh.fs_table[i].IsValid()) continue;
        if (i == r) {
            nh.fs_table[i].media_end_offset = u32(new_end / 0x200);
        } else if (nh.fs_table[i].GetOffset() >= old_end) {
            nh.fs_table[i].media_start_offset = u32((s64(nh.fs_table[i].GetOffset()) + shift) / 0x200);
            nh.fs_table[i].media_end_offset = u32((s64(nh.fs_table[i].GetOffsetEnd()) + shift) / 0x200);
        }
    }
    auto& nfh = nh.fs_header[r];
    auto& ivfc = nfh.hash_data.integrity_meta_info;
    for (size_t i = 0; i < lv.size(); i++) ivfc.info_level_hash.levels[i].hash_data_size = lv[i].size;
    std::memcpy(ivfc.master_hash, master, sizeof(master));
    nfh.compression_info.table_offset = sizes.table_offset;
    nfh.compression_info.table_size = table.size();
    nfh.compression_info.table_header.magic = 0x52544B42; // BKTR
    nfh.compression_info.table_header.version = 1;
    nfh.compression_info.table_header.count = u32(sizes.entries.size());
    sha256CalculateHash(nh.fs_header_hash[r].sha256, &nfh, sizeof(nfh));
    const u64 new_size = u64(s64(h.size) + shift);
    nh.size = new_size;
    u8 enc_header[0xC00];
    sphaira::crypto::cryptoAes128Xts(&nh, enc_header, k.header_key, 0, 0x200, sizeof(nh), true);

    // 4. written in order into a placeholder, encrypted and hashed
    NcmPlaceHolderId ph{};
    if (R_FAILED(rc = ncmContentStorageGeneratePlaceHolderId(&s.cs, &ph)) ||
        R_FAILED(rc = ncmContentStorageCreatePlaceHolder(&s.cs, &g.program, &ph, s64(new_size)))) {
        error = Packed(m.cz_room_t, m.cz_room_d, rc);
        return;
    }
    bool keep_ph = false;
    auto drop = [&] {
        if (!keep_ph) ncmContentStorageDeletePlaceHolder(&s.cs, &ph);
    };
    Writer w(s.cs, ph);
    std::vector<u8> buf;
    const auto fail = [&](Result why) {
        drop();
        error = stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d, why);
    };
    if (R_FAILED(rc = w.Write(enc_header, sizeof(enc_header)))) return fail(rc);
    // the sections in the order they are in the NCA, with what lies between them
    std::vector<int> order;
    for (int i = 0; i < NCA_SECTION_TOTAL; i++) {
        if (h.fs_table[i].IsValid()) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return h.fs_table[a].GetOffset() < h.fs_table[b].GetOffset(); });
    u64 old_at = sizeof(enc_header);
    for (int i : order) {
        const u64 os = h.fs_table[i].GetOffset(), oe = h.fs_table[i].GetOffsetEnd();
        if (os > old_at) {
            // between sections: as it is, the same size
            if (R_FAILED(rc = CopyRaw(s.cs, g.program, w, old_at, os, buf))) return fail(rc);
        }
        if (i == r) {
            // the hash levels made again, then the data level compressed, then the
            // table, all encrypted for their places
            std::vector<u8> head(size_t(data_at), 0);
            for (size_t l = 0; l < levels.size(); l++) std::memcpy(head.data() + lv[l].offset, levels[l].data(), levels[l].size());
            w.Encrypt(&fh, key.key);
            if (R_FAILED(rc = w.Write(head.data(), head.size()))) return fail(rc);
            lz4pack::BlockHasher check(data_block, Sha256);
            lz4pack::Packed again;
            Result wrc = 0;
            const bool ok = lz4pack::Pack(virt, kLevel, kMinGain, read,
                [&](u64, const u8* d, size_t n) { check.Feed(d, n); wrc = w.Write(d, n); return R_SUCCEEDED(wrc); },
                [&](u64 d) { progress(2, d, virt); return !stop; }, again, kThreads);
            if (!ok || again.entries.size() != sizes.entries.size() || again.table_offset != sizes.table_offset) {
                return fail(R_FAILED(wrc) ? wrc : Result(0));
            }
            if (R_FAILED(rc = w.Write(table.data(), table.size())) || R_FAILED(rc = w.Zeros(new_end - (os + data_at + stored)))) return fail(rc);
            // what was written is what the hashes were made of (a read that went wrong
            // the second time would leave a game that does not open)
            check.Feed(table.data(), table.size());
            if (check.Finish() != data_hashes) return fail(Result(0));
        } else if (os >= old_end) {
            // after the RomFS: moved up, so encrypted again for its new place
            const nca::FsHeader& sfh = h.fs_header[i];
            if (sfh.encryption_type == nca::EncryptionType_None) {
                w.Encrypt(nullptr, nullptr);
                buf.resize(4 * 1024 * 1024);
                for (u64 o = os; o < oe;) {
                    const size_t n = size_t(std::min<u64>(buf.size(), oe - o));
                    if (R_FAILED(rc = ncmContentStorageReadContentIdFile(&s.cs, buf.data(), n, &g.program, s64(o))) ||
                        R_FAILED(rc = w.Write(buf.data(), n))) {
                        return fail(rc);
                    }
                    o += n;
                }
            } else if (sfh.encryption_type == nca::EncryptionType_AesCtr) {
                w.Encrypt(&sfh, key.key);
                std::vector<u8> plain(1024 * 1024);
                for (u64 o = os; o < oe;) {
                    const size_t n = size_t(std::min<u64>(plain.size(), oe - o));
                    if (R_FAILED(rc = ReadPlain(s.cs, g.program, key.key, sfh, o, plain.data(), n, work)) || R_FAILED(rc = w.Write(plain.data(), n))) {
                        return fail(rc);
                    }
                    o += n;
                }
            } else {
                return fail(Result(0));
            }
        } else {
            // before the RomFS: in the same place, as it is
            if (R_FAILED(rc = CopyRaw(s.cs, g.program, w, os, oe, buf))) return fail(rc);
        }
        old_at = oe;
    }
    if (R_FAILED(rc = w.Flush()) || w.At() != new_size) return fail(rc);
    u8 program_hash[32];
    w.Hash(program_hash);
    NcmContentId new_program;
    std::memcpy(&new_program, program_hash, sizeof(new_program));

    // 5. the CNMT with the new program, made again (when the record lists it)
    progress(3, 0, 1);
    NcmContentId new_meta{};
    NcmPlaceHolderId mph{};
    s64 meta_size = 0;
    if (g.has_meta) {
    if (R_FAILED(rc = ncmContentStorageGetSizeFromContentId(&s.cs, &meta_size, &g.meta)) || meta_size <= 0xC00 || meta_size > 16 * 1024 * 1024) {
        return fail(rc);
    }
    std::vector<u8> mn(static_cast<size_t>(meta_size));
    if (R_FAILED(rc = ncmContentStorageReadContentIdFile(&s.cs, mn.data(), mn.size(), &g.meta, 0))) return fail(rc);
    nca::Header mh;
    if (R_FAILED(rc = nca::DecryptHeader(mn.data(), k, mh))) return fail(rc);
    nca::Header mcopy = mh;
    keys::KeyEntry mkey;
    if (R_FAILED(rc = nca::GetDecryptedTitleKey(mcopy, k, mkey))) return fail(rc);
    auto& mfh = mh.fs_header[0];
    const u64 ms = mh.fs_table[0].GetOffset(), me = mh.fs_table[0].GetOffsetEnd();
    if (mfh.hash_type != 2 || mfh.encryption_type != nca::EncryptionType_AesCtr || me > mn.size() || ms > me) return fail(Result(0));
    std::vector<u8> section(mn.begin() + ms, mn.begin() + me);
    {
        u8 ctr[16];
        CtrAt(mfh, ms, ctr);
        Aes128CtrContext ctx;
        aes128CtrContextCreate(&ctx, mkey.key, ctr);
        aes128CtrCrypt(&ctx, section.data(), section.data(), section.size());
    }
    const auto& hs = mfh.hash_data.hierarchical_sha256_data;
    CnmtLayers layers;
    layers.block_size = hs.block_size;
    layers.hash_offset = hs.hash_layer.offset;
    layers.hash_size = hs.hash_layer.size;
    layers.pfs0_offset = hs.pfs0_layer.offset;
    layers.pfs0_size = hs.pfs0_layer.size;
    u8 master[32];
    if (!PatchCnmt(section, layers, reinterpret_cast<const u8*>(&g.program), reinterpret_cast<const u8*>(&new_program), program_hash,
                   new_size, [](const u8* d, size_t n, u8 out[32]) { sha256CalculateHash(out, d, n); }, master)) {
        return fail(Result(0));
    }
    std::memcpy(mfh.hash_data.hierarchical_sha256_data.master_hash, master, 32);
    sha256CalculateHash(mh.fs_header_hash[0].sha256, &mfh, sizeof(mfh));
    {
        u8 ctr[16];
        CtrAt(mfh, ms, ctr);
        Aes128CtrContext ctx;
        aes128CtrContextCreate(&ctx, mkey.key, ctr);
        aes128CtrCrypt(&ctx, section.data(), section.data(), section.size());
    }
    std::copy(section.begin(), section.end(), mn.begin() + ms);
    sphaira::crypto::cryptoAes128Xts(&mh, mn.data(), k.header_key, 0, 0x200, sizeof(mh), true);
    u8 meta_hash[32];
    sha256CalculateHash(meta_hash, mn.data(), mn.size());
    std::memcpy(&new_meta, meta_hash, sizeof(new_meta));
    if (R_FAILED(rc = ncmContentStorageGeneratePlaceHolderId(&s.cs, &mph)) ||
        R_FAILED(rc = ncmContentStorageCreatePlaceHolder(&s.cs, &g.meta, &mph, meta_size)) ||
        R_FAILED(rc = ncmContentStorageWritePlaceHolder(&s.cs, &mph, 0, mn.data(), mn.size()))) {
        ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
        return fail(rc);
    }
    }

    // 6. both registered, then the record of the game; only then the old ones go
    if (R_FAILED(rc = RegisterAs(s.cs, new_program, ph))) {
        ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
        return fail(rc);
    }
    keep_ph = true;
    const auto undo = [&] {
        sphaira::ncm::Delete(&s.cs, &new_program);
        if (g.has_meta) {
            sphaira::ncm::Delete(&s.cs, &new_meta);
            ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
        }
    };
    if (g.has_meta && R_FAILED(rc = RegisterAs(s.cs, new_meta, mph))) {
        undo();
        error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
        return;
    }
    std::vector<u8> rec(0x10000);
    u64 rec_size = 0;
    if (R_FAILED(rc = ncmContentMetaDatabaseGet(&s.db, &g.key, &rec_size, rec.data(), rec.size())) || rec_size < 8) {
        undo();
        error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
        return;
    }
    const u16 ext = u16(rec[0] | rec[1] << 8), count = u16(rec[2] | rec[3] << 8);
    bool program_set = false;
    for (u16 c = 0; c < count; c++) {
        const size_t at = 8 + ext + size_t(c) * sizeof(NcmContentInfo);
        if (at + sizeof(NcmContentInfo) > rec_size) break;
        NcmContentInfo ci;
        std::memcpy(&ci, rec.data() + at, sizeof(ci));
        if (!std::memcmp(&ci.content_id, &g.program, sizeof(ci.content_id))) {
            ci.content_id = new_program;
            ncmU64ToContentInfoSize(new_size, &ci);
            program_set = true;
        } else if (g.has_meta && !std::memcmp(&ci.content_id, &g.meta, sizeof(ci.content_id))) {
            ci.content_id = new_meta;
            ncmU64ToContentInfoSize(u64(meta_size), &ci);
        }
        std::memcpy(rec.data() + at, &ci, sizeof(ci));
    }
    if (!program_set || R_FAILED(rc = ncmContentMetaDatabaseSet(&s.db, &g.key, rec.data(), rec_size)) ||
        R_FAILED(rc = ncmContentMetaDatabaseCommit(&s.db))) {
        undo();
        error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
        return;
    }
    // what the card kept, read back whole; otherwise the record goes back to the original
    if (!ReadBack(s.cs, new_program, stop)) {
        log_write("[NXDock] compress %016llX: the new copy did not read back right, put back\n", (unsigned long long)g.key.id);
        std::vector<u8> old(rec.begin(), rec.begin() + ptrdiff_t(rec_size));
        for (u16 c = 0; c < count; c++) {
            const size_t at = 8 + ext + size_t(c) * sizeof(NcmContentInfo);
            if (at + sizeof(NcmContentInfo) > rec_size) break;
            NcmContentInfo ci;
            std::memcpy(&ci, old.data() + at, sizeof(ci));
            if (!std::memcmp(&ci.content_id, &new_program, sizeof(ci.content_id))) {
                ci.content_id = g.program;
                ncmU64ToContentInfoSize(u64(g.program_size), &ci);
            } else if (g.has_meta && !std::memcmp(&ci.content_id, &new_meta, sizeof(ci.content_id))) {
                ci.content_id = g.meta;
                ncmU64ToContentInfoSize(u64(meta_size), &ci);
            }
            std::memcpy(old.data() + at, &ci, sizeof(ci));
        }
        ncmContentMetaDatabaseSet(&s.db, &g.key, old.data(), rec_size);
        ncmContentMetaDatabaseCommit(&s.db);
        undo();
        error = stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d);
        return;
    }
    sphaira::ncm::Delete(&s.cs, &g.program);
    if (g.has_meta) sphaira::ncm::Delete(&s.cs, &g.meta);
    saved = u64(g.program_size) - new_size;
    out = new_program;
    log_write("[NXDock] compressed %016llX: %llu bytes saved\n", (unsigned long long)g.key.id, (unsigned long long)saved);
    return;
}

// What compressing one content would save, from a sample of its RomFS (`e` gets the state, the
// share and the bytes). The program of a game, or the data of a DLC.
void EstimateNca(Storage& s, const keys::Keys& k, const GameContents& g, const std::atomic<bool>& stop, std::vector<u8>& work, GameEstimate& e) {
    u8 raw[0xC00];
    nca::Header h;
    e.state = GameEstimate::State::Unsupported;
    e.why = GameEstimate::Why::Read;
    if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, h))) return;
    bool already = false, sparse = false;
    const int r = RomfsSection(h, already, sparse);
    if (r < 0) {
        e.why = sparse ? GameEstimate::Why::Sparse : GameEstimate::Why::Read;
        return;
    }
    nca::Header copy = h;
    keys::KeyEntry key;
    if (R_FAILED(nca::GetDecryptedTitleKey(copy, k, key))) {
        e.why = GameEstimate::Why::Key;
        return;
    }
    const auto& fh = h.fs_header[r];
    const u64 base = h.fs_table[r].GetOffset() + DataLevel(fh).logical_offset;
    const u64 size = DataLevel(fh).hash_data_size;
    const lz4pack::ReadFn stored = [&](u64 o, u8* b, size_t n) {
        return !stop && R_SUCCEEDED(ReadPlain(s.cs, g.program, key.key, fh, base + o, b, n, work));
    };
    e.why = GameEstimate::Why::None;
    if (already) {
        // compressed by Nintendo: packed again when it used its fast level
        std::vector<lz4pack::Entry> old;
        u64 old_end = 0;
        if (!ReadOldTable(s.cs, g.program, key.key, fh, base, old, old_end, work)) {
            e.why = GameEstimate::Why::Read;
            return;
        }
        if (lz4pack::ShareAtLevel(old, 0) < kRepackShare) {
            e.state = GameEstimate::State::Already;
            return;
        }
        e.gain = lz4pack::EstimateRepack(old, old_end, kLevel, kSample, stored);
    } else {
        e.gain = lz4pack::EstimateGain(size, kLevel, kSample, stored);
    }
    e.saved = u64(double(size) * e.gain);
    e.state = e.gain >= kCompressWorth ? GameEstimate::State::Worth : GameEstimate::State::Low;
}

// A DLC in a storage: its key and its data content (found by its own ID).
bool FindAddOn(Storage& s, u64 id, GameContents& g) {
    const auto found = KeysOf(&s.db, NcmContentMetaType_AddOnContent, 0, id, id);
    if (found.empty()) return false;
    g.key = found[0];
    NcmContentInfo infos[16];
    s32 n = 0;
    if (R_FAILED(ncmContentMetaDatabaseListContentInfo(&s.db, &n, infos, 16, &g.key, 0))) return false;
    bool data = false, meta = false;
    for (s32 i = 0; i < n; i++) {
        if (infos[i].content_type == NcmContentType_Data && !data) {
            g.program = infos[i].content_id;
            data = true;
        }
        if (infos[i].content_type == NcmContentType_Meta) {
            g.meta = infos[i].content_id;
            meta = true;
        }
    }
    g.has_meta = meta;
    if (!data) return false;
    return R_SUCCEEDED(ncmContentStorageGetSizeFromContentId(&s.cs, &g.program_size, &g.program));
}

// The DLC of a game in a storage, by their IDs.
std::vector<u64> AddOnsOf(Storage& s, u64 app) {
    // the DLC of a game are in the block after it: (game + 0x1000) to (game + 0x1FFF)
    std::vector<u64> ids;
    for (const auto& k : KeysOf(&s.db, NcmContentMetaType_AddOnContent, app, app + 0x1000, app + 0x1FFF)) ids.push_back(k.id);
    return ids;
}

// A content NXDock compressed (a game, or a DLC), in the list of marks.
// The list of marks is read, changed and written by the screen, the compressor and the queue.
std::mutex g_marks;

void Mark(u64 id, const NcmContentId& content, u64 saved) {
    std::lock_guard lock(g_marks);
    auto marks = LoadCompressed(kCompressedList);
    std::erase_if(marks, [id](const CompressedGame& c) { return c.app_id == id; });
    marks.push_back({id, Hex(&content, sizeof(content)), saved, (long long)time(nullptr)});
    SaveCompressed(kCompressedList, marks);
}

// The DLC of a game that are worth it, compressed one after another, after the game itself (4A).
// A DLC that fails stays as it was (the game is already done): it is only written in the log.
// Gives what they saved.
u64 CompressDlcs(const keys::Keys& k, const std::vector<u64>& dlcs, const std::atomic<bool>& stop,
                 const std::function<void(int, u64, u64)>& progress) {
    u64 total = 0;
    for (const u64 id : dlcs) {
        if (stop) break;
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Storage s(sid);
            GameContents g;
            if (!s.ok || !FindAddOn(s, id, g)) continue;
            log_write("[NXDock] compressing the DLC %016llX\n", (unsigned long long)id);
            std::string error;
            u64 saved = 0;
            NcmContentId out{};
            CompressNca(s, k, g, stop, progress, error, saved, out);
            if (error.empty()) {
                Mark(id, out, saved);
                total += saved;
            } else {
                log_write("[NXDock] the DLC %016llX stays as it was\n", (unsigned long long)id);
            }
            break;
        }
    }
    return total;
}

} // namespace

Compressor::~Compressor() {
    m_stop = true;
    Join();
}

void Compressor::Join() {
    if (m_thread.joinable()) m_thread.join();
}

bool Compressor::Busy() {
    std::lock_guard lock(m_mutex);
    return m_view.running || m_view.analyzing;
}

void Compressor::Stop() { m_stop = true; }

Compressor::View Compressor::Snapshot() {
    std::lock_guard lock(m_mutex);
    return m_view;
}

std::vector<CompressedGame> Compressor::Marks() {
    std::lock_guard lock(g_marks);
    auto marks = LoadCompressed(kCompressedList);
    if (marks.empty() || R_FAILED(ncmInitialize())) return marks;
    // a mark stays while the program NXDock wrote is the one installed
    std::vector<CompressedGame> kept;
    for (const auto& m : marks) {
        bool still = false;
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Storage s(sid);
            GameContents g;
            // a game, or a DLC of one (their IDs do not end in 000)
            const bool found = s.ok && ((m.app_id & 0xFFF) ? FindAddOn(s, m.app_id, g) : FindGame(s, m.app_id, g));
            if (found && Hex(&g.program, sizeof(g.program)) == m.program) still = true;
        }
        if (still) kept.push_back(m);
    }
    ncmExit();
    if (kept.size() != marks.size()) SaveCompressed(kCompressedList, kept);
    return kept;
}

bool Compressor::StartAnalysis(std::vector<unsigned long long> apps) {
    {
        std::lock_guard lock(m_mutex);
        if (m_view.running || m_view.analyzing) return false;
        m_view.analyzing = true;
        m_view.check_done = m_view.measure_done = m_view.measure_total = 0;
        m_view.check_total = int(apps.size());
        for (auto a : apps) m_view.games.try_emplace(a);
    }
    Join();
    m_stop = false;
    m_thread = std::thread([this, apps] {
        // what each game is found to be, and why, in sdmc:/switch/NXDock/compress.log
        nxdock_log_open("sdmc:/switch/NXDock/compress.log");
        const auto note = [](unsigned long long app, const GameEstimate& e) {
            static const char* states[] = {"pending", "possible", "worth", "low", "already compressed", "compressed by NXDock", "cannot"};
            static const char* whys[] = {"", "not found", "sparse", "no key (ticket)", "could not be read", "update", "merged when compressed: undo first"};
            log_write("[NXDock] %016llX: %s%s%s, %llu MB, saves %.0f %%\n", app, states[int(e.state)], e.why != GameEstimate::Why::None ? ": " : "",
                      whys[int(e.why)], e.bytes >> 20, e.gain * 100);
        };
        const auto marks = Marks();
        keys::Keys k;
        const bool ready = R_SUCCEEDED(splCryptoInitialize()) && R_SUCCEEDED(keys::parse_keys(k, true)) && R_SUCCEEDED(ncmInitialize());
        if (!ready) log_write("[NXDock] the keys or ncm could not be opened: nothing can be checked\n");
        std::vector<u8> work;
        // 1. every game, from the header of its program alone: fast, even with hundreds
        std::vector<unsigned long long> possible;
        for (const auto app : apps) {
            if (m_stop || !ready) break;
            GameEstimate e;
            {
                std::lock_guard lock(m_mutex);
                m_view.check_done++;
                e = m_view.games[app];
            }
            if (e.state != GameEstimate::State::Pending && e.state != GameEstimate::State::Possible) continue;
            e.state = GameEstimate::State::Unsupported;
            e.why = GameEstimate::Why::NotFound;
            for (const auto& m : marks) {
                // one NXDock merged with a newer update installed over it: merged again (25E)
                if (m.app_id == app && !HasUpdate(app)) {
                    e.state = GameEstimate::State::Ours;
                    e.saved = m.saved;
                    e.why = GameEstimate::Why::None;
                }
            }
            for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                if (e.state == GameEstimate::State::Ours) break;
                Storage s(sid);
                GameContents g;
                if (!s.ok || !FindGame(s, app, g)) continue;
                e.bytes = u64(g.program_size);
                e.why = GameEstimate::Why::Read;
                u8 raw[0xC00];
                nca::Header h;
                if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, h))) break;
                bool already = false, sparse = false;
                const int r = RomfsSection(h, already, sparse);
                if (r < 0) {
                    e.why = sparse ? GameEstimate::Why::Sparse : GameEstimate::Why::Read;
                } else if (HasUpdate(app) && IsMergedGame(app) && !HasRecipe(app)) {
                    // a newer update over a game merged when Nintendo had compressed it: Undo, then
                    // merge again
                    e.state = GameEstimate::State::Unsupported;
                    e.why = GameEstimate::Why::Redo;
                    e.merge = e.merged = true;
                    e.patch = UpdateVersion(app);
                } else if (HasUpdate(app)) {
                    // merged with its update (25D): measured from a sample of the two read together
                    e.state = GameEstimate::State::Possible;
                    e.why = GameEstimate::Why::Update;
                    e.merge = true;
                    e.patch = UpdateVersion(app);
                    e.merged = IsMergedGame(app);
                    possible.push_back(app);
                } else {
                    e.state = GameEstimate::State::Possible;
                    e.why = GameEstimate::Why::None;
                    possible.push_back(app);
                }
                break;
            }
            std::lock_guard lock(m_mutex);
            m_view.games[app] = e;
            if (e.state != GameEstimate::State::Possible) note(app, e);
        }
        {
            std::lock_guard lock(m_mutex);
            m_view.measure_total = int(possible.size());
        }
        // 2. what those that can be compressed would save, from a sample of their data
        for (const auto app : possible) {
            if (m_stop) break;
            GameEstimate e;
            {
                std::lock_guard lock(m_mutex);
                e = m_view.games[app];
            }
            for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                Storage s(sid);
                GameContents g;
                if (!s.ok || !FindGame(s, app, g)) continue;
                u8 raw[0xC00];
                nca::Header h;
                if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, h))) break;
                bool already = false, sparse = false;
                const int r = RomfsSection(h, already, sparse);
                if (e.why == GameEstimate::Why::Update) {
                    // the base and its update read together, a block in kSample
                    Pair pair;
                    Opened o;
                    mergegame::Merger merger;
                    GameEstimate::Why why{};
                    if (!FindPair(s, app, pair) || !OpenMerger(s, k, pair, o, merger, why)) {
                        e.state = GameEstimate::State::Unsupported;
                        e.why = why == GameEstimate::Why::None ? GameEstimate::Why::Read : why;
                        break;
                    }
                    const double gain = lz4pack::EstimateGain(merger.RomfsSize(), kLevel, kSample, [&](u64 o2, u8* b, size_t n) {
                        return !m_stop && merger.ReadRomfs(o2, b, n);
                    });
                    const double after = double(merger.RomfsSize()) * (1.0 - gain);
                    e.bytes = pair.before;
                    e.saved = after < double(pair.before) ? u64(double(pair.before) - after) : 0;
                    e.gain = pair.before ? double(e.saved) / double(pair.before) : 0;
                    e.state = e.gain >= kCompressWorth ? GameEstimate::State::Worth : GameEstimate::State::Low;
                    e.why = GameEstimate::Why::None;
                    break;
                }
                if (r < 0) break;
                {
                    // the game alone; `bytes` stays what the program takes now
                    const u64 bytes = e.bytes;
                    EstimateNca(s, k, g, m_stop, work, e);
                    e.bytes = bytes;
                }
                break;
            }
            // its DLC (4A): measured in the same way, and those worth it go with the game
            if (e.state == GameEstimate::State::Worth) {
                e.dlcs.clear();
                e.dlc_saved = 0;
                e.dlc_total = 0;
                for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                    Storage s(sid);
                    if (!s.ok) continue;
                    for (const u64 id : AddOnsOf(s, app)) {
                        if (m_stop) break;
                        e.dlc_total++;
                        GameContents g;
                        if (!FindAddOn(s, id, g)) continue;
                        GameEstimate d;
                        EstimateNca(s, k, g, m_stop, work, d);
                        log_write("[NXDock] DLC %016llX: %llu MB, saves %.0f %%\n", (unsigned long long)id, (unsigned long long)(g.program_size >> 20),
                                  d.gain * 100);
                        if (d.state != GameEstimate::State::Worth) continue;
                        e.dlcs.push_back(id);
                        e.dlc_saved += d.saved;
                        e.bytes += u64(g.program_size);
                    }
                }
                e.saved += e.dlc_saved;
            }
            std::lock_guard lock(m_mutex);
            m_view.games[app] = e;
            m_view.measure_done++;
            note(app, e);
        }
        ncmExit();
        splCryptoExit();
        nxdock_log_close();
        std::lock_guard lock(m_mutex);
        m_view.analyzing = false;
    });
    return true;
}

std::recursive_mutex& TitlesLock() {
    static std::recursive_mutex lock;
    return lock;
}

void ForgetMerged(unsigned long long app) {
    std::remove(RecipePath(app).c_str());
    std::remove(SparePath(app).c_str());
    std::remove(MergedPath(app).c_str());
}

bool IsMergedGame(unsigned long long app) {
    // merged before the mark existed: its recipe says so
    return Exists(MergedPath(app)) || HasRecipe(app);
}

bool CanMergeAgain(unsigned long long app) { return HasRecipe(app); }

std::string MergeUpdate(unsigned long long app, const std::atomic<bool>& stop, const std::function<void(int, uint64_t, uint64_t)>& progress,
                        uint64_t& saved) {
    const More& m = TrM();
    saved = 0;
    keys::Keys k;
    Result rc = splCryptoInitialize();
    if (R_FAILED(rc)) return Packed(m.cz_failed_t, m.cz_failed_d, rc);
    rc = keys::parse_keys(k, true);
    if (R_SUCCEEDED(rc)) rc = ncmInitialize();
    if (R_FAILED(rc)) {
        splCryptoExit();
        return Packed(m.cz_failed_t, m.cz_failed_d, rc);
    }
    std::lock_guard titles(TitlesLock());
    std::string error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
    // merged when Nintendo had compressed it: a newer update cannot be merged over it (no recipe);
    // the original and the update are installed again, and merged then
    if (IsMergedGame(app) && !HasRecipe(app)) {
        ncmExit();
        splCryptoExit();
        return Packed(m.cz_redo_t, m.cz_redo_d);
    }
    for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
        Storage s(sid);
        Pair pair;
        if (!s.ok || !FindPair(s, app, pair)) continue;
        log_write("[NXDock] merging %016llX with its update v%u\n", (unsigned long long)app, pair.patch.version);
        error.clear();
        u64 kept = 0;
        NcmContentId merged{};
        MergeOne(s, app, k, pair, stop, progress, error, kept, merged);
        if (error.empty()) {
            saved = kept;
            std::lock_guard lock(g_marks);
            auto marks = LoadCompressed(kCompressedList);
            std::erase_if(marks, [app](const CompressedGame& c) { return c.app_id == app; });
            marks.push_back({app, Hex(&merged, sizeof(merged)), kept, (long long)time(nullptr)});
            SaveCompressed(kCompressedList, marks);
        }
        break;
    }
    ncmExit();
    splCryptoExit();
    return error;
}

bool Compressor::StartCompress(unsigned long long app) {
    {
        std::lock_guard lock(m_mutex);
        if (m_view.running || m_view.analyzing) return false;
        m_view.running = true;
        m_view.app = app;
        m_view.phase = 1;
        m_view.done = m_view.total = 0;
        m_view.error.clear();
        m_view.saved = 0;
    }
    // its DLC worth it, from the analysis: compressed after the game (4A)
    std::vector<u64> dlcs;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_view.games.find(app);
        if (it != m_view.games.end()) dlcs.assign(it->second.dlcs.begin(), it->second.dlcs.end());
    }
    Join();
    m_stop = false;
    m_thread = std::thread([this, app, dlcs] {
        net::Boost awake; // the console stays awake while a game is written again
        // the queue does not install nor remove while a game is written again
        std::lock_guard titles(TitlesLock());
        // what it does goes to sdmc:/switch/NXDock/compress.log, for when something goes wrong
        nxdock_log_open("sdmc:/switch/NXDock/compress.log");
        const More& m = TrM();
        std::string error;
        u64 saved = 0;
        const auto progress = [this](int phase, u64 done, u64 total) {
            std::lock_guard lock(m_mutex);
            m_view.phase = phase;
            m_view.done = done;
            m_view.total = total;
        };
        [&] {
            keys::Keys k;
            Result rc = splCryptoInitialize();
            if (R_SUCCEEDED(rc)) rc = keys::parse_keys(k, true);
            if (R_SUCCEEDED(rc)) rc = ncmInitialize();
            if (R_FAILED(rc)) {
                error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
                return;
            }
            for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                Storage s(sid);
                GameContents g;
                if (!s.ok || !FindGame(s, app, g)) continue;
                log_write("[NXDock] compressing %016llX\n", (unsigned long long)app);
                // with its update installed: merged with it into one Application (25D)
                if (HasUpdate(app)) {
                    error = MergeUpdate(app, m_stop, progress, saved);
                } else {
                    NcmContentId out{};
                    CompressNca(s, k, g, m_stop, progress, error, saved, out);
                    if (error.empty()) Mark(app, out, saved);
                }
                if (!error.empty()) return;
                // its DLC, one after another (4A): a DLC that fails stays as it was
                saved += CompressDlcs(k, dlcs, m_stop, progress);
                return;
            }
            error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        }();
        ncmExit();
        splCryptoExit();
        nxdock_log_close();
        std::lock_guard lock(m_mutex);
        m_view.running = false;
        m_view.error = error;
        m_view.saved = saved;
        m_view.finished++;
        if (error.empty()) {
            auto& e = m_view.games[app];
            e.state = GameEstimate::State::Ours;
            e.saved = saved;
        }
    });
    return true;
}

} // namespace nxdock
