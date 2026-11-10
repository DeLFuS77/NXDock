// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "mtp_proto.hpp"

#include <atomic>

namespace nxdock::mtp {

// The console as an MTP device on its USB port (usb:ds): a "still image" interface (class 6,
// subclass 1, protocol 1) with a bulk pair and an interrupt endpoint, as file explorers expect.
// The setup follows libnx's usb_comms.c (ISC licence); transfers wait in short slices so a stop
// or a pulled cable ends them.
class UsbTransport final : public Transport {
public:
    ~UsbTransport() override;
    bool Start();
    void Stop();      // ends a transfer waiting; Read and Write fail from then on
    bool Connected(); // configured by a computer
    // waits until a computer configures the console, or a stop
    bool WaitConnected();

    bool Read(void* buf, size_t size, size_t* got) override;
    bool Write(const void* buf, size_t size) override;
    bool WriteEnd() override;
    size_t PacketSize() const override { return m_packet; }

private:
    bool Transfer(bool in, void* buf, size_t size, size_t* done);
    bool m_started = false;
    std::atomic<bool> m_stop{false};
    size_t m_packet = 512;
    unsigned char* m_buffer = nullptr;
};

} // namespace nxdock::mtp
