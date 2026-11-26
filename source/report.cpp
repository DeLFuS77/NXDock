// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "report.hpp"

#include "brand.hpp"
#include "zip.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <unordered_map>

#ifdef __SWITCH__
#include <switch.h>
#include "http.hpp"
#endif

namespace nxdock::report {
namespace {

bool EndsWithNoCase(const std::string& s, const char* end) {
    const size_t n = std::strlen(end);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower((unsigned char)s[s.size() - n + i]) != std::tolower((unsigned char)end[i])) return false;
    }
    return true;
}

struct Found {
    std::string name;
    unsigned long long size = 0;
    long long mtime = 0;
};

// The plain files right in `dir` whose name ends with `ext`; no folder below it is opened.
std::vector<Found> List(const std::string& dir, const char* ext) {
    std::vector<Found> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.' || !EndsWithNoCase(name, ext)) continue;
        struct stat st{};
        if (stat((dir + "/" + name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        out.push_back({name, (unsigned long long)st.st_size, (long long)st.st_mtime});
    }
    closedir(d);
    return out;
}

std::string Bytes(long long b) {
    if (b < 0) return "unknown";
    char t[64];
    if (b >= (1ll << 30)) std::snprintf(t, sizeof(t), "%.2f GiB (%lld bytes)", double(b) / double(1ll << 30), b);
    else std::snprintf(t, sizeof(t), "%.1f MiB (%lld bytes)", double(b) / double(1ll << 20), b);
    return t;
}

// --- what is taken out of the files (report.hpp) ---

constexpr const char* kAddress = "<address>";
constexpr const char* kGame = "<game>";
constexpr const char* kFile = "<file>";
constexpr const char* kFolder = "<folder>";

bool Alnum(char c) { return std::isalnum((unsigned char)c) != 0; }
bool Alpha(char c) { return std::isalpha((unsigned char)c) != 0; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Hex(char c) { return std::isxdigit((unsigned char)c) != 0; }
bool Space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

bool SameNoCase(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

bool OneOf(const std::string& word, std::initializer_list<const char*> list) {
    for (const char* w : list) {
        if (SameNoCase(word, w)) return true;
    }
    return false;
}

bool At(const std::string& s, size_t at, const char* what) { return s.compare(at, std::strlen(what), what) == 0; }

// The 16 hex digits of a title ID at `at`.
bool TitleIdAt(const std::string& s, size_t at) {
    if (s.size() < at + 16) return false;
    for (size_t i = at; i < at + 16; i++) {
        if (!Hex(s[i])) return false;
    }
    return s.size() == at + 16 || !Hex(s[at + 16]);
}

// scheme://... up to a space or a quote, without the stop that ends a sentence
size_t UrlEnd(const std::string& t, size_t i) {
    if (i > 0 && (Alnum(t[i - 1]) || t[i - 1] == '+' || t[i - 1] == '-' || t[i - 1] == '.')) return 0;
    size_t j = i;
    while (j < t.size() && (Alnum(t[j]) || t[j] == '+' || t[j] == '-' || t[j] == '.')) j++;
    if (!At(t, j, "://")) return 0;
    j += 3;
    while (j < t.size() && !Space(t[j]) && t[j] != '"' && t[j] != '\'' && t[j] != '<' && t[j] != '>' && t[j] != '`') j++;
    while (j > i && std::strchr(".,;:!?)]}", t[j - 1])) j--;
    return j;
}

// sha256//<base64>: the pin of a key, as link.log writes it
size_t PinEnd(const std::string& t, size_t i) {
    if (!At(t, i, "sha256//") || (i > 0 && Alnum(t[i - 1]))) return 0;
    size_t j = i + 8;
    while (j < t.size() && (Alnum(t[j]) || t[j] == '+' || t[j] == '/' || t[j] == '=')) j++;
    return j > i + 8 ? j : 0;
}

// key=..., token=..., password=...: the whole pair
size_t SecretEnd(const std::string& t, size_t i) {
    if (i > 0 && (Alnum(t[i - 1]) || t[i - 1] == '_' || t[i - 1] == '-')) return 0;
    size_t j = i;
    while (j < t.size() && (Alnum(t[j]) || t[j] == '_' || t[j] == '-')) j++;
    if (j >= t.size() || t[j] != '=') return 0;
    const std::string name = t.substr(i, j - i);
    if (!OneOf(name, {"key", "apikey", "api_key", "api-key", "token", "access_token", "refresh_token", "auth", "authorization",
                      "password", "passwd", "pass", "pwd", "secret", "sig", "signature", "session", "sessionid", "sid"}))
        return 0;
    size_t k = j + 1;
    while (k < t.size() && !Space(t[k]) && t[k] != '&' && t[k] != ';' && t[k] != ',' && t[k] != '"' && t[k] != '\'') k++;
    return k > j + 1 ? k : 0;
}

// Bearer <token>
size_t BearerEnd(const std::string& t, size_t i) {
    if (!At(t, i, "Bearer ") || (i > 0 && Alnum(t[i - 1]))) return 0;
    size_t j = i + 7;
    while (j < t.size() && !Space(t[j])) j++;
    return j > i + 7 ? j : 0;
}

// an IPv6 address (eight groups, or shortened with ::) or a MAC address (six groups of two); the
// times of a day (12:34:56) are neither
size_t Ipv6End(const std::string& t, size_t i) {
    if (i > 0 && (Alnum(t[i - 1]) || t[i - 1] == '_' || t[i - 1] == ':' || t[i - 1] == '.')) return 0;
    size_t j = i;
    while (j < t.size() && (Hex(t[j]) || t[j] == ':')) j++;
    if (j < t.size() && (Alnum(t[j]) || t[j] == '_')) return 0;
    if (j - i >= 2 && t[j - 1] == ':' && t[j - 2] != ':') j--; // a colon after it ends a label
    const std::string run = t.substr(i, j - i);
    int colons = 0;
    for (char c : run) colons += c == ':';
    if (colons < 2) return 0;
    std::vector<std::string> groups;
    size_t start = 0;
    while (true) {
        const size_t c = run.find(':', start);
        groups.push_back(run.substr(start, c == std::string::npos ? std::string::npos : c - start));
        if (c == std::string::npos) break;
        start = c + 1;
    }
    int filled = 0;
    for (const auto& g : groups) {
        if (g.size() > 4) return 0;
        filled += !g.empty();
    }
    if (run.find("::") != std::string::npos) {
        if (run.find(":::") != std::string::npos || colons > 7) return 0;
        return filled >= 2 || run == "::1" ? j : 0;
    }
    if (colons == 7 && filled == 8) return j;
    if (colons == 5 && filled == 6) {
        for (const auto& g : groups) {
            if (g.size() != 2) return 0;
        }
        return j;
    }
    return 0;
}

// :<port> after a host
size_t PortEnd(const std::string& t, size_t j) {
    if (j + 1 < t.size() && t[j] == ':' && Digit(t[j + 1])) {
        size_t k = j + 1;
        while (k < t.size() && Digit(t[k])) k++;
        if (k - j - 1 <= 5 && (k >= t.size() || !Alnum(t[k]))) return k;
    }
    return j;
}

// four numbers to 255 with dots, and its port
size_t Ipv4End(const std::string& t, size_t i) {
    if (i > 0 && (Alnum(t[i - 1]) || t[i - 1] == '.' || t[i - 1] == '-')) return 0;
    size_t j = i;
    for (int part = 0; part < 4; part++) {
        if (part > 0) {
            if (j >= t.size() || t[j] != '.') return 0;
            j++;
        }
        size_t k = j;
        int value = 0;
        while (k < t.size() && Digit(t[k]) && k - j < 3) value = value * 10 + (t[k++] - '0');
        if (k == j || value > 255 || (k < t.size() && Digit(t[k]))) return 0;
        j = k;
    }
    if (j < t.size() && (Alnum(t[j]) || (t[j] == '.' && j + 1 < t.size() && Digit(t[j + 1])))) return 0;
    return PortEnd(t, j);
}

// a host name: localhost, a name with a known top-level domain, or any name with a port
size_t HostEnd(const std::string& t, size_t i) {
    if (i > 0 && (Alnum(t[i - 1]) || std::strchr("-._/\\:<", t[i - 1]))) return 0;
    size_t j = i, last = i;
    bool letter = false;
    int dots = 0;
    while (j < t.size()) {
        size_t k = j;
        while (k < t.size() && (Alnum(t[k]) || t[k] == '-')) {
            letter = letter || Alpha(t[k]);
            k++;
        }
        if (k == j) break;
        last = j;
        j = k;
        if (j + 1 < t.size() && t[j] == '.' && Alnum(t[j + 1])) {
            j++;
            dots++;
        } else {
            break;
        }
    }
    if (j == i || (j < t.size() && (t[j] == '_' || t[j] == '/' || t[j] == '\\'))) return 0;
    const std::string token = t.substr(i, j - i), tld = t.substr(last, j - last);
    const size_t port = PortEnd(t, j);
    if (SameNoCase(token, "localhost")) return port;
    if (dots >= 1 && OneOf(tld, {"com", "net", "org", "io", "dev", "app", "local", "lan", "home", "arpa", "info", "biz", "me", "tv",
                                 "xyz", "online", "site", "cloud", "link", "internal", "localdomain", "de", "es", "fr", "uk", "eu", "nl",
                                 "it", "ru", "jp", "cn", "br", "us", "ca", "au", "ch", "at", "be", "se", "no", "fi", "pl", "pt", "cz",
                                 "gr", "ie", "kr", "tw", "hk", "mx", "ar", "ro", "hu", "dk", "cat", "eus", "gal"}))
        return port;
    if (letter && port > j) return port;
    return 0;
}

// "[  12.34] " at the start of a line of NXDock's logs, "2026-11-26 10:00:00 " in mtp.log
size_t LinePrefix(const std::string& l) {
    if (!l.empty() && l[0] == '[') {
        size_t j = 1;
        while (j < l.size() && l[j] == ' ') j++;
        const size_t d = j;
        while (j < l.size() && (Digit(l[j]) || l[j] == '.')) j++;
        if (j > d && At(l, j, "] ")) return j + 2;
    }
    if (l.size() >= 20 && Digit(l[0]) && l[4] == '-' && l[7] == '-' && l[10] == ' ' && l[13] == ':' && l[16] == ':' && l[19] == ' ') return 20;
    return 0;
}

void AddName(std::vector<std::string>* names, const std::string& name) {
    if (names && !name.empty() && name != kGame) names->push_back(name);
}

// "<game> (<label>)" ending at `close`, its ")": both are names
void SplitGameLabel(const std::string& t, size_t close, std::vector<std::string>* names) {
    const size_t open = t.rfind(" (", close);
    if (open == std::string::npos) {
        AddName(names, t.substr(0, close));
        return;
    }
    AddName(names, t.substr(0, open));
    AddName(names, t.substr(open + 2, close - open - 2));
}

// The lines of NXDock that write the name of a game: the name, and its label, become <game>.
std::string KnownLines(const std::string& body, std::vector<std::string>* names) {
    if (At(body, 0, "[NXDock] ")) {
        const std::string r = body.substr(9);
        // install_queue: "[NXDock] <version>: <game> (<label>) <format>"
        const size_t colon = r.find(": ");
        if (colon != std::string::npos && colon > 0 && Digit(r[0]) && r.find('.') < colon && r.find(' ') > colon) {
            const std::string t = r.substr(colon + 2);
            size_t close = t.rfind(") ");
            std::string format;
            if (close != std::string::npos) format = t.substr(close + 2);
            if (close == std::string::npos || format.find_first_of(" ()") != std::string::npos) {
                close = !t.empty() && t.back() == ')' ? t.size() - 1 : std::string::npos;
                format.clear();
            }
            if (close != std::string::npos && t.rfind(" (", close) != std::string::npos) {
                SplitGameLabel(t, close, names);
                return "[NXDock] " + r.substr(0, colon + 2) + kGame + " (" + kGame + ")" + (close + 1 < t.size() ? " " + format : "");
            }
        }
        // install_queue: "[NXDock] removed <game> (<label>): 0x<result>"
        if (At(r, 0, "removed ")) {
            const std::string t = r.substr(8);
            const size_t close = t.rfind("): 0x");
            if (close != std::string::npos) {
                SplitGameLabel(t, close, names);
                return std::string("[NXDock] removed ") + kGame + " (" + kGame + t.substr(close);
            }
        }
    }
    // compress_nx: "=== <title ID> <game> ===", not the lines of its own words
    if (At(body, 0, "=== ") && body.size() > 25 && body.compare(body.size() - 4, 4, " ===") == 0 && TitleIdAt(body, 4) && body[20] == ' ') {
        const std::string name = body.substr(21, body.size() - 25);
        if (name != "merged with its update, after installing it") {
            AddName(names, name);
            return body.substr(0, 21) + kGame + " ===";
        }
    }
    // shortcuts.log: "<title ID> <game>: hidden, <why> (program <n> bytes)"
    if (TitleIdAt(body, 0) && body.size() > 17 && body[16] == ' ') {
        const size_t hidden = body.rfind(": hidden, ");
        if (hidden != std::string::npos && hidden > 17) {
            AddName(names, body.substr(17, hidden - 17));
            return body.substr(0, 17) + kGame + body.substr(hidden);
        }
    }
    return body;
}

// The saves sent over MTP: "saves: <folder of the game>/<file> refused: ...", "saves: receiving
// <path> (<n> bytes)". The path becomes <game>/<file>, with the title ID of its folder if it has one.
std::string SavePaths(const std::string& body) {
    const size_t at = body.find("saves: ");
    if (at == std::string::npos || (at > 0 && body[at - 1] != ' ')) return body;
    size_t from = at + 7;
    const bool receiving = At(body, from, "receiving ");
    if (receiving) from += 10;
    size_t end = std::string::npos;
    if (receiving) {
        end = body.rfind(" (");
        if (end != std::string::npos && end < from) end = std::string::npos;
    } else {
        for (const char* m : {" refused:", " did not arrive whole", " not restored:", " restored over ", " could not be restored"}) {
            const size_t p = body.find(m, from);
            if (p != std::string::npos && (end == std::string::npos || p < end)) end = p;
        }
    }
    if (end == std::string::npos || end <= from) return body;
    const std::string path = body.substr(from, end - from);
    std::string id;
    for (size_t i = 0; i + 16 <= path.size(); i++) {
        if ((i == 0 || !Hex(path[i - 1])) && TitleIdAt(path, i)) {
            id = path.substr(i, 16);
            break;
        }
    }
    const std::string clean =
        path.find('/') == std::string::npos ? std::string(kFile) : std::string(kGame) + (id.empty() ? "" : " [" + id + "]") + "/" + kFile;
    return body.substr(0, from) + clean + body.substr(end);
}

// A name that is an ID: hex digits (16 of a title, 32 of an NCA or a ticket), and [ ] _ - v.
bool IdName(const std::string& name) {
    const std::string stem = name.substr(0, name.find('.'));
    size_t run = 0, best = 0;
    for (char c : stem) {
        if (Hex(c)) {
            best = std::max(best, ++run);
        } else if (c == '[' || c == ']' || c == '_' || c == '-' || c == 'v' || c == ' ') {
            run = 0;
        } else {
            return false;
        }
    }
    return best >= 8;
}

// The folders of NXDock, of Atmosphère and of the console that a path may go through.
bool OwnFolder(const std::string& name) {
    return OneOf(name, {"switch", "NXDock", "atmosphere", "contents", "config", "crash_reports", "tmp", "saves", "mods", "covers",
                        "recipes", "resume", "tickets", "mtp-saves", ".installed", "exefs", "romfs", "Nintendo", "Album", "emuMMC",
                        "bootloader", "payloads", "registered", "placehld", "logs", "save", "system", "user"});
}

// The end of the last extension (".nsp": a dot, a letter, up to three letters or digits) in
// [from, end), npos without one.
size_t ExtensionEnd(const std::string& s, size_t from, size_t end) {
    size_t best = std::string::npos;
    for (size_t i = from; i + 2 <= end; i++) {
        if (s[i] != '.' || !Alpha(s[i + 1])) continue;
        size_t k = i + 1;
        while (k < end && Alnum(s[k])) k++;
        if (k - i - 1 <= 4) best = k;
    }
    return best;
}

// The paths of files on a storage (sdmc:/, ums0:/): each folder that is not NXDock's or the
// system's becomes <folder>, a file not named by an ID <file> with its extension.
std::string Paths(const std::string& body) {
    const auto mount = [&](size_t i) -> size_t {
        if (i > 0 && (Alnum(body[i - 1]) || body[i - 1] == '<')) return 0;
        if (!Alpha(body[i])) return 0;
        size_t j = i;
        while (j < body.size() && Alnum(body[j]) && j - i < 8) j++;
        if (j - i < 2 || !At(body, j, ":/") || At(body, j, "://")) return 0;
        return j + 2;
    };
    std::string out;
    size_t i = 0;
    while (i < body.size()) {
        const size_t root = mount(i);
        if (!root) {
            out += body[i++];
            continue;
        }
        // the path runs to the next storage named on the line, or to its end
        size_t limit = body.size();
        for (size_t k = root; k < body.size(); k++) {
            if (mount(k)) {
                limit = k;
                break;
            }
        }
        const size_t slash = body.rfind('/', limit - 1);
        // its file: to a tab, ", ", ": " or "; ", and to the end of its extension
        size_t end = limit;
        for (const char* stop : {"\t", ", ", ": ", "; "}) {
            const size_t p = body.find(stop, slash + 1);
            if (p != std::string::npos && p < end) end = p;
        }
        const size_t ext_end = ExtensionEnd(body, slash + 1, end);
        if (ext_end != std::string::npos) end = ext_end;
        out += body.substr(i, root - i);
        bool own = true;
        size_t from = root;
        while (from < slash + 1) {
            const size_t next = body.find('/', from);
            const std::string folder = body.substr(from, next - from);
            out += folder.empty() || folder[0] == '<' || OwnFolder(folder) || IdName(folder) ? folder : std::string(kFolder);
            out += '/';
            own = own && (folder.empty() || OwnFolder(folder));
            from = next + 1;
        }
        const std::string file = body.substr(slash + 1, end - slash - 1);
        const size_t dot = file.rfind('.');
        const std::string ext = dot == std::string::npos || ext_end == std::string::npos ? "" : file.substr(dot + 1);
        const bool game_file = OneOf(ext, {"nsp", "nsz", "xci", "xcz", "zip", "nro"}) && !SameNoCase(file, "NXDock.nro");
        if (file.empty() || file[0] == '<' || IdName(file) || OwnFolder(file) || (own && !ext.empty() && !game_file)) {
            out += file;
        } else {
            out += kFile;
            if (!ext.empty()) out += "." + ext;
        }
        i = end;
    }
    return out;
}

// Whether a name collected is looked for everywhere: 3 letters or more (each byte of a letter
// outside ASCII counts), and not a piece of the words put in place of others.
bool Searchable(const std::string& name) {
    int letters = 0;
    for (char c : name) letters += Alpha(c) || (unsigned char)c >= 0x80;
    if (letters < 3) return false;
    for (const char* mark : {kAddress, kGame, kFile, kFolder}) {
        if (std::strstr(mark, name.c_str())) return false;
    }
    return true;
}

template <typename F>
std::string EachLine(const std::string& text, F&& f) {
    std::string out;
    out.reserve(text.size());
    size_t start = 0;
    while (start < text.size()) {
        const size_t nl = text.find('\n', start);
        const size_t end = nl == std::string::npos ? text.size() : nl;
        out += f(text.substr(start, end - start));
        if (nl == std::string::npos) break;
        out += '\n';
        start = nl + 1;
    }
    return out;
}

std::vector<std::string> Fields(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return f;
}

// A line of history.tsv as History::Format writes it: "1", six figures and the result, then the
// format, the game, the label and the error.
bool HistoryLine(const std::vector<std::string>& f) { return f.size() >= 11 && f[0] == "1"; }

} // namespace

Kind KindOf(const std::string& name) {
    if (name.rfind("crash_reports/", 0) == 0) return Kind::Crash;
    if (EndsWithNoCase(name, "history.tsv")) return Kind::History;
    return Kind::Log;
}

std::string RedactAddresses(const std::string& t) {
    std::string out;
    out.reserve(t.size());
    size_t i = 0;
    while (i < t.size()) {
        const char c = t[i];
        size_t end = 0;
        if (Alpha(c)) {
            end = UrlEnd(t, i);
            if (!end) end = PinEnd(t, i);
            if (!end) end = SecretEnd(t, i);
            if (!end) end = BearerEnd(t, i);
        }
        if (!end && (Hex(c) || c == ':')) end = Ipv6End(t, i);
        if (!end && Digit(c)) end = Ipv4End(t, i);
        if (!end && Alnum(c)) end = HostEnd(t, i);
        if (end > i) {
            out += kAddress;
            i = end;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

std::string RedactLine(const std::string& line, std::vector<std::string>* names) {
    const size_t p = LinePrefix(line);
    const std::string body = KnownLines(line.substr(p), names);
    return line.substr(0, p) + Paths(SavePaths(RedactAddresses(body)));
}

std::string RedactNames(const std::string& text, const std::vector<std::string>& names) {
    // the names by their first three bytes, the longest first: one pass over the text
    std::vector<std::string> list;
    for (const auto& n : names) {
        if (Searchable(n)) list.push_back(n);
    }
    if (list.empty()) return text;
    std::sort(list.begin(), list.end(), [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() > b.size() : a < b; });
    list.erase(std::unique(list.begin(), list.end()), list.end());
    const auto key = [](const char* p) {
        return uint32_t((unsigned char)p[0]) | uint32_t((unsigned char)p[1]) << 8 | uint32_t((unsigned char)p[2]) << 16;
    };
    std::unordered_map<uint32_t, std::vector<const std::string*>> by_start;
    for (const auto& n : list) by_start[key(n.data())].push_back(&n);
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const std::string* hit = nullptr;
        if (i + 3 <= text.size()) {
            const auto it = by_start.find(key(text.data() + i));
            if (it != by_start.end()) {
                for (const std::string* n : it->second) {
                    if (text.compare(i, n->size(), *n) == 0) {
                        hit = n;
                        break;
                    }
                }
            }
        }
        if (hit) {
            out += kGame;
            i += hit->size();
        } else {
            out += text[i++];
        }
    }
    return out;
}

void CollectNames(const std::string& text, Kind kind, std::vector<std::string>& names) {
    if (kind == Kind::Crash) return;
    EachLine(text, [&](const std::string& line) {
        if (kind == Kind::History) {
            const auto f = Fields(line);
            if (HistoryLine(f)) {
                AddName(&names, f[8]);
                AddName(&names, f[9]);
            }
        } else {
            KnownLines(line.substr(LinePrefix(line)), &names);
        }
        return std::string();
    });
}

std::string Redact(const std::string& text, Kind kind, const std::vector<std::string>& names) {
    if (kind == Kind::Crash) return RedactAddresses(text);
    std::string out;
    if (kind == Kind::History) {
        out = EachLine(text, [](const std::string& line) {
            auto f = Fields(line);
            if (!HistoryLine(f)) return RedactAddresses(line);
            if (!f[8].empty()) f[8] = kGame;
            if (!f[9].empty()) f[9] = kGame;
            std::string joined;
            for (size_t i = 0; i < f.size(); i++) {
                if (i) joined += '\t';
                joined += i >= 10 ? Paths(RedactAddresses(f[i])) : f[i];
            }
            return joined;
        });
    } else {
        out = EachLine(text, [](const std::string& line) { return RedactLine(line); });
    }
    return RedactNames(out, names);
}

std::vector<File> Files(const std::string& sd) {
    std::vector<File> out;
    const std::string own = sd + "/switch/NXDock";
    std::vector<Found> logs = List(own, ".log");
    std::sort(logs.begin(), logs.end(), [](const Found& a, const Found& b) { return a.name < b.name; });
    struct stat st{};
    if (stat((own + "/history.tsv").c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        logs.push_back({"history.tsv", (unsigned long long)st.st_size, (long long)st.st_mtime});
    }
    for (const auto& f : logs) {
        const unsigned long long take = std::min(f.size, kMaxLog);
        out.push_back({own + "/" + f.name, "logs/" + f.name, f.size - take, take});
    }
    const std::string crashes = sd + "/atmosphere/crash_reports";
    std::vector<Found> reports = List(crashes, ".log");
    // the newest first: by the time the card keeps, then by name (Atmosphère begins it with the time)
    std::sort(reports.begin(), reports.end(), [](const Found& a, const Found& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.name > b.name;
    });
    if (reports.size() > size_t(kCrashReports)) reports.resize(size_t(kCrashReports));
    for (const auto& f : reports) {
        out.push_back({crashes + "/" + f.name, "crash_reports/" + f.name, 0, std::min(f.size, kMaxCrash)});
    }
    return out;
}

std::string Info(const ConsoleFacts& f, const std::string& version) {
    std::string out;
    const auto line = [&out](const char* label, const std::string& value) {
        out += label;
        out += ": ";
        out += value;
        out += "\n";
    };
    line("NXDock", version);
    // an official build, or one made without NXDOCK_OFFICIAL (brand.hpp)
    line("Build", brand::kOfficial ? "official" : "unofficial");
    line("Firmware", f.firmware.empty() ? "unknown" : f.firmware);
    line("Atmosphere", f.atmosphere.empty() ? "unknown" : f.atmosphere);
    line("emuMMC", f.emummc == 1 ? "yes" : f.emummc == 0 ? "no" : "unknown");
    line("Sigpatches", f.sigpatches.empty() ? "none found" : f.sigpatches);
    line("Mode", f.applet ? "applet" : "application");
    line("Memory used", Bytes(f.mem_used));
    line("Memory total", Bytes(f.mem_total));
    line("SD card free", Bytes(f.sd_free));
    line("SD card size", Bytes(f.sd_total));
    std::string date = "unknown";
    if (f.when > 0) {
        const std::time_t t = std::time_t(f.when);
        std::tm tm{};
        char text[40];
        if (gmtime_r(&t, &tm) && std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", &tm)) date = text;
    }
    line("Date", date);
    return out;
}

bool WriteZip(const std::string& path, const std::string& info, const std::vector<File>& files) {
    zip::Writer w;
    if (!w.Open(path)) return false;
    const auto from = [](const std::string& data) {
        return [p = &data](uint64_t off, void* buf, size_t n) {
            std::memcpy(buf, p->data() + off, n);
            return true;
        };
    };
    // read whole: a log written meanwhile gives the bytes it had, never a broken entry
    const auto read = [](const File& f, std::string& data) {
        FILE* in = std::fopen(f.path.c_str(), "rb");
        if (!in) return false; // gone since it was listed
        data.assign(size_t(f.size), '\0');
        size_t got = 0;
        if (fseeko(in, off_t(f.from), SEEK_SET) == 0) got = std::fread(data.data(), 1, data.size(), in);
        std::fclose(in);
        data.resize(got);
        return true;
    };
    // first the names of games in all of them, one file at a time for the memory; then each file
    // cleaned of those names, of addresses and of the names of files and folders
    std::vector<std::string> names;
    std::string data;
    for (const auto& f : files) {
        if (KindOf(f.name) != Kind::Crash && read(f, data)) CollectNames(data, KindOf(f.name), names);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    bool ok = w.AddFile("info.txt", info.size(), from(info), true);
    for (const auto& f : files) {
        if (!ok) break;
        if (!read(f, data)) continue;
        const std::string clean = Redact(data, KindOf(f.name), names);
        ok = w.AddFile(f.name, clean.size(), from(clean), true);
    }
    return w.Close() && ok;
}

#ifdef __SWITCH__

Result Send(const std::string& url, const std::string& key, const ConsoleFacts& facts) {
    Result r;
    if (url.empty() || key.empty()) {
        r.state = Result::State::NoLibrary;
        return r;
    }
    mkdir("sdmc:/switch/NXDock", 0777);
    mkdir("sdmc:/switch/NXDock/tmp", 0777);
    const std::string path = "sdmc:/switch/NXDock/tmp/report.zip";
    struct stat st{};
    if (!WriteZip(path, Info(facts, NXDOCK_VERSION), Files("sdmc:")) || stat(path.c_str(), &st) != 0) {
        std::remove(path.c_str());
        r.error = "zip";
        return r;
    }
    r.bytes = (unsigned long long)st.st_size;
    if (r.bytes > kMaxZip) {
        std::remove(path.c_str());
        r.state = Result::State::TooLarge;
        return r;
    }
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::remove(path.c_str());
        r.error = "zip";
        return r;
    }
    const auto answer = http::Send("POST", url + "/install/report", key, r.bytes,
                                   [f](char* buf, size_t max) { return long(std::fread(buf, 1, max, f)); }, {}, "application/zip");
    std::fclose(f);
    std::remove(path.c_str());
    if (answer.status >= 200 && answer.status < 300) {
        r.state = Result::State::Sent;
    } else {
        r.status = answer.status;
        r.error = answer.error;
    }
    return r;
}

#endif

} // namespace nxdock::report
