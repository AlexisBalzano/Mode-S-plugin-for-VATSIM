// EHS Link MSFS module (standalone WASM, lives in the Community folder).
//
// Reads the user aircraft's autopilot selections inside the simulator, using a profile per
// aircraft (profiles.ini), and publishes them in the SimConnect client data area
// "EHSLink.Selections", where the EHS Link vPilot plugin picks them up.
//
// Profile values are calculator (RPN) expressions, so any aircraft can be supported by
// editing profiles.ini: A:vars, L:vars, arithmetic. PMDG 777/737 MCP values come from PMDG's
// SDK data (needs EnableDataBroadcast=1 in the aircraft's Options ini).

#include <MSFS/MSFS.h>
#include <MSFS/MSFS_Network.h>
#include <MSFS/Legacy/gauges.h>
#include <SimConnect.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "EhsShared.h"
#include "Profiles.h"

namespace
{
// ------------------------------------------------------------------ profiles

using ehsprof::Profile;
using ehsprof::ProfileSet;
using ehsprof::Lower;

ProfileSet g_set;
std::vector<Profile>& g_profiles = g_set.list;

std::string ReadFile(const char* path, size_t maxBytes = 256 * 1024)
{
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && s.size() < maxBytes) s.append(buf, n);
    fclose(f);
    return s;
}

using ehsprof::ParseProfiles;

// Package files are read relative to the package root; the work folder is writable.
const char* kCachePath = "\\work\\profiles.ini";

bool LoadProfiles()
{
    const char* paths[] = {"profiles.ini", ".\\profiles.ini", "./profiles.ini", "\\profiles.ini", "/profiles.ini"};
    ProfileSet shipped;
    bool ok = false;
    for (auto p : paths) if ((ok = ParseProfiles(ReadFile(p), shipped))) break;
    if (!ok) return false;
    std::string url = shipped.url;   // the URL always comes from the shipped file

    ProfileSet cached;
    if (ParseProfiles(ReadFile(kCachePath), cached) && cached.version > shipped.version)
    {
        g_set = cached;
        std::fprintf(stdout, "[EHS Link] using downloaded profiles v%d (shipped v%d)\n", cached.version, shipped.version);
    }
    else g_set = shipped;
    g_set.url = url;
    return true;
}

void SelectProfile();

void OnProfilesDownloaded(FsNetworkRequestId id, int errorCode, void*)
{
    unsigned char* data = fsNetworkHttpRequestGetData(id);
    unsigned long size = fsNetworkHttpRequestGetDataSize(id);
    if (!data || size == 0)
    {
        std::fprintf(stdout, "[EHS Link] profile update failed (error %d)\n", errorCode);
        return;
    }
    std::string text(reinterpret_cast<const char*>(data), size);
    ProfileSet fresh;
    if (!ParseProfiles(text, fresh))
    {
        std::fprintf(stdout, "[EHS Link] downloaded profiles rejected (invalid or not read-only)\n");
        return;
    }
    if (fresh.version <= g_set.version)
    {
        std::fprintf(stdout, "[EHS Link] profiles up to date (v%d)\n", g_set.version);
        return;
    }
    if (FILE* f = fopen(kCachePath, "wb")) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
    std::string url = g_set.url;
    g_set = fresh;
    g_set.url = url;
    std::fprintf(stdout, "[EHS Link] profiles updated to v%d\n", g_set.version);
    SelectProfile();   // the current aircraft may now have a (better) profile
}

void StartProfileUpdate()
{
    const std::string& url = g_set.url;
    if (url.compare(0, 8, "https://") != 0 || url.find("YOUR-") != std::string::npos) return;   // not configured
    fsNetworkHttpRequestGet(url.c_str(), nullptr, OnProfilesDownloaded, nullptr);
}

// A profile with no "match" line is the fallback (use it last).
const Profile* FindProfile(const std::string& id) { return ehsprof::FindProfile(g_set, id); }

bool Eval(const std::string& code, double& out)
{
    if (code.empty()) return false;
    FLOAT64 f = 0;
    SINT32 i = 0;
    PCSTRINGZ s = nullptr;
    if (!execute_calculator_code(code.c_str(), &f, &i, &s)) return false;
    if (std::isnan(f) || std::isinf(f)) return false;
    out = f;
    return true;
}

// ------------------------------------------------------------------ SimConnect

enum : DWORD
{
    kDefUser = 1, kDefOut = 2, kDefPmdg777 = 3, kDefPmdg737 = 4, kDefControl = 5,
    kReqUser = 1, kReqPmdg777 = 2, kReqPmdg737 = 3, kReqControl = 4,
    kEvt6Hz = 1, kEvtAircraftLoaded = 2,
    kCdOut = 1, kCdPmdg777 = 2, kCdPmdg737 = 3, kCdControl = 4,
};

// PMDG SDK data: the struct starts at FSUIPC offset 0x6420, so struct offset = FSUIPC - 0x6420.
// 777: MCP_IASMach 0x6554, IASBlank 0x6558, Heading 0x655A, Altitude 0x655C, VertSpeed 0x655E,
//      VertSpeedBlank 0x6564, HDGDial_Mode 0x6570, VSDial_Mode 0x6571, EFIS_BaroSelHPA[0] 0x652E
// 737: MCP_IASMach 0x65C4, IASBlank 0x65C8, Heading 0x65CC, Altitude 0x65CE, VertSpeed 0x65D0,
//      VertSpeedBlank 0x65D2, EFIS_BaroSelHPA[0] 0x65B6
constexpr DWORD kPmdgBytes = 0x200;
unsigned char g_pmdg777[kPmdgBytes];
unsigned char g_pmdg737[kPmdgBytes];
bool g_have777 = false, g_have737 = false;

#pragma pack(push, 1)
struct UserData
{
    char title[256];
};
#pragma pack(pop)

HANDLE g_sc = 0;
std::string g_title, g_path;
const Profile* g_profile = nullptr;
bool g_profilesLoaded = false;
EhsShared g_out{};
int g_ticks = 0;

template <typename T> T Read(const unsigned char* b, unsigned off) { T v; memcpy(&v, b + off, sizeof(T)); return v; }

// ------------------------------------------------------------------ discovery snapshot
// Writes every L:var (name = value) plus the standard autopilot variables at all indexes to
// \work\snapshot.txt, so a profile can be built without FSUIPC. Triggered on request (the
// EHSLink.Control client data area) or automatically once per aircraft when the pilot dials
// the "learn" values ALT 12300 + HDG 123.
const char* kSnapshotPath = "\\work\\snapshot.txt";
bool g_learnSnapshotTaken = false;
int g_learnTicks = 0;

void WriteSnapshot(const char* reason)
{
    FILE* f = fopen(kSnapshotPath, "w");
    if (!f) return;
    fprintf(f, "# EHS Link variable snapshot (%s)\n", reason);
    fprintf(f, "title = %s\n", g_title.c_str());
    fprintf(f, "path = %s\n", g_path.c_str());
    fprintf(f, "profile = %s\n", g_profile ? g_profile->name.c_str() : "(none)");
    fprintf(f, "profiles_version = %d\n\n", g_set.version);

    const char* avars[][2] = {
        {"AUTOPILOT ALTITUDE LOCK VAR", "feet"}, {"AUTOPILOT HEADING LOCK DIR", "degrees"},
        {"AUTOPILOT AIRSPEED HOLD VAR", "knots"}, {"AUTOPILOT MACH HOLD VAR", "number"},
        {"AUTOPILOT VERTICAL HOLD VAR", "feet/minute"}, {"AUTOPILOT VERTICAL HOLD", "bool"},
        {"AUTOPILOT MANAGED SPEED IN MACH", "bool"}, {"AUTOPILOT MACH HOLD", "bool"},
        {"KOHLSMAN SETTING MB", "millibars"}, {"KOHLSMAN SETTING STD", "bool"},
    };
    for (auto& a : avars)
        for (int idx = 0; idx <= 3; ++idx)
        {
            char code[128];
            snprintf(code, sizeof(code), "(A:%s:%d, %s)", a[0], idx, a[1]);
            double v;
            if (Eval(code, v)) fprintf(f, "A:%s:%d = %.4f\n", a[0], idx, v);
        }
    fprintf(f, "\n");

    int count = 0;
    for (ID id = 0; id < 100000; ++id)
    {
        PCSTRINGZ name = get_name_of_named_variable(id);
        if (!name) break;
        fprintf(f, "L:%s = %.4f\n", name, get_named_variable_value(id));
        ++count;
    }
    fprintf(f, "\n# %d L:vars\n", count);
    fclose(f);
    std::fprintf(stdout, "[EHS Link] snapshot written (%s, %d L:vars)\n", reason, count);
}

// Cheap check every ~5 s: has the pilot dialled the learn values?
void CheckLearnTrigger()
{
    if (g_learnSnapshotTaken || ++g_learnTicks < 30) return;
    g_learnTicks = 0;
    bool alt = false, hdg = false;
    for (ID id = 0; id < 100000 && !(alt && hdg); ++id)
    {
        PCSTRINGZ name = get_name_of_named_variable(id);
        if (!name) break;
        double v = get_named_variable_value(id);
        alt |= std::fabs(v - 12300) < 0.5;
        hdg |= std::fabs(v - 123) < 0.5;
    }
    double v;
    if (!alt && Eval("(A:AUTOPILOT ALTITUDE LOCK VAR:3, feet)", v)) alt = std::fabs(v - 12300) < 1;
    if (!alt && Eval("(A:AUTOPILOT ALTITUDE LOCK VAR:1, feet)", v)) alt = std::fabs(v - 12300) < 1;   // GA / Working Title
    if (!hdg && Eval("(A:AUTOPILOT HEADING LOCK DIR, degrees)", v)) hdg = std::fabs(v - 123) < 0.5;
    if (alt && hdg) { g_learnSnapshotTaken = true; WriteSnapshot("learn values dialled"); }
}

// PMDG creates its client data area only when the aircraft loads, and a request made before
// that never delivers. So (re)request on every aircraft load and periodically while waiting.
void RequestPmdg()
{
    SimConnect_RequestClientData(g_sc, kCdPmdg777, kReqPmdg777, kDefPmdg777, SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET);
    SimConnect_RequestClientData(g_sc, kCdPmdg737, kReqPmdg737, kDefPmdg737, SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET);
}

int g_pmdgRetryTicks = 0;

void SelectProfile()
{
    std::string id = Lower(g_title + "|" + g_path);
    g_profile = FindProfile(id);
    g_have777 = g_have737 = false;   // fresh PMDG data needed after an aircraft change
    g_learnSnapshotTaken = false;    // new aircraft: the learn values may be dialled again
    if (g_sc) RequestPmdg();
    std::fprintf(stdout, "[EHS Link] aircraft '%s' -> profile '%s'\n", g_title.c_str(), g_profile ? g_profile->name.c_str() : "(none)");
}

void Sample()
{
    EhsShared o{};
    o.version = kEhsSharedVersion;
    double lat = 0, lon = 0;
    Eval("(A:PLANE LATITUDE, degrees)", lat);
    Eval("(A:PLANE LONGITUDE, degrees)", lon);
    o.lat = lat; o.lon = lon;
    strncpy(o.title, g_title.c_str(), sizeof(o.title) - 1);

    // Current airspeed: standard simulator variables, valid in every aircraft.
    double cias = 0, cmach = 0;
    if (Eval("(A:AIRSPEED INDICATED, knots)", cias) && Eval("(A:AIRSPEED MACH, mach)", cmach))
    {
        o.curIas = static_cast<float>(cias < 0 ? 0 : cias);
        o.curMach = static_cast<float>(cmach < 0 ? 0 : cmach);
        o.flags |= kFlagCurrent;
    }

    const Profile* p = g_profile;
    if (!g_profilesLoaded) o.status = kStatusNoProfiles;
    else if (!p) o.status = kStatusNoProfile;
    else
    {
        strncpy(o.source, p->name.c_str(), sizeof(o.source) - 1);
        if (p->verified) o.flags |= kFlagVerified;
        o.status = kStatusOk;
        double v;

        if (p->source == "pmdg777" || p->source == "pmdg737")
        {
            bool is777 = p->source == "pmdg777";
            const unsigned char* b = is777 ? g_pmdg777 : g_pmdg737;
            bool have = is777 ? g_have777 : g_have737;
            unsigned oIasMach = is777 ? 0x134 : 0x1A4, oIasBlank = is777 ? 0x138 : 0x1A8, oHdg = is777 ? 0x13A : 0x1AC,
                     oAlt = is777 ? 0x13C : 0x1AE, oVs = is777 ? 0x13E : 0x1B0, oVsBlank = is777 ? 0x144 : 0x1B2,
                     oHpa = is777 ? 0x10E : 0x196;
            float iasMach = have ? Read<float>(b, oIasMach) : 0.f;
            unsigned short hdg = have ? Read<unsigned short>(b, oHdg) : 0, alt = have ? Read<unsigned short>(b, oAlt) : 0;
            if (!have && ++g_pmdgRetryTicks >= 30) { g_pmdgRetryTicks = 0; RequestPmdg(); }   // every ~5 s
            if (!have || (iasMach == 0.f && hdg == 0 && alt == 0)) o.status = kStatusPmdgNoData;
            else
            {
                if (!b[oIasBlank]) { if (iasMach < 10.f) { o.mach = iasMach; o.flags |= kFlagMach; } else { o.ias = static_cast<int>(std::lround(iasMach)); o.flags |= kFlagIas; } }
                o.hdg = hdg % 360 == 0 ? 360 : hdg % 360; o.flags |= kFlagHdg;
                o.alt = alt; o.flags |= kFlagAlt;
                bool fpa = is777 && b[0x151] == 1;
                if (!b[oVsBlank] && !fpa) { o.vs = Read<short>(b, oVs); o.flags |= kFlagVs; }
                if (is777 && b[0x150] == 1) o.flags |= kFlagTrk;
                o.flags |= kFlagBaroUnitKnown | (b[oHpa] ? kFlagBaroHpa : 0);
            }
        }
        else
        {
            if (Eval(p->alt, v) && v >= 0) { o.alt = static_cast<int>(std::lround(v)); o.flags |= kFlagAlt; }
            if (Eval(p->hdg, v) && v >= 0) { int h = static_cast<int>(std::lround(v)) % 360; o.hdg = h == 0 ? 360 : h; o.flags |= kFlagHdg; }
            if (Eval(p->trk, v) && v != 0) o.flags |= kFlagTrk;
            double mm = 0;
            bool machMode = Eval(p->machMode, mm) && mm != 0;
            if (!p->speed.empty())
            {
                if (Eval(p->speed, v) && v > 0)
                {
                    if (v < 5) { o.mach = static_cast<float>(v); o.flags |= kFlagMach; }
                    else { o.ias = static_cast<int>(std::lround(v)); o.flags |= kFlagIas; }
                }
            }
            else if (machMode) { if (Eval(p->mach, v) && v > 0) { o.mach = static_cast<float>(v); o.flags |= kFlagMach; } }
            else if (Eval(p->ias, v) && v > 0) { o.ias = static_cast<int>(std::lround(v)); o.flags |= kFlagIas; }
            double vm = 1;
            bool vsActive = p->vsMode.empty() || (Eval(p->vsMode, vm) && vm != 0);
            if (vsActive && Eval(p->vs, v)) { o.vs = static_cast<int>(std::lround(v)); o.flags |= kFlagVs; }
            if (!p->baroHpa.empty() && Eval(p->baroHpa, v)) o.flags |= kFlagBaroUnitKnown | (v != 0 ? kFlagBaroHpa : 0);

            // Aircraft that don't use these variables leave everything at zero: send nothing.
            bool allZero = !(o.flags & (kFlagIas | kFlagMach)) && o.alt == 0 && (!(o.flags & kFlagVs) || o.vs == 0) &&
                           (!(o.flags & kFlagHdg) || o.hdg == 360);
            if (allZero) o.status = kStatusNoApData;
        }

        double isStd = 0;
        if (Eval(p->baroStd, isStd) && isStd != 0) { o.baro = 1013.25f; o.flags |= kFlagBaro; }
        else if (Eval(p->baro, v) && v > 800 && v < 1100) { o.baro = static_cast<float>(v); o.flags |= kFlagBaro; }
    }

    // Publish on change, and at least once a second so readers can tell the module is alive.
    bool changed = memcmp(&o.status, &g_out.status, sizeof(EhsShared) - offsetof(EhsShared, status)) != 0;
    if (changed || ++g_ticks >= 6)
    {
        g_ticks = 0;
        o.sequence = g_out.sequence + 1;
        g_out = o;
        SimConnect_SetClientData(g_sc, kCdOut, kDefOut, SIMCONNECT_CLIENT_DATA_SET_FLAG_DEFAULT, 0, sizeof(g_out), &g_out);
    }
}

void CALLBACK Dispatch(SIMCONNECT_RECV* pData, DWORD cbData, void*)
{
    switch (pData->dwID)
    {
        case SIMCONNECT_RECV_ID_EVENT:
        {
            auto* e = static_cast<SIMCONNECT_RECV_EVENT*>(pData);
            if (e->uEventID == kEvt6Hz) { Sample(); CheckLearnTrigger(); }
            break;
        }
        case SIMCONNECT_RECV_ID_EVENT_FILENAME:
        {
            auto* e = static_cast<SIMCONNECT_RECV_EVENT_FILENAME*>(pData);
            if (e->uEventID == kEvtAircraftLoaded) { g_path = e->szFileName; SelectProfile(); }
            break;
        }
        case SIMCONNECT_RECV_ID_SYSTEM_STATE:
        {
            auto* s = static_cast<SIMCONNECT_RECV_SYSTEM_STATE*>(pData);
            g_path = s->szString;
            SelectProfile();
            break;
        }
        case SIMCONNECT_RECV_ID_SIMOBJECT_DATA:
        {
            auto* d = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);
            if (d->dwRequestID == kReqUser)
            {
                auto* u = reinterpret_cast<UserData*>(&d->dwData);
                std::string t(u->title, strnlen(u->title, sizeof(u->title)));
                if (t != g_title) { g_title = t; SelectProfile(); }
            }
            break;
        }
        case SIMCONNECT_RECV_ID_CLIENT_DATA:
        {
            auto* d = static_cast<SIMCONNECT_RECV_CLIENT_DATA*>(pData);
            const unsigned char* src = reinterpret_cast<const unsigned char*>(&d->dwData);
            DWORD avail = cbData > sizeof(SIMCONNECT_RECV_CLIENT_DATA) - sizeof(DWORD)
                        ? cbData - static_cast<DWORD>(sizeof(SIMCONNECT_RECV_CLIENT_DATA) - sizeof(DWORD)) : 0;
            DWORD n = avail < kPmdgBytes ? avail : kPmdgBytes;
            if (d->dwRequestID == kReqControl)
            {
                int32_t req = 0;
                memcpy(&req, src, avail < 4 ? avail : 4);
                if (req != 0) WriteSnapshot("requested");   // the area starts at 0; any write = request
                break;
            }
            if (d->dwRequestID == kReqPmdg777) { memcpy(g_pmdg777, src, n); g_have777 = true; }
            else if (d->dwRequestID == kReqPmdg737) { memcpy(g_pmdg737, src, n); g_have737 = true; }
            break;
        }
        default: break;
    }
}
}   // namespace

extern "C" MSFS_CALLBACK void module_init(void)
{
    g_profilesLoaded = LoadProfiles();
    std::fprintf(stdout, "[EHS Link] module init, %u profiles\n", static_cast<unsigned>(g_profiles.size()));
    if (FAILED(SimConnect_Open(&g_sc, "EHS Link", nullptr, 0, 0, 0))) return;

    // Output area read by the vPilot plugin.
    SimConnect_MapClientDataNameToID(g_sc, kEhsSharedName, kCdOut);
    SimConnect_CreateClientData(g_sc, kCdOut, sizeof(EhsShared), SIMCONNECT_CREATE_CLIENT_DATA_FLAG_READ_ONLY);
    SimConnect_AddToClientDataDefinition(g_sc, kDefOut, 0, sizeof(EhsShared));

    // Discovery requests: a writable 4-byte counter; each write asks for a snapshot.
    SimConnect_MapClientDataNameToID(g_sc, "EHSLink.Control", kCdControl);
    SimConnect_CreateClientData(g_sc, kCdControl, 4, SIMCONNECT_CREATE_CLIENT_DATA_FLAG_DEFAULT);
    SimConnect_AddToClientDataDefinition(g_sc, kDefControl, 0, 4);
    SimConnect_RequestClientData(g_sc, kCdControl, kReqControl, kDefControl, SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET);

    // PMDG SDK data (only present when the aircraft's broadcast is enabled).
    SimConnect_MapClientDataNameToID(g_sc, "PMDG_777X_Data", kCdPmdg777);
    SimConnect_AddToClientDataDefinition(g_sc, kDefPmdg777, 0, kPmdgBytes);
    SimConnect_MapClientDataNameToID(g_sc, "PMDG_NG3_Data", kCdPmdg737);
    SimConnect_AddToClientDataDefinition(g_sc, kDefPmdg737, 0, kPmdgBytes);
    RequestPmdg();

    // Aircraft identity.
    SimConnect_AddToDataDefinition(g_sc, kDefUser, "TITLE", nullptr, SIMCONNECT_DATATYPE_STRING256);
    SimConnect_RequestDataOnSimObject(g_sc, kReqUser, kDefUser, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_SECOND,
                                      SIMCONNECT_DATA_REQUEST_FLAG_CHANGED);
    SimConnect_SubscribeToSystemEvent(g_sc, kEvtAircraftLoaded, "AircraftLoaded");
    SimConnect_RequestSystemState(g_sc, 1, "AircraftLoaded");

    // Sampling clock.
    SimConnect_SubscribeToSystemEvent(g_sc, kEvt6Hz, "6Hz");
    SimConnect_CallDispatch(g_sc, Dispatch, nullptr);

    // Fetch newer profiles from the project's GitHub (cached in the work folder).
    if (g_profilesLoaded) StartProfileUpdate();
}

extern "C" MSFS_CALLBACK void module_deinit(void)
{
    if (g_sc) SimConnect_Close(g_sc);
    g_sc = 0;
}
