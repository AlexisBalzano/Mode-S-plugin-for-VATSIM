// Desktop unit tests for the module's profile parser (same header the module uses).
//   cl /std:c++17 /EHsc profiles_test.cpp && profiles_test.exe ..\package\profiles.ini
#include "../src/Profiles.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main(int argc, char** argv)
{
    using namespace ehsprof;
    ProfileSet s;

    // The shipped file must parse, be read-only, and have the expected structure.
    if (argc > 1)
    {
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        CHECK(ParseProfiles(ss.str(), s));
        CHECK(s.version >= 1);
        CHECK(s.url.rfind("https://", 0) == 0);
        CHECK(FindProfile(s, "pmdg 777-300er air france|simobjects\\airplanes\\pmdg 777-300er\\aircraft.cfg")->name == "PMDG 777");
        CHECK(FindProfile(s, "pmdg 777-300ersf emirates skycargo|x")->source == "pmdg777");
        CHECK(FindProfile(s, "pmdg 737-800 ryanair|x")->name == "PMDG 737");
        CHECK(FindProfile(s, "cessna 152 asobo|x")->name == "Standard");   // fallback
    }

    const char* good =
        "[Update]\nversion = 7\nurl = https://example.invalid/p.ini\n"
        "[A320]\nmatch = flybywire|A32NX\nalt = (L:X_ALT)\nhdg = (L:X_HDG) 0 max\nverified = yes\n"
        "[Standard]\nalt = (A:AUTOPILOT ALTITUDE LOCK VAR, feet)\n";
    CHECK(ParseProfiles(good, s));
    CHECK(s.version == 7 && s.list.size() == 2 && s.list[0].verified && s.list[0].match.size() == 2);
    CHECK(s.list[0].match[1] == "a32nx");                                           // lower-cased
    CHECK(FindProfile(s, "flybywire a320neo|x")->name == "A320");
    CHECK(FindProfile(s, "something else|x")->name == "Standard");

    // Rejections: writes, no fallback, junk, oversize.
    CHECK(!ParseProfiles("[Standard]\nalt = 1 (>L:EVIL)\n", s) && s.list.empty());
    CHECK(!ParseProfiles("[Standard]\nalt = (A:X, feet)\nvs = 0 (>K:AP_MASTER)\n", s));
    CHECK(!ParseProfiles("[Only]\nmatch = foo\nalt = (L:A)\n", s));                  // no fallback
    CHECK(!ParseProfiles("<html>404 Not Found</html>", s));
    CHECK(!ParseProfiles("", s));
    CHECK(!ParseProfiles(std::string(300 * 1024, 'x'), s));
    CHECK(!ParseProfiles("[Broken\nalt = 1\n", s));
    CHECK(ParseProfiles("[Standard]\nalt = (A:X, feet) 100 > if{ 1 } els{ 0 }\n", s));  // comparison '>' is fine

    std::printf("%s (%d failure%s)\n", g_fail ? "PROFILE TESTS FAILED" : "Profile tests passed", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
