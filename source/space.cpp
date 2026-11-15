// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "space.hpp"
#include "compress_nx.hpp"

#include "defines.hpp"
#include "errors.hpp"
#include "http.hpp"
#include "i18n_more.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/ns.hpp"
#include "install_queue.hpp"
#include "resume.hpp"
#include "uninstall.hpp"
#include "console_order.hpp"

#include <switch.h>
#include <sys/stat.h>

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
    // `read` (when given) says false when the list could not be read: an empty list is then not
    // "nothing installed", and nothing may be deleted from it
    std::vector<NcmContentMetaKey> Keys(bool* read = nullptr) {
        std::vector<NcmContentMetaKey> keys(64);
        s32 total = 0, written = 0;
        if (read) *read = ok;
        while (ok) {
            if (R_FAILED(ncmContentMetaDatabaseList(&db, &total, &written, keys.data(), s32(keys.size()), NcmContentMetaType_Unknown, 0, 0,
                                                    UINT64_MAX, NcmContentInstallType_Full))) {
                if (read) *read = false;
                return {};
            }
            if (total <= s32(keys.size())) break;
            keys.resize(size_t(total));
        }
        keys.resize(size_t(written));
        return keys;
    }
    // every content of a key, the meta itself too
    std::vector<NcmContentInfo> Contents(const NcmContentMetaKey& key, bool* read = nullptr) {
        std::vector<NcmContentInfo> infos;
        if (R_FAILED(ncmx::GetContentInfos(&db, &key, infos))) {
            // not read: nothing of it is told, so nothing of it is taken for a leftover
            if (read) *read = false;
            return {};
        }
        if (read) *read = true;
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
        if (have > nsp.Size()) have = 0;
        svcSleepThread(2'000'000'000ull);
    }
}

// The system partition as SYSTEM:, where es reads the personal tickets and the certificates from;
// mounted only while it is needed, and only when nothing else mounted it (an install does).
class SystemMount {
public:
    SystemMount() {
        if (fsdevGetDeviceFileSystem("SYSTEM")) return;
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenBisFileSystem(&fs, FsBisPartitionId_System, ""))) {
            m_mine = fsdevMountDevice("SYSTEM", fs) >= 0;
            if (!m_mine) fsFsClose(&fs);
        }
    }
    ~SystemMount() {
        if (m_mine) fsdevUnmountDevice("SYSTEM");
    }

private:
    bool m_mine = false;
};

bool WriteFile(const std::string& path, const std::vector<u8>& data) {
    if (data.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

// A ticket and its certificate to the SD card; es must be open. False when its .tik is not written.
bool SaveTicket(const TicketId& t) {
    FsRightsId rid{};
    std::memcpy(rid.c, t.rights, sizeof(rid.c));
    std::vector<u8> tik, cert;
    const Result rc = t.personal ? esx::GetPersonalisedTicketAndCertificate(rid, tik, cert) : esx::GetCommonTicketAndCertificate(rid, tik, cert);
    if (R_FAILED(rc) || tik.empty()) return false;
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/NXDock", 0777);
    mkdir(kTicketsDir, 0777);
    const std::string base = std::string(kTicketsDir) + "/";
    if (!WriteFile(base + TicketFileName(t.rights, ".tik"), tik)) return false;
    if (!cert.empty()) WriteFile(base + TicketFileName(t.rights, ".cert"), cert);
    return true;
}

// The tickets of titles that are on the console in no way: neither installed nor in the records.
std::vector<TicketId> TicketsWithoutGame(const std::vector<TicketId>& all, const std::set<u64>& installed, const std::set<u64>& records) {
    std::vector<TicketId> out;
    for (const auto& t : all) {
        const u64 tid = TitleOfRights(t.rights);
        if (!installed.count(tid) && !records.count(AppOf(tid))) out.push_back(t);
    }
    return out;
}

} // namespace

bool ListTickets(std::vector<TicketId>& out) {
    out.clear();
    if (R_FAILED(esx::Initialize())) return false;
    std::vector<FsRightsId> common, personal;
    const bool ok = R_SUCCEEDED(esx::GetCommonTickets(common)) && R_SUCCEEDED(esx::GetPersonalisedTickets(personal));
    esx::Exit();
    for (const auto* list : {&common, &personal}) {
        for (const auto& r : *list) {
            TicketId t;
            std::memcpy(t.rights, r.c, sizeof(t.rights));
            t.personal = list == &personal;
            out.push_back(t);
        }
    }
    return ok;
}

std::set<unsigned long long> AppRecords() {
    std::set<unsigned long long> out;
    if (R_FAILED(nsInitialize())) return out;
    for (const u64 app : RecordApps()) out.insert(app);
    nsExit();
    return out;
}

std::vector<TicketId> TicketsOf(unsigned long long app) {
    std::vector<TicketId> all, out;
    ListTickets(all);
    for (const auto& t : all) {
        if (AppOf(TitleOfRights(t.rights)) == app) out.push_back(t);
    }
    return out;
}

int SaveAndDeleteTickets(const std::vector<TicketId>& tickets, int* kept) {
    if (kept) *kept = 0;
    if (tickets.empty()) return 0;
    SystemMount system; // before es, so the personal ones and the certificates can be read
    if (R_FAILED(esx::Initialize())) {
        if (kept) *kept = int(tickets.size());
        return 0;
    }
    std::vector<FsRightsId> saved;
    for (const auto& t : tickets) {
        if (!SaveTicket(t)) {
            if (kept) (*kept)++;
            continue;
        }
        FsRightsId rid{};
        std::memcpy(rid.c, t.rights, sizeof(rid.c));
        saved.push_back(rid);
    }
    const Result rc = DeleteTickets(saved);
    esx::Exit();
    if (R_FAILED(rc)) {
        if (kept) *kept = int(tickets.size());
        return 0;
    }
    return int(saved.size());
}

Space::~Space() {
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
    if (m_name_thread.joinable()) m_name_thread.join();
}

void Space::NameLater() {
    if (m_naming.exchange(true)) return;
    if (m_name_thread.joinable()) m_name_thread.join();
    m_name_thread = std::thread([this] {
        std::vector<u64> missing;
        {
            std::lock_guard lock(m_mutex);
            for (const auto& a : m_view.apps) {
                if (!m_names.count(a.app_id)) missing.push_back(a.app_id);
            }
        }
        {
            std::lock_guard lock(m_mutex);
            m_view.names_done = 0;
            m_view.names_total = int(missing.size());
        }
        if (!missing.empty() && R_SUCCEEDED(nsInitialize())) {
            auto control = std::make_unique<NsApplicationControlData>();
            for (const u64 app : missing) {
                if (m_cancel) break;
                {
                    std::lock_guard lock(m_mutex);
                    m_view.names_done++;
                }
                std::string name;
                u64 size = 0;
                // the cache of the console first (fast), then the game itself
                for (const auto source : {NsApplicationControlSource_CacheOnly, NsApplicationControlSource_Storage}) {
                    if (R_SUCCEEDED(nsGetApplicationControlData(source, app, control.get(), sizeof(*control), &size))) {
                        NacpLanguageEntry* lang = nullptr;
                        if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &lang)) && lang && lang->name[0]) {
                            name = lang->name;
                            break;
                        }
                    }
                }
                if (name.empty()) continue;
                // in place: the order of the list stays while it is on screen
                std::lock_guard lock(m_mutex);
                m_names[app] = name;
                for (auto& a : m_view.apps) {
                    if (a.app_id == app) a.name = name;
                }
            }
            nsExit();
        }
        {
            std::lock_guard lock(m_mutex);
            m_view.names_done = m_view.names_total = 0;
        }
        m_naming = false;
    });
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
        NameLater(); // names not known yet, without holding the screen
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
    int tickets_all = -1;
    Storage st;
    std::set<u64> installed_ids;
    if (R_SUCCEEDED(ncmInitialize())) {
        // how many to read, for the bar of the screen
        unsigned long long total = 0, done = 0;
        for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
            Store s(sid);
            if (s.ok) total += s.Keys().size();
        }
        Progress(0, total);
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
                if ((++done & 15) == 0) Progress(done, total);
                if (key.type != NcmContentMetaType_Application && key.type != NcmContentMetaType_Patch && key.type != NcmContentMetaType_AddOnContent) continue;
                installed_ids.insert(key.id);
                const u64 app = AppOf(key.id);
                if (!by_app.count(app)) {
                    by_app[app] = apps.size();
                    apps.push_back({});
                    apps.back().app_id = app;
                }
                auto& a = apps[by_app[app]];
                InstalledApp::Content c{key.id, key.version, key.type, nand, 0, 0};
                if (key.type == NcmContentMetaType_AddOnContent) {
                    // the version of the game it needs: in its extended header, after the header of
                    // 8 bytes and the ID of its game
                    u8 meta[0x40] = {};
                    u64 size = 0;
                    if (R_SUCCEEDED(ncmContentMetaDatabaseGet(&s.db, &key, &size, meta, sizeof(meta))) && size >= 20) {
                        u32 v = 0;
                        std::memcpy(&v, meta + 16, sizeof(v));
                        c.required = v;
                    }
                }
                for (const auto& info : s.Contents(key)) {
                    c.bytes += InfoSize(info);
                    referenced.insert(IdHex(&info.content_id, sizeof(info.content_id)));
                }
                (nand ? a.nand_bytes : a.sd_bytes) += c.bytes;
                a.contents.push_back(c);
            }
            // placeholders of installs that did not end (24A), except those NXDock can go on with
            {
                const auto keep = ResumePlaceholders(!nand);
                std::vector<NcmPlaceHolderId> phs(512);
                s32 n = 0;
                if (R_SUCCEEDED(ncmContentStorageListPlaceHolder(&s.cs, phs.data(), s32(phs.size()), &n))) {
                    for (s32 i = 0; i < n; i++) {
                        if (keep.count(IdHex(&phs[size_t(i)], sizeof(NcmPlaceHolderId)))) continue;
                        s64 size = 0;
                        ncmContentStorageGetSizeFromPlaceHolderId(&s.cs, &size, &phs[size_t(i)]);
                        left.placeholders++;
                        left.bytes += u64(std::max<s64>(0, size));
                    }
                }
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
    // the file systems, when the content storages do not say how much room there is
    if (st.sd_free < 0 || st.sd_total <= 0) {
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&fs))) {
            char root[FS_MAX_PATH] = "/";
            s64 v = 0;
            if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, root, &v))) st.sd_free = v;
            if (R_SUCCEEDED(fsFsGetTotalSpace(&fs, root, &v))) st.sd_total = v;
            fsFsClose(&fs);
        }
    }
    if (st.nand_free < 0 || st.nand_total <= 0) {
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenBisFileSystem(&fs, FsBisPartitionId_User, ""))) {
            char root[FS_MAX_PATH] = "/";
            s64 v = 0;
            if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, root, &v))) st.nand_free = v;
            if (R_SUCCEEDED(fsFsGetTotalSpace(&fs, root, &v))) st.nand_total = v;
            fsFsClose(&fs);
        }
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
            // how long each was played and how many times it was opened (3A), read once per scan
            for (auto& a : apps) {
                PdmPlayStatistics stats{};
                if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationId(a.app_id, false, &stats))) continue;
                a.play_minutes = (long long)(stats.playtime / 60'000'000'000ull);
                a.launches = int(stats.total_launches);
            }
            pdmqryExit();
        }
        // the names known already; the others come after, on their own thread (NameLater)
        {
            std::lock_guard lock(m_mutex);
            for (auto& a : apps) {
                auto it = m_names.find(a.app_id);
                if (it != m_names.end()) a.name = it->second;
            }
        }
        for (auto& a : apps) {
            if (a.name.empty()) {
                char t[20];
                std::snprintf(t, sizeof(t), "%016llX", (unsigned long long)a.app_id);
                a.name = t;
            }
        }
        // tickets of titles that are not on the console in any way
        const std::set<u64> records = RecordApps();
        // updates and DLC whose game is neither installed nor in the records (24A)
        for (const auto& a : apps) {
            if (a.archived || records.count(a.app_id)) continue;
            const bool base = std::any_of(a.contents.begin(), a.contents.end(), [](const InstalledApp::Content& c) {
                return c.type == NcmContentMetaType_Application;
            });
            if (base) continue;
            left.addons += int(a.contents.size());
            left.bytes += a.Bytes();
        }
        // the personal ones too (6A); they are saved to the SD card before they go
        std::vector<TicketId> tickets;
        if (ListTickets(tickets)) tickets_all = int(tickets.size());
        for (const auto& t : TicketsWithoutGame(tickets, installed_ids, records)) {
            left.tickets++;
            left.personal += t.personal;
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
    m_view.removed.clear(); // read again: those gone are not there any more
    m_view.leftovers = left;
    m_view.storage = st;
    m_view.scanned = true;
    m_view.card = card;
    m_view.tickets_all = tickets_all;
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
            for (const auto& key : s.Keys(&complete)) {
                installed_ids.insert(key.id);
                bool read = false;
                const auto infos = s.Contents(key, &read);
                if (!read || infos.empty()) complete = false;
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
            // placeholders, one by one: those of an install NXDock can go on with stay (24A)
            const auto keep = ResumePlaceholders(sid == NcmStorageId_SdCard);
            std::vector<NcmPlaceHolderId> phs(512);
            s32 n = 0;
            if (R_SUCCEEDED(ncmContentStorageListPlaceHolder(&s.cs, phs.data(), s32(phs.size()), &n))) {
                for (s32 i = 0; i < n; i++) {
                    if (!keep.count(IdHex(&phs[size_t(i)], sizeof(NcmPlaceHolderId)))) ncmContentStorageDeletePlaceHolder(&s.cs, &phs[size_t(i)]);
                }
            }
        }
        // updates and DLC whose game is neither installed nor in the records (24A)
        if (R_SUCCEEDED(nsInitialize())) {
            const std::set<u64> records = RecordApps();
            nsExit();
            for (const auto sid : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                std::vector<NcmContentMetaKey> lonely;
                {
                    Store s(sid);
                    if (!s.ok) continue;
                    const auto keys = s.Keys();
                    std::set<u64> bases;
                    for (const auto sid2 : {NcmStorageId_SdCard, NcmStorageId_BuiltInUser}) {
                        Store o(sid2);
                        if (!o.ok) continue;
                        for (const auto& k : o.Keys()) {
                            if (k.type == NcmContentMetaType_Application) bases.insert(AppOf(k.id));
                        }
                    }
                    for (const auto& k : keys) {
                        if ((k.type == NcmContentMetaType_Patch || k.type == NcmContentMetaType_AddOnContent) && !bases.count(AppOf(k.id)) &&
                            !records.count(AppOf(k.id))) {
                            lonely.push_back(k);
                        }
                    }
                }
                for (const auto& k : lonely) RemoveInstalled(k, sid);
            }
        }
        ncmExit();
        if (R_SUCCEEDED(nsInitialize())) {
            const std::set<u64> records = RecordApps();
            nsExit();
            // the tickets of titles that are not on the console, common and personal (6A), each
            // saved to the SD card first; one that cannot be saved stays
            std::vector<TicketId> tickets;
            if (ListTickets(tickets)) SaveAndDeleteTickets(TicketsWithoutGame(tickets, installed_ids, records));
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

bool Space::StartRemove(unsigned long long app, std::vector<InstalledApp::Content> items,
                        std::function<std::string(unsigned long long)> before, std::function<void(unsigned long long)> after) {
    if (items.empty()) return false;
    return StartRemove({{app, std::move(items)}}, std::move(before), std::move(after));
}

bool Space::StartRemove(std::vector<std::pair<unsigned long long, std::vector<InstalledApp::Content>>> games,
                        std::function<std::string(unsigned long long)> before, std::function<void(unsigned long long)> after) {
    if (Busy() || games.empty()) return false;
    // the progress counts each content, and the saves of each game copied first as one step more
    u64 total = 0;
    for (auto& [app, items] : games) {
        // the updates and the DLC before the game: the record of the game goes with the last of it
        std::stable_sort(items.begin(), items.end(), [](const InstalledApp::Content& a, const InstalledApp::Content& b) {
            return (a.type == NcmContentMetaType_Application) < (b.type == NcmContentMetaType_Application);
        });
        const bool whole = std::any_of(items.begin(), items.end(), [](const InstalledApp::Content& c) { return c.type == NcmContentMetaType_Application; });
        total += items.size() + (whole && before ? 1 : 0);
    }
    Run(Task::Remove, [this, games, total, before, after]() -> std::string {
        const More& m = TrM();
        std::lock_guard titles(TitlesLock());
        Result rc = 0;
        u64 i = 0;
        int index = 0;
        {
            std::lock_guard lock(m_mutex);
            m_view.removed.clear();
            m_view.remove_count = int(games.size());
        }
        for (const auto& [app, items] : games) {
            const bool whole = std::any_of(items.begin(), items.end(),
                                           [](const InstalledApp::Content& c) { return c.type == NcmContentMetaType_Application; });
            {
                // which game, of how many, and whether its saves are being copied: for the screen
                std::lock_guard lock(m_mutex);
                m_view.remove_app = app;
                m_view.remove_index = ++index;
                m_view.remove_saving = whole && before;
            }
            if (whole && before) {
                // its saves first; nothing of this game goes when they could not be copied
                Progress(i, total);
                const std::string error = before(app);
                if (!error.empty()) return error;
                i++;
                std::lock_guard lock(m_mutex);
                m_view.remove_saving = false;
            }
            bool base = false;
            for (const auto& c : items) {
                Progress(i++, total);
                NcmContentMetaKey key{};
                key.id = c.id;
                key.version = c.version;
                key.type = c.type;
                key.install_type = NcmContentInstallType_Full;
                rc = RemoveInstalled(key, c.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
                if (R_FAILED(rc)) break;
                if (c.type == NcmContentMetaType_Application) base = true;
            }
            // the marks of a game NXDock merged or compressed go with it
            if (base) ForgetMerged(app);
            if (R_FAILED(rc)) break;
            // then its mods and its tickets, when asked (7A)
            if (base && after) after(app);
            // gone: out of the list now, not at the end of all
            if (base) {
                std::lock_guard lock(m_mutex);
                m_view.removed.push_back(app);
            }
        }
        Progress(total, total);
        if (R_FAILED(rc)) {
            if (rc == Result_GameRunning) return Packed(m.ic_rm_running_t, m.ic_rm_running_d, rc);
            return Packed(m.ic_rm_failed_t, m.ic_rm_failed_d, rc);
        }
        return {};
    });
    return true;
}

bool Space::StartDeleteTickets(std::vector<TicketId> tickets) {
    if (Busy() || tickets.empty()) return false;
    Run(Task::Tickets, [this, tickets]() -> std::string {
        Progress(0, tickets.size());
        int kept = 0;
        const int done = SaveAndDeleteTickets(tickets, &kept);
        std::lock_guard lock(m_mutex);
        m_view.tickets_done = done;
        m_view.tickets_kept = kept;
        return {};
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
        bool listed = true;
        for (const auto& k : from.Keys(&listed)) {
            if (AppOf(k.id) == app) keys.push_back(k);
        }
        if (!listed) return Packed(m.e_storage_t, m.e_storage_d);
        if (keys.empty()) return {};
        // the contents of each key, read once: a key whose contents cannot be read stops the move
        // before anything is copied (an empty list would register it without its contents)
        std::vector<std::vector<NcmContentInfo>> contents;
        u64 need = 0;
        for (const auto& k : keys) {
            bool read = false;
            contents.push_back(from.Contents(k, &read));
            if (!read || contents.back().empty()) return Packed(m.e_storage_t, m.e_storage_d);
            for (const auto& i : contents.back()) need += InfoSize(i);
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
        for (size_t ki = 0; ki < keys.size(); ki++) {
            const auto& k = keys[ki];
            for (const auto& info : contents[ki]) {
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
        // 2. the record of the game says the new storage; if it cannot be changed, the old copy
        //    stays and the new one goes (the record still says where the old one is)
        rc = nsInitialize();
        if (R_SUCCEEDED(rc)) {
            std::vector<ncmx::ContentStorageRecord> records;
            rc = nsx::GetApplicationRecords(app, records);
            if (R_SUCCEEDED(rc)) {
                const auto before = records;
                for (auto& r : records) {
                    if (r.storage_id == from_id) r.storage_id = to_id;
                }
                nsx::DeleteApplicationRecord(app);
                rc = nsx::PushApplicationRecord(app, records.data(), u32(records.size()));
                // not pushed: the record as it was, so the game stays on the HOME menu
                if (R_FAILED(rc)) nsx::PushApplicationRecord(app, const_cast<ncmx::ContentStorageRecord*>(before.data()), u32(before.size()));
            }
            nsExit();
        }
        if (R_FAILED(rc)) {
            undo();
            return Packed(m.s_move_failed, m.s_move_failed_d, rc);
        }
        // 3. the old copy goes
        for (size_t ki = 0; ki < keys.size(); ki++) {
            for (const auto& info : contents[ki]) ncmContentStorageDelete(&from.cs, &info.content_id);
            ncmContentMetaDatabaseRemove(&from.db, &keys[ki]);
        }
        ncmContentMetaDatabaseCommit(&from.db);
        return {};
    });
    return true;
}

namespace {

// The files of an NSP of an installed content: its NCAs, and its ticket and certificate when it has
// a common one.
bool InstalledFiles(const std::shared_ptr<Store>& store, const NcmContentMetaKey& k, std::vector<NspMaker::File>& files) {
    NcmContentId meta_id{};
    ncmContentMetaDatabaseGetContentIdByType(&store->db, &meta_id, &k, NcmContentType_Meta);
    for (const auto& info : store->Contents(k)) {
        const bool meta = !std::memcmp(&info.content_id, &meta_id, sizeof(meta_id));
        const NcmContentId id = info.content_id;
        files.push_back({IdHex(&id, sizeof(id)) + (meta ? ".cnmt.nca" : ".nca"), InfoSize(info), [store, id](u64 off, void* buf, size_t n) {
                             return R_SUCCEEDED(ncmContentStorageReadContentIdFile(&store->cs, buf, n, &id, s64(off)));
                         }});
    }
    if (files.empty()) return false;
    FsRightsId rights{};
    for (const auto& info : store->Contents(k)) {
        NcmRightsId nr{};
        if ((info.content_type == NcmContentType_Program || info.content_type == NcmContentType_Data) &&
            R_SUCCEEDED(ncmContentStorageGetRightsIdFromContentId(&store->cs, &nr, &info.content_id, FsContentAttributes_All))) {
            rights = nr.rights_id;
            if (std::any_of(std::begin(rights.c), std::end(rights.c), [](u8 c) { return c != 0; })) break;
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
    return true;
}

} // namespace

std::shared_ptr<InstalledNsp> OpenInstalledNsp(unsigned long long id, unsigned version, unsigned char type, bool nand) {
    auto store = std::make_shared<Store>(nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard);
    if (!store->ok) return nullptr;
    NcmContentMetaKey k{};
    k.id = id;
    k.version = version;
    k.type = type;
    k.install_type = NcmContentInstallType_Full;
    std::vector<NspMaker::File> files;
    if (!InstalledFiles(store, k, files)) return nullptr;
    auto nsp = std::make_shared<NspMaker>(std::move(files));
    auto out = std::make_shared<InstalledNsp>();
    out->size = nsp->Size();
    out->read = [nsp](unsigned long long off, void* buf, size_t n) { return nsp->Read(off, buf, n); };
    return out;
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
        // its NCAs, and the ticket and its certificate when the game has one
        std::vector<NspMaker::File> files;
        if (!InstalledFiles(store, k, files)) return Packed(m.s_dump_failed, m.s_dump_failed_d);
        NspMaker nsp(std::move(files));
        Progress(0, nsp.Size(), name);
        return Upload(url, key, name, nsp, m_cancel, [this](u64 d, u64 t) { Progress(d, t); });
    });
    return true;
}

namespace {

// The NCAs of the secure partition of the game card, each with a reader (the dumps, and installing
// from the card, 14A). 1: no card (or it cannot be opened); 2: it could not be read.
int CardFiles(std::vector<NspMaker::File>& files, Result& rc) {
    files.clear();
    FsDeviceOperator op;
    rc = fsOpenDeviceOperator(&op);
    if (R_FAILED(rc)) return 1;
    bool in = false;
    FsGameCardHandle handle{};
    fsDeviceOperatorIsGameCardInserted(&op, &in);
    if (in) rc = fsDeviceOperatorGetGameCardHandle(&op, &handle);
    fsDeviceOperatorClose(&op);
    if (!in || R_FAILED(rc)) return 1;
    auto fs = std::shared_ptr<FsFileSystem>(new FsFileSystem{}, [](FsFileSystem* f) {
        fsFsClose(f);
        delete f;
    });
    rc = fsOpenGameCardFileSystem(fs.get(), &handle, FsGameCardPartition_Secure);
    if (R_FAILED(rc)) return 1;
    FsDir dir;
    char root[FS_MAX_PATH] = "/";
    rc = fsFsOpenDirectory(fs.get(), root, FsDirOpenMode_ReadFiles, &dir);
    if (R_FAILED(rc)) return 2;
    std::vector<FsDirectoryEntry> entries(64);
    s64 n = 0;
    fsDirRead(&dir, &n, entries.size(), entries.data());
    fsDirClose(&dir);
    for (s64 i = 0; i < n; i++) {
        const std::string fname = entries[size_t(i)].name;
        if (!fname.ends_with(".nca")) continue;
        auto file = std::shared_ptr<FsFile>(new FsFile{}, [](FsFile* f) {
            fsFileClose(f);
            delete f;
        });
        char p[FS_MAX_PATH];
        std::snprintf(p, sizeof(p), "/%s", fname.c_str());
        if (R_FAILED(fsFsOpenFile(fs.get(), p, FsOpenMode_Read, file.get()))) return 2;
        files.push_back({fname, u64(entries[size_t(i)].file_size), [file, fs](u64 off, void* b, size_t k) {
                             u64 got = 0;
                             return R_SUCCEEDED(fsFileRead(file.get(), s64(off), b, k, FsReadOption_None, &got)) && got == k;
                         }});
    }
    return files.empty() ? 2 : 0;
}

} // namespace

bool GameCardInserted() {
    bool in = false;
    FsDeviceOperator op;
    if (R_SUCCEEDED(fsOpenDeviceOperator(&op))) {
        fsDeviceOperatorIsGameCardInserted(&op, &in);
        fsDeviceOperatorClose(&op);
    }
    return in;
}

unsigned long long GameCard::Bytes() const {
    unsigned long long all = 0;
    for (const auto& c : contents) all += c.bytes;
    return all;
}

GameCard ReadGameCard() {
    GameCard card;
    if (!GameCardInserted() || R_FAILED(ncmInitialize())) return card;
    {
        Store store(NcmStorageId_GameCard);
        bool read = false;
        const auto keys = store.Keys(&read);
        for (const auto& k : keys) {
            if (k.type != NcmContentMetaType_Application && k.type != NcmContentMetaType_Patch && k.type != NcmContentMetaType_AddOnContent) continue;
            InstalledApp::Content c;
            c.id = k.id;
            c.version = k.version;
            c.type = k.type;
            for (const auto& info : store.Contents(k)) c.bytes += InfoSize(info);
            card.contents.push_back(c);
            if (k.type == NcmContentMetaType_Application && !card.app) card.app = k.id;
        }
        card.ok = read && card.app != 0;
    }
    ncmExit();
    const auto rank = [](unsigned char type) {
        return type == NcmContentMetaType_Application ? 0 : type == NcmContentMetaType_Patch ? 1 : 2;
    };
    std::stable_sort(card.contents.begin(), card.contents.end(), [&](const InstalledApp::Content& a, const InstalledApp::Content& b) {
        return rank(a.type) != rank(b.type) ? rank(a.type) < rank(b.type) : a.id < b.id;
    });
    if (card.ok && R_SUCCEEDED(nsInitialize())) {
        auto control = std::make_unique<NsApplicationControlData>();
        u64 size = 0;
        if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, card.app, control.get(), sizeof(*control), &size))) {
            NacpLanguageEntry* lang = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&control->nacp, &lang)) && lang && lang->name[0]) card.name = lang->name;
        }
        nsExit();
    }
    if (card.ok && card.name.empty()) {
        char id[20];
        std::snprintf(id, sizeof(id), "%016llX", card.app);
        card.name = id;
    }
    return card;
}

std::shared_ptr<InstalledNsp> OpenGameCardNsp() {
    std::vector<NspMaker::File> files;
    Result rc = 0;
    if (CardFiles(files, rc) != 0) return nullptr;
    auto nsp = std::make_shared<NspMaker>(std::move(files));
    auto out = std::make_shared<InstalledNsp>();
    out->size = nsp->Size();
    out->read = [nsp](unsigned long long off, void* buf, size_t n) { return nsp->Read(off, buf, n); };
    return out;
}

bool Space::StartCardDump(const std::string& url, const std::string& key, const std::string& name) {
    if (Busy()) return false;
    Run(Task::Dump, [this, url, key, name]() -> std::string {
        const More& m = TrM();
        std::vector<NspMaker::File> files;
        Result rc = 0;
        const int failed = CardFiles(files, rc);
        if (failed == 1) return Packed(m.s_no_card, m.s_no_card_d, rc);
        if (failed) return Packed(m.s_dump_failed, m.s_dump_failed_d, rc);
        NspMaker nsp(std::move(files));
        Progress(0, nsp.Size(), name);
        return Upload(url, key, name, nsp, m_cancel, [this](u64 d, u64 t) { Progress(d, t); });
    });
    return true;
}

} // namespace nxdock
