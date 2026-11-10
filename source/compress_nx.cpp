// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "compress_nx.hpp"

#include <cstring>

#include "cnmt_patch.hpp"
#include "errors.hpp"
#include "i18n_more.hpp"
#include "log.hpp"
#include "net.hpp"
#include "romfs_lz4.hpp"
#include "yati/nx/crypto.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"

#include <switch.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>

namespace nxdock {
namespace {

namespace nca = sphaira::nca;
namespace keys = sphaira::keys;

constexpr int kLevel = 0;          // the fast LZ4 of Nintendo (level 0): what most of its games use
constexpr uint32_t kMinGain = 64;  // a block must get this much smaller to be kept compressed
constexpr uint32_t kSample = 32;   // the analysis reads one block in 32

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

// The game in a storage: its key, its program and its CNMT.
struct GameContents {
    NcmContentMetaKey key{};
    NcmContentId program{}, meta{};
    s64 program_size = 0;
};

bool FindGame(Storage& s, u64 app, GameContents& g) {
    NcmContentMetaKey keys_found[4];
    s32 total = 0, written = 0;
    if (R_FAILED(ncmContentMetaDatabaseList(&s.db, &total, &written, keys_found, 4, NcmContentMetaType_Application, app, app, app,
                                            NcmContentInstallType_Full)) ||
        written < 1) {
        return false;
    }
    g.key = keys_found[0];
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
    if (!program || !meta) return false;
    return R_SUCCEEDED(ncmContentStorageGetSizeFromContentId(&s.cs, &g.program_size, &g.program));
}

// The RomFS section an NCA can have compressed: IVFC, AES-CTR, not a patch, not sparse, not
// compressed already. -1 when none; `already` when it is compressed.
int RomfsSection(const nca::Header& h, bool& already) {
    already = false;
    for (int i = 0; i < NCA_SECTION_TOTAL; i++) {
        const auto& fh = h.fs_header[i];
        if (!h.fs_table[i].IsValid() || fh.fs_type != 0 || fh.hash_type != 3) continue;
        if (fh.compression_info.table_size) {
            already = true;
            continue;
        }
        bool sparse = false;
        for (u8 b : fh.spares_info) sparse |= b != 0;
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
    auto marks = LoadCompressed(kCompressedList);
    if (marks.empty() || R_FAILED(ncmInitialize())) return marks;
    // a mark stays while the program NXDock wrote is the one installed
    std::vector<CompressedGame> kept;
    for (const auto& m : marks) {
        bool still = false;
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Storage s(sid);
            GameContents g;
            if (s.ok && FindGame(s, m.app_id, g) && Hex(&g.program, sizeof(g.program)) == m.program) still = true;
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
        for (auto a : apps) m_view.games.try_emplace(a);
    }
    Join();
    m_stop = false;
    m_thread = std::thread([this, apps] {
        const auto marks = Marks();
        keys::Keys k;
        const bool ready = R_SUCCEEDED(splCryptoInitialize()) && R_SUCCEEDED(keys::parse_keys(k, false)) && R_SUCCEEDED(ncmInitialize());
        std::vector<u8> work, block(lz4pack::kBlock);
        for (const auto app : apps) {
            if (m_stop || !ready) break;
            {
                std::lock_guard lock(m_mutex);
                if (m_view.games[app].state != GameEstimate::State::Pending) continue;
            }
            GameEstimate e;
            e.state = GameEstimate::State::Unsupported;
            for (const auto& m : marks) {
                if (m.app_id == app) {
                    e.state = GameEstimate::State::Ours;
                    e.saved = m.saved;
                }
            }
            for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                if (e.state == GameEstimate::State::Ours) break;
                Storage s(sid);
                GameContents g;
                if (!s.ok || !FindGame(s, app, g)) continue;
                e.bytes = u64(g.program_size);
                u8 raw[0xC00];
                nca::Header h;
                if (R_FAILED(ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(nca::DecryptHeader(raw, k, h))) break;
                bool already = false;
                const int r = RomfsSection(h, already);
                if (already) {
                    e.state = GameEstimate::State::Already;
                    break;
                }
                if (r < 0) break;
                nca::Header copy = h;
                keys::KeyEntry key;
                if (R_FAILED(nca::GetDecryptedTitleKey(copy, k, key))) break;
                const auto& fh = h.fs_header[r];
                const u64 base = h.fs_table[r].GetOffset() + DataLevel(fh).logical_offset;
                const u64 size = DataLevel(fh).hash_data_size;
                e.gain = lz4pack::EstimateGain(size, kLevel, kSample, [&](u64 o, u8* b, size_t n) {
                    return R_SUCCEEDED(ReadPlain(s.cs, g.program, key.key, fh, base + o, b, n, work));
                });
                e.saved = u64(double(size) * e.gain);
                e.state = e.gain >= kCompressWorth ? GameEstimate::State::Worth : GameEstimate::State::Low;
                break;
            }
            std::lock_guard lock(m_mutex);
            m_view.games[app] = e;
        }
        ncmExit();
        splCryptoExit();
        std::lock_guard lock(m_mutex);
        m_view.analyzing = false;
    });
    return true;
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
    Join();
    m_stop = false;
    m_thread = std::thread([this, app] {
        net::Boost awake; // the console stays awake while a game is written again
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
            if (R_SUCCEEDED(rc)) rc = keys::parse_keys(k, false);
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
                // 1. the program NCA and its RomFS
                u8 raw[0xC00];
                nca::Header h;
                if (R_FAILED(rc = ncmContentStorageReadContentIdFile(&s.cs, raw, sizeof(raw), &g.program, 0)) || R_FAILED(rc = nca::DecryptHeader(raw, k, h))) {
                    error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
                    return;
                }
                bool already = false;
                const int r = RomfsSection(h, already);
                if (r < 0) {
                    error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
                    return;
                }
                nca::Header copy = h;
                keys::KeyEntry key;
                if (R_FAILED(rc = nca::GetDecryptedTitleKey(copy, k, key))) {
                    error = Packed(m.cz_failed_t, m.cz_failed_d, rc);
                    return;
                }
                const nca::FsHeader fh = h.fs_header[r];
                const u64 start = h.fs_table[r].GetOffset(), old_end = h.fs_table[r].GetOffsetEnd();
                const u64 data_at = DataLevel(fh).logical_offset, virt = DataLevel(fh).hash_data_size;
                std::vector<u8> work;
                const lz4pack::ReadFn read = [&](u64 o, u8* b, size_t n) {
                    return !m_stop && R_SUCCEEDED(ReadPlain(s.cs, g.program, key.key, fh, start + data_at + o, b, n, work));
                };
                // 2. measured: what the compressed data level takes
                lz4pack::Packed sizes;
                if (!lz4pack::Pack(virt, kLevel, kMinGain, read, [](u64, const u8*, size_t) { return true; },
                                   [&](u64 d) { progress(1, d, virt); return !m_stop; }, sizes)) {
                    error = m_stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d);
                    return;
                }
                const auto table = lz4pack::BuildTable(sizes.entries, virt);
                const u64 stored = sizes.table_offset + table.size();
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
                const u32 data_level = nfh.hash_data.integrity_meta_info.info_level_hash.max_layers - 2;
                nfh.hash_data.integrity_meta_info.info_level_hash.levels[data_level].hash_data_size = stored;
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
                    error = m_stop ? Packed(m.cz_stopped_t, m.cz_stopped_d) : Packed(m.cz_failed_t, m.cz_failed_d, why);
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
                        // the hash levels as they are, then the data level compressed, then the table
                        if (R_FAILED(rc = CopyRaw(s.cs, g.program, w, os, os + data_at, buf))) return fail(rc);
                        w.Encrypt(&fh, key.key);
                        lz4pack::Packed again;
                        Result wrc = 0;
                        const bool ok = lz4pack::Pack(virt, kLevel, kMinGain, read,
                            [&](u64, const u8* d, size_t n) { wrc = w.Write(d, n); return R_SUCCEEDED(wrc); },
                            [&](u64 d) { progress(2, d, virt); return !m_stop; }, again);
                        if (!ok || again.entries.size() != sizes.entries.size() || again.table_offset != sizes.table_offset) {
                            return fail(R_FAILED(wrc) ? wrc : Result(0));
                        }
                        if (R_FAILED(rc = w.Write(table.data(), table.size())) || R_FAILED(rc = w.Zeros(new_end - (os + data_at + stored)))) return fail(rc);
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

                // 5. the CNMT with the new program, made again
                progress(3, 0, 1);
                s64 meta_size = 0;
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
                if (mfh.hash_type != 2 || mfh.encryption_type != nca::EncryptionType_AesCtr || me > mn.size()) return fail(Result(0));
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
                NcmContentId new_meta;
                std::memcpy(&new_meta, meta_hash, sizeof(new_meta));
                NcmPlaceHolderId mph{};
                if (R_FAILED(rc = ncmContentStorageGeneratePlaceHolderId(&s.cs, &mph)) ||
                    R_FAILED(rc = ncmContentStorageCreatePlaceHolder(&s.cs, &g.meta, &mph, meta_size)) ||
                    R_FAILED(rc = ncmContentStorageWritePlaceHolder(&s.cs, &mph, 0, mn.data(), mn.size()))) {
                    ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
                    return fail(rc);
                }

                // 6. both registered, then the record of the game; only then the old ones go
                if (R_FAILED(rc = RegisterAs(s.cs, new_program, ph))) {
                    ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
                    return fail(rc);
                }
                keep_ph = true;
                const auto undo = [&] {
                    sphaira::ncm::Delete(&s.cs, &new_program);
                    sphaira::ncm::Delete(&s.cs, &new_meta);
                    ncmContentStorageDeletePlaceHolder(&s.cs, &mph);
                };
                if (R_FAILED(rc = RegisterAs(s.cs, new_meta, mph))) {
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
                    } else if (!std::memcmp(&ci.content_id, &g.meta, sizeof(ci.content_id))) {
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
                sphaira::ncm::Delete(&s.cs, &g.program);
                sphaira::ncm::Delete(&s.cs, &g.meta);
                saved = u64(g.program_size) - new_size;
                auto marks = LoadCompressed(kCompressedList);
                std::erase_if(marks, [app](const CompressedGame& c) { return c.app_id == app; });
                marks.push_back({app, Hex(&new_program, sizeof(new_program)), saved, (long long)time(nullptr)});
                SaveCompressed(kCompressedList, marks);
                log_write("[NXDock] compressed %016llX: %llu bytes saved\n", (unsigned long long)app, (unsigned long long)saved);
                return;
            }
            error = Packed(m.cz_unsupported_t, m.cz_unsupported_d);
        }();
        ncmExit();
        splCryptoExit();
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
