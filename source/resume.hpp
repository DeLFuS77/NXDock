// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Going on after the console went off half way through an install (23A of the visual test of
// phases 16 to 23). While a job installs, a journal on the SD card says what it is, what waited
// after it, and every part written so far: its placeholder, and whether its SHA-256 was checked.
// Nothing is registered until the end, as always; so when NXDock opens and finds a journal, the
// install did not end, and it offers to go on (the checked parts are kept, the others written
// again) or to discard what was written (every placeholder deleted).
//
// The journal is written whole each time, to the older of two files, with a number that grows
// and a checksum at its end: a cut while writing leaves the other file as it was. The decrypted
// header of each checked part is kept beside it, for the tickets it needs.
//
// Plain C++, tested on the computer, cuts at every byte included.
#include <map>
#include <string>
#include <vector>

namespace nxdock {

// A job, as fields: what install_queue.cpp writes of an InstallJob (never the key).
struct ResumeJob {
    std::map<std::string, std::string> fields;
    std::vector<std::string> parts; // the parts of a split file
};

// A part written: its content id and placeholder id (32 hexadecimal digits each).
struct ResumePart {
    std::string content_id, placeholder;
    long long size = 0;
    bool checked = false; // its SHA-256 matched and it was flushed to the card
    bool sd = true;       // in the SD card, or the internal memory
};

struct ResumeState {
    unsigned long long seq = 0; // grows with every save
    ResumeJob job;
    std::vector<ResumeJob> waiting;
    std::vector<ResumePart> parts;
};

// The text of a journal, and back; Parse fails on anything not whole (a cut, a changed byte).
std::string FormatResume(const ResumeState& state);
bool ParseResume(const std::string& text, ResumeState& state);

// In a folder: Save raises seq and writes the older of the two files; Load takes the newest whole
// one; Clear deletes both and the headers.
bool SaveResume(const std::string& dir, ResumeState& state);
bool LoadResume(const std::string& dir, ResumeState& state);
void ClearResume(const std::string& dir);
bool SaveResumeHeader(const std::string& dir, const std::string& content_id, const void* data, size_t size);
bool LoadResumeHeader(const std::string& dir, const std::string& content_id, void* data, size_t size);

// What the install core tells (a part made, or checked) written into the state.
void ResumeNote(ResumeState& state, const std::string& content_id, const std::string& placeholder, long long size, bool checked, bool sd);
// The placeholder of a part checked before, in that storage; false when there is none.
bool ResumeLookup(const ResumeState& state, const std::string& content_id, bool sd, std::string& placeholder);
// Going on: the parts cut half way, whose placeholders go, taken out of the state.
std::vector<ResumePart> ResumeTakeCut(ResumeState& state);

// A part of the state by content id, added when missing.
ResumePart& ResumePartOf(ResumeState& state, const std::string& content_id);
// The checked parts, and their bytes.
int ResumeChecked(const ResumeState& state, long long* bytes = nullptr);

std::string ResumeHex(const void* data, size_t size);
bool ResumeUnhex(const std::string& hex, void* out, size_t size);

} // namespace nxdock
