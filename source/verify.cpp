// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "verify.hpp"

#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <cstring>
#include <memory>

namespace nxdock {

namespace ncm = sphaira::ncm;
namespace ns = sphaira::ns;

Result VerifyApplication(u64 app_id, VerifyProgress& progress, std::vector<VerifiedContent>& out) {
    out.clear();
    R_TRY(ns::Initialize());
    ON_SCOPE_EXIT(ns::Exit());
    std::vector<ncm::ContentStorageRecord> records;
    R_TRY(ns::GetApplicationRecords(app_id, records));

    R_TRY(ncmInitialize());
    ON_SCOPE_EXIT(ncmExit());
    NcmContentMetaDatabase db[2]{};
    NcmContentStorage cs[2]{};
    bool open[2]{};
    const NcmStorageId storages[2] = {NcmStorageId_SdCard, NcmStorageId_BuiltInUser};
    for (int i = 0; i < 2; i++) {
        open[i] = R_SUCCEEDED(ncmOpenContentMetaDatabase(&db[i], storages[i])) && R_SUCCEEDED(ncmOpenContentStorage(&cs[i], storages[i]));
    }
    ON_SCOPE_EXIT(for (int i = 0; i < 2; i++) {
        ncmContentMetaDatabaseClose(&db[i]);
        ncmContentStorageClose(&cs[i]);
    });

    // what to read: every part of every content of the game on the SD card or the internal memory
    struct Todo {
        size_t content;
        int storage;
        NcmContentInfo info;
        u64 size;
    };
    std::vector<Todo> todo;
    for (const auto& r : records) {
        const int s = r.storage_id == NcmStorageId_SdCard ? 0 : r.storage_id == NcmStorageId_BuiltInUser ? 1 : -1;
        if (s < 0 || !open[s]) {
            continue; // a game card, or nothing on a storage
        }
        bool has = false;
        if (R_FAILED(ncmContentMetaDatabaseHas(&db[s], &has, &r.key)) || !has) {
            continue; // an archived record
        }
        std::vector<NcmContentInfo> infos;
        R_TRY(ncm::GetContentInfos(&db[s], &r.key, infos));
        VerifiedContent c;
        c.key = r.key;
        c.nand = s == 1;
        out.push_back(c);
        for (const auto& info : infos) {
            if (info.content_type == NcmContentType_DeltaFragment) {
                continue;
            }
            u64 size;
            ncmContentInfoSizeToU64(&info, &size);
            todo.push_back({out.size() - 1, s, info, size});
            out.back().parts++;
            progress.total += size;
            progress.parts++;
        }
    }

    constexpr size_t kChunk = 4 * 1024 * 1024;
    auto buf = std::unique_ptr<u8[]>(new (std::nothrow) u8[kChunk]);
    R_UNLESS(buf, Result_OutOfMemory);
    for (const auto& t : todo) {
        R_UNLESS(!progress.cancel, Result_TransferCancelled);
        VerifiedContent& c = out[t.content];
        bool ok = false;
        bool has = false;
        if (R_SUCCEEDED(ncmContentStorageHas(&cs[t.storage], &has, &t.info.content_id)) && has) {
            Sha256Context sha;
            sha256ContextCreate(&sha);
            ok = true;
            for (u64 off = 0; off < t.size;) {
                R_UNLESS(!progress.cancel, Result_TransferCancelled);
                const u64 n = std::min<u64>(kChunk, t.size - off);
                if (R_FAILED(ncmContentStorageReadContentIdFile(&cs[t.storage], buf.get(), n, &t.info.content_id, s64(off)))) {
                    ok = false;
                    progress.done += t.size - off;
                    break;
                }
                sha256ContextUpdate(&sha, buf.get(), n);
                off += n;
                progress.done += n;
            }
            if (ok) {
                u8 hash[SHA256_HASH_SIZE];
                sha256ContextGetHash(&sha, hash);
                ok = !std::memcmp(hash, &t.info.content_id, sizeof(t.info.content_id));
            }
        } else {
            progress.done += t.size;
        }
        if (!ok) {
            log_write("[NXDock] check: %016lX v%u has a damaged or missing part\n", c.key.id, c.key.version);
            c.bad.push_back(t.info.content_id);
            c.bad_bytes += t.size;
        }
        progress.parts_done++;
    }
    R_SUCCEED();
}

} // namespace nxdock
