#include "config.h"

#include <cstdlib>
#include <cstring>

#include "json_utils.h"

namespace garadget {

uint32_t ConfigManager::computeCrc32(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t ConfigManager::calcConfigCrc() const {
    return computeCrc32(&cfg_, offsetof(DeviceConfig, crc32));
}

void ConfigManager::resetDefaults() {
    std::memset(&cfg_, 0, sizeof(cfg_));
    cfg_.magic = DeviceConfig::kMagic;
    cfg_.schemaVersion = DeviceConfig::kSchemaVersion;

    std::strncpy(cfg_.deviceName, "Garage", sizeof(cfg_.deviceName) - 1);
    cfg_.readTimeMs = 1000;
    cfg_.motionTimeMs = 10000;
    cfg_.relayTimeMs = 300;
    cfg_.relayPauseMs = 1000;
    cfg_.sensorReads = 3;
    cfg_.sensorThreshold = 15;
    cfg_.sensorMode = static_cast<uint8_t>(SensorMode::Laser);

    cfg_.alertEvents = 0;
    cfg_.alertOpenTimeout = 0;
    cfg_.alertNightStart = 0;
    cfg_.alertNightEnd = 0;
    std::strncpy(cfg_.timeZone, "0", sizeof(cfg_.timeZone) - 1);
    std::strncpy(cfg_.ntpServer, "pool.ntp.org", sizeof(cfg_.ntpServer) - 1);

    cfg_.mqttEnabled = 0;
    cfg_.haDiscovery = 1;
    cfg_.mqttPort = 1883;
    cfg_.mqttKeepAliveSec = 15;
    cfg_.mqttHost[0] = '\0';
    cfg_.mqttUser[0] = '\0';
    cfg_.mqttPass[0] = '\0';
    std::strncpy(cfg_.haPrefix, "homeassistant", sizeof(cfg_.haPrefix) - 1);
    cfg_.adminPass[0] = '\0';

    cfg_.ipMode = 0; // DHCP by default
    cfg_.staticIp[0] = 0; cfg_.staticIp[1] = 0; cfg_.staticIp[2] = 0; cfg_.staticIp[3] = 0;
    cfg_.staticMask[0] = 255; cfg_.staticMask[1] = 255; cfg_.staticMask[2] = 255; cfg_.staticMask[3] = 0;
    cfg_.staticGw[0] = 0; cfg_.staticGw[1] = 0; cfg_.staticGw[2] = 0; cfg_.staticGw[3] = 0;
    cfg_.staticDns[0] = 0; cfg_.staticDns[1] = 0; cfg_.staticDns[2] = 0; cfg_.staticDns[3] = 0;
    cfg_.ledStealth = 0;
    cfg_.ledBrightness = 100;
    cfg_.ledPad = 0;

    tz_.setConfig(cfg_.timeZone);
    cfg_.crc32 = calcConfigCrc();
}

bool ConfigManager::load(ConfigStorage& storage) {
    DeviceConfig loaded{};
    if (!storage.read(&loaded, sizeof(loaded))) {
        resetDefaults();
        return false;
    }

    if (loaded.magic != DeviceConfig::kMagic) {
        resetDefaults();
        return false;
    }

    if (loaded.schemaVersion == 2) {
        // Seamless v2 -> v4 migration: v2 crc32 lived at offsetof(DeviceConfig, staticIp)
        constexpr size_t kV2CrcOffset = offsetof(DeviceConfig, staticIp);
        uint32_t storedV2Crc = 0;
        std::memcpy(&storedV2Crc, reinterpret_cast<const uint8_t*>(&loaded) + kV2CrcOffset, sizeof(uint32_t));
        uint32_t expectedV2Crc = computeCrc32(&loaded, kV2CrcOffset);
        if (storedV2Crc != expectedV2Crc) {
            resetDefaults();
            return false;
        }
        loaded.schemaVersion = DeviceConfig::kSchemaVersion;
        loaded.ipMode = 0;
        std::memset(loaded.staticIp, 0, sizeof(loaded.staticIp));
        loaded.staticMask[0] = 255; loaded.staticMask[1] = 255; loaded.staticMask[2] = 255; loaded.staticMask[3] = 0;
        std::memset(loaded.staticGw, 0, sizeof(loaded.staticGw));
        std::memset(loaded.staticDns, 0, sizeof(loaded.staticDns));
        loaded.ledStealth = 0;
        loaded.ledBrightness = 100;
        loaded.ledPad = 0;
    } else if (loaded.schemaVersion == 3) {
        // Seamless v3 -> v4 migration: same struct layout, v3 CRC covers identical bytes before crc32
        uint32_t expectedV3Crc = computeCrc32(&loaded, offsetof(DeviceConfig, crc32));
        if (loaded.crc32 != expectedV3Crc) {
            resetDefaults();
            return false;
        }
        loaded.schemaVersion = DeviceConfig::kSchemaVersion;
        loaded.ledStealth = 0;
        loaded.ledBrightness = 100;
        loaded.ledPad = 0;
    } else if (loaded.schemaVersion == DeviceConfig::kSchemaVersion) {
        uint32_t expectedCrc = computeCrc32(&loaded, offsetof(DeviceConfig, crc32));
        if (loaded.crc32 != expectedCrc) {
            resetDefaults();
            return false;
        }
    } else {
        resetDefaults();
        return false;
    }

    // Ensure all strings are NUL-terminated before validating
    loaded.deviceName[sizeof(loaded.deviceName) - 1] = '\0';
    loaded.timeZone[sizeof(loaded.timeZone) - 1] = '\0';
    loaded.ntpServer[sizeof(loaded.ntpServer) - 1] = '\0';
    loaded.mqttHost[sizeof(loaded.mqttHost) - 1] = '\0';
    loaded.mqttUser[sizeof(loaded.mqttUser) - 1] = '\0';
    loaded.mqttPass[sizeof(loaded.mqttPass) - 1] = '\0';
    loaded.haPrefix[sizeof(loaded.haPrefix) - 1] = '\0';
    loaded.adminPass[sizeof(loaded.adminPass) - 1] = '\0';

    cfg_ = loaded;
    if (!validate() || !tz_.setConfig(cfg_.timeZone)) {
        resetDefaults();
        return false;
    }
    return true;
}

bool ConfigManager::save(ConfigStorage& storage) {
    cfg_.magic = DeviceConfig::kMagic;
    cfg_.schemaVersion = DeviceConfig::kSchemaVersion;
    cfg_.crc32 = calcConfigCrc();
    return storage.write(&cfg_, sizeof(cfg_));
}

bool ConfigManager::validate() const {
    if (cfg_.readTimeMs < kMinReadIntervalMs || cfg_.readTimeMs > kMaxReadIntervalMs) return false;
    if (cfg_.motionTimeMs < kMinMotionTimeMs || cfg_.motionTimeMs > kMaxMotionTimeMs) return false;
    if (cfg_.relayTimeMs < kMinRelayOnMs || cfg_.relayTimeMs > kMaxRelayOnMs) return false;
    if (cfg_.relayPauseMs < kMinRelayPauseMs || cfg_.relayPauseMs > kMaxRelayPauseMs) return false;
    if (cfg_.sensorReads < kMinSensorReads || cfg_.sensorReads > kMaxSensorReads) return false;
    if (cfg_.sensorThreshold < kMinSensorThreshold || cfg_.sensorThreshold > kMaxSensorThreshold) return false;
    if (cfg_.sensorMode > static_cast<uint8_t>(SensorMode::SwitchNC)) return false;
    if (cfg_.alertEvents > kMaxAlertBitmap) return false;
    if (cfg_.alertOpenTimeout > kMaxAlertTimeoutSec) return false;
    if (cfg_.alertNightStart >= kMinutesPerDay || cfg_.alertNightEnd >= kMinutesPerDay) return false;
    if (cfg_.mqttPort == 0) return false;
    if (cfg_.mqttKeepAliveSec < kMinMqttKeepAliveSec || cfg_.mqttKeepAliveSec > kMaxMqttKeepAliveSec) return false;
    if (!isValidHostOrEmpty(cfg_.mqttHost)) return false;
    if (!isValidHostOrEmpty(cfg_.ntpServer)) return false;
    if (cfg_.ipMode > 1) return false;
    if (cfg_.ipMode == 1) {
        auto isNonZero = [](const uint8_t ip[4]) { return (ip[0] | ip[1] | ip[2] | ip[3]) != 0; };
        if (!isNonZero(cfg_.staticIp) || !isNonZero(cfg_.staticMask) || !isNonZero(cfg_.staticGw)) {
            return false;
        }
    }
    if (cfg_.ledStealth > 1 || cfg_.ledBrightness > 100) return false;
    return true;
}

bool ConfigManager::parseIntStrict(const char* str, int32_t& out) {
    if (!str || str[0] == '\0') return false;
    if (std::strcmp(str, "true") == 0) {
        out = 1;
        return true;
    }
    if (std::strcmp(str, "false") == 0) {
        out = 0;
        return true;
    }
    char* endPtr = nullptr;
    long val = std::strtol(str, &endPtr, 10);
    if (endPtr == str || *endPtr != '\0') return false;
    out = static_cast<int32_t>(val);
    return true;
}

bool ConfigManager::isValidHostOrEmpty(const char* host) {
    if (!host || host[0] == '\0') return true;
    for (size_t i = 0; host[i] != '\0'; ++i) {
        char c = host[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

int ConfigManager::setParam(const char* key, const char* value, ConfigScope scope) {
    if (!key || !value) return -1;
    const bool allowFull = (scope == ConfigScope::Full);

    if (std::strcmp(key, "ver") == 0 || std::strcmp(key, "sys") == 0 ||
        std::strcmp(key, "id") == 0 || std::strcmp(key, "ssid") == 0 ||
        std::strcmp(key, "ip") == 0 || std::strcmp(key, "mac") == 0 ||
        std::strcmp(key, "mqpwSet") == 0 || std::strcmp(key, "apassSet") == 0) {
        return 0; // Read-only informational fields
    }

    int32_t num = 0;

    if (std::strcmp(key, "nme") == 0) {
        char clean[kDeviceNameCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (clean[0] == '\0') return -1;
        if (std::strcmp(clean, cfg_.deviceName) == 0) return 0;
        std::strncpy(cfg_.deviceName, clean, sizeof(cfg_.deviceName) - 1);
        cfg_.deviceName[sizeof(cfg_.deviceName) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "rdt") == 0) {
        if (!parseIntStrict(value, num) || num < kMinReadIntervalMs || num > kMaxReadIntervalMs) return -1;
        if (cfg_.readTimeMs == static_cast<uint16_t>(num)) return 0;
        cfg_.readTimeMs = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "mtt") == 0) {
        if (!parseIntStrict(value, num) || num < static_cast<int32_t>(kMinMotionTimeMs) ||
            num > static_cast<int32_t>(kMaxMotionTimeMs)) return -1;
        if (cfg_.motionTimeMs == static_cast<uint32_t>(num)) return 0;
        cfg_.motionTimeMs = static_cast<uint32_t>(num);
        return 1;
    }

    if (std::strcmp(key, "rlt") == 0) {
        if (!parseIntStrict(value, num) || num < kMinRelayOnMs || num > kMaxRelayOnMs) return -1;
        if (cfg_.relayTimeMs == static_cast<uint16_t>(num)) return 0;
        cfg_.relayTimeMs = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "rlp") == 0) {
        if (!parseIntStrict(value, num) || num < kMinRelayPauseMs || num > kMaxRelayPauseMs) return -1;
        if (cfg_.relayPauseMs == static_cast<uint16_t>(num)) return 0;
        cfg_.relayPauseMs = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "srr") == 0) {
        if (!parseIntStrict(value, num) || num < kMinSensorReads || num > kMaxSensorReads) return -1;
        if (cfg_.sensorReads == static_cast<uint8_t>(num)) return 0;
        cfg_.sensorReads = static_cast<uint8_t>(num);
        return 1;
    }

    if (std::strcmp(key, "srt") == 0) {
        if (!parseIntStrict(value, num) || num < kMinSensorThreshold || num > kMaxSensorThreshold) return -1;
        if (cfg_.sensorThreshold == static_cast<uint8_t>(num)) return 0;
        cfg_.sensorThreshold = static_cast<uint8_t>(num);
        return 1;
    }

    if (std::strcmp(key, "smode") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num > static_cast<int32_t>(SensorMode::SwitchNC)) return -1;
        if (cfg_.sensorMode == static_cast<uint8_t>(num)) return 0;
        cfg_.sensorMode = static_cast<uint8_t>(num);
        return 1;
    }

    if (std::strcmp(key, "aev") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num > kMaxAlertBitmap) return -1;
        if (cfg_.alertEvents == static_cast<uint16_t>(num)) return 0;
        cfg_.alertEvents = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "aot") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num > kMaxAlertTimeoutSec) return -1;
        if (cfg_.alertOpenTimeout == static_cast<uint16_t>(num)) return 0;
        cfg_.alertOpenTimeout = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "ans") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num >= kMinutesPerDay) return -1;
        if (cfg_.alertNightStart == static_cast<uint16_t>(num)) return 0;
        cfg_.alertNightStart = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "ane") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num >= kMinutesPerDay) return -1;
        if (cfg_.alertNightEnd == static_cast<uint16_t>(num)) return 0;
        cfg_.alertNightEnd = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "tzo") == 0) {
        Timezone testTz;
        if (!testTz.setConfig(value)) return -1;
        if (std::strcmp(value, cfg_.timeZone) == 0) return 0;
        std::strncpy(cfg_.timeZone, value, sizeof(cfg_.timeZone) - 1);
        cfg_.timeZone[sizeof(cfg_.timeZone) - 1] = '\0';
        tz_ = testTz;
        return 1;
    }

    if (std::strcmp(key, "ntp") == 0) {
        if (!allowFull) return -1;
        char clean[kNtpServerCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (!isValidHostOrEmpty(clean)) return -1;
        if (std::strcmp(clean, cfg_.ntpServer) == 0) return 0;
        std::strncpy(cfg_.ntpServer, clean, sizeof(cfg_.ntpServer) - 1);
        cfg_.ntpServer[sizeof(cfg_.ntpServer) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "mqtt") == 0) {
        if (!allowFull) return -1;
        if (!parseIntStrict(value, num) || num < 0 || num > 3) return -1;
        // Accept modern 0/1 AND legacy Garadget bitmask (2=MQTT only, 3=MQTT+Cloud)
        uint8_t enabled = (num == 1 || (num & 0x02)) ? 1 : 0;
        if (cfg_.mqttEnabled == enabled) return 0;
        cfg_.mqttEnabled = enabled;
        return 1;
    }

    if (std::strcmp(key, "ha") == 0) {
        if (!allowFull) return -1;
        if (!parseIntStrict(value, num) || num < 0 || num > 1) return -1;
        uint8_t enabled = static_cast<uint8_t>(num);
        if (cfg_.haDiscovery == enabled) return 0;
        cfg_.haDiscovery = enabled;
        return 1;
    }

    if (std::strcmp(key, "hapfx") == 0) {
        if (!allowFull) return -1;
        char clean[kHaPrefixCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (clean[0] == '\0') return -1;
        for (size_t i = 0; clean[i]; ++i) {
            if (clean[i] == '+' || clean[i] == '#' || clean[i] == ' ') return -1;
        }
        if (std::strcmp(clean, cfg_.haPrefix) == 0) return 0;
        std::strncpy(cfg_.haPrefix, clean, sizeof(cfg_.haPrefix) - 1);
        cfg_.haPrefix[sizeof(cfg_.haPrefix) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "mqip") == 0 || std::strcmp(key, "mqhost") == 0) {
        if (!allowFull) return -1;
        char clean[kMqttHostCap];
        sanitizeAscii(clean, sizeof(clean), value);
        // Normalize legacy "0.0.0.0" to empty
        if (std::strcmp(clean, "0.0.0.0") == 0) {
            clean[0] = '\0';
        }
        if (!isValidHostOrEmpty(clean)) return -1;
        if (std::strcmp(clean, cfg_.mqttHost) == 0) return 0;
        std::strncpy(cfg_.mqttHost, clean, sizeof(cfg_.mqttHost) - 1);
        cfg_.mqttHost[sizeof(cfg_.mqttHost) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "mqpt") == 0) {
        if (!allowFull) return -1;
        if (!parseIntStrict(value, num) || num < 1 || num > 65535) return -1;
        if (cfg_.mqttPort == static_cast<uint16_t>(num)) return 0;
        cfg_.mqttPort = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "mqus") == 0) {
        if (!allowFull) return -1;
        char clean[kMqttUserCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (std::strcmp(clean, cfg_.mqttUser) == 0) return 0;
        std::strncpy(cfg_.mqttUser, clean, sizeof(cfg_.mqttUser) - 1);
        cfg_.mqttUser[sizeof(cfg_.mqttUser) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "mqpw") == 0) {
        if (!allowFull) return -1;
        char clean[kMqttPassCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (std::strcmp(clean, cfg_.mqttPass) == 0) return 0;
        std::strncpy(cfg_.mqttPass, clean, sizeof(cfg_.mqttPass) - 1);
        cfg_.mqttPass[sizeof(cfg_.mqttPass) - 1] = '\0';
        return 1;
    }

    if (std::strcmp(key, "mqto") == 0) {
        if (!allowFull) return -1;
        if (!parseIntStrict(value, num) || num < 1) return -1;
        // Gracefully convert legacy millisecond values (e.g. 5000 -> 5s)
        if (num >= 1000) {
            num /= 1000;
        }
        if (num < kMinMqttKeepAliveSec) num = kMinMqttKeepAliveSec;
        if (num > kMaxMqttKeepAliveSec) num = kMaxMqttKeepAliveSec;
        if (cfg_.mqttKeepAliveSec == static_cast<uint16_t>(num)) return 0;
        cfg_.mqttKeepAliveSec = static_cast<uint16_t>(num);
        return 1;
    }

    if (std::strcmp(key, "apass") == 0) {
        if (!allowFull) return -1;
        char clean[kAdminPassCap];
        sanitizeAscii(clean, sizeof(clean), value);
        if (std::strlen(clean) < kMinAdminPassLen) return -1;
        if (std::strcmp(clean, cfg_.adminPass) == 0) return 0;
        std::strncpy(cfg_.adminPass, clean, sizeof(cfg_.adminPass) - 1);
        cfg_.adminPass[sizeof(cfg_.adminPass) - 1] = '\0';
        return 1;
    }

    auto parseIpv4Field = [](const char* s, uint8_t out[4], bool allowEmpty) -> bool {
        if (!s || s[0] == '\0') {
            if (!allowEmpty) return false;
            out[0] = out[1] = out[2] = out[3] = 0;
            return true;
        }
        int octet = 0, val = 0, digits = 0;
        for (size_t i = 0; ; ++i) {
            char c = s[i];
            if (c >= '0' && c <= '9') {
                val = val * 10 + (c - '0');
                if (val > 255) return false;
                digits++;
            } else if (c == '.' || c == '\0') {
                if (digits == 0 || octet >= 4) return false;
                out[octet++] = static_cast<uint8_t>(val);
                val = 0;
                digits = 0;
                if (c == '\0') break;
            } else {
                return false;
            }
        }
        return octet == 4;
    };

    if (std::strcmp(key, "ipm") == 0) {
        if (!allowFull) return -1;
        if (!parseIntStrict(value, num) || (num != 0 && num != 1)) return -1;
        if (cfg_.ipMode == static_cast<uint8_t>(num)) return 0;
        cfg_.ipMode = static_cast<uint8_t>(num);
        return 1;
    }

    if (std::strcmp(key, "sip") == 0 || std::strcmp(key, "smask") == 0 ||
        std::strcmp(key, "sgw") == 0 || std::strcmp(key, "sdns") == 0) {
        if (!allowFull) return -1;
        uint8_t parsed[4];
        if (!parseIpv4Field(value, parsed, true)) return -1;
        uint8_t* dst = (std::strcmp(key, "sip") == 0) ? cfg_.staticIp
                     : (std::strcmp(key, "smask") == 0) ? cfg_.staticMask
                     : (std::strcmp(key, "sgw") == 0) ? cfg_.staticGw
                     : cfg_.staticDns;
        if (std::memcmp(dst, parsed, 4) == 0) return 0;
        std::memcpy(dst, parsed, 4);
        return 1;
    }

    if (std::strcmp(key, "leds") == 0) {
        uint8_t next = 0;
        if (std::strcmp(value, "ON") == 0 || std::strcmp(value, "on") == 0 ||
            std::strcmp(value, "true") == 0 || std::strcmp(value, "1") == 0) {
            next = 1;
        } else if (std::strcmp(value, "OFF") == 0 || std::strcmp(value, "off") == 0 ||
                   std::strcmp(value, "false") == 0 || std::strcmp(value, "0") == 0) {
            next = 0;
        } else {
            return -1;
        }
        if (cfg_.ledStealth == next) return 0;
        cfg_.ledStealth = next;
        return 1;
    }

    if (std::strcmp(key, "ledb") == 0) {
        if (!parseIntStrict(value, num) || num < 0 || num > 100) return -1;
        if (cfg_.ledBrightness == static_cast<uint8_t>(num)) return 0;
        cfg_.ledBrightness = static_cast<uint8_t>(num);
        return 1;
    }

    return -1;
}

int ConfigManager::applyPipeConfig(const char* pipeStr, ConfigScope scope) {
    if (!pipeStr || pipeStr[0] == '\0') return -1;
    if (std::strcmp(pipeStr, "defaults") == 0) {
        if (scope != ConfigScope::Full) return -1;
        resetDefaults();
        return 1;
    }

    DeviceConfig backup = cfg_;
    Timezone tzBackup = tz_;
    int updates = 0;
    const char* cur = pipeStr;
    while (*cur) {
        const char* eq = std::strchr(cur, '=');
        if (!eq) {
            cfg_ = backup;
            tz_ = tzBackup;
            return -1;
        }

        size_t klen = static_cast<size_t>(eq - cur);
        if (klen == 0 || klen >= 32) {
            cfg_ = backup;
            tz_ = tzBackup;
            return -1;
        }
        char key[32];
        std::memcpy(key, cur, klen);
        key[klen] = '\0';

        const char* valStart = eq + 1;
        const char* pipe = std::strchr(valStart, '|');
        size_t vlen = pipe ? static_cast<size_t>(pipe - valStart) : std::strlen(valStart);
        if (vlen >= 96) {
            cfg_ = backup;
            tz_ = tzBackup;
            return -1;
        }
        char val[96];
        std::memcpy(val, valStart, vlen);
        val[vlen] = '\0';

        int res = setParam(key, val, scope);
        if (res < 0) {
            cfg_ = backup;
            tz_ = tzBackup;
            return -1;
        }
        if (res > 0) updates++;

        if (!pipe) break;
        cur = pipe + 1;
    }
    if (!validate()) {
        cfg_ = backup;
        tz_ = tzBackup;
        return -1;
    }
    return updates;
}

int ConfigManager::applyJsonConfig(const char* json, size_t len, ConfigScope scope) {
    DeviceConfig backup = cfg_;
    Timezone tzBackup = tz_;
    struct ApplyCtx {
        ConfigManager* self;
        ConfigScope scope;
        int updates;
        bool hadError;
    } ctx{this, scope, 0, false};

    auto cb = [](const char* key, const char* value, bool /*isString*/, void* user) -> bool {
        auto* c = static_cast<ApplyCtx*>(user);
        int res = c->self->setParam(key, value, c->scope);
        if (res < 0) {
            c->hadError = true;
            return false; // Fail fast and roll back atomically
        }
        if (res > 0) c->updates++;
        return true;
    };

    if (!parseFlatJsonObject(json, len, cb, &ctx) || ctx.hadError || !validate()) {
        cfg_ = backup;
        tz_ = tzBackup;
        return -1;
    }
    return ctx.updates;
}

bool ConfigManager::toJson(char* out, size_t outCap,
                           const char* deviceId,
                           const char* systemVersion,
                           const char* wifiSsid,
                           const char* ipStr,
                           const char* macStr) const {
    JsonWriter w(out, outCap);
    w.beginObject();
    w.keyString("ver", kFirmwareVersion);
    w.keyString("sys", systemVersion ? systemVersion : "");
    w.keyString("id", deviceId ? deviceId : "");
    w.keyString("ssid", wifiSsid ? wifiSsid : "");
    w.keyString("ip", ipStr ? ipStr : "");
    w.keyString("mac", macStr ? macStr : "");
    w.keyString("nme", cfg_.deviceName);

    w.keyUInt("rdt", cfg_.readTimeMs);
    w.keyUInt("mtt", cfg_.motionTimeMs);
    w.keyUInt("rlt", cfg_.relayTimeMs);
    w.keyUInt("rlp", cfg_.relayPauseMs);
    w.keyUInt("srr", cfg_.sensorReads);
    w.keyUInt("srt", cfg_.sensorThreshold);
    w.keyUInt("smode", cfg_.sensorMode);

    w.keyUInt("aev", cfg_.alertEvents);
    w.keyUInt("aot", cfg_.alertOpenTimeout);
    w.keyUInt("ans", cfg_.alertNightStart);
    w.keyUInt("ane", cfg_.alertNightEnd);
    w.keyString("tzo", cfg_.timeZone);
    w.keyString("ntp", cfg_.ntpServer);

    w.keyUInt("mqtt", cfg_.mqttEnabled);
    w.keyUInt("ha", cfg_.haDiscovery);
    w.keyString("hapfx", cfg_.haPrefix);
    w.keyString("mqip", cfg_.mqttHost);
    w.keyString("mqhost", cfg_.mqttHost);
    w.keyUInt("mqpt", cfg_.mqttPort);
    w.keyString("mqus", cfg_.mqttUser);
    w.keyBool("mqpwSet", cfg_.mqttPass[0] != '\0');
    w.keyUInt("mqto", cfg_.mqttKeepAliveSec);
    w.keyBool("apassSet", cfg_.adminPass[0] != '\0');

    auto fmtIp = [](const uint8_t ip[4], char outIp[16]) {
        if ((ip[0] | ip[1] | ip[2] | ip[3]) == 0) {
            outIp[0] = '\0';
        } else {
            std::snprintf(outIp, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        }
    };
    char ipBuf[16];
    w.keyUInt("ipm", cfg_.ipMode);
    fmtIp(cfg_.staticIp, ipBuf);   w.keyString("sip", ipBuf);
    fmtIp(cfg_.staticMask, ipBuf); w.keyString("smask", ipBuf);
    fmtIp(cfg_.staticGw, ipBuf);   w.keyString("sgw", ipBuf);
    fmtIp(cfg_.staticDns, ipBuf);  w.keyString("sdns", ipBuf);
    w.keyUInt("leds", cfg_.ledStealth);
    w.keyUInt("ledb", cfg_.ledBrightness);
    w.endObject();
    return !w.truncated();
}

void ConfigManager::buildTopicId(char* out, size_t outCap, const char* fallbackId) const {
    if (!out || outCap == 0) return;
    const char* src = (cfg_.deviceName[0] != '\0') ? cfg_.deviceName : (fallbackId ? fallbackId : "garadget");
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < outCap; ++i) {
        char c = src[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok) {
            out[j++] = c;
        } else if (c == ' ') {
            out[j++] = '_';
        }
    }
    if (j == 0) {
        std::strncpy(out, "garadget", outCap - 1);
        out[outCap - 1] = '\0';
    } else {
        out[j] = '\0';
    }
}

} // namespace garadget
