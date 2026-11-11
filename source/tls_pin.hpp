// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxdock::tls {

// HTTPS with a pinned key. A library app behind HTTPS often has a certificate of its own, which
// the console cannot check against any authority. So the first time NXDock pairs with it, it
// keeps the hash of the app's public key ("trust on first use"), and from then on every request
// to that app is refused unless the app shows that same key: nobody in between can take the
// console key.

// The pin in use ("sha256//<base64>"), empty for none; set when a library is linked.
// `url` is the address of the library: the pin is used only for that origin (a shop on https is
// another server, with its own certificate).
void SetPin(const std::string& pin, const std::string& url = "");
std::string Pin();

// Sets up a curl handle for the address: with https, the pinned key when there is one (the
// first contact takes any key, to read it); nothing for http.
void Apply(void* curl, const std::string& url);

// Reads the public key of the app at an https address and gives its pin; empty on failure.
std::string ReadPin(const std::string& url, std::string& error);

// --- the pieces, tested on the computer
std::vector<uint8_t> Base64Decode(const std::string& text);
std::string Base64Encode(const uint8_t* data, size_t size);
// The DER of the first certificate in a PEM text.
std::vector<uint8_t> PemToDer(const std::string& pem);
// The SubjectPublicKeyInfo of a certificate, as it is (the bytes curl hashes for a pin).
std::vector<uint8_t> SpkiOfCertificate(const std::vector<uint8_t>& der);
void Sha256(const uint8_t* data, size_t size, uint8_t out[32]);
// "sha256//" + base64 of the SHA-256 of the key of a PEM certificate; empty when not one.
std::string PinOfPem(const std::string& pem);

} // namespace nxdock::tls
