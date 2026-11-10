// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "usb_tinfoil.hpp"

#include "defines.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nxdock {
namespace tinfoil {
namespace {

void Put32(unsigned char* p, unsigned v) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i));
}
void Put64(unsigned char* p, unsigned long long v) {
    for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i));
}
unsigned Get32(const unsigned char* p) {
    return unsigned(p[0]) | unsigned(p[1]) << 8 | unsigned(p[2]) << 16 | unsigned(p[3]) << 24;
}
unsigned long long Get64(const unsigned char* p) {
    unsigned long long v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

std::vector<unsigned char> Header(unsigned cmd, unsigned long long data_size) {
    std::vector<unsigned char> h(32, 0);
    Put32(&h[0], kCommandMagic);
    h[4] = 0; // a request
    Put32(&h[8], cmd);
    Put64(&h[12], data_size);
    return h;
}

} // namespace

long long ListLength(const unsigned char* header16) {
    if (Get32(header16) != kListMagic) return -1;
    return (long long)Get32(header16 + 4);
}

std::vector<std::string> SplitList(const char* data, size_t size) {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i < size; i++) {
        const char c = data[i];
        if (c == '\n' || c == '\r' || c == '\0') {
            if (!line.empty()) out.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::vector<unsigned char> RangeRequest(const std::string& name, unsigned long long offset, unsigned long long size) {
    auto out = Header(kCmdFileRange, 0x20 + name.size());
    unsigned char range[32] = {};
    Put64(range, size);
    Put64(range + 8, offset);
    Put64(range + 16, name.size());
    out.insert(out.end(), range, range + 32);
    out.insert(out.end(), name.begin(), name.end());
    return out;
}

std::vector<unsigned char> ExitRequest() {
    return Header(kCmdExit, 0);
}

long long AnswerLength(const unsigned char* header32) {
    if (Get32(header32) != kCommandMagic || header32[4] != 1) return -1;
    return (long long)Get64(header32 + 12);
}

} // namespace tinfoil
} // namespace nxdock

#ifdef __SWITCH__
#include <switch.h>

namespace nxdock {
namespace {

constexpr size_t kBufferSize = 8 * 1024 * 1024;
constexpr u64 kSlice = 100'000'000ull; // the wait is cut in slices to see a cancel

} // namespace

TinfoilUsb& UsbLink() {
    static TinfoilUsb link;
    return link;
}

TinfoilUsb::~TinfoilUsb() {
    Stop();
}

bool TinfoilUsb::Start() {
    if (m_started) return true;
    if (!m_buffer) {
        m_buffer = static_cast<unsigned char*>(aligned_alloc(0x1000, kBufferSize));
        if (!m_buffer) return false;
    }
    m_cancel = false;
    m_started = R_SUCCEEDED(usbCommsInitialize());
    return m_started;
}

void TinfoilUsb::Listen(bool on) {
    m_listen = on;
    if (on && !m_thread.joinable()) {
        m_quit = false;
        m_thread = std::thread([this] { ListenLoop(); });
    }
}

void TinfoilUsb::ListenLoop() {
    while (!m_quit) {
        bool want;
        {
            std::lock_guard lock(m_list_mutex);
            want = m_listen && !m_have_list;
        }
        std::vector<std::string> names;
        if (want && Connected() && PollList(names)) {
            std::lock_guard lock(m_list_mutex);
            m_list = std::move(names);
            m_have_list = m_list_new = true;
        } else {
            svcSleepThread(200'000'000ull);
        }
    }
}

bool TinfoilUsb::TakeList(std::vector<std::string>& names) {
    std::lock_guard lock(m_list_mutex);
    if (!m_list_new) return false;
    m_list_new = false;
    names = m_list;
    return true;
}

void TinfoilUsb::Stop() {
    m_listen = false;
    m_quit = true;
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard lock(m_list_mutex);
        m_have_list = m_list_new = false;
    }
    std::lock_guard lock(m_mutex);
    if (m_started) {
        usbCommsExit();
        m_started = false;
    }
    std::free(m_buffer);
    m_buffer = nullptr;
}

bool TinfoilUsb::Connected() {
    UsbState state = UsbState_Detached;
    return m_started && R_SUCCEEDED(usbDsGetState(&state)) && state == UsbState_Configured;
}

Result TinfoilUsb::Transfer(bool read, void* data, size_t size, u64 timeout_ns) {
    auto p = static_cast<unsigned char*>(data);
    while (size) {
        const size_t n = std::min(size, kBufferSize);
        if (!read) std::memcpy(m_buffer, p, n);
        u32 urb = 0;
        R_UNLESS(Connected(), Result_NetworkLost);
        R_TRY(read ? usbCommsReadAsync(m_buffer, n, &urb, 0) : usbCommsWriteAsync(m_buffer, n, &urb, 0));
        Event* ev = read ? usbCommsGetReadCompletionEvent(0) : usbCommsGetWriteCompletionEvent(0);
        u64 waited = 0;
        while (R_FAILED(eventWait(ev, kSlice))) {
            waited += kSlice;
            if (m_cancel) R_THROW(Result_TransferCancelled);
            if (waited >= timeout_ns || !Connected()) R_THROW(Result_NetworkLost);
        }
        u32 got = 0;
        R_TRY(read ? usbCommsGetReadResult(urb, &got, 0) : usbCommsGetWriteResult(urb, &got, 0));
        R_UNLESS(got > 0 && got <= n, Result_NetworkLost);
        if (read) std::memcpy(p, m_buffer, got);
        p += got;
        size -= got;
    }
    R_SUCCEED();
}

bool TinfoilUsb::PollList(std::vector<std::string>& names) {
    std::lock_guard lock(m_mutex);
    if (!Connected()) return false;
    m_cancel = false;
    unsigned char header[16];
    if (R_FAILED(Transfer(true, header, sizeof(header), 500'000'000ull))) return false;
    const long long len = tinfoil::ListLength(header);
    if (len <= 0 || len > 4 * 1024 * 1024) return false;
    std::vector<char> list(static_cast<size_t>(len));
    if (R_FAILED(Transfer(true, list.data(), list.size(), 5'000'000'000ull))) return false;
    names = tinfoil::SplitList(list.data(), list.size());
    return !names.empty();
}

Result TinfoilUsb::ReadRange(const std::string& name, u64 offset, u64 size, void* out) {
    std::lock_guard lock(m_mutex);
    const auto request = tinfoil::RangeRequest(name, offset, size);
    // the header, the range and the name, each in its own transfer: the computer reads them so
    R_TRY(Transfer(false, const_cast<unsigned char*>(request.data()), 32, 10'000'000'000ull));
    R_TRY(Transfer(false, const_cast<unsigned char*>(request.data() + 32), 32, 10'000'000'000ull));
    R_TRY(Transfer(false, const_cast<unsigned char*>(request.data() + 64), request.size() - 64, 10'000'000'000ull));
    unsigned char answer[32];
    R_TRY(Transfer(true, answer, sizeof(answer), 30'000'000'000ull));
    const long long len = tinfoil::AnswerLength(answer);
    R_UNLESS(len >= 0 && u64(len) == size, Result_ServerRefused);
    return Transfer(true, out, size_t(size), 30'000'000'000ull);
}

void TinfoilUsb::SayDone() {
    {
        std::lock_guard lock(m_list_mutex);
        m_have_list = m_list_new = false; // the next list is waited for
    }
    std::lock_guard lock(m_mutex);
    if (!Connected()) return;
    const auto request = tinfoil::ExitRequest();
    Transfer(false, const_cast<unsigned char*>(request.data()), request.size(), 2'000'000'000ull);
}

Result TinfoilSource::Read(void* buf, s64 off, s64 size, u64* bytes_read) {
    *bytes_read = 0;
    auto out = static_cast<unsigned char*>(buf);
    // in pieces the computer's side sends at once
    while (size > 0) {
        const s64 n = std::min<s64>(size, s64(kBufferSize));
        R_TRY(m_usb.ReadRange(m_name, u64(off), u64(n), out));
        out += n;
        off += n;
        size -= n;
        *bytes_read += u64(n);
    }
    R_SUCCEED();
}

} // namespace nxdock
#endif
