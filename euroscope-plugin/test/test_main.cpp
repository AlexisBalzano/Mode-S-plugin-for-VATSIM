// EHS Link core tests.
//   ehs_test.exe              unit tests (parsing, tag strings, alert state machine)
//   ehs_test.exe listen [s]   subscribe to the live broker and print what arrives for s seconds
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../src/EhsAlerts.h"
#include "../src/EhsCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void UnitTests()
{
    using namespace ehs;
    Selections s;
    CHECK(ParsePayload(R"({"v":1,"cs":"BAW117","t":1763215200,"lat":51.4706,"lon":-0.4619,"src":"PMDG777","alt":24000,"hdg":270,"trk":false,"ias":null,"mach":0.78,"vs":null,"extra":"x"})", s));
    CHECK(s.callsign == "BAW117" && s.source == "PMDG777" && s.hasPos);
    CHECK(s.alt == 24000 && s.hdg == 270 && !s.ias && s.mach && *s.mach > 0.779 && !s.vs && !s.trk);
    CHECK(!s.baro && TagBaro(s, BaroDisplay::Pilot) == "---");
    Selections b;
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","baro":1013.2,"baroUnit":"inHg"})", b));
    CHECK(TagBaro(b, BaroDisplay::Pilot) == "29.92" && TagBaro(b, BaroDisplay::Hpa) == "1013" && TagBaro(b, BaroDisplay::InHg) == "29.92");
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","baro":998.6,"baroUnit":"hPa"})", b));
    CHECK(TagBaro(b, BaroDisplay::Pilot) == "998" && TagBaro(b, BaroDisplay::InHg) == "29.49");   // hPa decimal cut like an altimeter
    CHECK(ParsePayload(R"({"v":1,"cs":"ATR1","baro":1007.9})", b) && TagBaro(b, BaroDisplay::Hpa) == "1007" && TagBaro(b, BaroDisplay::InHg) == "29.76");   // ATR PFD: 1007 / 29.76
    {
        AircraftAlerts q; Assigned qa; qa.baroRef = 1007; Selections qs; qs.baro = 1007.9; qs.baroUnit = "";
        Evaluate(qs, qa, Tolerances{}, q, 0); CHECK(q.f[kBaro].match == Match::Exact);              // green, not blue
    }
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","baro":1020.0,"baroUnit":null})", b));
    CHECK(TagBaro(b, BaroDisplay::Pilot) == "1020");   // unknown unit -> hPa
    CHECK(TagCurIas(b) == "---" && TagCurMach(b) == "---");
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","cias":251,"cmach":0.78})", b));
    CHECK(TagCurIas(b) == "251" && TagCurMach(b) == "M78");
    CHECK(b.verified);                                                              // no "ver": treated as verified
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","ver":false})", b) && !b.verified);
    CHECK(ParsePayload(R"({"v":1,"cs":"AFR1","ver":true})", b) && b.verified);
    CHECK(!ParsePayload(R"({"v":2,"cs":"X"})", s));
    CHECK(!ParsePayload("garbage", s));
    CHECK(!ParsePayload("", s));
    // 0.2.1: absurd values (broken profile, overflow) are dropped, never formatted
    {
        Selections g;
        CHECK(ParsePayload(R"({"v":1,"cs":"BAD1","alt":1e300,"hdg":-5,"ias":12345,"mach":1e40,"vs":99999,"baro":1e30,"cias":-1,"cmach":1e300,"lat":500,"lon":0})", g));
        CHECK(!g.alt && !g.hdg && !g.ias && !g.mach && !g.vs && !g.baro && !g.curIas && !g.curMach && !g.hasPos);
        CHECK(TagSpd(g) == "---" && TagBaro(g, BaroDisplay::InHg) == "---");
        Selections h; h.mach = 1e40; h.baro = 1e30; h.baroUnit = "inHg";   // formatting itself must not abort either
        CHECK(!TagSpd(h).empty() && !TagBaro(h, BaroDisplay::Pilot).empty());
        Assigned ha; ha.baroRef = 1013.25;
        CHECK(!Describe(kBaro, h, ha).empty() && !Describe(kSpd, h, ha).empty());
        CHECK(!ParsePayload(R"({"v":1,"cs":"WAYTOOLONGCALLSIGN"})", g));
    }

    CHECK(TagAlt(s) == "240" && TagHdg(s) == "270" && TagSpd(s) == "M78" && TagVs(s) == "---");
    CHECK(TagSummary(s) == "240 270 M78");
    Selections v; v.vs = -1500; v.ias = 250; v.hdg = 5; v.trk = true; v.alt = 5000;
    CHECK(TagVs(v) == "-15" && TagSpd(v) == "250" && TagHdg(v) == "005T" && TagAlt(v) == "050");
    v.vs = 30; CHECK(TagVs(v) == "0");

    // Flight plan marker
    CHECK(HasEhsCode("PBN/A1B1 DOF/261115 REG/GSTBA CODE/FE5000 RMK/TCAS", "FE5"));
    CHECK(HasEhsCode("code/fe5a3c", "FE5"));
    CHECK(!HasEhsCode("REG/GSTBA CODE/4CA2D6 RMK/TCAS", "FE5"));     // a real airframe address
    CHECK(!HasEhsCode("CODE/FE50", "FE5"));                           // too short
    CHECK(!HasEhsCode("CODE/FE50001", "FE5"));                        // too long
    CHECK(!HasEhsCode("XCODE/FE5000", "FE5"));
    CHECK(!HasEhsCode("", "FE5"));
    CHECK(HasEhsCode("CODE/ABC123 CODE/FE5123", "FE5"));
    CHECK(HasEhsRemark("PBN/A1B1 DOF/261116 REG/FGSQB RMK/EHSLINK"));
    CHECK(HasEhsRemark("RMK/TCAS EHSLINK /V/"));
    CHECK(HasEhsRemark("rmk/ehslink"));
    CHECK(!HasEhsRemark("RMK/TCAS EHS"));                 // the short form isn't the marker
    CHECK(!HasEhsRemark("RMK/XEHSLINKY"));                // whole word only
    CHECK(!HasEhsRemark(""));
    CHECK(IsValidCodePrefix("FE5") &&!IsValidCodePrefix("FG5") && !IsValidCodePrefix("") && !IsValidCodePrefix("FE50000"));

    auto brokers = ParseBrokerList("a.example, b.example:8883 ; ,c;1884");
    CHECK(brokers.size() == 3 && brokers[1].port == 8883 && brokers[2].host == "c" && brokers[2].port == 1884);

    // Alert state machine
    Tolerances t; t.graceSec = 10;
    AircraftAlerts st;
    Assigned a; a.cfl = 24000; a.hdg = 270; a.mach = 78;
    Selections p; p.alt = 24000; p.hdg = 272; p.mach = 0.78;
    CHECK(Evaluate(p, a, t, st, 0) == 0 && !st.AnyAlerting());              // all within tolerance

    p.alt = 23000;                                                         // pilot dialled the wrong level
    CHECK(Evaluate(p, a, t, st, 1000) == 0 && st.f[kAlt].mismatch);        // grace period running
    CHECK(Evaluate(p, a, t, st, 10999) == 0);
    CHECK(Evaluate(p, a, t, st, 11000) == (1u << kAlt) && st.AnyAlerting());
    CHECK(Evaluate(p, a, t, st, 12000) == 0);                              // alert fires once

    Acknowledge(p, st);
    CHECK(!st.AnyAlerting() && st.f[kAlt].acked);
    p.alt = 22000;                                                         // changed but still wrong: re-arm
    CHECK(Evaluate(p, a, t, st, 13000) == 0 && !st.f[kAlt].acked && st.f[kAlt].mismatch);
    CHECK(Evaluate(p, a, t, st, 23000) == (1u << kAlt));

    a.cfl = 22000;                                                         // controller re-clears: matches now
    CHECK(Evaluate(p, a, t, st, 24000) == 0 && !st.AnyAlerting());

    a.cfl = 18000;                                                         // new clearance restarts grace
    CHECK(Evaluate(p, a, t, st, 25000) == 0 && st.f[kAlt].mismatch);
    CHECK(Evaluate(p, a, t, st, 35000) == (1u << kAlt));

    // Heading wrap-around, speed units, V/S, Mach scaling
    AircraftAlerts h; Assigned ah; ah.hdg = 360; Selections ph; ph.hdg = 3;
    CHECK(Evaluate(ph, ah, t, h, 0) == 0 && !h.f[kHdg].mismatch);
    ph.hdg = 10; Evaluate(ph, ah, t, h, 0);
    CHECK(h.f[kHdg].mismatch);

    AircraftAlerts sp; Assigned as; as.spd = 250; Selections ps; ps.mach = 0.70;
    Evaluate(ps, as, t, sp, 0);
    CHECK(sp.f[kSpd].mismatch);                                            // Mach selected, IAS assigned
    ps.mach.reset(); ps.ias = 254; Evaluate(ps, as, t, sp, 0);
    CHECK(!sp.f[kSpd].mismatch);

    AircraftAlerts m3; Assigned am; am.mach = 780; Selections pm; pm.mach = 0.78;   // x1000 variant
    Evaluate(pm, am, t, m3, 0);
    CHECK(m3.f[kSpd].match == Match::Exact);
    pm.mach = 0.7799999; Evaluate(pm, am, t, m3, 0);
    CHECK(m3.f[kSpd].match == Match::Exact);
    pm.mach = 0.82; Evaluate(pm, am, t, m3, 0);
    CHECK(m3.f[kSpd].match == Match::Near);
    pm.mach = 0.83; Evaluate(pm, am, t, m3, 0);
    CHECK(m3.f[kSpd].match == Match::Near);                                // 0.05 is still blue
    pm.mach = 0.84; Evaluate(pm, am, t, m3, 0);
    CHECK(m3.f[kSpd].match == Match::Off && m3.f[kSpd].mismatch);

    // Exact / near / off bands: alt 300 ft, hdg 5, spd 5 kt, V/S 200
    {
        AircraftAlerts g; Assigned ga; ga.cfl = 24000; ga.hdg = 270; ga.spd = 250; ga.rate = 1500;
        Selections gs; gs.alt = 24000; gs.hdg = 270; gs.ias = 250; gs.vs = -1500;
        Evaluate(gs, ga, t, g, 0);
        CHECK(g.f[kAlt].match == Match::Exact && g.f[kHdg].match == Match::Exact && g.f[kSpd].match == Match::Exact && g.f[kVs].match == Match::Exact);
        gs.alt = 24300; gs.hdg = 275; gs.ias = 245; gs.vs = -1700;
        Evaluate(gs, ga, t, g, 0);
        CHECK(g.f[kAlt].match == Match::Near && g.f[kHdg].match == Match::Near && g.f[kSpd].match == Match::Near && g.f[kVs].match == Match::Near);
        CHECK(!g.f[kAlt].mismatch);
        gs.alt = 24400; gs.hdg = 276; gs.ias = 244; gs.vs = -1800;
        Evaluate(gs, ga, t, g, 0);
        CHECK(g.f[kAlt].match == Match::Off && g.f[kHdg].match == Match::Off && g.f[kSpd].match == Match::Off && g.f[kVs].match == Match::Off);
    }

    // Altimeter
    CHECK(ParseMetarQnh("LFPO 151630Z 24012KT 9999 FEW030 12/06 Q1018 NOSIG") == 1018.0);
    CHECK(ParseMetarQnh("KJFK 151651Z 31015KT 10SM FEW250 08/M06 A2992 RMK AO2").has_value());
    CHECK(std::fabs(*ParseMetarQnh("KJFK 151651Z A2992") - 1013.2) < 0.1);
    CHECK(!ParseMetarQnh("LFPO 151630Z 24012KT 9999 Q10X8").has_value());
    CHECK(ExpectedBaro(35000, 5000, 35000, 1018.0) == kStdHpa);            // cruise: STD
    CHECK(ExpectedBaro(3000, 5000, 3000, 1018.0) == 1018.0);               // below TA: QNH
    CHECK(ExpectedBaro(12000, 5000, 4000, 1018.0) == 1018.0);              // cleared to an altitude: QNH
    CHECK(ExpectedBaro(3000, 5000, 3000, std::nullopt) == 0);              // no METAR: nothing to compare
    CHECK(ExpectedBaro(3000, 0, 3000, 1018.0) == 0);                       // unknown TA
    {
        AircraftAlerts q; Assigned qa; qa.baroRef = 1018; Selections qs; qs.baro = 1018.0; qs.baroUnit = "hPa";
        Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Exact);
        qs.baro = 1017.0; Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Near);
        qs.baro = 1016.0; Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Off);
        qa.baroRef = 29.92 * 33.8639; qs.baroUnit = "inHg";
        qs.baro = 29.92 * 33.8639; Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Exact);
        qs.baro = 29.93 * 33.8639; Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Near);
        qs.baro = 29.94 * 33.8639; Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Off);
        qa.baroRef = kStdHpa; qs.baro = 1013.2; qs.baroUnit = "hPa";      // STD as sent by the sim
        Evaluate(qs, qa, t, q, 0); CHECK(q.f[kBaro].match == Match::Exact);
    }

    AircraftAlerts vs; Assigned av; av.rate = 1500; Selections pv; pv.vs = -1500;
    Evaluate(pv, av, t, vs, 0); CHECK(!vs.f[kVs].mismatch);
    pv.vs = -800; Evaluate(pv, av, t, vs, 0); CHECK(vs.f[kVs].mismatch);
    pv.vs.reset(); Evaluate(pv, av, t, vs, 0); CHECK(!vs.f[kVs].active);   // not in V/S mode: no comparison

    AircraftAlerts ils; Assigned ai; ai.cfl = 1; Selections pi; pi.alt = 3000;
    Evaluate(pi, ai, t, ils, 0); CHECK(!ils.f[kAlt].active);              // "cleared ILS" isn't a level

    printf("%s (%d failure%s)\n", g_fail ? "UNIT TESTS FAILED" : "Unit tests passed", g_fail, g_fail == 1 ? "" : "s");
}

// topic: "+" = every pilot, else one callsign. After `switchAfter` seconds (if >0) the
// subscription switches to `switchTo` to exercise live subscribe/unsubscribe.
static void Listen(int seconds, const std::string& topic = "+", int switchAfter = 0, const std::string& switchTo = "")
{
    ehs::MqttSubscriber sub;
    sub.SetTopics({std::string(ehs::kTopicRoot) + topic});
    sub.Start({{"broker.hivemq.com", 1883}, {"broker.emqx.io", 1883}},
        [](const std::string& topic, const std::string& payload) {
            ehs::Selections s;
            if (payload.empty()) { printf("CLEAR %s\n", topic.c_str()); return; }
            if (ehs::ParsePayload(payload, s))
                printf("DATA  %-10s src=%s ALT %s HDG %s SPD %s V/S %s  pos %.4f %.4f\n", s.callsign.c_str(), s.source.c_str(),
                       ehs::TagAlt(s).c_str(), ehs::TagHdg(s).c_str(), ehs::TagSpd(s).c_str(), ehs::TagVs(s).c_str(), s.lat, s.lon);
            else printf("BAD   %s: %s\n", topic.c_str(), payload.c_str());
            fflush(stdout);
        },
        [](const std::string& text) { printf("STATUS %s\n", text.c_str()); fflush(stdout); });
    if (switchAfter > 0 && switchAfter < seconds)
    {
        Sleep(switchAfter * 1000);
        printf("SWITCH subscription to %s\n", switchTo.c_str()); fflush(stdout);
        sub.SetTopics({std::string(ehs::kTopicRoot) + switchTo});
        Sleep((seconds - switchAfter) * 1000);
    }
    else Sleep(seconds * 1000);
    sub.Stop();
}

int main(int argc, char** argv)
{
    // listen [seconds] [callsign|+] [switchAfterSeconds switchToCallsign]
    if (argc > 1 && strcmp(argv[1], "listen") == 0)
    {
        Listen(argc > 2 ? atoi(argv[2]) : 30, argc > 3 ? argv[3] : "+", argc > 5 ? atoi(argv[4]) : 0, argc > 5 ? argv[5] : "");
        return 0;
    }
    UnitTests();
    return g_fail ? 1 : 0;
}
