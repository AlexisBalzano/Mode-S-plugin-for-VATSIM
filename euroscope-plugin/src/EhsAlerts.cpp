#include "EhsAlerts.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ehs
{
// hPa as an altimeter shows it: the decimal is cut, not rounded (29.76 inHg = 1007.8 hPa is
// shown as 1007). The small epsilon keeps 1006.9999 from becoming 1006.
int HpaShown(double hpa) { return static_cast<int>(std::floor(hpa + 0.01)); }

namespace
{
double AssignedMach(int raw) { return raw >= 100 ? raw / 1000.0 : raw / 100.0; }

int AngleDiff(int a, int b)
{
    int d = std::abs(a - b) % 360;
    return d > 180 ? 360 - d : d;
}

std::string PilotKey(Field f, const Selections& s)
{
    char buf[48];
    switch (f)
    {
        case kAlt: return s.alt ? std::to_string(*s.alt) : "-";
        case kHdg: return s.hdg ? std::to_string(*s.hdg) : "-";
        case kSpd:
            if (s.ias) return "I" + std::to_string(*s.ias);
            if (s.mach) { sprintf_s(buf, "M%.2f", *s.mach); return buf; }
            return "-";
        case kVs: return s.vs ? std::to_string(*s.vs) : "-";
        case kBaro: return s.baro ? std::to_string(std::lround(*s.baro * 10)) : "-";
        default: return "";
    }
}

Match Grade(double diff, double tol)
{
    if (diff < 1e-6) return Match::Exact;
    return diff <= tol + 1e-6 ? Match::Near : Match::Off;
}

// Compares one field; sets the assignment key used to detect new clearances.
Match Compare(Field f, const Selections& s, const Assigned& a, const Tolerances& t, long long& key)
{
    key = 0;
    switch (f)
    {
        case kAlt:
            key = a.cfl;
            if (a.cfl <= 2 || !s.alt) return Match::None;
            return Grade(std::abs(*s.alt - a.cfl), t.alt);
        case kHdg:
            key = a.hdg;
            if (a.hdg <= 0 || !s.hdg) return Match::None;
            return Grade(AngleDiff(*s.hdg, a.hdg), t.hdg);
        case kSpd:
            key = static_cast<long long>(a.spd) * 100000 + a.mach;
            if (!s.ias && !s.mach) return Match::None;
            if (a.spd > 0) return s.ias ? Grade(std::abs(*s.ias - a.spd), t.spd) : Match::Off;   // Mach selected = wrong unit
            if (a.mach > 0)
            {
                if (!s.mach) return Match::Off;
                // compare in whole hundredths so 0.7799999 == 0.78
                long sel = std::lround(*s.mach * 100), asg = std::lround(AssignedMach(a.mach) * 100);
                return Grade(std::labs(sel - asg) / 100.0, t.mach);
            }
            return Match::None;
        case kVs:
            key = a.rate;
            if (a.rate <= 0 || !s.vs) return Match::None;
            return Grade(std::abs(std::abs(*s.vs) - a.rate), t.vs);
        case kBaro:
            key = std::llround(a.baroRef * 10);
            if (a.baroRef <= 0 || !s.baro) return Match::None;
            if (s.baroUnit == "inHg")   // compare what the pilot sees: inHg to 2 decimals
            {
                long sel = std::lround(*s.baro / 33.8639 * 100), ref = std::lround(a.baroRef / 33.8639 * 100);
                return Grade(std::labs(sel - ref) / 100.0, t.qnhInHg);
            }
            return Grade(std::abs(HpaShown(*s.baro) - HpaShown(a.baroRef)), t.qnhHpa);
        default: return Match::None;
    }
}
}   // namespace

std::optional<double> ParseMetarQnh(const std::string& metar)
{
    // tokens are space-separated; take the first Q#### or A####
    size_t i = 0;
    while (i < metar.size())
    {
        while (i < metar.size() && metar[i] == ' ') ++i;
        size_t j = metar.find(' ', i);
        if (j == std::string::npos) j = metar.size();
        std::string tok = metar.substr(i, j - i);
        if (tok.size() == 5 && (tok[0] == 'Q' || tok[0] == 'A'))
        {
            bool digits = true;
            for (size_t k = 1; k < 5; ++k) digits &= tok[k] >= '0' && tok[k] <= '9';
            if (digits)
            {
                int v = std::atoi(tok.c_str() + 1);
                if (tok[0] == 'Q' && v > 850 && v < 1100) return static_cast<double>(v);
                if (tok[0] == 'A' && v > 2500 && v < 3250) return v / 100.0 * 33.8639;
            }
        }
        i = j + 1;
    }
    return std::nullopt;
}

double ExpectedBaro(int pressureAltFt, int transitionAltFt, int cfl, std::optional<double> refQnh)
{
    if (transitionAltFt <= 0) return 0;
    bool clearedToAltitude = cfl > 2 && cfl <= transitionAltFt;
    bool qnhExpected = pressureAltFt < transitionAltFt || clearedToAltitude;
    if (!qnhExpected) return kStdHpa;
    return refQnh ? *refQnh : 0;
}

bool AircraftAlerts::AnyAlerting() const
{
    for (auto& x : f) if (x.alerting && !x.acked) return true;
    return false;
}

unsigned Evaluate(const Selections& sel, const Assigned& a, const Tolerances& t, AircraftAlerts& st, uint64_t nowMs)
{
    unsigned started = 0;
    for (int i = 0; i < kFieldCount; ++i)
    {
        Field f = static_cast<Field>(i);
        FieldState& fs = st.f[i];
        long long key;
        Match m = Compare(f, sel, a, t, key);
        bool mismatch = m == Match::Off;

        if (key != fs.assignedKey)   // new clearance: restart the grace period
        {
            fs.assignedKey = key;
            fs.mismatch = fs.alerting = fs.acked = false;
        }
        fs.active = m != Match::None;
        fs.match = m;
        if (!mismatch)
        {
            fs.mismatch = fs.alerting = fs.acked = false;
            continue;
        }
        if (fs.acked && PilotKey(f, sel) != fs.pilotKey)   // pilot changed it but still wrong: start over
        {
            fs.acked = fs.alerting = fs.mismatch = false;
        }
        if (!fs.mismatch) { fs.mismatch = true; fs.since = nowMs; }
        if (!fs.alerting && nowMs - fs.since >= static_cast<uint64_t>(t.graceSec) * 1000)
        {
            fs.alerting = true;
            started |= 1u << i;
        }
    }
    return started;
}

void Acknowledge(const Selections& sel, AircraftAlerts& st)
{
    for (int i = 0; i < kFieldCount; ++i)
    {
        FieldState& fs = st.f[i];
        if (fs.alerting && !fs.acked)
        {
            fs.acked = true;
            fs.pilotKey = PilotKey(static_cast<Field>(i), sel);
        }
    }
}

std::string Describe(Field f, const Selections& s, const Assigned& a)
{
    char buf[96];
    switch (f)
    {
        case kAlt:
            sprintf_s(buf, "SEL ALT %d ft, cleared %d ft", s.alt.value_or(0), a.cfl);
            break;
        case kHdg:
            sprintf_s(buf, "SEL %s %03d, assigned HDG %03d", s.trk ? "TRK" : "HDG", s.hdg.value_or(0), a.hdg);
            break;
        case kSpd:
        {
            char sel[16];
            if (s.ias) sprintf_s(sel, "%d kt", *s.ias);
            else if (s.mach) sprintf_s(sel, "M%.2f", *s.mach);
            else sprintf_s(sel, "none");
            if (a.spd > 0) sprintf_s(buf, "SEL SPD %s, assigned %d kt", sel, a.spd);
            else sprintf_s(buf, "SEL SPD %s, assigned M%.2f", sel, AssignedMach(a.mach));
            break;
        }
        case kVs:
            sprintf_s(buf, "SEL V/S %+d ft/min, assigned %d ft/min", s.vs.value_or(0), a.rate);
            break;
        case kBaro:
        {
            bool isStd = std::fabs(a.baroRef - 1013.25) < 0.01;
            if (s.baroUnit == "inHg")
                sprintf_s(buf, "Altimeter %.2f inHg, expected %s%.2f", s.baro.value_or(0) / 33.8639, isStd ? "STD " : "QNH ", a.baroRef / 33.8639);
            else
                sprintf_s(buf, "Altimeter %d hPa, expected %s%d", HpaShown(s.baro.value_or(0)), isStd ? "STD " : "QNH ", HpaShown(a.baroRef));
            break;
        }
        default: buf[0] = 0;
    }
    return buf;
}

std::string TagAlt(const Selections& s)
{
    if (!s.alt) return "---";
    char b[16];
    sprintf_s(b, "%03d", (*s.alt + 50) / 100);
    return b;
}

std::string TagHdg(const Selections& s)
{
    if (!s.hdg) return "---";
    char b[16];
    sprintf_s(b, "%03d%s", *s.hdg, s.trk ? "T" : "");
    return b;
}

std::string TagSpd(const Selections& s)
{
    char b[16];
    if (s.ias) { sprintf_s(b, "%d", *s.ias); return b; }
    if (s.mach) { sprintf_s(b, "M%02d", static_cast<int>(std::lround(*s.mach * 100))); return b; }
    return "---";
}

std::string TagVs(const Selections& s)
{
    if (!s.vs) return "---";
    int h = static_cast<int>(std::lround(*s.vs / 100.0));
    if (h == 0) return "0";
    char b[16];
    sprintf_s(b, "%+d", h);
    return b;
}

std::string TagCurIas(const Selections& s)
{
    if (!s.curIas) return "---";
    return std::to_string(*s.curIas);
}

std::string TagCurMach(const Selections& s)
{
    if (!s.curMach) return "---";
    char b[16];
    sprintf_s(b, "M%02d", static_cast<int>(std::lround(*s.curMach * 100)));
    return b;
}

std::string TagBaro(const Selections& s, BaroDisplay d)
{
    if (!s.baro) return "---";
    bool inHg = d == BaroDisplay::InHg || (d == BaroDisplay::Pilot && s.baroUnit == "inHg");
    char b[16];
    if (inHg) sprintf_s(b, "%.2f", *s.baro / 33.8639);
    else sprintf_s(b, "%d", HpaShown(*s.baro));
    return b;
}

std::string TagSummary(const Selections& s)
{
    std::string r = TagAlt(s) + " " + TagHdg(s) + " " + TagSpd(s);
    return r.size() > 15 ? r.substr(0, 15) : r;
}
}   // namespace ehs
