// Layout of the SimConnect client data area the module writes and the vPilot plugin reads.
// Packed, little-endian. Keep in sync with ModuleReader.cs (EhsShared struct).
#pragma once

#include <cstdint>

constexpr const char* kEhsSharedName = "EHSLink.Selections";
constexpr int32_t kEhsSharedVersion = 1;

enum EhsStatus : int32_t
{
    kStatusOk = 0,
    kStatusNoProfiles = 1,   // profiles.ini missing/unreadable
    kStatusNoProfile = 2,    // no profile (not even a fallback) for this aircraft
    kStatusPmdgNoData = 3,   // PMDG aircraft but no SDK data (EnableDataBroadcast off)
    kStatusNoApData = 4,     // profile variables all zero: aircraft doesn't use them
};

enum EhsFlags : int32_t
{
    kFlagAlt = 1 << 0,
    kFlagHdg = 1 << 1,
    kFlagIas = 1 << 2,
    kFlagMach = 1 << 3,
    kFlagVs = 1 << 4,
    kFlagBaro = 1 << 5,
    kFlagTrk = 1 << 6,
    kFlagBaroUnitKnown = 1 << 7,
    kFlagBaroHpa = 1 << 8,
    kFlagVerified = 1 << 9,
    kFlagCurrent = 1 << 10,   // curIas / curMach valid
};

#pragma pack(push, 1)
struct EhsShared
{
    int32_t version;
    int32_t sequence;   // increments on every write (at least 1/s): module alive
    int32_t status;     // EhsStatus
    int32_t flags;      // EhsFlags: which values are present
    int32_t alt;        // ft
    int32_t hdg;        // deg 1..360
    int32_t ias;        // kt
    int32_t vs;         // ft/min
    float mach;
    float baro;         // hPa
    double lat, lon;    // deg
    char source[32];    // profile name
    char title[96];     // aircraft title
    float curIas;       // current indicated airspeed, kt   (kFlagCurrent)
    float curMach;      // current Mach                     (kFlagCurrent)
};
#pragma pack(pop)

static_assert(sizeof(EhsShared) == 192, "EhsShared layout changed: update ModuleReader.cs");
