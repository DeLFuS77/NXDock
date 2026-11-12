// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Timing of NXDock for development, built only with -DNXDOCK_PERF=ON: the boot in steps, every
// frame cut in its parts, and a script of buttons and touches read from the SD card so two builds
// can be compared doing the same. Written to sdmc:/switch/NXDock/perf.log. Without the option all
// of it is gone.

#include <switch.h>

namespace nxdock::perf {

enum Stage { kUpdate, kAcquire, kUpload, kDraw, kPresent, kStages };

#ifdef NXDOCK_PERF
void Mark(const char* what);
void FrameBegin();
void Lap(Stage stage);
void Count(int n);
// how long something took, added up by name (decoding a cover, reading the catalog)
void Took(const char* what, double ms);
// the script (sdmc:/switch/NXDock/perf-script.txt): buttons held this frame, and a touch
void Script(u64& held, bool& quit);
bool ScriptTouch(HidTouchScreenState& state);
// the touch read from the console, or the one of the script
void ReadTouch(HidTouchScreenState& state);
double Ms(u64 ticks);
#else
inline void Mark(const char*) {}
inline void FrameBegin() {}
inline void Lap(Stage) {}
inline void Count(int) {}
inline void Took(const char*, double) {}
inline void Script(u64&, bool&) {}
inline void ReadTouch(HidTouchScreenState& state) { hidGetTouchScreenStates(&state, 1); }
#endif

// Times a block with Took.
struct Timer {
#ifdef NXDOCK_PERF
    const char* what;
    u64 start;
    explicit Timer(const char* w) : what(w), start(armGetSystemTick()) {}
    ~Timer() { Stop(); }
    void Stop() {
        if (what) Took(what, Ms(armGetSystemTick() - start));
        what = nullptr;
    }
#else
    explicit Timer(const char*) {}
    void Stop() {}
#endif
};

} // namespace nxdock::perf
