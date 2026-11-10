// Tests of the catalog NXDock reads from the app, run on the computer: tools/test.sh
#include <cstdio>
#include <string>
#include "../source/catalog.hpp"

using namespace nxdock;

static int failures = 0;

static void Expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    // as /install/catalog gives it
    const std::string json = R"({"app":{"app":"switch-library-manager-web","id":"ab","name":"Switch Library Manager","version":"2.0.3","port":3000,"enabled":true},
      "games":[
        {"key":"0100000000010","name":"Game","titleId":"0100000000010000","cover":"/install/cover/c.jpg","files":[
          {"id":"f1","kind":"game","titleId":"0100000000010000","version":0,"size":1000,"format":"nsp","name":"Game [0100000000010000][v0].nsp","requiredSystem":251658240,"url":"/install/f/f1/Game%20%5B0100000000010000%5D%5Bv0%5D.nsp"},
          {"id":"f2","kind":"update","titleId":"0100000000010800","version":131072,"size":300,"format":"nsz","name":"u2.nsz","url":"/install/f/f2/u2.nsz"},
          {"id":"f3","kind":"update","titleId":"0100000000010800","version":65536,"size":200,"format":"nsp","name":"u1.nsp","url":"/install/f/f3/u1.nsp"},
          {"id":"f4","kind":"dlc","titleId":"0100000000011001","version":0,"size":50,"format":"nsp","name":"d.nsp","dlcName":"Extra","url":"/install/f/f4/d.nsp"}]},
        {"key":"0100000000020","name":"Only an update","files":[
          {"id":"f5","kind":"update","titleId":"0100000000020800","version":65536,"size":10,"format":"nsp","name":"o.nsp","url":"/install/f/f5/o.nsp"}]},
        {"key":"x","name":"No files","files":[]}
      ]})";
    std::vector<Game> games;
    Expect(ParseCatalog(json, games), "it reads");
    Expect(games.size() == 2, "a game without files is left out");
    const Game& g = games[0];
    Expect(g.name == "Game" && g.title_id == 0x0100000000010000ull && g.cover == "/install/cover/c.jpg", "the game");
    Expect(g.files.size() == 4, "its files");
    Expect(g.Base() && g.Base()->required_system == 251658240u, "the base and its firmware");
    Expect(g.NewestUpdate() && g.NewestUpdate()->version == 131072u, "the newest update");
    Expect(g.Dlc().size() == 1 && g.Dlc()[0]->dlc_name == "Extra" && g.Dlc()[0]->title_id == 0x0100000000011001ull, "the DLC");
    Expect(g.SizeOfAll() == 1000 + 300 + 50, "the size of all: the base, the newest update, the DLC");

    // what the console has changes what is offered
    Game installed = g;
    Expect(!installed.UpdateAvailable() && !installed.MissingDlc(), "nothing to offer when not installed");
    installed.installed.base = true;
    installed.installed.patch = 65536;
    Expect(installed.UpdateAvailable(), "a newer update than the one installed");
    Expect(installed.MissingDlc(), "a DLC not installed");
    installed.installed.patch = 131072;
    installed.installed.dlc.insert(0x0100000000011001ull);
    Expect(!installed.UpdateAvailable() && !installed.MissingDlc(), "up to date");

    // a game known only by its update belongs to its application
    Expect(games[1].title_id == 0x0100000000020000ull, "the application of an update");

    Expect(!ParseCatalog("not json", games), "not JSON");
    Expect(!ParseCatalog(R"({"games":3})", games), "no list of games");

    // what the web sends (/install/push/wait)
    std::vector<PushItem> pushed;
    Expect(ParsePush(R"({"items":[
        {"id":"p1","fileId":"f1","url":"/install/f/f1/Game.nsz","game":"Game","titleId":"0100000000010000","kind":"game","version":0,"size":123,"format":"nsz","name":"Game.nsz","from":"ana"},
        {"id":"p2","fileId":"f2","url":"http://elsewhere/x.nsp","game":"Game","kind":"update"},
        {"id":"p3","fileId":"f3","url":"//elsewhere/x.nsp","game":"Game","kind":"update"}]})", pushed), "push: it reads");
    Expect(pushed.size() == 1 && pushed[0].id == "p1" && pushed[0].from == "ana" && pushed[0].size == 123 &&
               pushed[0].title_id == 0x0100000000010000ull && pushed[0].url == "/install/f/f1/Game.nsz",
           "push: a file of the app only, never another address");
    Expect(!ParsePush("nope", pushed) && !ParsePush(R"({"items":1})", pushed), "push: not an answer");

    // the inventory, only sent when the user says yes
    const std::string inv = InventoryJson({{0x0100000000010000ull, 0x80, 0, false}, {0x0100000000010800ull, 0x81, 65536, true}});
    Expect(inv == R"({"items":[{"titleId":"0100000000010000","type":"game","version":0,"storage":"sd"},{"titleId":"0100000000010800","type":"update","version":65536,"storage":"nand"}]})",
           "inventory: the JSON of the app");
    Expect(InventoryJson({}) == R"({"items":[]})", "inventory: empty");

    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
