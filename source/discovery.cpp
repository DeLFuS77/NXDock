// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "discovery.hpp"

#include <switch.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include "http.hpp"

namespace nxdock {
namespace {

constexpr int kDiscoveryPort = 45730;
constexpr const char* kAsk = "NXDOCK-DISCOVER 1";
constexpr const char* kAppName = "switch-library-manager-web";
// the port of the app, and the one most installs publish it on
constexpr int kPorts[] = {3000, 3030};
constexpr int kParallel = 32;

// The address and mask of the console, in network order. False without network.
bool LocalNetwork(u32& address, u32& mask) {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) {
        return false;
    }
    u32 gateway = 0, dns1 = 0, dns2 = 0;
    const bool ok = R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&address, &mask, &gateway, &dns1, &dns2)) && address != 0;
    nifmExit();
    return ok;
}

std::string Dotted(u32 network_order) {
    in_addr a{};
    a.s_addr = network_order;
    return inet_ntoa(a);
}

void AddFound(std::vector<Library>& found, const Library& lib) {
    for (const auto& known : found) {
        if (known.id == lib.id) {
            return;
        }
    }
    found.push_back(lib);
}

// The UDP question to the whole network; the answers that come within a moment.
void AskByUdp(u32 address, u32 mask, std::vector<Library>& found, const std::atomic<bool>& cancel) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(kDiscoveryPort);
    // the broadcast of this network and the general one: some routers keep only one
    for (const u32 target : {(address & mask) | ~mask, u32(INADDR_BROADCAST)}) {
        to.sin_addr.s_addr = target;
        sendto(fd, kAsk, std::strlen(kAsk), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }

    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    char buffer[1024];
    while (std::chrono::steady_clock::now() < until && !cancel) {
        // waits with poll, never in recvfrom: an answer that does not come never holds the search
        pollfd ready{fd, POLLIN, 0};
        if (poll(&ready, 1, 200) <= 0 || !(ready.revents & POLLIN)) {
            continue;
        }
        sockaddr_in from{};
        socklen_t len = sizeof(from);
        const ssize_t n = recvfrom(fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &len);
        if (n <= 0) {
            continue;
        }
        buffer[n] = 0;
        const std::string body(buffer);
        const long long port = http::JsonNumber(body, "port", 3000);
        // the answer says the port the app listens on; in Docker it may be published on another,
        // so the address is checked with hello on that port and on the usual ones
        const std::string host = Dotted(from.sin_addr.s_addr);
        std::vector<int> ports{int(port)};
        for (int p : kPorts) {
            if (p != port) ports.push_back(p);
        }
        for (int p : ports) {
            const std::string url = "http://" + host + ":" + std::to_string(p);
            const auto r = http::Get(url + "/install/hello", "", 1500);
            Library lib;
            if (r.status == 200 && ReadHello(r.body, url, lib)) {
                AddFound(found, lib);
                break;
            }
        }
    }
    close(fd);
}

struct Probe {
    std::string url;
    std::string body;
};

size_t OnProbe(char* data, size_t size, size_t count, void* user) {
    auto* probe = static_cast<Probe*>(user);
    if (probe->body.size() + size * count > 4096) {
        return 0;
    }
    probe->body.append(data, size * count);
    return size * count;
}

// Asks hello on the usual ports of every address of the network, a few at a time.
void AskEveryAddress(u32 address, u32 mask, std::vector<Library>& found, const std::atomic<bool>& cancel,
                     const std::function<void(float)>& progress) {
    // a large network is looked at in the /24 of the console only: enough at home, and quick
    const u32 host_order = ntohl(address);
    u32 first = ntohl(address & mask) + 1, last = ntohl((address & mask) | ~mask) - 1;
    if (last - first > 254) {
        first = (host_order & 0xFFFFFF00u) + 1;
        last = (host_order & 0xFFFFFF00u) + 254;
    }
    std::vector<std::string> targets;
    for (u32 h = first; h <= last; h++) {
        if (h == host_order) continue;
        for (int port : kPorts) {
            targets.push_back("http://" + Dotted(htonl(h)) + ":" + std::to_string(port));
        }
    }

    CURLM* multi = curl_multi_init();
    std::vector<std::unique_ptr<Probe>> probes;
    std::vector<CURL*> active;
    size_t next = 0, done = 0;
    int running = 0;
    auto start = [&](const std::string& url) {
        auto probe = std::make_unique<Probe>();
        probe->url = url;
        CURL* easy = curl_easy_init();
        const std::string hello = url + "/install/hello";
        curl_easy_setopt(easy, CURLOPT_URL, hello.c_str());
        curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 700L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 1500L);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, OnProbe);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, probe.get());
        curl_easy_setopt(easy, CURLOPT_PRIVATE, probe.get());
        curl_multi_add_handle(multi, easy);
        active.push_back(easy);
        probes.push_back(std::move(probe));
    };
    while (next < targets.size() && next < size_t(kParallel)) {
        start(targets[next++]);
    }
    do {
        curl_multi_perform(multi, &running);
        int left = 0;
        while (CURLMsg* msg = curl_multi_info_read(multi, &left)) {
            if (msg->msg != CURLMSG_DONE) continue;
            CURL* easy = msg->easy_handle;
            Probe* probe = nullptr;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &probe);
            long status = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
            Library lib;
            if (msg->data.result == CURLE_OK && status == 200 && probe && ReadHello(probe->body, probe->url, lib)) {
                AddFound(found, lib);
            }
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
            active.erase(std::remove(active.begin(), active.end(), easy), active.end());
            done++;
            if (progress) progress(0.2f + 0.8f * float(done) / float(targets.size()));
            if (next < targets.size() && !cancel) {
                start(targets[next++]);
                running++;
            }
        }
        if (running > 0 && !cancel) {
            curl_multi_poll(multi, nullptr, 0, 100, nullptr);
        }
    } while (running > 0 && !cancel);
    // whatever is still going when cancelled
    for (CURL* easy : active) {
        curl_multi_remove_handle(multi, easy);
        curl_easy_cleanup(easy);
    }
    curl_multi_cleanup(multi);
}

} // namespace

bool ReadHello(const std::string& body, const std::string& url, Library& out) {
    if (http::JsonString(body, "app") != kAppName) {
        return false;
    }
    out.id = http::JsonString(body, "id");
    out.name = http::JsonString(body, "name");
    out.version = http::JsonString(body, "version");
    out.enabled = http::JsonBool(body, "enabled");
    out.url = url;
    return !out.id.empty();
}

std::vector<Library> Discover(const std::atomic<bool>& cancel, const std::function<void(float)>& progress) {
    std::vector<Library> found;
    u32 address = 0, mask = 0;
    if (!LocalNetwork(address, mask)) {
        return found;
    }
    if (progress) progress(0.02f);
    AskByUdp(address, mask, found, cancel);
    if (progress) progress(0.2f);
    if (found.empty() && !cancel) {
        AskEveryAddress(address, mask, found, cancel, progress);
    }
    if (progress) progress(1.f);
    std::sort(found.begin(), found.end(), [](const Library& a, const Library& b) { return a.url < b.url; });
    return found;
}

std::string ConsoleAddress() {
    u32 address = 0, mask = 0;
    return LocalNetwork(address, mask) ? Dotted(address) : "";
}

} // namespace nxdock
