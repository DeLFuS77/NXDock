// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "link.hpp"

#include <switch.h>
#include <chrono>
#include "http.hpp"
#include "i18n.hpp"

namespace nxdock {
namespace {

using Clock = std::chrono::steady_clock;

std::string TrimUrl(std::string url) {
    while (!url.empty() && (url.back() == '/' || url.back() == ' ')) {
        url.pop_back();
    }
    if (!url.empty() && url.find("://") == std::string::npos) {
        url = "http://" + url;
    }
    return url;
}

// The name of the console as its owner set it, to name its key in the app.
std::string ConsoleName() {
    std::string name = "NXDock";
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysDeviceNickName nick{};
        if (R_SUCCEEDED(setsysGetDeviceNickname(&nick)) && nick.nickname[0]) {
            name = nick.nickname;
        }
        setsysExit();
    }
    return name;
}

void SleepMs(int ms, const std::atomic<bool>& cancel) {
    for (int waited = 0; waited < ms && !cancel; waited += 50) {
        svcSleepThread(50'000'000);
    }
}

} // namespace

Link::~Link() {
    Stop();
}

void Link::Stop() {
    m_cancel = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Link::Run(void (Link::*job)()) {
    Stop();
    m_cancel = false;
    m_thread = std::thread([this, job] { (this->*job)(); });
}

void Link::Set(State state) {
    std::lock_guard lock(m_mutex);
    m_view.state = state;
    if (state != State::Failed && state != State::Offline) {
        m_view.error.clear();
    }
}

void Link::Fail(State state, const std::string& error) {
    std::lock_guard lock(m_mutex);
    m_view.state = state;
    m_view.error = error;
}

Link::View Link::Snapshot() {
    std::lock_guard lock(m_mutex);
    View v = m_view;
    v.url = m_session.url;
    v.name = m_session.name;
    return v;
}

Config Link::CurrentConfig() {
    std::lock_guard lock(m_mutex);
    Config c = m_session;
    c.light = m_config.light;
    return c;
}

void Link::SaveTheme(bool light) {
    std::lock_guard lock(m_mutex);
    m_config.light = light;
    m_config.Save();
}

void Link::SetRemember(bool remember) {
    std::lock_guard lock(m_mutex);
    m_view.remember = remember;
}

void Link::Start(const Config& config) {
    {
        std::lock_guard lock(m_mutex);
        m_config = config;
        m_session = config;
    }
    Run(config.Linked() ? &Link::JobReconnect : &Link::JobSearch);
}

void Link::Search() {
    Run(&Link::JobSearch);
}

void Link::Choose(int index) {
    {
        std::lock_guard lock(m_mutex);
        if (index < 0 || index >= int(m_view.found.size())) {
            return;
        }
        m_view.chosen = index;
        m_target = m_view.found[index];
    }
    Run(&Link::JobPair);
}

void Link::UseAddress(const std::string& typed) {
    {
        std::lock_guard lock(m_mutex);
        m_typed = TrimUrl(typed);
    }
    Run(&Link::JobAddress);
}

void Link::JobAddress() {
    Set(State::Searching);
    std::string url;
    {
        std::lock_guard lock(m_mutex);
        url = m_typed;
    }
    const auto r = http::Get(url + "/install/hello", "", 4000);
    if (m_cancel) {
        return;
    }
    Library lib;
    if (r.status != 200 || !ReadHello(r.body, url, lib)) {
        Fail(State::Choose, Tr().link_not_app);
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        bool known = false;
        for (size_t i = 0; i < m_view.found.size(); i++) {
            if (m_view.found[i].id == lib.id) {
                m_view.chosen = int(i);
                known = true;
            }
        }
        if (!known) {
            m_view.found.push_back(lib);
            m_view.chosen = int(m_view.found.size()) - 1;
        }
        m_target = lib;
    }
    JobPair();
}

void Link::UseKey(const std::string& typed, const std::string& key) {
    {
        std::lock_guard lock(m_mutex);
        m_session.url = TrimUrl(typed);
        m_session.key = key;
        m_session.id.clear();
        m_session.name.clear();
    }
    Run(&Link::JobReconnect);
}

void Link::Forget() {
    Stop();
    {
        std::lock_guard lock(m_mutex);
        const bool light = m_config.light;
        m_config = Config{};
        m_config.light = light;
        m_config.Save();
        m_session = m_config;
        m_view = View{};
    }
    Run(&Link::JobSearch);
}

// Checks that the app takes the key: a byte of its speed test.
bool Link::CheckKey(const std::string& url, const std::string& key, bool& enabled, std::string& error) {
    const auto r = http::Get(url + "/install/speedtest?size=1", key, 4000);
    enabled = true;
    switch (r.status) {
        case 200:
        case 206:
            return true;
        case 404:
            // the key is fine, the network install is off for now
            enabled = false;
            return true;
        case 401:
            error = Tr().err_key;
            return false;
        case 403:
            error = Tr().err_forbidden;
            return false;
        default:
            error = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
            return false;
    }
}

void Link::JobReconnect() {
    Set(State::Reconnecting);
    Config session;
    {
        std::lock_guard lock(m_mutex);
        session = m_session;
    }
    // 1. where it was
    std::string url = session.url;
    auto hello = http::Get(url + "/install/hello", "", 3000);
    Library lib;
    bool here = hello.status == 200 && ReadHello(hello.body, url, lib) && (session.id.empty() || lib.id == session.id);
    // 2. moved: the same app on another address
    if (!here && !session.id.empty() && !m_cancel) {
        Set(State::Searching);
        const auto found = Discover(m_cancel, [this](float p) {
            std::lock_guard lock(m_mutex);
            m_view.progress = p;
        });
        for (const auto& candidate : found) {
            if (candidate.id == session.id) {
                lib = candidate;
                url = candidate.url;
                here = true;
            }
        }
    }
    if (m_cancel) {
        return;
    }
    if (!here) {
        Fail(State::Offline, Tr().link_offline);
        return;
    }
    bool enabled = true;
    std::string error;
    if (!CheckKey(url, session.key, enabled, error)) {
        // the key was removed in the app: pair again
        {
            std::lock_guard lock(m_mutex);
            m_view.found = {lib};
            m_view.chosen = 0;
            m_target = lib;
            m_view.error = error;
        }
        JobPair();
        return;
    }
    std::lock_guard lock(m_mutex);
    const bool moved = m_session.url != url || m_session.id != lib.id;
    m_session.url = url;
    m_session.id = lib.id;
    if (moved && m_config.Linked() && m_config.key == m_session.key) {
        // remembered: keep the new address
        m_config.url = url;
        m_config.id = lib.id;
        m_config.Save();
    }
    m_view.app_name = lib.name;
    m_view.enabled = enabled;
    m_view.state = State::Connected;
    m_view.error.clear();
}

void Link::JobSearch() {
    {
        std::lock_guard lock(m_mutex);
        m_view.state = State::Searching;
        m_view.found.clear();
        m_view.chosen = -1;
        m_view.progress = 0;
        m_view.error.clear();
    }
    auto found = Discover(m_cancel, [this](float p) {
        std::lock_guard lock(m_mutex);
        m_view.progress = p;
    });
    if (m_cancel) {
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        m_view.found = found;
        m_view.state = State::Choose;
        if (found.size() != 1) {
            return;
        }
        // only one: no need to ask which
        m_view.chosen = 0;
        m_target = found[0];
    }
    JobPair();
}

void Link::JobPair() {
    Library target;
    {
        std::lock_guard lock(m_mutex);
        target = m_target;
        m_view.state = State::Asking;
        m_view.code.clear();
    }
    const std::string body = "{\"console\":" + http::JsonQuote(ConsoleName()) + "}";
    const auto asked = http::PostJson(target.url + "/install/pair", body, "", 5000);
    if (m_cancel) {
        return;
    }
    if (asked.status != 201) {
        const auto& s = Tr();
        Fail(State::Failed, asked.status == 429 ? s.link_busy : asked.status == 403 ? s.err_forbidden
                            : asked.error.empty() ? "HTTP " + std::to_string(asked.status) : asked.error);
        return;
    }
    const std::string id = http::JsonString(asked.body, "id");
    const int life = int(http::JsonNumber(asked.body, "expires", 600));
    {
        std::lock_guard lock(m_mutex);
        m_view.code = http::JsonString(asked.body, "code");
        m_view.seconds_left = life;
        m_view.state = State::Waiting;
    }
    const auto start = Clock::now();
    while (!m_cancel) {
        SleepMs(2000, m_cancel);
        if (m_cancel) {
            return;
        }
        const int left = life - int(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count());
        {
            std::lock_guard lock(m_mutex);
            m_view.seconds_left = std::max(0, left);
        }
        const auto r = http::Get(target.url + "/install/pair/" + id, "", 4000);
        if (r.status == 202 || r.status == 0) {
            // still waiting, or a moment without network: keep asking until the code runs out
            if (left <= 0) {
                Fail(State::Expired, Tr().link_expired);
                return;
            }
            continue;
        }
        if (r.status == 403) {
            Fail(State::Refused, Tr().link_refused);
            return;
        }
        if (r.status != 200) {
            Fail(State::Expired, Tr().link_expired);
            return;
        }
        const std::string key = http::JsonString(r.body, "key");
        std::lock_guard lock(m_mutex);
        m_session.url = target.url;
        m_session.key = key;
        m_session.id = target.id;
        m_session.name = http::JsonString(r.body, "name");
        if (m_view.remember) {
            const bool light = m_config.light;
            m_config = m_session;
            m_config.light = light;
            m_config.Save();
        }
        m_view.app_name = target.name;
        // the app tells it in the answer (an administrator may have turned it on as they accepted)
        m_view.enabled = http::JsonBool(r.body, "enabled", target.enabled);
        m_view.state = State::Connected;
        m_view.error.clear();
        return;
    }
}

} // namespace nxdock
