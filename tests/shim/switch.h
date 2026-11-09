// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The few libnx names the network source uses, so it can be tested on the computer.
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res) ((res) != 0)
#define R_MODULE(res) ((res) & 0x1FF)
#define R_DESCRIPTION(res) (((res) >> 9) & 0x1FFF)
#define MAKERESULT(module, description) ((((module) & 0x1FF)) | ((description) & 0x1FFF) << 9)

enum { Module_Libnx = 345 };
enum { LibnxError_NotFound = 1 };

inline void svcSleepThread(s64 ns) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

struct UEvent {
    int signalled;
};
inline void ueventCreate(UEvent* e, bool) { e->signalled = 0; }
inline void ueventSignal(UEvent* e) { e->signalled = 1; }
