// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for Sphaira's ui::ProgressBox. Sphaira draws a dialog; NXDock's install
// queue reads these counters from the interface thread instead, every frame, to show the four
// stages of the queue: network, decompress, write and check. Everything here is safe to read
// from another thread.
#pragma once

#include "defines.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace sphaira::ui {

class ProgressBox {
public:
    enum class Phase : int {
        Starting,    // opening services, reading the container
        Reading,     // reading the cnmt of each content
        Installing,  // downloading, decompressing, writing and checking the NCAs
        Registering, // all checked: tickets, records
    };

    ProgressBox() { ueventCreate(&m_cancel_event, false); }
    ProgressBox(const ProgressBox&) = delete;
    ProgressBox& operator=(const ProgressBox&) = delete;

    // --- asked by the interface
    void RequestCancel() {
        m_cancel = true;
        ueventSignal(&m_cancel_event);
    }
    bool Cancelled() const { return m_cancel.load(); }

    Phase GetPhase() const { return m_phase.load(); }
    u64 NetBytes() const { return m_net.load(); }
    u64 ReadyBytes() const { return m_ready.load(); }
    u64 WrittenBytes() const { return m_done_bytes.load() + m_current.load(); }
    u64 TotalBytes() const { return m_total.load(); }
    u32 NcasChecked() const { return m_checked.load(); }
    u32 NcasTotal() const { return m_ncas.load(); }
    int Threads() const { return m_threads.load(); }
    // set when the install stopped for room or firmware: what it needed.
    u64 NeededBytes() const { return m_needed.load(); }
    u32 NeededFirmware() const { return m_needed_fw.load(); }
    std::string Current() {
        std::lock_guard lock(m_mutex);
        return m_what;
    }

    // --- told by the install core and the source
    Result ShouldExitResult() const { return m_cancel ? Result(Result_TransferCancelled) : Result(0); }
    UEvent* GetCancelEvent() { return &m_cancel_event; }

    void NewTransfer(const std::string& what) {
        m_done_bytes += m_current.exchange(0);
        std::lock_guard lock(m_mutex);
        m_what = what;
    }
    void UpdateTransfer(s64 offset, s64 /*size*/) { m_current = u64(offset); }
    void Yield() { svcSleepThread(1000000); }

    void SetPhase(Phase phase) { m_phase = phase; }
    void SetPlan(u64 total_bytes, u32 ncas) {
        m_total = total_bytes;
        m_ncas = ncas;
    }
    void SetNeeded(u64 bytes, u32 firmware) {
        m_needed = bytes;
        m_needed_fw = firmware;
    }
    void SetThreads(int threads) { m_threads = threads; }
    void AddNet(u64 bytes) { m_net += bytes; }
    void AddReady(u64 bytes) { m_ready += bytes; }
    void NcaChecked() { m_checked++; }

private:
    UEvent m_cancel_event{};
    std::atomic<bool> m_cancel{false};
    std::atomic<Phase> m_phase{Phase::Starting};
    std::atomic<u64> m_net{0}, m_ready{0}, m_done_bytes{0}, m_current{0}, m_total{0}, m_needed{0};
    std::atomic<u32> m_checked{0}, m_ncas{0}, m_needed_fw{0};
    std::atomic<int> m_threads{-1};
    std::mutex m_mutex;
    std::string m_what;
};

} // namespace sphaira::ui
