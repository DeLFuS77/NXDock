// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "yati/source/base.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

// Installing over a USB cable from NS-USBloader (or anything that speaks the USB protocol of
// Tinfoil and Awoo): the computer sends the list of its files ("TUL0"), and the console asks
// for pieces of them ("TUC0", command 1) until it says it is done (command 0).
//
// The console side is libnx's usbComms (the usual 057E:3000 device), with every transfer
// posted asynchronously and waited for with a timeout, so a pulled cable or a cancel never
// leaves a thread stuck.
class TinfoilUsb {
public:
    ~TinfoilUsb();

    bool Start();
    void Stop();
    bool Started() const { return m_started; }
    // The cable is in and the computer configured the console.
    bool Connected();

    // Waits a little for the list of files (pick it up again until it comes); true with it.
    bool PollList(std::vector<std::string>& names);
    // A piece of a file of the list.
    Result ReadRange(const std::string& name, u64 offset, u64 size, void* out);
    // Tells the computer that everything asked for was installed: it stops sending.
    void SayDone();
    // Stops a transfer waiting.
    void Cancel() { m_cancel = true; }
    void ResetCancel() { m_cancel = false; }

    // A thread waits for the list of the computer while `on`; the list is taken once, and the
    // next one is waited for after SayDone.
    void Listen(bool on);
    bool TakeList(std::vector<std::string>& names);

private:
    void ListenLoop();
    std::thread m_thread;
    std::atomic<bool> m_listen{false}, m_quit{false};
    std::mutex m_list_mutex;
    std::vector<std::string> m_list;
    bool m_have_list = false, m_list_new = false;
    Result Transfer(bool read, void* data, size_t size, u64 timeout_ns);
    bool m_started = false;
    std::atomic<bool> m_cancel{false};
    std::mutex m_mutex;
    unsigned char* m_buffer = nullptr; // page aligned, for the transfers
};

// The one USB link, used by the USB screen and by the install queue.
TinfoilUsb& UsbLink();

// A file of the computer's list, read through the link.
class TinfoilSource final : public sphaira::yati::source::Base {
public:
    TinfoilSource(TinfoilUsb& usb, std::string name) : m_usb(usb), m_name(std::move(name)) { m_usb.ResetCancel(); }
    Result Read(void* buf, s64 off, s64 size, u64* bytes_read) override;
    void SignalCancel() override { m_usb.Cancel(); }

private:
    TinfoilUsb& m_usb;
    std::string m_name;
};

// The Tinfoil protocol as bytes, apart from the cable, to test on the computer.
namespace tinfoil {
constexpr unsigned kListMagic = 0x304C5554;    // "TUL0"
constexpr unsigned kCommandMagic = 0x30435554; // "TUC0"
constexpr unsigned kCmdExit = 0, kCmdFileRange = 1;
// The 16 bytes before the list: its length, or -1 when it is not a list.
long long ListLength(const unsigned char* header16);
// The names of a list (one per line).
std::vector<std::string> SplitList(const char* data, size_t size);
// The request of a piece: a 32-byte header, then 32 bytes of range and the name.
std::vector<unsigned char> RangeRequest(const std::string& name, unsigned long long offset, unsigned long long size);
std::vector<unsigned char> ExitRequest();
// The 32-byte header of the answer: the bytes that follow, or -1 when it is not one.
long long AnswerLength(const unsigned char* header32);
} // namespace tinfoil

} // namespace nxdock
