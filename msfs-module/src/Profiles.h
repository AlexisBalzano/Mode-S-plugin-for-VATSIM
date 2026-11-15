// Aircraft profile parsing for the EHS Link module. Header-only and free of MSFS APIs, so the
// same code is unit-tested on the desktop (test\profiles_test.cpp).
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace ehsprof
{
struct Profile
{
    std::string name;
    std::vector<std::string> match;   // lower-case substrings of title / aircraft.cfg path
    std::string source;               // "" (expressions) or "pmdg777" / "pmdg737"
    bool verified = false;
    std::string alt, hdg, trk, ias, mach, machMode, speed, vs, vsMode, baro, baroHpa, baroStd;
};

struct ProfileSet
{
    std::vector<Profile> list;
    int version = 0;      // [Update] version
    std::string url;      // [Update] url (only honoured from the shipped file)
};

constexpr size_t kMaxProfileBytes = 256 * 1024;

inline std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

inline std::string Lower(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// Parses a profiles file. Returns false (and an empty set) for anything that isn't a sane,
// read-only profile set with a fallback profile.
inline bool ParseProfiles(const std::string& text, ProfileSet& result)
{
    result = ProfileSet{};
    if (text.empty() || text.size() > kMaxProfileBytes) return false;
    ProfileSet out;
    bool inUpdate = false, inProfile = false;
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string l = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (l.empty() || l[0] == ';' || l[0] == '#') continue;
        if (l[0] == '[')
        {
            size_t close = l.find(']');
            if (close == std::string::npos) return false;
            std::string name = Trim(l.substr(1, close - 1));
            inUpdate = Lower(name) == "update";
            inProfile = !inUpdate;
            if (inProfile) { out.list.push_back(Profile{}); out.list.back().name = name; }
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
        if (inUpdate)
        {
            if (k == "version") out.version = std::atoi(v.c_str());
            else if (k == "url") out.url = v;
            continue;
        }
        if (!inProfile) continue;
        // Profiles may only READ the aircraft: calculator writes look like "(>L:...)", "(>K:...)".
        if (v.find("(>") != std::string::npos) return false;
        Profile& cur = out.list.back();
        if (k == "match")
        {
            size_t s = 0;
            while (s <= v.size())
            {
                size_t e = v.find('|', s);
                if (e == std::string::npos) e = v.size();
                std::string m = Lower(Trim(v.substr(s, e - s)));
                if (!m.empty()) cur.match.push_back(m);
                s = e + 1;
            }
        }
        else if (k == "source") cur.source = Lower(v);
        else if (k == "verified") cur.verified = Lower(v) == "yes" || v == "1" || Lower(v) == "true";
        else if (k == "alt") cur.alt = v;
        else if (k == "hdg") cur.hdg = v;
        else if (k == "trk") cur.trk = v;
        else if (k == "ias") cur.ias = v;
        else if (k == "mach") cur.mach = v;
        else if (k == "mach_mode") cur.machMode = v;
        else if (k == "speed") cur.speed = v;
        else if (k == "vs") cur.vs = v;
        else if (k == "vs_mode") cur.vsMode = v;
        else if (k == "baro") cur.baro = v;
        else if (k == "baro_hpa") cur.baroHpa = v;
        else if (k == "baro_std") cur.baroStd = v;
    }
    bool hasFallback = false;
    for (auto& p : out.list) hasFallback |= p.match.empty();
    if (out.list.empty() || !hasFallback) return false;
    result = std::move(out);
    return true;
}

// First profile whose match text appears in id (lower-case title|path); else the fallback.
inline const Profile* FindProfile(const ProfileSet& set, const std::string& id)
{
    const Profile* fallback = nullptr;
    for (auto& p : set.list)
    {
        if (p.match.empty()) { if (!fallback) fallback = &p; continue; }
        for (auto& m : p.match) if (id.find(m) != std::string::npos) return &p;
    }
    return fallback;
}
}   // namespace ehsprof
