// Tests of the JSON NXDock reads, run on the computer: tools/test-json.sh
#include <cstdio>
#include <string>
#include "../source/http.hpp"

using namespace nxdock::http;

static int failures = 0;

static void Expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

int main() {
    // the answer of an accepted pairing: the app's own name is inside "app", the key's outside
    const std::string accepted =
        R"({"app":{"app":"switch-library-manager-web","enabled":false,"id":"abc","name":"Switch Library Manager","port":3000},)"
        R"("enabled":true,"key":"slm_x","name":"Switch del sal\u00f3n","state":"accepted","user":"bob"})";
    Expect(JsonString(accepted, "name") == "Switch del sal\xC3\xB3n", "the outer name, with an escaped letter");
    Expect(JsonString(accepted, "app").empty(), "an object is not a string");
    Expect(JsonBool(accepted, "enabled") == true, "the outer enabled");
    Expect(JsonString(accepted, "state") == "accepted", "state");
    Expect(JsonNumber(accepted, "port", -1) == -1, "a nested number is not taken");

    const std::string hello = R"({ "app" : "switch-library-manager-web", "port": 3030, "enabled": false, "id": "0f" })";
    Expect(JsonString(hello, "app") == "switch-library-manager-web", "spaces around the colon");
    Expect(JsonNumber(hello, "port") == 3030, "a number");
    Expect(JsonBool(hello, "enabled", true) == false, "false");
    Expect(JsonString(hello, "missing").empty(), "a missing field");
    Expect(JsonNumber(hello, "missing", 7) == 7, "the fallback");

    // a value that holds the name of a field is not taken for it
    const std::string tricky = R"({"note":"\"id\": no","id":"real","q":"a\\"})";
    Expect(JsonString(tricky, "id") == "real", "a field name inside a string");
    Expect(JsonString(tricky, "q") == "a\\", "an escaped backslash at the end");

    Expect(JsonQuote("Switch \"A\"\n") == R"("Switch \"A\"\n")", "quoting");

    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
