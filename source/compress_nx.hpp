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
//  - The key of a game's ticket is opened with the user's prod.keys (sdmc:/switch/prod.keys), read only
//    here, kept in memory, never shown or saved; without it the screen and the error say so.
// An analysis first, from a sample of the RomFS of each game, says what it would save.
#include "compress_meta.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
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
    // Redo: a newer update over a game merged when Nintendo had compressed it; it is undone (the
    // original and the update installed again) and merged again
    enum class Why { None, NotFound, Sparse, Key, Read, Update, Redo };
    Why why = Why::None;           // Unsupported: why
    // With its update installed, the game is merged with it ("Merge and compress", 25F): `bytes`
    // and `saved` count the base and the update together. `merged`: NXDock merged it already, and
    // a newer update installed over it (from the eShop, say) waits to be merged in the same way.
    bool merge = false, merged = false;
    unsigned patch = 0;            // the version of the update installed
    // Its DLC go with it, in the same row and the same step (4A): those that save 10 % or more are
    // compressed after the game, and `saved` counts them too.
    int dlc_total = 0;
    std::vector<unsigned long long> dlcs;
    unsigned long long dlc_saved = 0;
};

// A game NXDock merged with its update (it keeps its recipe): a newer update installed over it is
// merged in the same way, right after it is installed (25G).
bool IsMergedGame(unsigned long long app);
// Whether a newer update can be merged over it: false for one merged when Nintendo had compressed
// it (it keeps no recipe).
bool CanMergeAgain(unsigned long long app);
// Undo of a merged game: its recipe, what was kept of its base and its mark go, so the original
// installed again is not merged by itself, and can be merged anew.
void ForgetMerged(unsigned long long app);

// Held while games are written again (Compress games) and while the queue installs or removes:
// the two never change the contents and records of the console at the same time.
std::recursive_mutex& TitlesLock();
// Merges the update installed over a game with it (25D, 25E), on the calling thread: progress gets
// the phase (1 measuring, 2 writing, 3 registering and checking), what is done and of how much.
// Gives the error to show (errors.hpp), empty when it went well; `saved` what it saved.
std::string MergeUpdate(unsigned long long app, const std::atomic<bool>& stop, const std::function<void(int, uint64_t, uint64_t)>& progress,
                        uint64_t& saved);

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
