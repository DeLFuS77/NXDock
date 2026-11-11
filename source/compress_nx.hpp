// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// "Compress games" (24A, experimental, off until the user turns it on). The RomFS of the program
// NCA of an installed game is compressed in place with the LZ4 layer of the NCA (firmware 12 and
// later) and the LZ4 HC of Nintendo at level 9, as the games Nintendo ships compressed
// (romfs_lz4.hpp): one NCA at a time, so no double room is needed; the NCA is written again and
// encrypted, with its IVFC made again for the data as it is stored, its CNMT made again with the
// new ids, and both registered; only then does the original go. A failure before that leaves the
// game as it was.
//  - A game Nintendo compressed with its fast level 0 is packed again at level 9 (25A); one it
//    compressed with its HC levels is as small as LZ4 gets, and stays.
//  - A game with its update installed is merged with it (25D, merge_game): the update reads the
//    base game by the places of its stored bytes, so the base cannot be written again alone. One
//    program NCA is made from the two (what the update no longer reads left out, what it reads more
//    than once stored once), with a CNMT of one Application of the version of the update and no
//    required version; the database and the record of the game are changed to it, the console
//    reads the whole RomFS through its own file system, and only then do the base and the update
//    go (a failure puts everything back). An official update cannot be installed over it (25E).
//  - The firmware the game needs is not raised (a merged game asks at least 12.0.0, the one of the
//    LZ4 layer, as its CNMT is made again).
//  - The NCA header is no longer signed by Nintendo: the game needs sigpatches (compress_meta).
// An analysis first, from a sample of the RomFS of each game, says what it would save.
#include "compress_meta.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nxdock {

constexpr const char* kCompressedList = "sdmc:/switch/NXDock/compressed.txt";
constexpr double kCompressWorth = 0.10; // offered from 10 % saved

struct GameEstimate {
    enum class State { Pending, Possible, Worth, Low, Already, Ours, Unsupported };
    State state = State::Pending;
    double gain = 0;               // share of the RomFS saved, from a sample
    unsigned long long bytes = 0;  // the program NCA now
    unsigned long long saved = 0;  // what it would save (Worth, Low), or saved (Ours)
    // Update: being measured with its update; unsupported, a newer update over a merged game that
    // needs its base game, which the library has not (nothing is changed)
    enum class Why { None, NotFound, Sparse, Key, Read, Update };
    Why why = Why::None;           // Unsupported: why
};

class Compressor {
public:
    struct View {
        bool running = false;
        bool analyzing = false;
        std::map<unsigned long long, GameEstimate> games;
        int check_done = 0, check_total = 0;     // the headers read (every game)
        int measure_done = 0, measure_total = 0; // the samples read (those that can be compressed)
        int phase = 0; // 1 measuring, 2 writing, 3 registering
        unsigned long long done = 0, total = 0;
        unsigned long long app = 0;   // the game being compressed, or the last one
        unsigned long long saved = 0; // of the last one, when it went well
        std::string error;            // packed (errors.hpp); empty when it went well
        int finished = 0;             // counts the compressions that ended
    };

    ~Compressor();
    // What each game would save, on a thread; games already known are not analysed again.
    bool StartAnalysis(std::vector<unsigned long long> apps);
    // One game, on a thread; false while something else runs.
    bool StartCompress(unsigned long long app);
    void Stop();
    View Snapshot();
    bool Busy();
    // The marks of the games NXDock compressed, checked against what is installed now.
    static std::vector<CompressedGame> Marks();

private:
    void Join();
    std::thread m_thread;
    std::mutex m_mutex;
    View m_view;
    std::atomic<bool> m_stop{false};
};

} // namespace nxdock
