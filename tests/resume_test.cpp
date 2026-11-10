// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The journal of an install that may be cut by the console going off (23A): written and read
// back whole, refused when cut at any byte or changed, and a cut while saving leaves the journal
// before it to go on from.
#include "resume.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static ResumeState Sample() {
    ResumeState s;
    s.job.fields = {{"game", "Game \\ with\nnew line"}, {"label", "Update v65536"}, {"origin", "0"}, {"nand", "0"}, {"size", "123456789"}};
    s.job.parts = {"sdmc:/games/a.nsp.00", "sdmc:/games/a.nsp.01"};
    ResumeJob w;
    w.fields = {{"game", "Next"}, {"origin", "1"}};
    s.waiting.push_back(w);
    s.parts.push_back({"0123456789ABCDEF0123456789ABCDEF", "FEDCBA9876543210FEDCBA9876543210", 1000, true, true});
    s.parts.push_back({"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB", 2000, false, false});
    return s;
}

static bool Exists(const std::string& p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

int main(int argc, char** argv) {
    const std::string tmp = argc > 1 ? argv[1] : "/tmp";
    {
        // written and read back whole
        ResumeState s = Sample();
        s.seq = 7;
        ResumeState r;
        CHECK(ParseResume(FormatResume(s), r));
        CHECK(r.seq == 7 && r.job.fields == s.job.fields && r.job.parts == s.job.parts && r.waiting.size() == 1 &&
              r.waiting[0].fields.at("game") == "Next" && r.parts.size() == 2 && r.parts[0].checked && r.parts[0].sd &&
              !r.parts[1].checked && !r.parts[1].sd && r.parts[1].size == 2000);
        long long bytes = 0;
        CHECK(ResumeChecked(r, &bytes) == 1 && bytes == 1000);
    }
    {
        // cut at every byte, or a byte changed: refused, never half read
        const std::string text = FormatResume(Sample());
        int accepted = 0;
        for (size_t n = 0; n < text.size(); n++) {
            ResumeState r;
            if (ParseResume(text.substr(0, n), r)) accepted++;
        }
        CHECK(accepted == 0);
        std::string changed = text;
        changed[text.size() / 2] ^= 1;
        ResumeState r;
        CHECK(!ParseResume(changed, r));
        CHECK(!ParseResume("", r) && !ParseResume("NXDOCK-RESUME 1\n", r));
    }
    {
        // two files: the newest whole one is read; a cut while saving leaves the one before
        const std::string dir = tmp + "/resume_journal";
        ClearResume(dir);
        ResumeState s = Sample();
        CHECK(SaveResume(dir, s) && s.seq == 1);
        s.parts[1].checked = true;
        CHECK(SaveResume(dir, s) && s.seq == 2);
        ResumeState r;
        CHECK(LoadResume(dir, r) && r.seq == 2 && r.parts[1].checked);
        // the third save cut at every byte: the second stays
        ResumeState next = s;
        next.seq++;
        next.parts.push_back({"CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC", "DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD", 3000, false, true});
        const std::string text = FormatResume(next);
        const std::string slot = dir + "/journal-" + std::to_string(next.seq % 2) + ".txt";
        int wrong = 0;
        for (size_t n = 0; n < text.size(); n += 7) {
            FILE* f = std::fopen(slot.c_str(), "wb");
            std::fwrite(text.data(), 1, n, f);
            std::fclose(f);
            ResumeState after;
            if (!LoadResume(dir, after) || after.seq != 2 || after.parts.size() != 2) wrong++;
        }
        CHECK(wrong == 0);
        // and once whole, the third is read
        CHECK(SaveResume(dir, s) && s.seq == 3);
        CHECK(LoadResume(dir, r) && r.seq == 3);
        // the headers of the parts
        unsigned char header[0xC00], back[0xC00];
        for (size_t i = 0; i < sizeof(header); i++) header[i] = (unsigned char)(i * 7);
        CHECK(SaveResumeHeader(dir, s.parts[0].content_id, header, sizeof(header)));
        CHECK(LoadResumeHeader(dir, s.parts[0].content_id, back, sizeof(back)) && std::equal(header, header + sizeof(header), back));
        CHECK(!LoadResumeHeader(dir, s.parts[1].content_id, back, sizeof(back)));
        // discarded: nothing left
        ClearResume(dir);
        CHECK(!Exists(dir));
        CHECK(!LoadResume(dir, r));
    }
    {
        // an install of four parts, the console going off after every step of it: on going on,
        // the parts checked by then are kept, the one cut half way is deleted, and nothing else
        const std::string dir = tmp + "/resume_cuts";
        const char* ids[4] = {"11111111111111111111111111111111", "22222222222222222222222222222222", "33333333333333333333333333333333",
                              "44444444444444444444444444444444"};
        // the steps: part i made (2i), part i checked (2i+1)
        int wrong = 0;
        for (int cut = 0; cut <= 8; cut++) {
            ClearResume(dir);
            ResumeState s;
            s.job.fields = {{"game", "Game"}};
            SaveResume(dir, s);
            for (int step = 0; step < cut; step++) {
                const int i = step / 2;
                const std::string ph(32, char('A' + i));
                if (step % 2 == 1) SaveResumeHeader(dir, ids[i], "header", 6);
                ResumeNote(s, ids[i], ph, 1000 * (i + 1), step % 2 == 1, true);
                SaveResume(dir, s);
            }
            // the console went off here; NXDock opens again
            ResumeState found;
            if (!LoadResume(dir, found)) {
                wrong++;
                continue;
            }
            const auto cut_parts = ResumeTakeCut(found);
            const int checked = cut / 2, half = cut % 2;
            if (int(found.parts.size()) != checked || int(cut_parts.size()) != half) wrong++;
            for (int i = 0; i < 4; i++) {
                std::string ph;
                const bool reused = ResumeLookup(found, ids[i], true, ph);
                if (reused != (i < checked) || (reused && ph != std::string(32, char('A' + i)))) wrong++;
                if (ResumeLookup(found, ids[i], false, ph)) wrong++; // never from the other storage
                char header[6];
                if (reused && !LoadResumeHeader(dir, ids[i], header, 6)) wrong++;
            }
        }
        CHECK(wrong == 0);
        ClearResume(dir);
    }
    {
        unsigned char id[16];
        CHECK(ResumeUnhex("00112233445566778899AABBCCDDEEFF", id, 16) && id[15] == 0xFF && ResumeHex(id, 16) == "00112233445566778899AABBCCDDEEFF");
        CHECK(!ResumeUnhex("0011", id, 16) && !ResumeUnhex("ZZ112233445566778899AABBCCDDEEFF", id, 16));
        ResumeState s;
        ResumePartOf(s, "A").size = 5;
        ResumePartOf(s, "A").checked = true;
        CHECK(s.parts.size() == 1 && s.parts[0].size == 5 && s.parts[0].checked);
    }
    std::printf(failures ? "%d failed\n" : "resume: all passed\n", failures);
    return failures ? 1 : 0;
}
