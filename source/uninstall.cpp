// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "uninstall.hpp"

#include "defines.hpp"
#include "log.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <algorithm>
#include <vector>

namespace nxdock {

namespace ncm = sphaira::ncm;
namespace ns = sphaira::ns;

u64 OwnProgramId() {
    u64 id = 0;
    svcGetInfo(&id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    return id;
}

bool StorageSpace(NcmStorageId storage, s64& free_bytes, s64& total_bytes) {
    free_bytes = total_bytes = -1;
    if (R_FAILED(ncmInitialize())) {
        return false;
    }
    NcmContentStorage cs{};
    bool ok = false;
    if (R_SUCCEEDED(ncmOpenContentStorage(&cs, storage))) {
        ok = R_SUCCEEDED(ncmContentStorageGetFreeSpaceSize(&cs, &free_bytes)) &&
             R_SUCCEEDED(ncmContentStorageGetTotalSpaceSize(&cs, &total_bytes));
        ncmContentStorageClose(&cs);
    }
    ncmExit();
    if (!ok) {
        free_bytes = total_bytes = -1;
    }
    return ok;
}

s64 StorageFreeSpace(NcmStorageId storage) {
    s64 free_bytes, total_bytes;
    StorageSpace(storage, free_bytes, total_bytes);
    return free_bytes;
}

Result RemoveInstalled(const NcmContentMetaKey& key, NcmStorageId storage) {
    const u64 app_id = ncm::GetAppId(key);
    if (app_id == OwnProgramId()) {
        // the game NXDock runs in: its files are open
        R_THROW(Result_GameRunning);
    }
    log_write("[NXDock] removing %016lX v%u type %u from storage %u\n", key.id, key.version, key.type, storage);

    R_TRY(ncmInitialize());
    ON_SCOPE_EXIT(ncmExit());
    NcmContentMetaDatabase db{};
    NcmContentStorage cs{};
    R_TRY(ncmOpenContentMetaDatabase(&db, storage));
    ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));
    R_TRY(ncmOpenContentStorage(&cs, storage));
    ON_SCOPE_EXIT(ncmContentStorageClose(&cs));

    bool has = false;
    R_TRY(ncmContentMetaDatabaseHas(&db, &has, &key));
    if (has) {
        R_TRY(ncm::DeleteKey(&cs, &db, &key));
    } else {
        log_write("[NXDock] not in the database any more\n");
    }

    // the record of the game without it; the rest of the game stays as it is
    R_TRY(ns::Initialize());
    ON_SCOPE_EXIT(ns::Exit());
    std::vector<ncm::ContentStorageRecord> records;
    R_TRY(ns::GetApplicationRecords(app_id, records));
    const size_t before = records.size();
    std::erase_if(records, [&](const ncm::ContentStorageRecord& r) {
        return r.key.id == key.id && r.key.version == key.version && r.key.type == key.type && r.storage_id == storage;
    });
    if (records.size() != before) {
        R_TRY(ns::DeleteApplicationRecord(app_id));
        if (!records.empty()) {
            R_TRY(ns::PushApplicationRecord(app_id, records.data(), u32(records.size())));
            ns::SetLowestLaunchVersion(app_id, records);
        }
        ns::InvalidateApplicationControlCache(app_id);
    }
    R_SUCCEED();
}

} // namespace nxdock
