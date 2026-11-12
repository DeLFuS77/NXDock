// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The check of a new version's signature, on the computer: a signature made by tools/minisign (the
// one releasesign uses) is taken; a changed file, a changed trusted comment, another key or a
// broken text are refused; and the keys of release.pub are read.
#include "minisig.hpp"

#include <cstdio>
#include <string>

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

// made with minisign.Sign of tools/minisign from a fixed seed (1, 2, … 32) and the ID 1…8
static const char* kPub = "untrusted comment: minisign public key 0807060504030201\n"
                          "RWQBAgMEBQYHCHm1Vi6P5lT5QHixEuipi6eQH4U65pW+1+DjkQutBJZk\n";
static const char* kSig = "untrusted comment: signature from the key of NXDock\n"
                          "RUQBAgMEBQYHCOsGe7eU7+YWPrzmisDUsILm0JjNlZcrEQNbHS8NrYtdln4LqU+8G2yoObqkGbI12qYODxlU82Cti8RY0stYSgE=\n"
                          "trusted comment: NXDock 0.9.1\n"
                          "8/LtkQp4CS6ig/rCOApK0r41s9hAdZR/o6kkBzTyhfgJB0G+wx2tT0fnJxpmhh8orq1FgFi20FHfLsxYCCYFCA==\n";
static const char* kData = "aaaa  NXDock.nro\nbbbb  NXDock-0.9.1-sd.zip\n";

int main() {
    // the keys: one in the text of release.pub, none in its comments
    const auto keys = minisig::ParseKeys(kPub);
    CHECK(keys.size() == 1);
    CHECK(minisig::ParseKeys("# The public keys the releases of NXDock are signed with\n").empty());
    CHECK(minisig::ParseKeys("RWQBAgMEBQYHCHm1Vi6P5lT5QHixEuipi6eQH4U65pW+1+DjkQutBJZk RWQBAgMEBQYHCHm1Vi6P5lT5QHixEuipi6eQH4U65pW+1+DjkQutBJZk").size() == 2);

    std::string comment;
    CHECK(minisig::Verify(kData, kSig, keys, comment));
    CHECK(comment == "NXDock 0.9.1");

    // a file changed by one letter
    std::string data = kData;
    data[0] = 'b';
    CHECK(!minisig::Verify(data, kSig, keys, comment));
    // the trusted comment changed (an older release passed for a newer one)
    std::string sig = kSig;
    sig.replace(sig.find("0.9.1"), 5, "0.9.2");
    CHECK(!minisig::Verify(kData, sig, keys, comment));
    // no keys, or a text that is no signature
    CHECK(!minisig::Verify(kData, kSig, {}, comment));
    CHECK(!minisig::Verify(kData, "untrusted comment: x\nnot base64!\ntrusted comment: y\nz\n", keys, comment));
    CHECK(!minisig::Verify(kData, "", keys, comment));
    // CRLF line ends are fine
    std::string crlf;
    for (const char* p = kSig; *p; p++) {
        if (*p == '\n') crlf += '\r';
        crlf += *p;
    }
    CHECK(minisig::Verify(kData, crlf, keys, comment));

    // base64
    std::vector<uint8_t> out;
    CHECK(minisig::Base64("aGVsbG8=", out) && std::string(out.begin(), out.end()) == "hello");
    CHECK(!minisig::Base64("aGVs*G8=", out));

    if (failures) {
        std::printf("minisig_test: %d failed\n", failures);
        return 1;
    }
    std::printf("minisig_test: ok\n");
    return 0;
}
