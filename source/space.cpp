// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "space.hpp"

#include "defines.hpp"
#include "errors.hpp"
#include "http.hpp"
#include "i18n_more.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"

#include <switch.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace nxdock {
namespace {

namespace ncmx = sphaira::ncm;
namespace nsx = sphaira::ns;
namespace esx = sphaira::es;

std::string Packed(const char* title, const char* todo, Result rc = 0, const std::string& what = {}) {
    ErrorText e;
    e.title = title;
    e.what = what;
    e.todo = todo;
    if (rc) e.code = ErrorCode(rc);
    return PackError(e);
}

// The application a title belongs to: an update or a DLC to its game.
u64 AppOf(u64 tid) {
    if ((tid & 0xFFF) == 0x800) return tid ^ 0x800;
    if ((tid & 0xFFF) != 0) return (tid - 0x1000) & ~0xFFFull;
    return tid;
}

// A storage opened: its contents and its list of what is installed.
struct Store {
    NcmStorageId id;
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};
    bool ok = false;
    explicit Store(NcmStorageId sid) : id(sid) {
        ok = R_SUCCEEDED(ncmOpenContentStorage(&cs, sid));
        if (ok && R_FAILED(ncmOpenContentMetaDatabase(&db, sid))) {
            ncmContentStorageClose(&cs);
            ok = false;
        }
    }
    ~Store() {
        if (ok) {
            ncmContentMetaDatabaseClose(&db);
            ncmContentStorageClose(&cs);
        }
    }
    std::vector<NcmContentMetaKey> Keys() {
        std::vector<NcmContentMetaKey> keys(64);
        s32 total = 0, written = 0;
        while (ok) {
            if (R_FAILED(ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), s32(keys.size()), NcmContentMetaType_Unknown, 0, 0,
                                                    UINT64_MAX, NcmContentInstallType_Full))) {
                return {};
            }
            if (total <= s32(keys.size())) break;
            keys.resize(size_t(total));
        }
        keys.resize(size_t(written));
        return keys;
    }
    // every content of a key, the meta itself too
    std::vector<NcmContentInfo> Contents(const NcmContentMetaKey& key) {
        std::vector<NcmContentInfo> infos;
        ncmx::GetContentInfos(&db, &key, infos);
        NcmContentId meta{};
        if (R_SUCCEEDED(ncmContentMetaDatabaseGetContentIdByType(&db, &meta, &key, NcmContentType_Meta))) {
            const bool listed = std::any_of(infos.begin(), infos.end(), [&](const NcmContentInfo& i) { return !std::memcmp(&i.content_id, &meta, sizeof(meta)); });
            if (!listed) {
                NcmContentInfo info{};
                info.content_id = meta;
                info.content_type = NcmContentType_Meta;
                s64 size = 0;
                ncmContentStorageGetSizeFromContentId(&cs, &size, &meta);
                ncmU64ToContentInfoSize(u64(size), &info);
                infos.push_back(info);
            }
        }
        return infos;
    }
};

u64 InfoSize(const NcmContentInfo& i) {
    u64 size = 0;
    ncmContentInfoSizeToU64(&i, &size);
    return size;
}

std::string IdHex(const void* p, size_t n) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = static_cast<const unsigned char*>(p)[i];
        out += hex[c >> 4];
        out += hex[c & 15];
    }
    return out;
}

std::set<u64> RecordApps() {
    std::set<u64> out;
    NsApplicationRecord records[64];
    s32 offset = 0, count = 0;
    while (R_SUCCEEDED(nsListApplicationRecord(records, 64, offset, &count)) && count > 0) {
        for (s32 i = 0; i < count; i++) out.insert(records[i].application_id);
        offset += count;
    }
    return out;
}

// ES command 3, DeleteTicket: the tickets of these rights ids go.
Result DeleteTickets(const std::vector<FsRightsId>& ids) {
    if (ids.empty()) R_SUCCEED();
    return serviceDispatch(esx::GetServiceSession(), 3, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
                           .buffers = {{ids.data(), ids.size() * sizeof(FsRightsId)}});
}

// An NSP made on the fly from pieces: the PFS0 header, then each file. Read at any offset, so
// an upload that broke goes on from where it stopped.
class NspMaker {
public:
    struct File {
        std::string name;
        u64 size = 0;
        std::function<bool(u64, void*, size_t)> read;
    };
    explicit NspMaker(std::vector<File> files) : m_files(std::move(files)) {
        std::string names;
        std::vector<u32> name_at;
        for (const auto& f : m_files) {
            name_at.push_back(u32(names.size()));
            names += f.name;
            names += '\0';
        }
        const size_t head = 16 + 24 * m_files.size();
        size_t table = names.size();
        // the data starts on a 0x20 boundary, as NSPs do
        while ((head + table) % 0x20) table++;
        names.resize(table, '\0');
        m_header.assign(head + table, 0);
        std::memcpy(m_header.data(), "PFS0", 4);
        Put32(4, u32(m_files.size()));
        Put32(8, u32(table));
        u64 off = 0;
        for (size_t i = 0; i < m_files.size(); i++) {
            Put64(16 + 24 * i, off);
            Put64(24 + 24 * i, m_files[i].size);
            Put32(32 + 24 * i, name_at[i]);
            m_starts.push_back(off);
            off += m_files[i].size;
        }
        std::memcpy(m_header.data() + head, names.data(), table);
        m_size = m_header.size() + off;
    }
    u64 Size() const { return m_size; }
    bool Read(u64 off, void* buf, size_t size) {
        auto out = static_cast<unsigned char*>(buf);
        while (size) {
            size_t n;
            if (off < m_header.size()) {
                n = std::min<size_t>(size, size_t(m_header.size() - off));
                std::memcpy(out, m_header.data() + off, n);
            } else {
                const u64 data = off - m_header.size();
                size_t i = 0;
                while (i + 1 < m_files.size() && data >= m_starts[i + 1]) i++;
                const u64 in = data - m_starts[i];
                n = size_t(std::min<u64>(size, m_files[i].size - in));
                if (!m_files[i].read(in, out, n)) return false;
            }
            out += n;
            off += n;
            size -= n;
        }
        return true;
    }

private:
    void Put32(size_t at, u32 v) {
        for (int i = 0; i < 4; i++) m_header[at + size_t(i)] = (unsigned char)(v >> (8 * i));
    }
    void Put64(size_t at, u64 v) {
        for (int i = 0; i < 8; i++) m_header[at + size_t(i)] = (unsigned char)(v >> (8 * i));
    }
    std::vector<File> m_files;
    std::vector<unsigned char> m_header;
    std::vector<u64> m_starts;
    u64 m_size = 0;
};

std::string EncodeName(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// Sends an NSP to the library in pieces of 32 MB; a piece that fails is sent again from where
// the library says it has.
std::string Upload(const std::string& url, const std::string& key, const std::string& name, NspMaker& nsp, std::atomic<bool>& cancel,
                   const std::function<void(u64, u64)>& progress) {
    const More& m = TrM();
    const std::string base = url + "/install/upload/" + EncodeName(name);
    constexpr u64 kPiece = 32ull * 1024 * 1024;
    u64 have = u64(std::max<long long>(0, http::JsonNumber(http::Get(base, key, 8000).body, "have")));
    if (have > nsp.Size()) have = 0;
    int retries = 0;
    std::vector<char> buf(1024 * 1024);
    while (true) {
        if (cancel) return Packed(m.e_cancelled_t, m.e_cancelled_d);
        const u64 n = std::min(kPiece, nsp.Size() - have);
        const bool last = have + n == nsp.Size();
        u64 sent = 0;
        bool read_ok = true;
        const auto r = http::Send("PUT", base + "?offset=" + std::to_string(have) + (last ? "&done=1" : ""), key, n,
                                  [&](char* out, size_t max) -> long {
                                      const size_t k = size_t(std::min<u64>(max, n - sent));
                                      if (k && !nsp.Read(have + sent, out, k)) {
                                          read_ok = false;
                                          return -1;
                                      }
                                      sent += k;
                                      return long(k);
                                  },
                                  [&](uint64_t d) {
                                      progress(have + d, nsp.Size());
                                      return !cancel.load();
                                  });
        if (!read_ok) return Packed(m.e_storage_t, m.e_storage_d);
        if (r.status == 200) {
            have += n;
            retries = 0;
            if (last) return {};
            continue;
        }
        if (r.status == 403) return Packed(m.d_uploads_off, m.d_uploads_off_d);
        if (r.status == 507) return Packed(m.d_library_full, m.d_library_full_d);
        // the library says how much it has; go on from there
        if (++retries > 5) return Packed(m.e_network_t, m.e_network_d, 0, r.error);
        const auto h = http::Get(base, key, 8000);
        if (h.status == 200) have = u64(std::max<long long>(0, http::JsonNumber(h.body, "have")));
        svcSleepThread(2'000'000'000ull);
    }
}

} // namespace

Space::~Space() {
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

bool Space::Busy() {
    std::lock_guard lock(m_mutex);
    return m_busy;
}

Space::View Space::Snapshot() {
    std::lock_guard lock(m_mutex);
    return m_view;
}

void Space::Progress(unsigned long long done, unsigned long long total, const std::string& current) {
    std::lock_guard lock(m_mutex);
    m_view.done = done;
    m_view.total = total;
    if (!current.empty()) m_view.current = current;
}

void Space::Run(Task task, std::function<std::string()> work) {
    {
        std::lock_guard lock(m_mutex);
        if (m_busy) return;
        m_busy = true;
        m_view.task = task;
        m_view.done = m_view.total = 0;
        m_view.current.clear();
    }
    if (m_thread.joinable()) m_thread.join();
    m_cancel = false;
    m_thread = std::thread([this, task, work] {
        const std::string error = work();
        if (task != Task::Scan) ScanNow(); // what changed, read again
        std::lock_guard lock(m_mutex);
        m_view.error = error;
        m_view.last = task;
        m_view.task = Task::None;
        m_view.finished++;
        m_busy = false;
    });
}

void Space::ScanNow() {
    std::vector<InstalledApp> apps;
    std::map<u64, size_t> by_app;
    Leftovers left;
    Storage st;
    std::set<u64> installed_ids;
    if (R_SUCCEEDED(ncmInitialize())) {
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Store s(sid);
            if (!s.ok) continue;
            const bool nand = sid == NcmStorageId_BuiltInUser;
            s64 free_b = -1, total_b = -1;
            ncmContentStorageGetFreeSpaceSize(&s.cs, &free_b);
            ncmContentStorageGetTotalSpaceSize(&s.cs, &total_b);
            (nand ? st.nand_free : st.sd_free) = free_b;
            (nand ? st.nand_total : st.sd_total) = total_b;
            std::set<std::string> referenced;
            for (const auto& key : s.Keys()) {
                if (key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch && key.type != NcmContentMetaType_AddOnContent) continue;
                installed_ids.insert(key.id);
                const u64 app = AppOf(key.id);
                if (!by_app.count(app)) {
                    by_app[app] = apps.size();
                    apps.push_back({});
                    apps.back().app_id = app;
                }
                auto& a = apps[by_app[app]];
                InstalledApp::Content c{key.id, key.version, key.type, nand, 0};
                for (const auto& info : s.Contents(key)) {
                    c.bytes += InfoSize(info);
                    referenced.insert(IdHex(&info.content_id, sizeof(info.content_id)));
                }
                (nand ? a.nand_bytes : a.sd_bytes) += c.bytes;
                a.contents.push_back(c);
            }
            // contents no title refers to
            std::vector<NcmContentId> ids(256);
            s32 written = 0, offset = 0;
            while (R_SUCCEEDED(ncmContentStorageListContentId(&s.cs, ids.data(), s32(ids.size()), &written, offset)) && written > 0) {
                for (s32 i = 0; i < written; i++) {
                    if (!referenced.count(IdHex(&ids[size_t(i)], sizeof(NcmContentId)))) {
                        s64 size = 0;
                        ncmContentStorageGetSizeFromContentId(&s.cs, &size, &ids[size_t(i)]);
                        left.contents++;
                        left.bytes += u64(std::max<s64>(0, size));
                    }
                }
                offset += written;
                if (written < s32(ids.size())) break;
            }
        }
        ncmExit();
    }
    // archived games (a record without content) and the names
    if (R_SUCCEEDED(nsInitialize())) {
        for (const u64 app : RecordApps()) {
            if (!by_app.count(app)) {
                by_app[app] = apps.size();
                InstalledApp a;
                a.app_id = app;
                a.archived = true;
                apps.push_back(a);
            }
        }
        // when each game was last played
        if (R_SUCCEEDED(pdmqryInitialize())) {
            std::vector<u64> ids;
            for (const auto& a : apps) ids.push_back(a.app_id);
            for (size_t at = 0; at < ids.size(); at += 64) {
                const s32 n = s32(std::min<size_t>(64, ids.size() - at));
                std::vector<PdmLastPlayTime> times(static_cast<size_t>(n));
                s32 total = 0;
                if (R_FAILED(pdmqryQueryLastPlayTime(true, times.data(), ids.data() + at, n, &total))) continue;
                for (s32 i = 0; i < total; i++) {
                    if (!times[size_t(i)].flag) continue;
                    for (auto& a : apps) {
                        if (a.app_id == times[size_t(i)].application_id) a.idle_minutes = times[size_t(i)].last_played_minutes;
                    }
                }
            }
            pdmqryExit();
        }
        auto control = std::make_unique<NsApplicationControlData>();
        for (auto& a : apps) {
            u64 size = 0;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, a.app_id, control.get(), sizeof(*control), &size))) {
                NacpLanguageEntry* lang = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &lang)) && lang) a.name = lang->name;
            }
            if (a.name.empty()) {
                char t[20];
                std::snprintf(t, sizeof(t), "%016llX", (unsigned long long)a.app_id);
                a.name = t;
            }
        }
        // tickets of titles that are not on the console in any way
        const std::set<u64> records = RecordApps();
        if (R_SUCCEEDED(esx::Initialize())) {
            std::vector<FsRightsId> ids;
            esx::GetCommonTickets(ids);
            for (const auto& r : ids) {
                u64 tid = 0;
                for (int i = 0; i < 8; i++) tid = tid << 8 | r.c[i];
                if (!installed_ids.count(tid) && !records.count(AppOf(tid))) left.tickets++;
            }
            esx::Exit();
        }
        nsExit();
    }
    // a game card
    bool card = false;
    FsDeviceOperator op;
    if (R_SUCCEEDED(fsOpenDeviceOperator(&op))) {
        fsDeviceOperatorIsGameCardInserted(&op, &card);
        fsDeviceOperatorClose(&op);
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp& a, const InstalledApp& b) { return a.name < b.name; });
    std::lock_guard lock(m_mutex);
    m_view.apps = std::move(apps);
    m_view.leftovers = left;
    m_view.storage = st;
    m_view.scanned = true;
    m_view.card = card;
}

bool Space::StartScan() {
    if (Busy()) return false;
    Run(Task::Scan, [this] {
        ScanNow();
        return std::string{};
    });
    return true;
}

bool Space::StartClean() {
    if (Busy()) return false;
    Run(Task::Clean, [this]() -> std::string {
        const More& m = TrM();
        Result rc = ncmInitialize();
        if (R_FAILED(rc)) return Packed(m.e_storage_t, m.e_storage_d, rc);
        std::set<u64> installed_ids;
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Store s(sid);
            if (!s.ok) continue;
            std::set<std::string> referenced;
            bool complete = true;
            for (const auto& key : s.Keys()) {
                installed_ids.insert(key.id);
                const auto infos = s.Contents(key);
                if (infos.empty()) complete = false;
                for (const auto& info : infos) referenced.insert(IdHex(&info.content_id, sizeof(info.content_id)));
            }
            // never on a list that could not be read whole
            if (!complete) continue;
            std::vector<NcmContentId> ids(256), orphans;
            s32 written = 0, offset = 0;
            while (R_SUCCEEDED(ncmContentStorageListContentId(&s.cs, ids.data(), s32(ids.size()), &written, offset)) && written > 0) {
                for (s32 i = 0; i < written; i++) {
                    if (!referenced.count(IdHex(&ids[size_t(i)], sizeof(NcmContentId)))) orphans.push_back(ids[size_t(i)]);
                }
                offset += written;
                if (written < s32(ids.size())) break;
            }
            for (const auto& id : orphans) ncmContentStorageDelete(&s.cs, &id);
            ncmContentStorageCleanupAllPlaceHolder(&s.cs);
        }
        ncmExit();
        if (R_SUCCEEDED(nsInitialize())) {
            const std::set<u64> records = RecordApps();
            nsExit();
            if (R_SUCCEEDED(esx::Initialize())) {
                std::vector<FsRightsId> ids, drop;
                esx::GetCommonTickets(ids);
                for (const auto& r : ids) {
                    u64 tid = 0;
                    for (int i = 0; i < 8; i++) tid = tid << 8 | r.c[i];
                    if (!installed_ids.count(tid) && !records.count(AppOf(tid))) drop.push_back(r);
                }
                rc = DeleteTickets(drop);
                esx::Exit();
                if (R_FAILED(rc)) return Packed(m.e_storage_t, m.e_storage_d, rc);
            }
        }
        return {};
    });
    return true;
}

bool Space::StartArchive(std::vector<unsigned long long> apps) {
    if (Busy()) return false;
    Run(Task::Archive, [this, apps]() -> std::string {
        const More& m = TrM();
        Result rc = nsInitialize();
        if (R_FAILED(rc)) return Packed(m.e_storage_t, m.e_storage_d, rc);
        u64 i = 0;
        for (const auto app : apps) {
            Progress(i++, apps.size());
            // the content goes; the record and its icon stay on the HOME menu
            rc = nsDeleteApplicationEntity(app);
            if (R_FAILED(rc)) break;
        }
        nsExit();
        return R_FAILED(rc) ? Packed(m.s_archive_failed, m.s_archive_failed_d, rc) : std::string{};
    });
    return true;
}

bool Space::StartMove(unsigned long long app, bool to_nand) {
    if (Busy()) return false;
    Run(Task::Move, [this, app, to_nand]() -> std::string {
        const More& m = TrM();
        const NcmStorageId from_id = to_nand ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser;
        const NcmStorageId to_id = to_nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
        Result rc = ncmInitialize();
        if (R_FAILED(rc)) return Packed(m.e_storage_t, m.e_storage_d, rc);
        ON_SCOPE_EXIT(ncmExit());
        Store from(from_id), to(to_id);
        if (!from.ok || !to.ok) return Packed(m.e_storage_t, m.e_storage_d);
        std::vector<NcmContentMetaKey> keys;
        for (const auto& k : from.Keys()) {
            if (AppOf(k.id) == app) keys.push_back(k);
        }
        if (keys.empty()) return {};
        // the room first
        u64 need = 0;
        for (const auto& k : keys) {
            for (const auto& i : from.Contents(k)) need += InfoSize(i);
        }
        s64 free_b = 0;
        ncmContentStorageGetFreeSpaceSize(&to.cs, &free_b);
        if (u64(free_b) < need + 64ull * 1024 * 1024) {
            return Packed(to_nand ? m.e_space_nand_t : m.e_space_t, to_nand ? m.e_space_nand_d : m.e_space_d);
        }
        // 1. every content copied and registered on the other storage; on a failure what was
        //    copied there goes again, and nothing changed
        std::vector<NcmContentId> copied;
        const auto undo = [&] {
            for (const auto& id : copied) ncmContentStorageDelete(&to.cs, &id);
            for (const auto& k : keys) ncmContentMetaDatabaseRemove(&to.db, &k);
            ncmContentMetaDatabaseCommit(&to.db);
        };
        std::vector<unsigned char> buf(4 * 1024 * 1024);
        u64 done = 0;
        for (const auto& k : keys) {
            for (const auto& info : from.Contents(k)) {
                bool has = false;
                if (R_SUCCEEDED(ncmContentStorageHas(&to.cs, &has, &info.content_id)) && has) continue;
                const u64 size = InfoSize(info);
                NcmPlaceHolderId ph{};
                rc = ncmContentStorageGeneratePlaceHolderId(&to.cs, &ph);
                if (R_SUCCEEDED(rc)) rc = ncmContentStorageCreatePlaceHolder(&to.cs, &info.content_id, &ph, s64(size));
                for (u64 off = 0; R_SUCCEEDED(rc) && off < size;) {
                    if (m_cancel) rc = Result_TransferCancelled;
                    const size_t n = size_t(std::min<u64>(buf.size(), size - off));
                    if (R_SUCCEEDED(rc)) rc = ncmContentStorageReadContentIdFile(&from.cs, buf.data(), n, &info.content_id, s64(off));
                    if (R_SUCCEEDED(rc)) rc = ncmContentStorageWritePlaceHolder(&to.cs, &ph, off, buf.data(), n);
                    off += n;
                    done += n;
                    Progress(done, need);
                }
                if (R_SUCCEEDED(rc)) rc = ncmContentStorageRegister(&to.cs, &info.content_id, &ph);
                ncmContentStorageDeletePlaceHolder(&to.cs, &ph);
                if (R_FAILED(rc)) {
                    undo();
                    return Packed(m.s_move_failed, m.s_move_failed_d, rc);
                }
                copied.push_back(info.content_id);
            }
            u64 meta_size = 0;
            std::vector<unsigned char> meta(64 * 1024);
            rc = ncmContentMetaDatabaseGet(&from.db, &k, &meta_size, meta.data(), meta.size());
            if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseSet(&to.db, &k, meta.data(), meta_size);
            if (R_FAILED(rc)) {
                undo();
                return Packed(m.s_move_failed, m.s_move_failed_d, rc);
            }
        }
        rc = ncmContentMetaDatabaseCommit(&to.db);
        if (R_FAILED(rc)) {
            undo();
            return Packed(m.s_move_failed, m.s_move_failed_d, rc);
        }
        // 2. the record of the game says the new storage
        if (R_SUCCEEDED(nsInitialize())) {
            std::vector<ncmx::ContentStorageRecord> records;
            if (R_SUCCEEDED(nsx::GetApplicationRecords(app, records))) {
                for (auto& r : records) {
                    if (r.storage_id == from_id) r.storage_id = to_id;
                }
                nsx::DeleteApplicationRecord(app);
                rc = nsx::PushApplicationRecord(app, records.data(), u32(records.size()));
            }
            nsExit();
        }
        if (R_FAILED(rc)) {
            undo();
            return Packed(m.s_move_failed, m.s_move_failed_d, rc);
        }
        // 3. the old copy goes
        for (const auto& k : keys) {
            for (const auto& info : from.Contents(k)) ncmContentStorageDelete(&from.cs, &info.content_id);
            ncmContentMetaDatabaseRemove(&from.db, &k);
        }
        ncmContentMetaDatabaseCommit(&from.db);
        return {};
    });
    return true;
}

bool Space::StartDump(const std::string& url, const std::string& key, unsigned long long content_id, unsigned version, unsigned char type,
                      bool nand, const std::string& name) {
    if (Busy()) return false;
    Run(Task::Dump, [this, url, key, content_id, version, type, nand, name]() -> std::string {
        const More& m = TrM();
        Result rc = ncmInitialize();
        if (R_FAILED(rc)) return Packed(m.e_storage_t, m.e_storage_d, rc);
        ON_SCOPE_EXIT(ncmExit());
        auto store = std::make_shared<Store>(nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
        if (!store->ok) return Packed(m.e_storage_t, m.e_storage_d);
        NcmContentMetaKey k{};
        k.id = content_id;
        k.version = version;
        k.type = type;
        k.install_type = NcmContentInstallType_Full;
        std::vector<NspMaker::File> files;
        NcmContentId meta_id{};
        ncmContentMetaDatabaseGetContentIdByType(&store->db, &meta_id, &k, NcmContentType_Meta);
        for (const auto& info : store->Contents(k)) {
            const bool meta = !std::memcmp(&info.content_id, &meta_id, sizeof(meta_id));
            const NcmContentId id = info.content_id;
            files.push_back({IdHex(&id, sizeof(id)) + (meta ? ".cnmt.nca" : ".nca"), InfoSize(info), [store, id](u64 off, void* buf, size_t n) {
                                 return R_SUCCEEDED(ncmContentStorageReadContentIdFile(&store->cs, buf, n, &id, s64(off)));
                             }});
        }
        if (files.empty()) return Packed(m.s_dump_failed, m.s_dump_failed_d);
        // the ticket and its certificate, when the game has one
        FsRightsId rights{};
        for (const auto& info : store->Contents(k)) {
            NcmRightsId nr{};
            if (info.content_type == NcmContentType_Program &&
                R_SUCCEEDED(ncmContentStorageGetRightsIdFromContentId(&store->cs, &nr, &info.content_id, FsContentAttributes_All))) {
                rights = nr.rights_id;
                break;
            }
        }
        const bool has_rights = std::any_of(std::begin(rights.c), std::end(rights.c), [](u8 c) { return c != 0; });
        if (has_rights && R_SUCCEEDED(esx::Initialize())) {
            auto tik = std::make_shared<std::vector<u8>>(), cert = std::make_shared<std::vector<u8>>();
            if (R_SUCCEEDED(esx::GetCommonTicketAndCertificate(rights, *tik, *cert))) {
                const std::string base = IdHex(&rights, sizeof(rights));
                files.push_back({base + ".tik", tik->size(), [tik](u64 off, void* b, size_t n) {
                                     std::memcpy(b, tik->data() + off, n);
                                     return true;
                                 }});
                files.push_back({base + ".cert", cert->size(), [cert](u64 off, void* b, size_t n) {
                                     std::memcpy(b, cert->data() + off, n);
                                     return true;
                                 }});
            }
            esx::Exit();
        }
        NspMaker nsp(std::move(files));
        Progress(0, nsp.Size(), name);
        return Upload(url, key, name, nsp, m_cancel, [this](u64 d, u64 t) { Progress(d, t); });
    });
    return true;
}

bool Space::StartCardDump(const std::string& url, const std::string& key, const std::string& name) {
    if (Busy()) return false;
    Run(Task::Dump, [this, url, key, name]() -> std::string {
        const More& m = TrM();
        FsDeviceOperator op;
        Result rc = fsOpenDeviceOperator(&op);
        if (R_FAILED(rc)) return Packed(m.s_no_card, m.s_no_card_d, rc);
        bool in = false;
        FsGameCardHandle handle{};
        fsDeviceOperatorIsGameCardInserted(&op, &in);
        if (in) rc = fsDeviceOperatorGetGameCardHandle(&op, &handle);
        fsDeviceOperatorClose(&op);
        if (!in || R_FAILED(rc)) return Packed(m.s_no_card, m.s_no_card_d, rc);
        auto fs = std::shared_ptr<FsFileSystem>(new FsFileSystem{}, [](FsFileSystem* f) {
            fsFsClose(f);
            delete f;
        });
        rc = fsOpenGameCardFileSystem(fs.get(), &handle, FsGameCardPartition_Secure);
        if (R_FAILED(rc)) return Packed(m.s_no_card, m.s_no_card_d, rc);
        FsDir dir;
        char root[FS_MAX_PATH] = "/";
        rc = fsFsOpenDirectory(fs.get(), root, FsDirOpenMode_ReadFiles, &dir);
        if (R_FAILED(rc)) return Packed(m.s_dump_failed, m.s_dump_failed_d, rc);
        std::vector<FsDirectoryEntry> entries(64);
        s64 n = 0;
        fsDirRead(&dir, &n, entries.size(), entries.data());
        fsDirClose(&dir);
        std::vector<NspMaker::File> files;
        for (s64 i = 0; i < n; i++) {
            const std::string fname = entries[size_t(i)].name;
            if (!fname.ends_with(".nca")) continue;
            auto file = std::shared_ptr<FsFile>(new FsFile{}, [](FsFile* f) {
                fsFileClose(f);
                delete f;
            });
            char p[FS_MAX_PATH];
            std::snprintf(p, sizeof(p), "/%s", fname.c_str());
            if (R_FAILED(fsFsOpenFile(fs.get(), p, FsOpenMode_Read, file.get()))) return Packed(m.s_dump_failed, m.s_dump_failed_d);
            files.push_back({fname, u64(entries[size_t(i)].file_size), [file, fs](u64 off, void* b, size_t k) {
                                 u64 got = 0;
                                 return R_SUCCEEDED(fsFileRead(file.get(), s64(off), b, k, FsReadOption_None, &got)) && got == k;
                             }});
        }
        if (files.empty()) return Packed(m.s_dump_failed, m.s_dump_failed_d);
        NspMaker nsp(std::move(files));
        Progress(0, nsp.Size(), name);
        return Upload(url, key, name, nsp, m_cancel, [this](u64 d, u64 t) { Progress(d, t); });
    });
    return true;
}

} // namespace nxdock
