// Compares pilot-selected values with controller-assigned values and runs the alert state
// machine (grace period, acknowledge). No EuroScope dependency.
#pragma once

#include "EhsCore.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ehs
{
enum Field { kAlt = 0, kHdg, kSpd, kVs, kBaro, kFieldCount };

// Result of comparing one pilot selection with the controller's value.
enum class Match { None, Exact, Near, Off };   // None = nothing to compare

// Controller-assigned values, exactly as EuroScope returns them.
struct Assigned
{
    int cfl = 0;          // 0 none, 1 ILS, 2 visual, else feet
    int hdg = 0;          // 0 none
    int spd = 0;          // knots, 0 none
    int mach = 0;         // Mach * 100 (some ES versions * 1000), 0 none
    int rate = 0;         // ft/min, unsigned, 0 none
    double baroRef = 0;   // expected altimeter setting, hPa (QNH from METAR, or 1013.25 for STD); 0 none
};

// Differences up to these values are "near" (blue); more is "off" (red); zero is "exact" (green).
struct Tolerances
{
    int alt = 300;            // ft
    int hdg = 5;              // degrees
    int spd = 5;              // kt
    double mach = 0.05;
    int vs = 200;             // ft/min
    int qnhHpa = 1;           // hPa (pilot using hPa)
    double qnhInHg = 0.01;    // inHg (pilot using inHg)
    int graceSec = 10;        // an "off" value must last this long before notifying
};

struct FieldState
{
    bool active = false;       // controller assigned something we can compare
    Match match = Match::None;
    bool mismatch = false;     // match == Off
    bool alerting = false;     // mismatch has outlasted the grace period
    bool acked = false;        // controller acknowledged this alert
    uint64_t since = 0;        // when the current mismatch began
    long long assignedKey = 0;
    std::string pilotKey;      // pilot value when acknowledged
};

struct AircraftAlerts
{
    FieldState f[kFieldCount];
    bool AnyAlerting() const;     // alerting and not acknowledged
};

constexpr double kStdHpa = 1013.25;

// QNH in hPa from a METAR ("Q1013" or "A2992"); nullopt if none.
std::optional<double> ParseMetarQnh(const std::string& metar);

// Expected altimeter setting for an aircraft, hPa: STD above the transition altitude unless
// cleared to an altitude below it; otherwise the reference QNH. 0 = nothing to compare.
double ExpectedBaro(int pressureAltFt, int transitionAltFt, int cfl, std::optional<double> refQnh);

// Updates the state; returns a bitmask (1 << Field) of fields that started alerting now.
unsigned Evaluate(const Selections& sel, const Assigned& a, const Tolerances& t, AircraftAlerts& st, uint64_t nowMs);

// Acknowledge all currently alerting fields (until the pilot's value changes again).
void Acknowledge(const Selections& sel, AircraftAlerts& st);

// Text for the alert message, e.g. "SEL ALT 23000 ft, CFL 24000 ft".
std::string Describe(Field f, const Selections& sel, const Assigned& a);

// Tag strings (max 15 chars).
std::string TagAlt(const Selections& s);    // "240"
std::string TagHdg(const Selections& s);    // "270" / "270T"
std::string TagSpd(const Selections& s);    // "250" / "M78" / "---"
std::string TagVs(const Selections& s);     // "-15" / "+20" / "0" / "---"
std::string TagSummary(const Selections& s);

enum class BaroDisplay { Pilot, Hpa, InHg };
std::string TagBaro(const Selections& s, BaroDisplay d);   // "1013" / "29.92" / "---"
}   // namespace ehs
