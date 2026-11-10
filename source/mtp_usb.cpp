// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The device setup follows _usbCommsInterfaceInit5x and usbCommsInitializeEx of libnx
// (nx/source/runtime/devices/usb_comms.c, ISC licence, Copyright (c) 2017-2026 libnx Authors).
#include "mtp_usb.hpp"

#include <switch.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace nxdock::mtp {
namespace {

constexpr size_t kBuffer = 4 * 1024 * 1024;
constexpr u64 kSlice = 50'000'000ull;

UsbDsInterface* g_interface = nullptr;
UsbDsEndpoint *g_in = nullptr, *g_out = nullptr, *g_event = nullptr;

Result Setup() {
    Result rc = usbDsInitialize();
    if (R_FAILED(rc)) return rc;
    u8 manufacturer = 0, product = 0, serial = 0;
    static const u16 langs[1] = {0x0409};
    rc = usbDsAddUsbLanguageStringDescriptor(nullptr, langs, 1);
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&manufacturer, "DeLFuS77");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&product, "NXDock");
    if (R_SUCCEEDED(rc)) rc = usbDsAddUsbStringDescriptor(&serial, "0000000000000001");
    usb_device_descriptor device = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0110,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = 0x40,
        .idVendor = 0x057E,
        .idProduct = 0x201D,
        .bcdDevice = 0x0100,
        .iManufacturer = manufacturer,
        .iProduct = product,
        .iSerialNumber = serial,
        .bNumConfigurations = 0x01,
    };
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device);
    device.bcdUSB = 0x0200;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device);
    device.bcdUSB = 0x0300;
    device.bMaxPacketSize0 = 0x09;
    if (R_SUCCEEDED(rc)) rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device);
    u8 bos[0x16] = {0x05, USB_DT_BOS, 0x16, 0x00, 0x02, 0x07, USB_DT_DEVICE_CAPABILITY, 0x02, 0x02, 0x00, 0x00, 0x00,
                    0x0A, USB_DT_DEVICE_CAPABILITY, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00};
    if (R_SUCCEEDED(rc)) rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));
    if (R_SUCCEEDED(rc)) rc = usbDsRegisterInterface(&g_interface);
    if (R_FAILED(rc)) return rc;

    // the still image class, as MTP devices are
    usb_interface_descriptor intf = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = g_interface->interface_index,
        .bAlternateSetting = 0,
        .bNumEndpoints = 3,
        .bInterfaceClass = 0x06,
        .bInterfaceSubClass = 0x01,
        .bInterfaceProtocol = 0x01,
        .iInterface = 0,
    };
    usb_endpoint_descriptor in = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = u8(USB_ENDPOINT_IN + intf.bInterfaceNumber + 1),
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = 0x40,
        .bInterval = 0,
    };
    usb_endpoint_descriptor out = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = u8(USB_ENDPOINT_OUT + intf.bInterfaceNumber + 1),
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = 0x40,
        .bInterval = 0,
    };
    usb_endpoint_descriptor event = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = u8(USB_ENDPOINT_IN + intf.bInterfaceNumber + 2),
        .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
        .wMaxPacketSize = 0x18,
        .bInterval = 0x06,
    };
    usb_ss_endpoint_companion_descriptor companion = {
        .bLength = sizeof(usb_ss_endpoint_companion_descriptor),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F,
        .bmAttributes = 0x00,
        .wBytesPerInterval = 0x00,
    };
    usb_ss_endpoint_companion_descriptor event_companion = companion;
    event_companion.bMaxBurst = 0;
    event_companion.wBytesPerInterval = 0x18;

    const auto add = [](UsbDeviceSpeed speed, const void* d, u32 size) { return usbDsInterface_AppendConfigurationData(g_interface, speed, d, size); };
    for (const auto speed : {UsbDeviceSpeed_Full, UsbDeviceSpeed_High, UsbDeviceSpeed_Super}) {
        const u16 packet = speed == UsbDeviceSpeed_Full ? 0x40 : speed == UsbDeviceSpeed_High ? 0x200 : 0x400;
        in.wMaxPacketSize = out.wMaxPacketSize = packet;
        const bool super = speed == UsbDeviceSpeed_Super;
        if (R_SUCCEEDED(rc)) rc = add(speed, &intf, USB_DT_INTERFACE_SIZE);
        if (R_SUCCEEDED(rc)) rc = add(speed, &in, USB_DT_ENDPOINT_SIZE);
        if (R_SUCCEEDED(rc) && super) rc = add(speed, &companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        if (R_SUCCEEDED(rc)) rc = add(speed, &out, USB_DT_ENDPOINT_SIZE);
        if (R_SUCCEEDED(rc) && super) rc = add(speed, &companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
        if (R_SUCCEEDED(rc)) rc = add(speed, &event, USB_DT_ENDPOINT_SIZE);
        if (R_SUCCEEDED(rc) && super) rc = add(speed, &event_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
    }
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_interface, &g_in, in.bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_interface, &g_out, out.bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_RegisterEndpoint(g_interface, &g_event, event.bEndpointAddress);
    if (R_SUCCEEDED(rc)) rc = usbDsInterface_EnableInterface(g_interface);
    if (R_SUCCEEDED(rc)) rc = usbDsEnable();
    return rc;
}

} // namespace

UsbTransport::~UsbTransport() {
    Stop();
}

bool UsbTransport::Start() {
    if (m_started) return true;
    m_stop = false;
    if (!m_buffer) m_buffer = static_cast<unsigned char*>(aligned_alloc(0x1000, kBuffer));
    if (!m_buffer) return false;
    if (R_FAILED(Setup())) {
        usbDsExit();
        g_interface = nullptr;
        return false;
    }
    m_started = true;
    return true;
}

void UsbTransport::Stop() {
    m_stop = true;
    if (m_started) {
        if (g_in) usbDsEndpoint_Cancel(g_in);
        if (g_out) usbDsEndpoint_Cancel(g_out);
        usbDsExit();
        g_interface = nullptr;
        g_in = g_out = g_event = nullptr;
        m_started = false;
    }
    std::free(m_buffer);
    m_buffer = nullptr;
}

bool UsbTransport::Connected() {
    UsbState state = UsbState_Detached;
    return m_started && R_SUCCEEDED(usbDsGetState(&state)) && state == UsbState_Configured;
}

bool UsbTransport::WaitConnected() {
    while (!m_stop) {
        if (Connected()) {
            UsbDeviceSpeed speed = UsbDeviceSpeed_High;
            if (R_SUCCEEDED(usbDsGetSpeed(&speed))) m_packet = speed == UsbDeviceSpeed_Super ? 1024 : speed == UsbDeviceSpeed_Full ? 64 : 512;
            return true;
        }
        svcSleepThread(100'000'000ull);
    }
    return false;
}

bool UsbTransport::Transfer(bool in, void* buf, size_t size, size_t* done) {
    UsbDsEndpoint* ep = in ? g_in : g_out;
    if (!m_started || m_stop || !ep) return false;
    u32 urb = 0;
    if (R_FAILED(usbDsEndpoint_PostBufferAsync(ep, buf, size, &urb))) return false;
    // waits in slices: a stop, or the cable pulled, ends it
    while (R_FAILED(eventWait(&ep->CompletionEvent, kSlice))) {
        if (m_stop || !Connected()) {
            usbDsEndpoint_Cancel(ep);
            eventWait(&ep->CompletionEvent, kSlice);
            eventClear(&ep->CompletionEvent);
            return false;
        }
    }
    eventClear(&ep->CompletionEvent);
    UsbDsReportData report;
    u32 transferred = 0;
    if (R_FAILED(usbDsEndpoint_GetReportData(ep, &report)) || R_FAILED(usbDsParseReportData(&report, urb, nullptr, &transferred))) return false;
    *done = transferred;
    return true;
}

bool UsbTransport::Read(void* buf, size_t size, size_t* got) {
    const size_t n = std::min(size, kBuffer);
    size_t done = 0;
    if (!Transfer(false, m_buffer, n, &done)) return false;
    std::memcpy(buf, m_buffer, std::min(done, n));
    *got = std::min(done, n);
    return true;
}

bool UsbTransport::Write(const void* buf, size_t size) {
    auto p = static_cast<const unsigned char*>(buf);
    while (size) {
        const size_t n = std::min(size, kBuffer);
        std::memcpy(m_buffer, p, n);
        size_t done = 0;
        if (!Transfer(true, m_buffer, n, &done) || done != n) return false;
        p += n;
        size -= n;
    }
    return true;
}

bool UsbTransport::WriteEnd() {
    size_t done = 0;
    return Transfer(true, m_buffer, 0, &done);
}

} // namespace nxdock::mtp
