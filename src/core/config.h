#pragma once

#include <cstddef>
#include <cstdint>

#include "timezone.h"
#include "types.h"
#include "../version.h"

namespace garadget {

// EEPROM-persisted configuration with magic header, schema version, and CRC32 integrity check.
// Must remain well under Particle P1's 2047-byte emulated EEPROM limit.
struct DeviceConfig {
    static constexpr uint32_t kMagic = 0x47444732u; // "GDG2"
    static constexpr uint16_t kSchemaVersion = 3;

    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t reserved;

    // Device identity
    char deviceName[kDeviceNameCap];

    // Sensor & door timing
    uint16_t readTimeMs;        // rdt: sensor scan interval (200..60000 ms, default 1000)
    uint32_t motionTimeMs;      // mtt: full door travel time (1000..120000 ms, default 10000)
    uint16_t relayTimeMs;       // rlt: relay pulse duration (10..2000 ms, default 300)
    uint16_t relayPauseMs;      // rlp: pause between multi-click pulses (10..5000 ms, default 1000)
    uint8_t  sensorReads;       // srr: samples averaged per laser scan (1..20, default 3)
    uint8_t  sensorThreshold;   // srt: reflection % threshold for closed (1..80, default 15)
    uint8_t  sensorMode;        // smode: 0=Laser, 1=SwitchNO, 2=SwitchNC

    // Alerts & time
    uint16_t alertEvents;       // aev: 9-bit status alert bitmap
    uint16_t alertOpenTimeout;  // aot: open timeout alert in seconds (0=off, max 43200)
    uint16_t alertNightStart;   // ans: night window start in minutes from midnight (0..1439)
    uint16_t alertNightEnd;     // ane: night window end in minutes from midnight (0..1439)
    char timeZone[kTimeZoneCap];
    char ntpServer[kNtpServerCap];

    // MQTT & Home Assistant
    uint8_t  mqttEnabled;       // mqtt: 0=off, 1=on (also accepts legacy bitmask: bit 1 => on)
    uint8_t  haDiscovery;       // ha: 0=off, 1=on (default 1)
    uint16_t mqttPort;          // mqpt: broker port (1..65535, default 1883)
    uint16_t mqttKeepAliveSec;  // mqto: keepalive/reconnect seconds (5..300, default 15)
    char mqttHost[kMqttHostCap];     // mqhost / mqip: IPv4 or hostname
    char mqttUser[kMqttUserCap];     // mqus: optional username
    char mqttPass[kMqttPassCap];     // mqpw: optional password
    char haPrefix[kHaPrefixCap];     // hapfx: HA discovery prefix (default "homeassistant")

    // Optional local Web UI admin password (empty = open on LAN)
    char adminPass[kAdminPassCap];   // apass

    // Optional Static IPv4 & DNS configuration (added in schema v3)
    uint8_t staticIp[4];             // sip
    uint8_t staticMask[4];           // smask
    uint8_t staticGw[4];             // sgw
    uint8_t staticDns[4];            // sdns
    uint8_t ipMode;                  // ipm: 0 = DHCP, 1 = Static IP
    uint8_t ipPad[3];

    uint32_t crc32;
};

static_assert(sizeof(DeviceConfig) < 1024, "DeviceConfig must comfortably fit in P1 EEPROM (2047 bytes)");

// Abstract storage backend (EEPROM on P1, in-memory buffer in unit tests).
class ConfigStorage {
public:
    virtual ~ConfigStorage() = default;
    virtual bool read(void* dst, size_t len) = 0;
    virtual bool write(const void* src, size_t len) = 0;
};

class ConfigManager {
public:
    ConfigManager() = default;

    // Loads from storage, verifies magic + schema + CRC32 + field bounds; resets to defaults if invalid.
    bool load(ConfigStorage& storage);

    // Recomputes CRC32 and persists to storage.
    bool save(ConfigStorage& storage);

    // Resets in-memory config to safe defaults (does not automatically write storage unless saved).
    void resetDefaults();

    // Validates all fields in current config.
    bool validate() const;

    // Sets a single parameter by key & string value.
    // Returns: -1 on validation error, 0 if unchanged, +1 if updated.
    // Set allowAdminPass=false on untrusted channels (MQTT, SoftAP) to prevent changing `apass`.
    int setParam(const char* key, const char* value, bool allowAdminPass = true);

    // Parses legacy pipe-delimited string ("rdt=1000|srt=20|nme=Garage") or "defaults".
    // Returns number of updated parameters, or -1 on parse failure.
    int applyPipeConfig(const char* pipeStr, bool allowAdminPass = true);

    // Parses JSON object ("{\"srt\":20,\"mqhost\":\"192.168.1.5\"}").
    // Returns number of updated parameters, or -1 on parse/validation failure.
    int applyJsonConfig(const char* json, size_t len, bool allowAdminPass = true);

    // Writes safe JSON representation (secrets redacted as boolean flags).
    void toJson(char* out, size_t outCap,
                const char* deviceId,
                const char* systemVersion,
                const char* wifiSsid,
                const char* ipStr,
                const char* macStr) const;

    // Computes IEEE 802.3 CRC32 over buffer.
    static uint32_t computeCrc32(const void* data, size_t len);

    const DeviceConfig& data() const { return cfg_; }
    DeviceConfig& mutableData() { return cfg_; }
    const Timezone& timezone() const { return tz_; }

    // Builds an MQTT-safe topic slug from deviceName (alphanumeric, '-', '_'; falls back to fallbackId).
    void buildTopicId(char* out, size_t outCap, const char* fallbackId) const;

private:
    static bool parseIntStrict(const char* str, int32_t& out);
    static bool isValidHostOrEmpty(const char* host);
    uint32_t calcConfigCrc() const;

    DeviceConfig cfg_{};
    Timezone tz_{};
};

} // namespace garadget
