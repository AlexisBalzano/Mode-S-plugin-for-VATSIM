// EHS Link core: MQTT subscriber, payload parsing and the received-data store.
// No EuroScope dependency, so it can be tested from a console program.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ehs
{
constexpr const char* kTopicRoot = "ehslink/v1/";

// One pilot's downlinked selections (see PROTOCOL.md).
struct Selections
{
    std::string callsign;
    std::string source;
    double lat = 0, lon = 0;
    bool hasPos = false;
    std::optional<int> alt, hdg, ias, vs;
    std::optional<double> mach;
    bool trk = false;
    std::optional<double> baro;   // altimeter setting, hPa
    std::string baroUnit;         // "hPa", "inHg" or empty (unknown)
    bool verified = true;         // "ver": profile checked against the cockpit (absent = true)
    std::optional<int> curIas;    // current indicated airspeed, kt
    std::optional<double> curMach;// current Mach
    int64_t sentUnix = 0;
    uint64_t receivedTick = 0;   // GetTickCount64() when received
};

// Parses a v1 payload. Returns false if it isn't a valid v1 message.
bool ParsePayload(const std::string& json, Selections& out);

// Thread-safe map callsign -> latest selections.
class Store
{
public:
    void Put(const Selections& s);
    void Remove(const std::string& callsign);
    // Returns data no older than maxAgeMs (by local receive time).
    std::optional<Selections> Get(const std::string& callsign, uint64_t maxAgeMs) const;
    void PurgeOlderThan(uint64_t maxAgeMs);
    size_t Count(uint64_t maxAgeMs) const;

private:
    mutable std::mutex m_lock;
    std::map<std::string, Selections> m_data;
};

// Pilots announce EHS Link in their flight plan with a Mode S code (ICAO 24-bit address,
// 6 hex digits) starting with this prefix, e.g. CODE/FE5000 filed from SimBrief.
// 0xF... addresses aren't allocated to real aircraft, so a real airframe code never matches.
constexpr const char* kDefaultCodePrefix = "FE5";

// True if remarks contain CODE/<6 hex digits> starting with prefix (case-insensitive).
bool HasEhsCode(const std::string& remarks, const std::string& prefix);
bool IsValidCodePrefix(const std::string& prefix);   // 1-6 hex digits

struct Broker
{
    std::string host;
    int port = 1883;
};

std::vector<Broker> ParseBrokerList(const std::string& text);   // "host[:port], host[:port]"
std::string FormatBrokerList(const std::vector<Broker>& list);

// Minimal MQTT 3.1.1 subscriber (QoS 0) running on its own thread, with broker failover
// and automatic reconnect. The set of subscribed topics can change at any time: the network
// thread subscribes/unsubscribes the difference within a second and resubscribes everything
// after a reconnect.
class MqttSubscriber
{
public:
    using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;
    using StatusHandler = std::function<void(const std::string& text)>;

    ~MqttSubscriber() { Stop(); }
    void Start(std::vector<Broker> brokers, MessageHandler onMessage, StatusHandler onStatus);
    void Stop();
    void SetTopics(std::set<std::string> topics);   // any thread
    bool IsConnected() const { return m_connected; }
    size_t SubscribedCount() const { return m_subscribedCount; }
    std::string CurrentBroker() const;

private:
    void Run();
    bool Session(const Broker& b);   // returns when the connection ends
    bool SyncTopics(uintptr_t sock); // network thread: send the subscribe/unsubscribe difference
    void Status(const std::string& s) { if (m_onStatus) m_onStatus(s); }
    bool SleepInterruptible(int ms);

    std::vector<Broker> m_brokers;
    std::mutex m_topicLock;
    std::set<std::string> m_wanted;       // guarded by m_topicLock
    std::set<std::string> m_subscribed;   // network thread only
    std::atomic<size_t> m_subscribedCount{0};
    uint16_t m_packetId = 0;
    MessageHandler m_onMessage;
    StatusHandler m_onStatus;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    std::atomic<uintptr_t> m_socket{~uintptr_t(0)};
    mutable std::mutex m_brokerLock;
    std::string m_currentBroker;
};
}   // namespace ehs
