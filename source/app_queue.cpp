// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The queue: the jobs added, what the web and the library app send, updates that would stop a game, its screen.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

std::string FileLabel(const InstallJob& j) {
    return j.label.empty() ? j.game : j.game + " · " + j.label;
}

// The jobs in the order the queue screen shows them: the one installing, those waiting, then
// the finished ones, the latest first.
std::vector<InstallJob> QueueOrder(std::vector<InstallJob> jobs) {
    std::vector<InstallJob> out;
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Running) out.push_back(j);
    }
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Waiting) out.push_back(j);
    }
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it) {
        if (it->state != InstallJob::State::Running && it->state != InstallJob::State::Waiting) out.push_back(*it);
    }
    return out;
}

} // namespace

void App::EnqueuePicked() {
    const auto& s = Tr();
    const Game& g = m_games[m_detail_game];
    const Config c = m_link.CurrentConfig();
    int picked = 0;
    std::vector<InstallJob> jobs;
    // the game first, then the update, then the DLC
    for (const auto kind : {CatalogFile::Kind::Game, CatalogFile::Kind::Update, CatalogFile::Kind::Dlc}) {
        for (size_t i = 0; i < g.files.size(); i++) {
            const CatalogFile& f = g.files[i];
            if (!m_detail_pick[i] || f.kind != kind) {
                continue;
            }
            picked++;
            std::string label;
            switch (f.kind) {
                case CatalogFile::Kind::Game: label = s.base_game; break;
                case CatalogFile::Kind::Update: label = VersionText(f.version); break;
                case CatalogFile::Kind::Dlc: label = f.dlc_name.empty() ? f.name : f.dlc_name; break;
            }
            // a newer firmware than the console's is told by the check before installing (2A)
            InstallJob job = JobFor(g, f);
            job.label = label;
            job.nand = m_to_nand;
            jobs.push_back(std::move(job));
        }
    }
    if (!picked) {
        Notice(s.nothing_picked);
        return;
    }
    if (jobs.empty()) {
        return;
    }
    if (m_ui.dest == 2 && !m_dest_touched) {
        // "ask each time" (wizard): SD card or internal memory, and the internal memory asks again
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Where;
        m_dialog.jobs = std::move(jobs);
        m_dialog.focus = 1; // the SD card, the default
        return;
    }
    if (m_to_nand) {
        // never to the internal memory without asking: the question (1A), Cancel in focus
        m_sd_checked = -100;
        CheckSdSpace();
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand;
        m_dialog.jobs = std::move(jobs);
        m_to_nand = false; // the next one goes to the SD card again
        return;
    }
    AddJobs(std::move(jobs));
    m_to_nand = false;
}

InstallJob App::JobFor(const Game& g, const CatalogFile& f) {
    const auto& s = Tr();
    const Config c = m_link.CurrentConfig();
    InstallJob job;
    job.game = g.name;
    switch (f.kind) {
        case CatalogFile::Kind::Game: job.label = s.base_game; break;
        case CatalogFile::Kind::Update: job.label = VersionText(f.version); break;
        case CatalogFile::Kind::Dlc: job.label = f.dlc_name.empty() ? f.name : f.dlc_name; break;
    }
    job.version = f.version;
    job.resumable = !m_shop_active; // a journal while it installs (23A); a shop's key is not written down
    job.cover = g.cover;
    job.color_key = g.key;
    job.file_id = f.id;
    const bool whole = f.url.rfind("http://", 0) == 0 || f.url.rfind("https://", 0) == 0;
    const bool path = !f.url.empty() && f.url[0] == '/' && f.url.rfind("//", 0) != 0 && f.url.find('@') == std::string::npos;
    job.url = whole ? f.url : c.url + f.url;
    // a key goes only to the server it belongs to: a file elsewhere comes without it
    if (m_shop_active) job.key = http::SameOrigin(job.url, m_shop_url) ? m_shop_key : "";
    else job.key = (whole ? http::SameOrigin(job.url, c.url) : path) ? c.key : "";
    job.format = f.format;
    job.kind = f.kind;
    job.title_id = f.title_id;
    job.size = f.size;
    if (f.kind == CatalogFile::Kind::Dlc) {
        job.dlc_needs = f.required_system; // for a DLC, the version of its game it needs
    } else {
        job.required_system = f.required_system;
    }
    return job;
}

bool App::UpdatePending(const Game& g) const {
    if (!g.UpdateAvailable()) {
        return false;
    }
    const u32 hos = hosversionGet();
    const u32 console = (HOSVER_MAJOR(hos) << 26) | (HOSVER_MINOR(hos) << 20) | (HOSVER_MICRO(hos) << 16);
    const CatalogFile* f = g.NewestUpdate();
    return !(f->required_system && (f->required_system & 0xFFFF0000) > console); // else the console cannot run it yet
}

std::vector<InstallJob> App::PendingUpdates() {
    std::vector<InstallJob> jobs;
    for (const auto& g : m_games) {
        if (!UpdatePending(g)) {
            continue;
        }
        const CatalogFile* f = g.NewestUpdate();
        InstallJob job = JobFor(g, *f);
        job.label = VersionText(g.installed.patch) + " → " + VersionText(f->version);
        job.nand = g.installed.base_nand;
        jobs.push_back(std::move(job));
    }
    return jobs;
}

std::string App::ContentLabel(const Game& g, const NcmContentMetaKey& key) const {
    const auto& s = Tr();
    switch (key.type) {
        case NcmContentMetaType_Application: return s.base_game;
        case NcmContentMetaType_Patch: return VersionText(key.version);
        default:
            for (const auto& f : g.files) {
                if (f.kind == CatalogFile::Kind::Dlc && f.title_id == key.id) {
                    return s.dlc_prefix + (f.dlc_name.empty() ? f.name : f.dlc_name);
                }
            }
            char id[40];
            std::snprintf(id, sizeof(id), "%s%016lX", s.dlc_prefix, key.id);
            return id;
    }
}

bool App::RepairJobs(const InstallQueue::VerifyView& v, std::vector<InstallJob>& out) {
    out.clear();
    if (m_dialog.game < 0 || m_dialog.game >= int(m_games.size())) {
        return false;
    }
    return RepairJobsFor(m_games[m_dialog.game], v, out);
}

// The repairs of a check, from the files of a game of the library; false when it lacks a version.
bool App::RepairJobsFor(const Game& g, const InstallQueue::VerifyView& v, std::vector<InstallJob>& out) {
    out.clear();
    bool all = true;
    for (const auto& c : v.result) {
        if (c.bad.empty()) {
            continue;
        }
        const CatalogFile* match = nullptr;
        for (const auto& f : g.files) {
            const bool kind = (c.key.type == NcmContentMetaType_Application && f.kind == CatalogFile::Kind::Game) ||
                              (c.key.type == NcmContentMetaType_Patch && f.kind == CatalogFile::Kind::Update) ||
                              (c.key.type == NcmContentMetaType_AddOnContent && f.kind == CatalogFile::Kind::Dlc);
            if (kind && f.title_id == c.key.id && f.version == c.key.version) {
                match = &f;
            }
        }
        if (!match) {
            all = false;
            continue;
        }
        InstallJob job = JobFor(g, *match);
        job.label = ContentLabel(g, c.key) + " · " + Tr().repair_tag;
        job.file_id += ":repair";
        job.nand = c.nand;
        job.repair = c.bad;
        job.size = c.bad_bytes;
        out.push_back(std::move(job));
    }
    return all && !out.empty();
}

// The queue as the web shows it (22A): each job, and for the one installing its stage, speed and
// time left.
std::string App::QueueJson() {
    const auto jobs = m_queue->Jobs();
    const auto live = m_queue->GetLive();
    std::string out = std::string("{\"paused\":") + (m_queue->Paused() ? "true" : "false") + ",\"jobs\":[";
    bool first = true;
    for (const auto& j : jobs) {
        const char* state = j.state == InstallJob::State::Waiting   ? "waiting"
                            : j.state == InstallJob::State::Running ? "running"
                            : j.state == InstallJob::State::Done    ? "done"
                            : j.state == InstallJob::State::Failed  ? "failed"
                                                                    : "cancelled";
        char head[160];
        std::snprintf(head, sizeof(head), "{\"id\":%d,\"state\":\"%s\",\"size\":%llu", j.id, state, (unsigned long long)j.size);
        std::string item = head;
        item += ",\"game\":" + http::JsonQuote(j.game) + ",\"label\":" + http::JsonQuote(j.label);
        if (j.state == InstallJob::State::Running && live.running && live.job == j.id) {
            const char* stage = live.copying_save ? "copy" : live.phase >= 3 ? "register" : live.phase <= 1 ? "network" : "write";
            const double rate = live.total ? live.write_rate : live.net_rate;
            const double left = live.write_rate > 0.1 && live.total ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1048576.0) : 0;
            char more[200];
            std::snprintf(more, sizeof(more), ",\"stage\":\"%s\",\"percent\":%.1f,\"speed\":%.1f,\"left\":%d", stage, double(JobFraction(live)) * 100.0, rate,
                          int(left));
            item += more;
        }
        if (j.state == InstallJob::State::Failed && !j.error.empty()) {
            // the title of the packed error is enough for the web
            item += ",\"error\":" + http::JsonQuote(UnpackError(j.error).title);
        }
        item += "}";
        out += (first ? "" : ",") + item;
        first = false;
    }
    return out + "]}";
}

void App::FollowQueueWeb() {
    if (!m_queue || !m_report) return;
    const Config c = m_link.CurrentConfig();
    if (!c.Linked()) return;
    m_report->SetLink(c.url, c.key);
    const auto jobs = m_queue->Jobs();
    bool busy = false;
    for (const auto& j : jobs) {
        if (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running) {
            busy = true;
            if (std::find(m_report_jobs.begin(), m_report_jobs.end(), j.id) == m_report_jobs.end()) m_report_jobs.push_back(j.id);
        }
    }
    // what the queue does, every two seconds while it works, and once when it stops
    if ((busy && m_time - m_report_at >= 2) || (!busy && m_report_busy)) {
        m_report_at = m_time;
        m_report->Live(QueueJson());
    }
    if (!busy && m_report_busy) {
        // the end of the queue: what went well and what failed, for the phone (21A)
        std::string items;
        for (const auto& j : jobs) {
            if (std::find(m_report_jobs.begin(), m_report_jobs.end(), j.id) == m_report_jobs.end()) continue;
            if (j.state != InstallJob::State::Done && j.state != InstallJob::State::Failed) continue;
            if (!items.empty()) items += ",";
            items += "{\"game\":" + http::JsonQuote(j.game) + ",\"label\":" + http::JsonQuote(j.label) + ",\"ok\":" +
                     (j.state == InstallJob::State::Done ? "true" : "false");
            if (j.state == InstallJob::State::Failed) items += ",\"error\":" + http::JsonQuote(UnpackError(j.error).title);
            items += "}";
        }
        if (m_ui.notify_done && !items.empty() && !m_queue->Paused()) m_report->Done("{\"items\":[" + items + "]}");
        m_report_jobs.clear();
    }
    m_report_busy = busy;
    // what the web asked
    const More& m = TrM();
    for (auto& cmd : m_report->TakeCommands()) {
        if (cmd.action == "pause") {
            m_queue->SetPaused(true);
            Notice(m.qw_paused_web);
        } else if (cmd.action == "resume") {
            m_queue->SetPaused(false);
            Notice(m.qw_resumed);
        } else {
            m_web_cancels.push_back(std::move(cmd));
        }
    }
    // a cancel is asked about on the screen first, as what the web sends to install
    if (!m_web_cancels.empty() && m_dialog.kind == DialogKind::None && !m_wizard) {
        const QueueCommand cmd = m_web_cancels.front();
        m_web_cancels.erase(m_web_cancels.begin());
        const auto it = std::find_if(jobs.begin(), jobs.end(), [&cmd](const InstallJob& j) { return j.id == cmd.job; });
        if (it != jobs.end() && (it->state == InstallJob::State::Waiting || it->state == InstallJob::State::Running)) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::WebCancel;
            m_dialog.entry = cmd.job;
            m_dialog.text = it->game + (it->label.empty() ? "" : " \xC2\xB7 " + it->label);
        }
    }
}

void App::CheckResume() {
    m_resume_checked = true;
    auto state = std::make_unique<ResumeState>();
    if (!LoadResume(kResumeDir, *state)) return;
    m_resume = std::move(state);
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::Resume;
    m_dialog.focus = 1;
}

void App::FollowQueueAsk() {
    if (!m_queue || m_dialog.kind != DialogKind::None || m_wizard) return;
    if (!m_resume_checked && !TourActive()) {
        CheckResume();
        if (m_dialog.kind != DialogKind::None) return;
    }
    if (m_mods_after) {
        // the update asked about ended: Cheats and mods of the game, for the new version (19A)
        bool pending = false;
        for (const auto& j : m_queue->Jobs()) {
            if ((j.title_id & ~0x1FFFull) == m_mods_after &&
                (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running)) {
                pending = true;
            }
        }
        if (!pending) {
            const unsigned long long app = m_mods_after;
            m_mods_after = 0;
            OpenMods();
            OpenModsGame(app, m_mods_after_name);
            m_tab = Tab::More;
        }
    }
    const auto ask = m_queue->PendingAsk();
    if (!ask.waiting) return;
    m_dialog = Dialog{};
    m_dialog.kind = ask.why == 10 ? DialogKind::ModsAsk : DialogKind::SaveCopyAsk;
    m_dialog.text = ask.game;
    m_dialog.from = ask.error;
    m_dialog.entry = ask.why;
    m_dialog.ids = {ask.app};
}

void App::FollowPush() {
    const Link::View link = m_link.Snapshot();
    const Config c = m_link.CurrentConfig();
    const bool connected = link.state == Link::State::Connected && c.Linked();
    m_push->SetLink(connected ? c.url : std::string(), c.key);
    if (!connected) {
        return;
    }
    for (auto& item : m_push->Take()) {
        m_pushed.push_back(std::move(item));
    }
    if (m_dialog.kind != DialogKind::None || m_wizard || TourActive()) {
        return; // the wizard asks about sharing itself; what the web sent waits for it to end
    }
    // once, the first time a library is linked: share what is installed? (2A)
    if (m_prefs.share_installed < 0) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Share;
        return;
    }
    // what the web sent: asked about, never installed without A (1A)
    if (!m_pushed.empty()) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Push;
        for (const auto& p : m_pushed) {
            m_dialog.push.push_back(p.id);
            if (m_dialog.from.empty()) m_dialog.from = p.from;
            InstallJob job;
            bool found = false;
            for (const auto& g : m_games) {
                for (const auto& f : g.files) {
                    if (f.id == p.file_id) {
                        job = JobFor(g, f);
                        found = true;
                    }
                }
            }
            if (!found) {
                const auto& s = Tr();
                job.game = p.game;
                job.label = p.kind == "update" ? VersionText(p.version)
                          : p.kind == "dlc" ? p.name : std::string(s.base_game);
                job.color_key = p.game;
                job.file_id = p.file_id;
                job.url = c.url + p.url;
                job.key = c.key;
                job.format = p.format;
                job.kind = p.kind == "update" ? CatalogFile::Kind::Update : p.kind == "dlc" ? CatalogFile::Kind::Dlc : CatalogFile::Kind::Game;
                job.title_id = p.title_id;
                job.size = p.size;
            }
            m_dialog.jobs.push_back(std::move(job));
        }
        m_pushed.clear();
        return;
    }
    // the inventory, when the user said yes: at the start and after every install or removal
    if (m_prefs.share_installed == 1 && (m_inventory_sent != m_queue->Version() || m_inventory_url != c.url) && !m_queue->Busy()) {
        m_inventory_sent = m_queue->Version();
        m_inventory_url = c.url;
        std::vector<InstalledContent> list;
        ReadInstalled(&list);
        m_push->SendInventory(InventoryJson(list));
    }
}

bool TitleFromName(const std::string& name, u64& id, u32& version) {
    unsigned long long tid = 0;
    unsigned v = 0;
    const bool found = ParseTitleName(name, tid, v);
    id = tid;
    version = v;
    return found;
}

int TitleKind(u64 id) {
    const u64 low = id & 0xFFF;
    return low == 0 ? 0 : low == 0x800 ? 1 : 2;
}

// An update in `jobs` that would stop its game (update_over_game.hpp): its game comes neither in
// these jobs nor in the queue (the queue puts a game before its update), and the console has it
// compressed by NXDock or optimized. One for each job; Fine for the rest.
std::vector<App::UpdateCheck> App::UpdatesThatStop(const std::vector<InstallJob>& jobs) const {
    const auto idof = [](const InstallJob& j, u32& version) {
        u64 id = j.title_id;
        version = j.version;
        if (j.origin != InstallJob::Origin::Library || !id) {
            u64 named = 0;
            u32 v = 0;
            if (TitleFromName(j.game + " " + j.label, named, v)) {
                if (!id) id = named;
                if (!version) version = v;
            }
        }
        return id;
    };
    std::set<u64> coming;
    const auto queued = m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
    for (const auto* list : {&jobs, &queued}) {
        for (const auto& j : *list) {
            if (list == &queued && j.state != InstallJob::State::Waiting && j.state != InstallJob::State::Running) continue;
            u32 v = 0;
            const u64 id = idof(j, v);
            if (id && TitleKind(id) == 0) coming.insert(id);
        }
    }
    std::vector<UpdateCheck> out(jobs.size());
    std::map<u64, BaseGame> seen;
    for (size_t i = 0; i < jobs.size(); i++) {
        u32 version = 0;
        const u64 id = idof(jobs[i], version);
        if (!id || TitleKind(id) != 1 || jobs[i].downgrade || !jobs[i].repair.empty()) continue;
        const u64 app = GameOf(id);
        if (coming.count(app)) continue;
        if (!seen.count(app)) seen[app] = BaseGameOf(app);
        // a version not known is taken as a newer one
        out[i] = {UpdateOverGame(seen[app], version ? version : ~0u), seen[app], app};
    }
    return out;
}

// The original games of the library that let those updates work, to go before them.
std::vector<InstallJob> App::OriginalsFor(const std::vector<InstallJob>& jobs) {
    std::vector<InstallJob> out;
    std::set<u64> done;
    for (const auto& c : UpdatesThatStop(jobs)) {
        if (!OriginalHelps(c.over) || done.count(c.app)) continue;
        for (const auto& g : m_games) {
            if (g.title_id != c.app) continue;
            const CatalogFile* base = g.Base();
            if (!base) break;
            InstallJob job = JobFor(g, *base);
            job.nand = g.installed.base_nand;
            job.reinstall_ok = true; // the same version as the one compressed: it takes its place
            if (c.base.version > 0) job.downgrade = true; // an optimized game is at the version of its update
            out.push_back(std::move(job));
            done.insert(c.app);
            break;
        }
    }
    return out;
}

void App::AddJobs(std::vector<InstallJob> jobs) {
    const auto& s = Tr();
    // a file from the SD card, a USB disk or the computer: the cover and the name of its game in
    // the library, found by the title ID in its name ("Game [0100…][v0].nsz")
    for (auto& j : jobs) {
        if (!j.cover.empty()) continue;
        const size_t at = j.game.find("[01");
        if (at == std::string::npos || at + 18 > j.game.size() || j.game[at + 17] != ']') continue;
        u64 id = 0;
        bool hex = true;
        for (size_t i = at + 1; i < at + 17; i++) {
            const char c = j.game[i];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) hex = false;
            id = id << 4 | u64(v < 0 ? 0 : v);
        }
        if (!hex) continue;
        // the game of an update (…800) or of a DLC (its game's ID plus 0x1000 and a number)
        const u64 low = id & 0xFFF;
        const u64 app = low == 0 || low == 0x800 ? id & ~u64(0xFFF) : (id - 0x1000) & ~u64(0xFFF);
        for (const auto& g : m_games) {
            if (g.title_id != app) continue;
            j.cover = g.cover;
            j.color_key = g.key;
            if (j.title_id == 0) j.title_id = id;
            break;
        }
    }
    // the files of the SD card or a USB disk: what each needs, read before anything (on a thread)
    if (std::any_of(jobs.begin(), jobs.end(), [](const InstallJob& j) { return j.origin == InstallJob::Origin::File && !j.preflighted; })) {
        m_preflight_dropped = false;
        m_preflight = std::async(std::launch::async, [jobs]() mutable {
            PreflightFiles(jobs);
            return jobs;
        });
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Preflight;
        m_dialog.single = true;
        return;
    }
    // a DLC whose game is older than it asks (neither installed nor coming in this queue)
    {
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        const auto idof = [](const InstallJob& j) {
            u64 id = j.title_id;
            u32 v = 0;
            if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
            return id;
        };
        std::map<u64, u32> coming;
        for (const auto& j : jobs) {
            const u64 id = idof(j);
            if (id && TitleKind(id) == 1) {
                u64 named = 0;
                u32 v = j.version;
                if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, named, v);
                coming[id & ~u64(0xFFF)] = std::max(coming[id & ~u64(0xFFF)], v);
            }
        }
        bool any = false;
        for (const auto& j : jobs) {
            const u64 id = idof(j);
            if (!j.dlc_needs || j.dlc_ok || !id || TitleKind(id) != 2) continue;
            const u64 app = (id - 0x1000) & ~u64(0xFFF);
            u32 have = coming.count(app) ? coming[app] : 0;
            const auto it = installed.find(app);
            if (it != installed.end()) have = std::max(have, it->second.patch);
            if (j.dlc_needs > have) any = true;
        }
        if (any) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::DlcNeeds;
            m_dialog.jobs = std::move(jobs);
            return;
        }
        for (auto& j : jobs) j.dlc_ok = true;
    }
    // an official update over a game compressed by NXDock, or over an optimized one: the game would
    // not start with it; asked first, with the original game as the way out
    {
        const auto over = UpdatesThatStop(jobs);
        if (std::any_of(over.begin(), over.end(), [](const UpdateCheck& c) { return StopsTheGame(c.over); })) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::UpdateStops;
            m_dialog.jobs = std::move(jobs);
            m_dialog.checks = over;
            m_dialog.single = OriginalsFor(m_dialog.jobs).empty();
            return;
        }
    }
    // the same version installed already: asked first, as it would end at once and change nothing
    {
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        bool any = false;
        for (auto& j : jobs) {
            if (j.reinstall_ok || !j.repair.empty() || j.downgrade || j.resumed) continue;
            u64 id = j.title_id;
            u32 version = j.version;
            if (j.origin != InstallJob::Origin::Library) {
                u64 named = 0;
                u32 v = 0;
                if (TitleFromName(j.game + " " + j.label, named, v)) {
                    id = named;
                    version = v;
                }
            }
            // an optimized game with an update of it under it: installing it takes that update
            // away (merged_updates.hpp), so it is no repeat and nothing is asked
            const auto it = id && TitleKind(id) == 0 ? installed.find(id) : installed.end();
            if (it != installed.end() && nxdock::ReinstallTakesUpdate(version, it->second.patch)) {
                j.reinstall_ok = true;
                continue;
            }
            if (id && HasContent(id, version)) {
                any = true;
            } else {
                j.reinstall_ok = true; // not installed: nothing to ask
            }
        }
        if (any) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::Reinstall;
            m_dialog.jobs = std::move(jobs);
            return;
        }
    }
    // the firmware, the keys and Atmosphère against what the contents ask for (2A): a window
    // only when something does not fit; NXDock never installs firmware or sigpatches
    u32 required = 0;
    bool unchecked = false;
    for (auto& j : jobs) {
        if (!j.checked) {
            unchecked = true;
            required = std::max(required, j.required_system);
        }
    }
    if (unchecked) {
        const PreCheck check = CheckBeforeInstall(m_sysinfo, required);
        for (auto& j : jobs) j.checked = true;
        if (check.Warns()) {
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::PreCheck;
            m_dialog.jobs = std::move(jobs);
            m_dialog.check = check;
            return;
        }
    }
    if (!m_risk_ok && m_safety && m_safety->Risky() && !jobs.empty()) {
        // not on an emuMMC, or Nintendo reachable: said once, before the first install
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Risk;
        m_dialog.jobs = std::move(jobs);
        return;
    }
    int added = 0, duplicate = 0;
    for (auto& job : jobs) {
        if (m_queue->Add(std::move(job))) {
            added++;
        } else {
            duplicate++;
        }
    }
    if (added) {
        NoticeOk(s.added_to_queue);
        // little battery and no charger: said before the install starts, it is not stopped
        if (m_ui.battery_notice && m_battery >= 0 && m_battery < m_ui.battery_level && !m_charging) {
            char text[200];
            std::snprintf(text, sizeof(text), s.battery_low, m_battery);
            Notice(text);
            m_notice_until = m_time + 6;
        }
        // back to the library: the strip shows the install going on
        m_detail = false;
    } else if (duplicate) {
        Notice(m_queue->Undoing() ? s.removing : s.already_in_queue);
    }
}

void App::FollowQueue() {
    if (!m_queue) {
        return;
    }
    // the best of the last speed test sets how many connections each download uses
    if (!m_speed_kept) {
        const SpeedTest::View v = m_speed.Snapshot();
        if (v.stage == SpeedTest::Stage::Done) {
            m_speed_kept = true;
            const int best = (v.net[0] >= v.net[1] && v.net[0] >= v.net[2]) ? 1 : (v.net[1] >= v.net[2] ? 2 : 4);
            if (v.net[best == 1 ? 0 : best == 2 ? 1 : 2] > 0 && best != m_prefs.connections) {
                m_prefs.connections = best;
                m_prefs.Save();
                m_queue->SetOptions(m_prefs.connections, m_prefs.download_test);
            }
        }
    }
    if (m_queue->Version() == m_queue_version) {
        return;
    }
    // a job ended: what the console has now, and the free space
    m_queue_version = m_queue->Version();
    const auto installed = m_queue->Installed();
    for (auto& g : m_games) {
        const auto it = installed.find(g.title_id);
        g.installed = it != installed.end() ? it->second : InstalledState{};
    }
    RebuildView();
    m_sd_checked = -100;
}

void App::UpdateQueue(u64 down, u64 held) {
    const auto rows = QueueOrder(m_queue->Jobs());
    const int count = int(rows.size());
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_queue_focus > 0) m_queue_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_queue_focus < count - 1) m_queue_focus++;
    m_queue_focus = std::clamp(m_queue_focus, 0, std::max(0, count - 1));
    if (count == 0) {
        return;
    }
    const InstallJob& j = rows[m_queue_focus];
    if (down & HidNpadButton_X) {
        if (j.state == InstallJob::State::Running) {
            // the one installing is cancelled only after a question, Keep installing in focus
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::QueueCancel;
            m_dialog.entry = j.id;
            m_dialog.text = j.game + (j.label.empty() ? "" : " · " + j.label);
            return;
        }
        m_queue->Remove(j.id); // a waiting or finished one goes at once
    }
    if ((down & HidNpadButton_A) && (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled)) {
        m_queue->Retry(j.id);
    }
    if (down & HidNpadButton_Y) {
        m_queue->Clear();
    }
    if (down & HidNpadButton_ZR) {
        const bool paused = !m_queue->Paused();
        m_queue->SetPaused(paused);
        Notice(paused ? TrM().qw_paused : TrM().qw_resumed);
    }
}

void App::DrawQueueStrip(NVGcontext* vg, float qy) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    Panel(vg, t, kGridX, qy, kWidth - 2 * kGridX, 54, 12);
    AddHit(kGridX, qy, kWidth - 2 * kGridX, 54, HitKind::QueueStrip);
    const auto live = m_queue->GetLive();
    if (!live.running) {
        const float qw = ui::Text(vg, 58, qy + 27, 16, t.muted, s.queue_empty) - 58;
        ui::Text(vg, 58 + qw + 12, qy + 27, 15, t.muted, m_queue->Busy() ? s.preparing : s.queue_empty_hint);
        StripKey(vg, qy);
        return;
    }
    const auto jobs = m_queue->Jobs();
    const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const InstallJob& j) { return j.id == live.job; });
    if (it == jobs.end()) {
        return;
    }
    // as chosen (1A): the picture, the name and how many wait, the bar, then the figures
    ui::Cover(vg, t, 54, qy + 10, 34, PlaceholderColor(it->color_key), Initial(it->game).c_str(), nullptr, false, false, 7,
              m_covers.Get(it->cover));
    CoverLoading(vg, 54, qy + 10, 34, it->cover);
    char title[160];
    std::snprintf(title, sizeof(title), s.installing_name, it->game.c_str());
    std::string head = title;
    const int waiting = m_queue->Waiting();
    if (waiting > 0) {
        char more[48];
        std::snprintf(more, sizeof(more), s.in_queue_count, waiting);
        head += std::string(" · ") + more;
    }
    nvgSave(vg);
    nvgScissor(vg, 100, qy, 380, 54);
    ui::Text(vg, 104, qy + 27, 16, t.text, head.c_str());
    nvgRestore(vg);

    // the four stages, as figures: network, decompress, write, check
    char figures[200];
    const float frac = JobFraction(live);
    if (live.merging) {
        std::snprintf(figures, sizeof(figures), "%s… %d %%", TrM().qm_merging, int(frac * 100));
    } else if (live.copying_save) {
        std::snprintf(figures, sizeof(figures), "%s…", TrM().sc_copying);
    } else if (live.phase >= 3) {
        std::snprintf(figures, sizeof(figures), "%s…", s.registering);
    } else if (live.total == 0) {
        std::snprintf(figures, sizeof(figures), "%s… · %s %.0f MB/s", s.preparing, s.network, live.net_rate);
    } else {
        // 45 % · 25 MB/s · 2:10 left
        const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
        char rest[64] = "";
        if (left >= 0) std::snprintf(rest, sizeof(rest), TrM().qs_left_fmt, ShortDuration(left).c_str());
        std::snprintf(figures, sizeof(figures), "%d %% · %.0f MB/s%s%s", int(frac * 100), live.write_rate, rest[0] ? " · " : "", rest);
    }
    const float fw = ui::TextWidth(vg, 15, figures);
    const float key_x = StripKey(vg, qy);
    const float bar_x = 494, bar_end = key_x - 16 - fw - 18;
    if (bar_end - bar_x > 40) {
        ui::Progress(vg, t, bar_x, qy + 23, bar_end - bar_x, 8, frac, live.merging ? t.violet : t.work, m_time);
    }
    ui::Text(vg, key_x - 16, qy + 27, 15, t.muted, figures, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
}

float App::StripKey(NVGcontext* vg, float qy) {
    // "X Queue" at the right end of the strip; returns where it starts
    const float lw = ui::TextWidth(vg, 15, Tr().queue);
    const float lx = kWidth - kGridX - 16 - lw;
    ui::Text(vg, lx, qy + 27, 15, m_theme.text, Tr().queue);
    ui::Text(vg, lx - 16, qy + 28, 22, m_theme.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    return lx - 30;
}

void App::DrawQueue(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const auto rows = QueueOrder(m_queue->Jobs());
    if (rows.empty()) {
        ui::Text(vg, kWidth / 2, 320, 22, t.text, s.queue_empty, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::Text(vg, kWidth / 2, 356, 16, t.muted, s.queue_empty_hint, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {{ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    m_queue_focus = std::clamp(m_queue_focus, 0, int(rows.size()) - 1);
    const auto live = m_queue->GetLive();
    const bool running = live.running && rows.front().state == InstallJob::State::Running;
    float y = 98;

    // The game being installed, with its four stages (3A).
    if (running) {
        const InstallJob& j = rows.front();
        const float px = 48, py = y, pw = kWidth - 96, ph = 236;
        Panel(vg, t, px, py, pw, ph, 16);
        if (m_queue_focus == 0) {
            ui::Focus(vg, t, px, py, pw, ph, 16, m_time);
        }
        AddHit(px, py, pw, ph, HitKind::QueueRow, 0);
        ui::Cover(vg, t, px + 24, py + 24, 120, PlaceholderColor(j.color_key), Initial(j.game).c_str(), nullptr, false, false, 10,
                  m_covers.Get(j.cover));
        CoverLoading(vg, px + 24, py + 24, 120, j.cover);
        const float cx = px + 168;
        nvgSave(vg);
        nvgScissor(vg, cx, py + 16, pw - 168 - 140, 48);
        const std::string sub = j.label + " · " + AsciiUpper(j.format) + " · " + (j.nand ? s.internal : "SD") +
                                (live.merging ? std::string(" · ") + TrM().qm_tag : std::string());
        // a long name glides to be read whole; what it is keeps its room after it
        const float room = pw - 168 - 140, sub_w = std::min(ui::TextWidth(vg, 16, sub.c_str()), room * 0.45f);
        const float nw = ui::Marquee(vg, cx, py + 40, room - sub_w - 14, 24, t.text, j.game.c_str(), m_time) - cx;
        ui::Text(vg, cx + nw + 14, py + 41, 16, t.muted, sub.c_str());
        nvgRestore(vg);

        const float frac = JobFraction(live);
        char pct[16];
        std::snprintf(pct, sizeof(pct), "%d %%", int(frac * 100));
        ui::Text(vg, px + pw - 24, py + 40, 26, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        char eta[200];
        if (live.merging) {
            // installed, now merged with the game (25G, as chosen: A)
            const u64 left_bytes = live.merge_total > live.merge_done ? live.merge_total - live.merge_done : 0;
            const double left = live.merge_rate > 0.1 && live.merge_phase < 3 ? double(left_bytes) / (live.merge_rate * 1024 * 1024) : -1;
            std::snprintf(eta, sizeof(eta), "%s… %s / %s%s%s · %s", TrM().qm_merging, SizeText(live.merge_done).c_str(),
                          SizeText(live.merge_total).c_str(), left >= 0 ? (std::string(" · ") + s.left + " ").c_str() : "",
                          left >= 0 ? Duration(left).c_str() : "", TrM().qm_playable);
        } else if (live.copying_save) {
            std::snprintf(eta, sizeof(eta), "%s… %s", TrM().sc_copying, live.copy_total ? (SizeText(live.copy_done) + " / " + SizeText(live.copy_total)).c_str() : "");
        } else if (live.phase >= 3) {
            std::snprintf(eta, sizeof(eta), "%s…", s.registering);
        } else if (live.total == 0) {
            std::snprintf(eta, sizeof(eta), "%s… · %s", s.preparing, Duration(live.elapsed).c_str());
        } else {
            const double left = live.write_rate > 0.1 ? double(live.total - std::min(live.total, live.written)) / (live.write_rate * 1024 * 1024) : -1;
            std::snprintf(eta, sizeof(eta), "%s / %s%s%s", SizeText(live.written).c_str(), SizeText(live.total).c_str(),
                          left >= 0 ? (std::string(" · ") + s.left + " ").c_str() : "", left >= 0 ? Duration(left).c_str() : "");
        }
        const float ew = ui::Text(vg, cx, py + 70, 16, t.muted, eta) - cx;
        if (live.saved > 0 && !live.merging) {
            // what the console had already, in the green of what went well (16A)
            char saved[64];
            std::snprintf(saved, sizeof(saved), TrM().saved_fmt, SizeText(live.saved).c_str());
            const float sx = cx + ew + 12;
            nvgBeginPath(vg);
            nvgCircle(vg, sx + 4, py + 70, 4);
            nvgFillColor(vg, t.ok);
            nvgFill(vg);
            ui::Text(vg, sx + 14, py + 70, 16, t.ok, saved);
        }
        ui::Progress(vg, t, cx, py + 88, pw - 168 - 24, 12, frac, live.merging ? t.violet : t.work, m_time);

        struct Lane { std::string title; NVGcolor color; std::string value; std::string unit; float meter; int segments; };
        char v[32];
        std::vector<Lane> lanes;
        if (live.merging) {
            // the three steps of the merge: read and measure, compress and write, check
            const float part = live.merge_total ? std::clamp(float(double(live.merge_done) / double(live.merge_total)), 0.f, 1.f) : 0.f;
            const auto step = [&](int phase) { return live.merge_phase > phase ? 1.f : live.merge_phase == phase ? part : 0.f; };
            const auto rate = [&](int phase) {
                if (live.merge_phase != phase) return std::string(live.merge_phase > phase ? "100" : "\u2014");
                std::snprintf(v, sizeof(v), "%.0f", live.merge_rate);
                return std::string(v);
            };
            const auto unit = [&](int phase) { return std::string(live.merge_phase == phase ? "MB/s" : live.merge_phase > phase ? "%" : ""); };
            lanes.push_back({TrM().qm_read, t.work, rate(1), unit(1), step(1), -1});
            lanes.push_back({TrM().qm_pack, t.violet, rate(2), unit(2), step(2), -1});
            lanes.push_back({TrM().qm_check, t.ok, live.merge_phase >= 3 ? rate(3) : "\u2014", live.merge_phase >= 3 ? "MB/s" : TrM().qm_at_end, step(3), -1});
        } else {
        std::snprintf(v, sizeof(v), "%.0f", live.net_rate);
        lanes.push_back({s.network, t.work, v, "MB/s", live.source_size ? float(double(live.net) / double(live.source_size)) : 0.f, -1});
        if (live.threads == 0) {
            lanes.push_back({s.decompress, t.violet, "—", s.not_compressed, -1, 0});
        } else {
            std::snprintf(v, sizeof(v), "%.0f", live.ready_rate);
            lanes.push_back({s.decompress, t.violet, v, "MB/s", -1, std::max(0, live.threads)});
        }
        std::snprintf(v, sizeof(v), "%.0f", live.write_rate);
        lanes.push_back({j.nand ? s.write_nand : s.write_sd, t.orange, v, "MB/s", live.total ? float(double(live.written) / double(live.total)) : 0.f, -1});
        std::snprintf(v, sizeof(v), "%u", live.checked);
        lanes.push_back({s.check_sha, t.ok, v, std::string(s.parts_of) + " " + std::to_string(live.ncas),
                         live.ncas ? float(live.checked) / float(live.ncas) : 0.f, -1});
        }
        const int nl = int(lanes.size());
        const float lw = (pw - 168 - 24 - (nl - 1) * 14) / nl;
        for (int i = 0; i < nl; i++) {
            const Lane& lane = lanes[i];
            const float lx = cx + i * (lw + 14), ly = py + 118;
            ui::FillRound(vg, lx, ly, lw, 92, 12, t.panel2);
            ui::StrokeRound(vg, lx + .5f, ly + .5f, lw - 1, 91, 12, t.line, 1);
            nvgBeginPath(vg);
            nvgCircle(vg, lx + 18, ly + 20, 5);
            nvgFillColor(vg, lane.color);
            nvgFill(vg);
            nvgSave(vg);
            nvgScissor(vg, lx, ly, lw - 8, 92);
            ui::Text(vg, lx + 30, ly + 20, 14, t.muted, lane.title.c_str());
            const float vw = ui::Text(vg, lx + 14, ly + 48, 22, t.text, lane.value.c_str()) - (lx + 14);
            ui::Text(vg, lx + 14 + vw + 6, ly + 50, 14, t.muted, lane.unit.c_str());
            nvgRestore(vg);
            if (lane.segments < 0) {
                // with the moving stripes of the main bar while it works, still once it is full
                const float frac = std::clamp(lane.meter, 0.f, 1.f);
                ui::Progress(vg, t, lx + 14, ly + 70, lw - 28, 6, frac, lane.color, m_time, frac < 1.f);
            } else {
                // One segment per decompression thread; the fourth core is the system's. The ones
                // at work carry the moving stripes.
                const float sw = (lw - 28 - 12) / 4;
                for (int k = 0; k < 4; k++) {
                    if (k < lane.segments) {
                        ui::Progress(vg, t, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 1.f, lane.color, m_time, true);
                    } else {
                        NVGcolor c = lane.color;
                        c.a = 0.25f;
                        ui::FillRound(vg, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 3, c);
                    }
                }
            }
        }
        y = py + ph + 26;
        if (live.merging) {
            // what the job did already: the update, installed
            ui::Text(vg, 62, y, 13, t.muted, TrM().qm_installed);
            const std::string what = SizeText(live.installed_bytes) + " \u00b7 " + Duration(live.installed_seconds);
            const float mx = 62 + ui::TextWidth(vg, 13, TrM().qm_installed) + 20;
            nvgBeginPath(vg);
            nvgCircle(vg, mx, y, 7);
            nvgFillColor(vg, t.ok);
            nvgFill(vg);
            ui::CheckMark(vg, mx, y, 8, t.panel, 2.f);
            ui::Text(vg, mx + 14, y, 13, t.ok, what.c_str());
            y += 26;
        }
    }

    // Waiting, then done; the list scrolls to keep the row in focus in sight.
    const int first_row = running ? 1 : 0;
    // a failed row in focus shows what happened and what to do under the list (1A)
    const InstallJob& focused = rows[m_queue_focus];
    // or, installed but not merged with its game, why (25G)
    const std::string& card_text = focused.state == InstallJob::State::Done ? focused.merge_error : focused.error;
    const bool card = (focused.state == InstallJob::State::Failed || focused.state == InstallJob::State::Done) && !card_text.empty();
    const float card_h = card ? ErrorCard(vg, t, 48, 0, kWidth - 96, card_text, false) : 0;
    const float list_top = y, list_bottom = card ? std::max(y + 100, 650 - card_h - 12) : 640;
    if (card) {
        ErrorCard(vg, t, 48, 650 - card_h, kWidth - 96, card_text, true);
    }
    const float pitch = 54;
    ListArea(48, 98, kWidth - 96, list_bottom - 98, pitch);
    const int visible = std::max(1, int((list_bottom - list_top - 34) / pitch));
    const int focus_in_list = std::max(0, m_queue_focus - first_row);
    const int skip = std::max(0, focus_in_list - visible + 1);
    bool shown_waiting = false, shown_done = false;
    int drawn = 0;
    for (int r = first_row; r < int(rows.size()); r++) {
        const InstallJob& j = rows[r];
        if (r - first_row < skip) {
            continue;
        }
        if (drawn >= visible) {
            break;
        }
        const bool waiting = j.state == InstallJob::State::Waiting;
        if (waiting && !shown_waiting) {
            ui::Text(vg, 62, y, 13, t.muted, s.in_queue);
            y += 22;
            shown_waiting = true;
        } else if (!waiting && !shown_done) {
            y += shown_waiting ? 12 : 0;
            ui::Text(vg, 62, y + 8, 13, t.muted, s.done);
            y += 30;
            shown_done = true;
        }
        if (y + 44 > list_bottom) {
            break;
        }
        if (r == m_queue_focus) {
            ui::Focus(vg, t, 54, y - 5, kWidth - 108, 54, 10, m_time);
        }
        AddHit(54, y - 5, kWidth - 108, 54, HitKind::QueueRow, r);
        nvgGlobalAlpha(vg, waiting ? 0.55f : 1.f);
        ui::Cover(vg, t, 62, y, 44, PlaceholderColor(j.color_key), Initial(j.game).c_str(), nullptr, false, false, 8, m_covers.Get(j.cover));
        CoverLoading(vg, 62, y, 44, j.cover);
        nvgSave(vg);
        nvgScissor(vg, 122, y, 600, 44);
        ui::Marquee(vg, 122, y + 22, 590, 17, t.text, FileLabel(j).c_str(), m_time);
        nvgRestore(vg);
        std::string info = AsciiUpper(j.format) + " · " + SizeText(j.size) + " · " + (j.nand ? s.internal_short : "SD");
        if (j.state == InstallJob::State::Done) info += " · " + Duration(j.seconds);
        if (j.state == InstallJob::State::Done && j.saved > 0) {
            char saved[64];
            std::snprintf(saved, sizeof(saved), TrM().saved_fmt, SizeText(j.saved).c_str());
            info += std::string(" · ") + saved;
        }
        if (j.state == InstallJob::State::Done && j.merged) {
            char merged[96];
            std::snprintf(merged, sizeof(merged), TrM().qm_merged_fmt, SizeText(j.merge_saved).c_str());
            info += std::string(" \u00b7 ") + merged;
        }
        if (j.state == InstallJob::State::Done && !j.merge_error.empty()) {
            // installed, not merged: why, in place of the figures (the game waits in Compress games)
            nvgSave(vg);
            nvgScissor(vg, 730, y, 340, 44);
            ui::Text(vg, 1070, y + 22, 14, t.orange, ErrorTitle(j.merge_error).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
        } else if (j.state == InstallJob::State::Failed || j.state == InstallJob::State::Cancelled) {
            // why, in place of the figures
            nvgSave(vg);
            nvgScissor(vg, 730, y, 340, 44);
            ui::Text(vg, 1070, y + 22, 14, j.state == InstallJob::State::Failed ? t.err : t.muted, ErrorTitle(j.error).c_str(),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
        } else {
            ui::Text(vg, 1070, y + 22, 15, t.muted, info.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        switch (j.state) {
            case InstallJob::State::Waiting:
                ui::Text(vg, 1218, y + 22, 15, t.muted, s.waiting, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            case InstallJob::State::Done: {
                const float cw = ui::TextWidth(vg, 15, s.checked);
                ui::Text(vg, 1218, y + 22, 15, t.ok, s.checked, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgCircle(vg, 1218 - cw - 14, y + 22, 9);
                nvgFillColor(vg, t.ok);
                nvgFill(vg);
                ui::CheckMark(vg, 1218 - cw - 14, y + 22, 10, t.panel, 2.2f);
                break;
            }
            case InstallJob::State::Failed:
                ui::Text(vg, 1218, y + 22, 15, t.err, s.failed, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            case InstallJob::State::Cancelled:
                ui::Text(vg, 1218, y + 22, 15, t.muted, s.cancelled, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                break;
            default: break;
        }
        nvgGlobalAlpha(vg, 1.f);
        y += pitch;
        drawn++;
    }

    const InstallJob& f = rows[m_queue_focus];
    std::vector<ui::Hint> hints;
    if (f.state == InstallJob::State::Failed || f.state == InstallJob::State::Cancelled) {
        hints.push_back({ui::glyph::A, s.retry, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::X, f.state == InstallJob::State::Running ? s.cancel : s.remove, HidNpadButton_X});
    // clearing, pausing and the screen off (11A) are in the menu of + (1A, 14/11)
    hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus});
    const bool paused = m_queue->Paused();
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, paused ? TrM().qw_paused : m_prefs.download_test ? s.download_test : live.merging ? TrM().qm_note : (running ? s.no_sleep : s.queue_done_hint), hints,
                m_hint_rects);
}

// For the menu of actions (more_actions.cpp): the queue as shown, and what can be undone.
std::vector<InstallJob> QueueAsShown(std::vector<InstallJob> jobs) { return QueueOrder(std::move(jobs)); }

} // namespace nxdock
