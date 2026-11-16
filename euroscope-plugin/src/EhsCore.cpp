#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "EhsCore.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>

namespace ehs
{
// ===================================================================== JSON (flat objects only)

namespace
{
struct JsonValue
{
    enum Kind { Null, Number, String, Bool } kind = Null;
    double num = 0;
    std::string str;
    bool b = false;
};

void SkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

bool ParseString(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"')
    {
        char c = s[i++];
        if (c == '\\' && i < s.size())
        {
            char e = s[i++];
            switch (e)
            {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': i += 4; out += '?'; break;   // never used by the protocol
                default: out += e; break;
            }
        }
        else out += c;
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
}

bool ParseFlatObject(const std::string& s, std::map<std::string, JsonValue>& out)
{
    size_t i = 0;
    SkipWs(s, i);
    if (i >= s.size() || s[i++] != '{') return false;
    while (true)
    {
        SkipWs(s, i);
        if (i < s.size() && s[i] == '}') return true;
        std::string key;
        if (!ParseString(s, i, key)) return false;
        SkipWs(s, i);
        if (i >= s.size() || s[i++] != ':') return false;
        SkipWs(s, i);
        if (i >= s.size()) return false;
        JsonValue v;
        if (s[i] == '"') { v.kind = JsonValue::String; if (!ParseString(s, i, v.str)) return false; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else if (s.compare(i, 4, "true") == 0) { v.kind = JsonValue::Bool; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.kind = JsonValue::Bool; i += 5; }
        else
        {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            v.num = std::strtod(start, &end);
            if (end == start) return false;   // nested objects/arrays aren't part of v1
            v.kind = JsonValue::Number;
            i += static_cast<size_t>(end - start);
        }
        out[key] = v;
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
}

std::optional<int> OptInt(const std::map<std::string, JsonValue>& m, const char* key)
{
    auto it = m.find(key);
    if (it == m.end() || it->second.kind != JsonValue::Number) return std::nullopt;
    return static_cast<int>(it->second.num >= 0 ? it->second.num + 0.5 : it->second.num - 0.5);
}
}   // namespace

bool ParsePayload(const std::string& json, Selections& out)
{
    std::map<std::string, JsonValue> m;
    if (!ParseFlatObject(json, m)) return false;
    auto v = m.find("v");
    if (v == m.end() || v->second.kind != JsonValue::Number || static_cast<int>(v->second.num) != 1) return false;
    auto cs = m.find("cs");
    if (cs == m.end() || cs->second.kind != JsonValue::String || cs->second.str.empty()) return false;

    out = Selections{};
    out.callsign = cs->second.str;
    if (auto it = m.find("src"); it != m.end() && it->second.kind == JsonValue::String) out.source = it->second.str;
    if (auto it = m.find("t"); it != m.end() && it->second.kind == JsonValue::Number) out.sentUnix = static_cast<int64_t>(it->second.num);
    auto lat = m.find("lat"), lon = m.find("lon");
    if (lat != m.end() && lon != m.end() && lat->second.kind == JsonValue::Number && lon->second.kind == JsonValue::Number)
    {
        out.lat = lat->second.num; out.lon = lon->second.num; out.hasPos = true;
    }
    out.alt = OptInt(m, "alt");
    out.hdg = OptInt(m, "hdg");
    out.ias = OptInt(m, "ias");
    out.vs = OptInt(m, "vs");
    if (auto it = m.find("mach"); it != m.end() && it->second.kind == JsonValue::Number) out.mach = it->second.num;
    if (auto it = m.find("trk"); it != m.end() && it->second.kind == JsonValue::Bool) out.trk = it->second.b;
    if (auto it = m.find("baro"); it != m.end() && it->second.kind == JsonValue::Number) out.baro = it->second.num;
    if (auto it = m.find("baroUnit"); it != m.end() && it->second.kind == JsonValue::String) out.baroUnit = it->second.str;
    if (auto it = m.find("ver"); it != m.end() && it->second.kind == JsonValue::Bool) out.verified = it->second.b;
    out.curIas = OptInt(m, "cias");
    if (auto it = m.find("cmach"); it != m.end() && it->second.kind == JsonValue::Number) out.curMach = it->second.num;
    return true;
}

// ===================================================================== Store

void Store::Put(const Selections& s)
{
    std::lock_guard<std::mutex> g(m_lock);
    m_data[s.callsign] = s;
}

void Store::Remove(const std::string& callsign)
{
    std::lock_guard<std::mutex> g(m_lock);
    m_data.erase(callsign);
}

std::optional<Selections> Store::Get(const std::string& callsign, uint64_t maxAgeMs) const
{
    std::lock_guard<std::mutex> g(m_lock);
    auto it = m_data.find(callsign);
    if (it == m_data.end() || GetTickCount64() - it->second.receivedTick > maxAgeMs) return std::nullopt;
    return it->second;
}

void Store::PurgeOlderThan(uint64_t maxAgeMs)
{
    std::lock_guard<std::mutex> g(m_lock);
    auto now = GetTickCount64();
    for (auto it = m_data.begin(); it != m_data.end();)
        it = (now - it->second.receivedTick > maxAgeMs) ? m_data.erase(it) : std::next(it);
}

size_t Store::Count(uint64_t maxAgeMs) const
{
    std::lock_guard<std::mutex> g(m_lock);
    auto now = GetTickCount64();
    size_t n = 0;
    for (auto& kv : m_data) if (now - kv.second.receivedTick <= maxAgeMs) ++n;
    return n;
}

// ===================================================================== Flight plan code

bool IsValidCodePrefix(const std::string& prefix)
{
    if (prefix.empty() || prefix.size() > 6) return false;
    for (char c : prefix) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool HasEhsCode(const std::string& remarks, const std::string& prefix)
{
    std::string up = remarks;
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string pre = prefix;
    for (auto& c : pre) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (size_t at = up.find("CODE/"); at != std::string::npos; at = up.find("CODE/", at + 1))
    {
        if (at > 0 && std::isalnum(static_cast<unsigned char>(up[at - 1]))) continue;   // e.g. "XCODE/"
        std::string code = up.substr(at + 5, 6);
        if (code.size() != 6) continue;
        bool hex = true;
        for (char c : code) hex &= std::isxdigit(static_cast<unsigned char>(c)) != 0;
        if (at + 11 < up.size() && std::isalnum(static_cast<unsigned char>(up[at + 11]))) hex = false;   // 7+ chars
        if (hex && code.compare(0, pre.size(), pre) == 0) return true;
    }
    return false;
}

bool HasEhsRemark(const std::string& remarks)
{
    std::string up = remarks;
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const std::string m = kRemarkMarker;
    for (size_t at = up.find(m); at != std::string::npos; at = up.find(m, at + 1))
    {
        bool startOk = at == 0 || !std::isalnum(static_cast<unsigned char>(up[at - 1]));
        size_t end = at + m.size();
        bool endOk = end >= up.size() || !std::isalnum(static_cast<unsigned char>(up[end]));
        if (startOk && endOk) return true;
    }
    return false;
}

// ===================================================================== Broker list

std::vector<Broker> ParseBrokerList(const std::string& text)
{
    std::vector<Broker> list;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(start, end - start);
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
        while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
        if (!item.empty())
        {
            Broker b;
            auto colon = item.find_first_of(":;");   // ';' form is used in ES settings (no ':' allowed)
            b.host = item.substr(0, colon);
            if (colon != std::string::npos) b.port = std::atoi(item.c_str() + colon + 1);
            if (b.port <= 0 || b.port > 65535) b.port = 1883;
            if (!b.host.empty()) list.push_back(b);
        }
        start = end + 1;
    }
    return list;
}

std::string FormatBrokerList(const std::vector<Broker>& list)
{
    std::string s;
    for (auto& b : list)
    {
        if (!s.empty()) s += ", ";
        s += b.host;
        if (b.port != 1883) s += ";" + std::to_string(b.port);   // ':' isn't allowed in ES settings values
    }
    return s;
}

// ===================================================================== MQTT subscriber

namespace
{
void PutString(std::string& buf, const std::string& s)
{
    buf += static_cast<char>((s.size() >> 8) & 0xFF);
    buf += static_cast<char>(s.size() & 0xFF);
    buf += s;
}

std::string Packet(unsigned char header, const std::string& body)
{
    std::string p;
    p += static_cast<char>(header);
    size_t len = body.size();
    do
    {
        unsigned char d = len % 128;
        len /= 128;
        if (len > 0) d |= 0x80;
        p += static_cast<char>(d);
    } while (len > 0);
    return p + body;
}

bool SendAll(SOCKET s, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        int r = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

// Waits up to timeoutMs for data. 1 = readable, 0 = timeout, -1 = error.
int WaitReadable(SOCKET s, int timeoutMs)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int r = select(0, &set, nullptr, nullptr, &tv);
    return r < 0 ? -1 : (r == 0 ? 0 : 1);
}

bool RecvExact(SOCKET s, char* buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        int r = recv(s, buf + got, static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

SOCKET ConnectTcp(const std::string& host, int port, int timeoutMs, std::string& error)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
    {
        error = "can't resolve " + host;
        return INVALID_SOCKET;
    }
    SOCKET result = INVALID_SOCKET;
    for (addrinfo* a = res; a && result == INVALID_SOCKET; a = a->ai_next)
    {
        SOCKET s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen));
        fd_set w, e;
        FD_ZERO(&w); FD_SET(s, &w);
        FD_ZERO(&e); FD_SET(s, &e);
        timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        if (select(0, nullptr, &w, &e, &tv) > 0 && FD_ISSET(s, &w))
        {
            nb = 0;
            ioctlsocket(s, FIONBIO, &nb);
            result = s;
        }
        else closesocket(s);
    }
    freeaddrinfo(res);
    if (result == INVALID_SOCKET) error = "can't connect to " + host;
    return result;
}
}   // namespace

void MqttSubscriber::SetTopics(std::set<std::string> topics)
{
    std::lock_guard<std::mutex> g(m_topicLock);
    m_wanted.swap(topics);
}

bool MqttSubscriber::SyncTopics(uintptr_t sock)
{
    std::vector<std::string> add, remove;
    {
        std::lock_guard<std::mutex> g(m_topicLock);
        for (auto& t : m_wanted) if (!m_subscribed.count(t)) add.push_back(t);
        for (auto& t : m_subscribed) if (!m_wanted.count(t)) remove.push_back(t);
    }
    SOCKET s = static_cast<SOCKET>(sock);
    const size_t kBatch = 50;   // topics per packet
    for (size_t i = 0; i < add.size(); i += kBatch)
    {
        std::string body;
        uint16_t id = ++m_packetId ? m_packetId : ++m_packetId;
        body += static_cast<char>(id >> 8); body += static_cast<char>(id & 0xFF);
        for (size_t k = i; k < add.size() && k < i + kBatch; ++k) { PutString(body, add[k]); body += static_cast<char>(0); }
        if (!SendAll(s, Packet(0x82, body))) return false;
        for (size_t k = i; k < add.size() && k < i + kBatch; ++k) m_subscribed.insert(add[k]);
    }
    for (size_t i = 0; i < remove.size(); i += kBatch)
    {
        std::string body;
        uint16_t id = ++m_packetId ? m_packetId : ++m_packetId;
        body += static_cast<char>(id >> 8); body += static_cast<char>(id & 0xFF);
        for (size_t k = i; k < remove.size() && k < i + kBatch; ++k) PutString(body, remove[k]);
        if (!SendAll(s, Packet(0xA2, body))) return false;
        for (size_t k = i; k < remove.size() && k < i + kBatch; ++k) m_subscribed.erase(remove[k]);
    }
    m_subscribedCount = m_subscribed.size();
    return true;
}

void MqttSubscriber::Start(std::vector<Broker> brokers, MessageHandler onMessage, StatusHandler onStatus)
{
    Stop();
    m_brokers = std::move(brokers);
    m_onMessage = std::move(onMessage);
    m_onStatus = std::move(onStatus);
    m_stop = false;
    m_thread = std::thread([this] { Run(); });
}

void MqttSubscriber::Stop()
{
    m_stop = true;
    uintptr_t s = m_socket.exchange(~uintptr_t(0));
    if (s != ~uintptr_t(0)) closesocket(static_cast<SOCKET>(s));   // unblocks select/recv
    if (m_thread.joinable()) m_thread.join();
    m_connected = false;
}

std::string MqttSubscriber::CurrentBroker() const
{
    std::lock_guard<std::mutex> g(m_brokerLock);
    return m_currentBroker;
}

bool MqttSubscriber::SleepInterruptible(int ms)
{
    for (int t = 0; t < ms && !m_stop; t += 100) Sleep(100);
    return !m_stop;
}

void MqttSubscriber::Run()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    size_t index = 0;
    while (!m_stop && !m_brokers.empty())
    {
        const Broker& b = m_brokers[index % m_brokers.size()];
        bool hadSession = Session(b);
        if (m_stop) break;
        if (!hadSession) ++index;   // couldn't connect: try the next broker
        SleepInterruptible(hadSession ? 2000 : 5000);
    }
    WSACleanup();
}

bool MqttSubscriber::Session(const Broker& b)
{
    std::string error;
    SOCKET s = ConnectTcp(b.host, b.port, 5000, error);
    if (s == INVALID_SOCKET) { Status("EHS Link: " + error + ", trying the next broker."); return false; }
    m_socket = static_cast<uintptr_t>(s);

    auto closeSocket = [&] {
        uintptr_t mine = m_socket.exchange(~uintptr_t(0));
        if (mine != ~uintptr_t(0)) closesocket(static_cast<SOCKET>(mine));
        m_connected = false;
    };

    // CONNECT (clean session, keep-alive 30 s)
    std::mt19937 rng(static_cast<unsigned>(GetTickCount64() ^ GetCurrentProcessId()));
    char id[24];
    sprintf_s(id, "ehsc-%08x", static_cast<unsigned>(rng()));
    std::string body;
    PutString(body, "MQTT");
    body += static_cast<char>(4);
    body += static_cast<char>(0x02);
    body += static_cast<char>(0);
    body += static_cast<char>(30);
    PutString(body, id);
    if (!SendAll(s, Packet(0x10, body))) { closeSocket(); return false; }

    char ack[4];
    if (WaitReadable(s, 5000) != 1 || !RecvExact(s, ack, 4) || static_cast<unsigned char>(ack[0]) != 0x20 || ack[3] != 0)
    {
        Status("EHS Link: " + b.host + " refused the connection, trying the next broker.");
        closeSocket();
        return false;
    }

    // New session: nothing is subscribed yet (clean session), so subscribe everything wanted.
    m_subscribed.clear();
    m_subscribedCount = 0;
    if (!SyncTopics(static_cast<uintptr_t>(s))) { closeSocket(); return false; }

    {
        std::lock_guard<std::mutex> g(m_brokerLock);
        m_currentBroker = b.host;
    }
    m_connected = true;
    Status("EHS Link connected to " + b.host + ".");

    uint64_t lastPing = GetTickCount64(), lastRx = GetTickCount64();
    std::string buf;
    while (!m_stop)
    {
        if (!SyncTopics(static_cast<uintptr_t>(s))) break;   // aircraft came into / left range
        int w = WaitReadable(s, 1000);
        uint64_t now = GetTickCount64();
        if (w < 0) break;
        if (w == 0)
        {
            if (now - lastPing >= 15000)
            {
                if (!SendAll(s, Packet(0xC0, ""))) break;
                lastPing = now;
            }
            if (now - lastRx > 60000) break;   // broker went silent (no PINGRESP)
            continue;
        }
        unsigned char header;
        if (!RecvExact(s, reinterpret_cast<char*>(&header), 1)) break;
        size_t len = 0, mult = 1;
        unsigned char d;
        bool ok = true;
        do
        {
            if (!RecvExact(s, reinterpret_cast<char*>(&d), 1)) { ok = false; break; }
            len += (d & 0x7F) * mult;
            mult *= 128;
        } while ((d & 0x80) && mult <= 128 * 128 * 128);
        if (!ok || len > 1 << 20) break;
        buf.resize(len);
        if (len > 0 && !RecvExact(s, &buf[0], len)) break;
        lastRx = GetTickCount64();

        if ((header >> 4) == 3 && len >= 2)   // PUBLISH
        {
            size_t tlen = (static_cast<unsigned char>(buf[0]) << 8) | static_cast<unsigned char>(buf[1]);
            size_t pos = 2 + tlen;
            if (((header >> 1) & 3) > 0) pos += 2;
            if (pos > len) continue;
            std::string topic = buf.substr(2, tlen);
            std::string payload = buf.substr(pos);
            if (m_onMessage) m_onMessage(topic, payload);
        }
    }
    bool wasStopping = m_stop;
    closeSocket();
    if (!wasStopping) Status("EHS Link lost the connection to " + b.host + ", reconnecting.");
    return true;
}
}   // namespace ehs
