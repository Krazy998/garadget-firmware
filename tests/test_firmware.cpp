#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/alert_engine.h"
#include "core/config.h"
#include "core/door_controller.h"
#include "core/http_security.h"
#include "core/json_utils.h"
#include "core/ota_validator.h"
#include "core/timer.h"
#include "core/timezone.h"
#include "core/types.h"
#include "net/ha_mqtt.h"
#include "net/mqtt_client.h"

using namespace garadget;

namespace {

int gTestsRun = 0;

#define RUN_TEST(fn)                                    \
    do {                                                \
        fn();                                           \
        gTestsRun++;                                    \
        std::printf("  [PASS] %s\n", #fn);              \
    } while (0)

class MemStorage : public ConfigStorage {
public:
    uint8_t buf[2048]{};
    bool read(void* dst, size_t len) override {
        if (len > sizeof(buf)) return false;
        std::memcpy(dst, buf, len);
        return true;
    }
    bool write(const void* src, size_t len) override {
        if (len > sizeof(buf)) return false;
        std::memcpy(buf, src, len);
        return true;
    }
};

class MockHardware : public DoorHardware {
public:
    bool relayState = false;
    int relayOnCount = 0;
    int relayOffCount = 0;
    SensorReading nextReading{80, 1000, 75};

    void setRelay(bool active) override {
        if (active && !relayState) relayOnCount++;
        if (!active && relayState) relayOffCount++;
        relayState = active;
    }

    SensorReading readSensor(SensorMode /*mode*/, uint8_t /*samples*/) override {
        return nextReading;
    }
};

void testTimerRollover() {
    Timer t(500);
    uint32_t nearMax = 0xFFFFFE00u; // 512 ms before rollover
    t.start(nearMax);
    assert(t.isRunning());
    assert(!t.expired(nearMax + 499u));
    assert(t.isRunning());
    assert(t.expired(nearMax + 500u));
    assert(!t.isRunning());
    // Must not fire twice
    assert(!t.expired(nearMax + 600u));

    // Rollover right across 0xFFFFFFFF -> 0
    uint32_t wrapStart = 0xFFFFFF00u;
    t.start(wrapStart, 512u);
    assert(!t.expired(0x000000FFu));
    assert(t.expired(0x00000100u));
}

void testJsonWriterAndParser() {
    char buf[256];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.keyString("name", "Bob's \"Garage\" \\ Door\n");
    w.keyInt("rssi", -62);
    w.keyUInt("srt", 25);
    w.keyBool("ha", true);
    w.endObject();
    assert(!w.truncated());
    assert(std::strstr(buf, "\"name\":\"Bob's \\\"Garage\\\" \\\\ Door\\n\"") != nullptr);
    assert(std::strstr(buf, "\"rssi\":-62") != nullptr);
    assert(std::strstr(buf, "\"ha\":true") != nullptr);

    // Parse it back
    struct Ctx {
        char name[64] = {};
        int rssi = 0;
        bool ha = false;
        int fields = 0;
    } ctx;

    bool ok = parseFlatJsonObject(
        buf, std::strlen(buf),
        [](const char* k, const char* v, bool /*isStr*/, void* u) -> bool {
            auto* c = static_cast<Ctx*>(u);
            c->fields++;
            if (std::strcmp(k, "name") == 0) std::strncpy(c->name, v, sizeof(c->name) - 1);
            if (std::strcmp(k, "rssi") == 0) c->rssi = std::atoi(v);
            if (std::strcmp(k, "ha") == 0) c->ha = (std::strcmp(v, "true") == 0);
            return true;
        },
        &ctx);
    assert(ok);
    assert(ctx.fields == 4);
    assert(std::strcmp(ctx.name, "Bob's \"Garage\" \\ Door\n") == 0);
    assert(ctx.rssi == -62);
    assert(ctx.ha == true);

    // Truncation safety
    char tiny[12];
    JsonWriter tw(tiny, sizeof(tiny));
    tw.beginObject();
    tw.keyString("longkey", "longvalue");
    tw.endObject();
    assert(tw.truncated());
    assert(std::strlen(tiny) < sizeof(tiny));
}

void testTimezoneNorthAndSouth() {
    Timezone tz;

    // Fixed offset with optional disambiguating letter
    assert(tz.setConfig("-7.00A"));
    assert(!tz.hasDst());
    assert(tz.offsetMinutesForUtc(1700000000) == -420);

    // Half-hour offset (India +5:30)
    assert(tz.setConfig("5.5"));
    assert(tz.offsetMinutesForUtc(1700000000) == 330);

    // Northern Hemisphere: US Mountain (MST -7 on 1st Sun Nov 2am, MDT -6 on 2nd Sun Mar 2am)
    assert(tz.setConfig("10112-7.00,20032-6.00"));
    assert(tz.hasDst());
    // 2026-01-15 12:00 UTC -> Winter (MST = -420)
    assert(tz.offsetMinutesForUtc(1768478400) == -420);
    // 2026-07-15 12:00 UTC -> Summer (MDT = -360)
    assert(tz.offsetMinutesForUtc(1784116800) == -360);

    // Southern Hemisphere: Sydney (AEST +10 on 1st Sun Apr 3am, AEDT +11 on 1st Sun Oct 2am)
    assert(tz.setConfig("10043+10.00,10102+11.00"));
    assert(tz.hasDst());
    // 2026-01-15 12:00 UTC -> Australian Summer (AEDT = +660)
    assert(tz.offsetMinutesForUtc(1768478400) == 660);
    // 2026-07-15 12:00 UTC -> Australian Winter (AEST = +600)
    assert(tz.offsetMinutesForUtc(1784116800) == 600);
    // 2026-11-20 00:00 UTC -> Australian Summer (AEDT = +660)
    assert(tz.offsetMinutesForUtc(1795132800) == 660);

    // Invalid rules rejected
    assert(!tz.setConfig(""));
    assert(!tz.setConfig("99999+5"));
}

void testConfigPersistenceAndValidation() {
    MemStorage storage;
    ConfigManager cfg;

    // Empty EEPROM -> fails CRC & loads defaults
    assert(!cfg.load(storage));
    assert(std::strcmp(cfg.data().deviceName, "Garage") == 0);
    assert(cfg.data().readTimeMs == 1000);
    assert(cfg.data().sensorThreshold == 15);
    assert(cfg.data().haDiscovery == 1);

    // Update via JSON (including legacy fields & ms->s conversion)
    const char* j = "{\"nme\":\"Main Door\",\"srt\":28,\"mqtt\":2,\"mqhost\":\"mqtt.home.lan\",\"mqpt\":1883,\"mqpw\":\"secret123\",\"mqto\":5000,\"smode\":1}";
    int updated = cfg.applyJsonConfig(j, std::strlen(j));
    assert(updated == 7);
    assert(cfg.data().mqttEnabled == 1);
    assert(cfg.data().mqttKeepAliveSec == 5);
    assert(cfg.data().sensorMode == 1);
    assert(cfg.save(storage));

    // Reload from storage -> succeeds with intact data
    ConfigManager cfg2;
    assert(cfg2.load(storage));
    assert(std::strcmp(cfg2.data().deviceName, "Main Door") == 0);
    assert(std::strcmp(cfg2.data().mqttHost, "mqtt.home.lan") == 0);
    assert(cfg2.data().sensorThreshold == 28);

    // Corrupt a byte in EEPROM -> CRC catches it and resets cleanly
    storage.buf[20] ^= 0xFF;
    ConfigManager cfg3;
    assert(!cfg3.load(storage));
    assert(cfg3.data().sensorThreshold == 15);

    // JSON serialization must redact passwords
    char jsonOut[1024];
    cfg2.toJson(jsonOut, sizeof(jsonOut), "abc123", "3.3.1", "HomeWiFi", "192.168.1.50", "AA:BB:CC:DD:EE:FF");
    assert(std::strstr(jsonOut, "secret123") == nullptr);
    assert(std::strstr(jsonOut, "\"mqpwSet\":true") != nullptr);

    // Topic slug sanitization
    char slug[32];
    cfg2.buildTopicId(slug, sizeof(slug), "fallback");
    assert(std::strcmp(slug, "Main_Door") == 0);

    // Out-of-range values rejected
    assert(cfg2.setParam("srt", "0") == -1);
    assert(cfg2.setParam("srt", "81") == -1);
    assert(cfg2.setParam("rlt", "5000") == -1);
    assert(cfg2.setParam("mqpt", "0") == -1);
}

void testLaserMathSafety() {
    // Normal reflected laser: ambient ADC 3000, laser hits reflector -> ADC drops to 600
    int32_t base1[3] = {3000, 3000, 3000};
    int32_t lit1[3] = {600, 600, 600};
    uint8_t refl1 = DoorController::computeLaserReflection(base1, lit1, 3, 100);
    // delta=2400, effBase=2900 => 82%
    assert(refl1 == 82);

    // Extreme sunlight (base < bias): must NOT underflow!
    int32_t base2[3] = {50, 40, 20};
    int32_t lit2[3] = {10, 10, 5};
    uint8_t refl2 = DoorController::computeLaserReflection(base2, lit2, 3, 100);
    assert(refl2 == 0);

    // Ambient got dimmer during pulse (lit > base): must NOT underflow!
    int32_t base3[3] = {2000, 2000, 2000};
    int32_t lit3[3] = {2500, 2500, 2500};
    uint8_t refl3 = DoorController::computeLaserReflection(base3, lit3, 3, 100);
    assert(refl3 == 0);
}

void testDoorControllerStateMachine() {
    ConfigManager cfgMgr;
    cfgMgr.resetDefaults();
    cfgMgr.setParam("rdt", "1000");
    cfgMgr.setParam("mtt", "10000");
    cfgMgr.setParam("rlt", "300");
    cfgMgr.setParam("rlp", "1000");
    cfgMgr.setParam("srt", "20");

    MockHardware hw;
    hw.nextReading = {80, 1000, 75}; // Above threshold => Closed

    DoorController door(hw, cfgMgr.data());
    std::vector<DoorStatus> transitions;
    door.setStatusCallback(
        [](DoorStatus, DoorStatus next, void* ctx) {
            static_cast<std::vector<DoorStatus>*>(ctx)->push_back(next);
        },
        &transitions);

    // Boot scan sets Closed without firing spurious transition
    door.init(0);
    assert(door.status() == DoorStatus::Closed);
    assert(!hw.relayState);
    assert(transitions.empty());

    // Open command from Closed => 1 click, stays Closed until sensor drops
    assert(door.executeCommand(DoorCommand::Open, 100));
    assert(hw.relayState);
    assert(door.status() == DoorStatus::Closed);
    // Duplicate command rejected while relay busy
    assert(!door.executeCommand(DoorCommand::Open, 200));

    // Relay releases after 300ms
    door.tick(400);
    assert(!hw.relayState);
    assert(hw.relayOnCount == 1 && hw.relayOffCount == 1);

    // Door starts moving off reflector -> sensor reads 5% -> Opening
    hw.nextReading = {5, 2500, 38};
    door.tick(1100);
    assert(door.status() == DoorStatus::Opening);
    assert(transitions.size() == 1 && transitions.back() == DoorStatus::Opening);

    // After motionTimeMs (10000ms), Opening -> Open
    door.tick(11100);
    assert(door.status() == DoorStatus::Open);
    assert(transitions.back() == DoorStatus::Open);

    // Close command from Open -> 1 click, status becomes Closing immediately
    assert(door.executeCommand(DoorCommand::Close, 12000));
    assert(door.status() == DoorStatus::Closing);
    assert(hw.relayState);
    door.tick(12300);
    assert(!hw.relayState);

    // Door obstructed (reflection stays low after mtt) -> Stopped
    door.tick(22000);
    assert(door.status() == DoorStatus::Stopped);

    // Open from Stopped requires 2 clicks!
    int prevOn = hw.relayOnCount;
    assert(door.executeCommand(DoorCommand::Open, 23000));
    assert(door.status() == DoorStatus::Opening);
    assert(hw.relayState);
    door.tick(23300); // End click 1
    assert(!hw.relayState);
    door.tick(24300); // Pause ends -> start click 2
    assert(hw.relayState);
    door.tick(24600); // End click 2
    assert(!hw.relayState);
    assert(hw.relayOnCount == prevOn + 2);
}

void testAlertEngine() {
    ConfigManager cfgMgr;
    cfgMgr.resetDefaults();
    cfgMgr.setParam("aev", "2");    // Open alert only (bit 1)
    cfgMgr.setParam("aot", "60");   // 60s open timeout
    cfgMgr.setParam("ans", "1320"); // 22:00
    cfgMgr.setParam("ane", "360");  // 06:00

    AlertEngine alerts(cfgMgr.data());
    std::vector<std::string> fired;
    alerts.setCallback(
        [](const char* type, const char* data, const char* /*json*/, void* ctx) {
            auto* vec = static_cast<std::vector<std::string>*>(ctx);
            vec->push_back(std::string(type) + ":" + data);
        },
        &fired);

    alerts.onInit(DoorStatus::Closed, 0);
    assert(fired.empty());

    // Transition to Open fires "state:open"
    alerts.onStatusChange(DoorStatus::Open, 1000);
    assert(fired.size() == 1 && fired[0] == "state:open");

    // Daytime (14:00 = 840 min): no night alert; after 60s fires timeout alert
    alerts.tick(DoorStatus::Open, 30000, true, 840);
    assert(fired.size() == 1);
    alerts.tick(DoorStatus::Open, 61000, true, 840);
    assert(fired.size() == 2 && fired[1] == "timeout:60s");

    // Nighttime (23:00 = 1380 min): fires night alert once
    alerts.tick(DoorStatus::Open, 62000, true, 1380);
    assert(fired.size() == 3 && fired[2] == "night:1320-360");
    alerts.tick(DoorStatus::Open, 63000, true, 1380);
    assert(fired.size() == 3); // Must not repeat

    // Closing resets timeout & night alert latches
    alerts.onStatusChange(DoorStatus::Closed, 70000);
    assert(alerts.historyCount() == 3);
    assert(std::strcmp(alerts.historyItem(0).type, "night") == 0);

    // Changing aot while door is already open reschedules timer dynamically
    fired.clear();
    alerts.onStatusChange(DoorStatus::Open, 80000);
    assert(fired.size() == 1); // state:open
    cfgMgr.setParam("aot", "15");
    alerts.tick(DoorStatus::Open, 90000, true, 840);
    assert(fired.size() == 1); // 10s elapsed < 15s
    alerts.tick(DoorStatus::Open, 95000, true, 840);
    assert(fired.size() == 2 && fired[1] == "timeout:15s");
}

void testMqttAndHaDiscovery() {
    uint8_t ip[4];
    assert(MqttClient::parseIpv4("192.168.1.42", ip));
    assert(ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 42);
    assert(!MqttClient::parseIpv4("mqtt.home.lan", ip));
    assert(!MqttClient::parseIpv4("999.1.1.1", ip));

    uint8_t enc[4];
    assert(MqttClient::encodeRemainingLength(127, enc) == 1 && enc[0] == 127);
    assert(MqttClient::encodeRemainingLength(128, enc) == 2 && enc[0] == 0x80 && enc[1] == 0x01);
    assert(MqttClient::encodeRemainingLength(321, enc) == 2 && enc[0] == 0xC1 && enc[1] == 0x02);

    ConfigManager cfgMgr;
    cfgMgr.resetDefaults();
    cfgMgr.setParam("nme", "Garage");
    char topic[128];
    char payload[768];

    for (uint8_t i = 0; i < HaMqttBridge::kDiscoveryEntityCount; ++i) {
        bool ok = HaMqttBridge::buildDiscoveryPayload(
            i, cfgMgr.data(), "1a2b3c4d", "Garage", "192.168.1.77",
            topic, sizeof(topic), payload, sizeof(payload));
        assert(ok);
        assert(std::strncmp(topic, "homeassistant/", 14) == 0);
        assert(std::strstr(payload, "\"avty_t\":\"garadget/Garage/LWT\"") != nullptr);
    }

    // Verify cover entity specifics (index 0)
    assert(HaMqttBridge::buildDiscoveryPayload(
        0, cfgMgr.data(), "1a2b3c4d", "Garage", "192.168.1.77",
        topic, sizeof(topic), payload, sizeof(payload)));
    assert(std::strcmp(topic, "homeassistant/cover/1a2b3c4d_door/config") == 0);
    assert(std::strstr(payload, "\"dev_cla\":\"garage\"") != nullptr);
    assert(std::strstr(payload, "\"cu\":\"http://192.168.1.77/\"") != nullptr);
    assert(std::strstr(payload, "\"stat_stopped\":\"stopped\"") != nullptr);

    // Verify Stealth Mode Switch (index 8) & LED Brightness Number (index 9)
    assert(HaMqttBridge::buildDiscoveryPayload(
        8, cfgMgr.data(), "1a2b3c4d", "Garage", "192.168.1.77",
        topic, sizeof(topic), payload, sizeof(payload)));
    assert(std::strcmp(topic, "homeassistant/switch/1a2b3c4d_stealth/config") == 0);
    assert(std::strstr(payload, "\"cmd_t\":\"garadget/Garage/set-stealth\"") != nullptr);
    assert(std::strstr(payload, "\"pl_on\":\"ON\"") != nullptr);

    assert(HaMqttBridge::buildDiscoveryPayload(
        9, cfgMgr.data(), "1a2b3c4d", "Garage", "192.168.1.77",
        topic, sizeof(topic), payload, sizeof(payload)));
    assert(std::strcmp(topic, "homeassistant/number/1a2b3c4d_led_brightness/config") == 0);
    assert(std::strstr(payload, "\"cmd_t\":\"garadget/Garage/set-led-brightness\"") != nullptr);
}

void testOtaValidator() {
    // Construct a synthetic valid Particle P1 user-part binary (1024 bytes)
    std::vector<uint8_t> bin(1024, 0xAA);
    auto wr32le = [&](size_t off, uint32_t v) {
        bin[off + 0] = static_cast<uint8_t>(v & 0xFF);
        bin[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        bin[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        bin[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    wr32le(0, 0x080A0000u);            // start_addr
    wr32le(4, 0x080A0000u + 1020u);    // end_addr
    bin[12] = 8; bin[13] = 0;          // platform_id = 8 (P1)
    bin[14] = 5;                       // module_func = 5 (user-part)

    // Append big-endian IEEE 802.3 CRC32 over bytes [0..1020)
    uint32_t crc = ConfigManager::computeCrc32(bin.data(), bin.size() - 4);
    bin[1020] = static_cast<uint8_t>((crc >> 24) & 0xFF);
    bin[1021] = static_cast<uint8_t>((crc >> 16) & 0xFF);
    bin[1022] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    bin[1023] = static_cast<uint8_t>(crc & 0xFF);

    assert(OtaValidator::validateHeader(bin.data(), bin.size()) == nullptr);

    // Stream in uneven chunks (e.g. 200 bytes each)
    OtaValidator v;
    v.reset(bin.size());
    for (size_t off = 0; off < bin.size(); off += 200) {
        size_t n = (bin.size() - off < 200) ? (bin.size() - off) : 200;
        v.feed(bin.data() + off, n);
    }
    assert(v.verifyCrc());

    // Wrong platform (6 = Photon) must be rejected
    auto badPlat = bin;
    badPlat[12] = 6;
    assert(std::strcmp(OtaValidator::validateHeader(badPlat.data(), badPlat.size()), "wrong_platform") == 0);

    // Wrong start address must be rejected
    auto badStart = bin;
    badStart[2] = 0x06;
    assert(std::strcmp(OtaValidator::validateHeader(badStart.data(), badStart.size()), "invalid_start_addr") == 0);

    // Oversized binary (>128KB) must be rejected
    assert(std::strcmp(OtaValidator::validateHeader(bin.data(), 131073), "invalid_size") == 0);

    // Length mismatch (truncated or padded relative to endAddr - startAddr + 4) must be rejected
    assert(std::strcmp(OtaValidator::validateHeader(bin.data(), bin.size() - 4), "length_mismatch") == 0);
    assert(std::strcmp(OtaValidator::validateHeader(bin.data(), bin.size() + 4), "length_mismatch") == 0);

    // Single-bit flip in payload must fail CRC32
    auto corrupted = bin;
    corrupted[500] ^= 0x01;
    v.reset(corrupted.size());
    v.feed(corrupted.data(), corrupted.size());
    assert(!v.verifyCrc());
}

void testHttpSecurityAndAcl() {
    // 1. Header scanner must NOT scan past \r\n\r\n into body
    const char* reqWithBodySmuggle =
        "POST /api/command HTTP/1.1\r\n"
        "Host: 192.168.1.50\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "X-Auth-Token: secret\r\n";
    size_t hdrEnd = static_cast<size_t>((std::strstr(reqWithBodySmuggle, "\r\n\r\n") - reqWithBodySmuggle) + 4);
    assert(findHttpHeaderValue(reqWithBodySmuggle, hdrEnd, "x-auth-token") == nullptr);
    assert(!verifyAuthToken(reqWithBodySmuggle, hdrEnd, "secret"));

    // 2. Constant-time auth token check (rejects prefix/suffix/wrong length)
    const char* authedReq =
        "POST /api/command HTTP/1.1\r\n"
        "Host: 192.168.1.50\r\n"
        "Origin: http://192.168.1.50\r\n"
        "Content-Type: application/json\r\n"
        "X-Auth-Token: MyPass123\r\n\r\n";
    size_t aLen = std::strlen(authedReq);
    assert(verifyAuthToken(authedReq, aLen, "MyPass123"));
    assert(!verifyAuthToken(authedReq, aLen, "MyPass12"));
    assert(!verifyAuthToken(authedReq, aLen, "MyPass1234"));

    // 3. CSRF protection: same-origin JSON passes; cross-origin or text/plain fails
    assert(verifyCsrfProtection(authedReq, aLen, "POST", "/api/command"));

    const char* crossOriginReq =
        "POST /api/command HTTP/1.1\r\n"
        "Host: 192.168.1.50\r\n"
        "Origin: http://evil.example\r\n"
        "Content-Type: application/json\r\n\r\n";
    assert(!verifyCsrfProtection(crossOriginReq, std::strlen(crossOriginReq), "POST", "/api/command"));

    const char* simpleTextPlainCsrf =
        "POST /api/command HTTP/1.1\r\n"
        "Host: 192.168.1.50\r\n"
        "Content-Type: text/plain\r\n\r\n";
    assert(!verifyCsrfProtection(simpleTextPlainCsrf, std::strlen(simpleTextPlainCsrf), "POST", "/api/command"));

    const char* validOtaReq =
        "POST /api/ota HTTP/1.1\r\n"
        "Host: 192.168.1.50\r\n"
        "Content-Type: application/octet-stream\r\n\r\n";
    assert(verifyCsrfProtection(validOtaReq, std::strlen(validOtaReq), "POST", "/api/ota"));

    // 4. Per-IP auth rate limiter locks out offending IP after 5 failures without blocking another IP
    AuthRateLimiter lim;
    const uint32_t ipA = 0xC0A8010Au;
    const uint32_t ipB = 0xC0A8010Bu;
    for (int i = 0; i < 4; ++i) {
        lim.recordFailure(1000, ipA);
        assert(!lim.isLockedOut(1000, ipA));
    }
    lim.recordFailure(1000, ipA);
    assert(lim.isLockedOut(1000, ipA));
    assert(!lim.isLockedOut(1000, ipB)); // Other client unaffected!
    assert(lim.isLockedOut(10999, ipA));
    assert(!lim.isLockedOut(11000, ipA));

    // 5. Mandatory apass length + untrusted channels (MQTT) cannot modify apass or reset defaults; atomic rollback
    ConfigManager cfg;
    cfg.resetDefaults();
    assert(!cfg.hasAdminPassword());
    assert(cfg.setParam("apass", "", true) == -1);     // Empty rejected
    assert(cfg.setParam("apass", "abc", true) == -1);  // < 4 chars rejected
    assert(cfg.setParam("apass", "Admin123", true) == 1);
    assert(cfg.hasAdminPassword());

    // Atomic JSON rollback: forbidden key or invalid field rejects whole payload with zero partial writes
    const char* mqttAttempt = "{\"apass\":\"NewPass\",\"srt\":22}";
    assert(cfg.applyJsonConfig(mqttAttempt, std::strlen(mqttAttempt), false) == -1);
    assert(std::strcmp(cfg.data().adminPass, "Admin123") == 0); // Unchanged!
    assert(cfg.data().sensorThreshold == 15);                   // Rolled back!

    const char* partialBadJson = "{\"nme\":\"Changed\",\"srt\":99}";
    assert(cfg.applyJsonConfig(partialBadJson, std::strlen(partialBadJson), true) == -1);
    assert(std::strcmp(cfg.data().deviceName, "Garage") == 0);  // Rolled back!

    // Read-only keys from GET /api/config are cleanly ignored on re-import
    const char* withReadOnly = "{\"ssid\":\"Net\",\"ip\":\"1.2.3.4\",\"mac\":\"AA:BB\",\"mqpwSet\":false,\"apassSet\":true,\"srt\":22}";
    assert(cfg.applyJsonConfig(withReadOnly, std::strlen(withReadOnly), false) == 1);
    assert(cfg.data().sensorThreshold == 22);

    // Pipe config must validate & roll back atomically (prevents ipm=1 without sip/sgw corrupting EEPROM)
    assert(cfg.applyPipeConfig("defaults", false) == -1);
    assert(cfg.applyPipeConfig("nme=Temp|ipm=1", true) == -1);
    assert(cfg.data().ipMode == 0);
    assert(std::strcmp(cfg.data().deviceName, "Garage") == 0);

    // Full worst-case toJson() fits comfortably in 1152-byte buffer without truncation
    {
        ConfigManager maxCfg;
        maxCfg.resetDefaults();
        maxCfg.setParam("nme", "Very Long Double Garage Door 01");
        maxCfg.setParam("tzo", "10043+10.00,10102+11.00");
        maxCfg.setParam("ntp", "ntp-primary.internal.home.example.local.domain.test");
        maxCfg.setParam("mqhost", "mqtt-broker-01.internal.home.example.local.domain.test");
        maxCfg.setParam("mqus", "garadget_homeassistant_user");
        maxCfg.setParam("hapfx", "homeassistant_custom_prefix");
        maxCfg.applyJsonConfig("{\"ipm\":1,\"sip\":\"192.168.100.200\",\"smask\":\"255.255.255.0\",\"sgw\":\"192.168.100.254\",\"sdns\":\"192.168.100.253\"}", 107);
        char bigJson[1152];
        maxCfg.toJson(bigJson, sizeof(bigJson), "1234567890abcdef12345678", "3.3.1",
                      "MySuperLongHomeWifiNetworkName32", "192.168.100.200", "AA:BB:CC:DD:EE:FF");
        size_t jlen = std::strlen(bigJson);
        assert(jlen < sizeof(bigJson) - 1);
        assert(bigJson[jlen - 1] == '}');
    }

    // 6. Static IP & DNS validation + atomic rollback on incomplete static IP config
    const char* badStatic = "{\"ipm\":1}"; // Missing sip/sgw -> must reject & roll back!
    assert(cfg.applyJsonConfig(badStatic, std::strlen(badStatic)) == -1);
    assert(cfg.data().ipMode == 0);

    const char* goodStatic = "{\"ipm\":1,\"sip\":\"192.168.50.20\",\"smask\":\"255.255.255.0\",\"sgw\":\"192.168.50.1\",\"sdns\":\"1.1.1.1\"}";
    assert(cfg.applyJsonConfig(goodStatic, std::strlen(goodStatic)) > 0);
    assert(cfg.data().ipMode == 1);
    assert(cfg.data().staticIp[0] == 192 && cfg.data().staticIp[3] == 20);
    assert(cfg.data().staticDns[0] == 1 && cfg.data().staticDns[3] == 1);

    // 7. Seamless schema v2 -> v3 EEPROM migration preserves settings
    MemStorage migStorage;
    DeviceConfig v2{};
    ConfigManager tmp;
    tmp.resetDefaults();
    tmp.setParam("nme", "Workshop");
    tmp.setParam("srt", "33");
    v2 = tmp.data();
    v2.schemaVersion = 2;
    constexpr size_t kV2CrcOff = offsetof(DeviceConfig, staticIp);
    uint32_t v2Crc = ConfigManager::computeCrc32(&v2, kV2CrcOff);
    std::memcpy(reinterpret_cast<uint8_t*>(&v2) + kV2CrcOff, &v2Crc, sizeof(uint32_t));
    migStorage.write(&v2, sizeof(v2));

    ConfigManager migrated;
    assert(migrated.load(migStorage));
    assert(migrated.data().schemaVersion == 4);
    assert(std::strcmp(migrated.data().deviceName, "Workshop") == 0);
    assert(migrated.data().sensorThreshold == 33);
    assert(migrated.data().ipMode == 0);
    assert(migrated.data().ledStealth == 0);
    assert(migrated.data().ledBrightness == 100);

    // 8. LED Stealth Mode & Brightness validation + v3 -> v4 migration
    assert(migrated.setParam("leds", "ON") == 1 && migrated.data().ledStealth == 1);
    assert(migrated.setParam("leds", "OFF") == 1 && migrated.data().ledStealth == 0);
    assert(migrated.setParam("leds", "maybe") == -1);
    assert(migrated.setParam("ledb", "25") == 1 && migrated.data().ledBrightness == 25);
    assert(migrated.setParam("ledb", "101") == -1);
    assert(migrated.setParam("ledb", "-5") == -1);

    // Simulate schema v3 EEPROM blob -> migrates seamlessly to v4 preserving Static IP
    MemStorage v3Storage;
    DeviceConfig v3 = cfg.data();
    v3.schemaVersion = 3;
    v3.ledStealth = 0;
    v3.ledBrightness = 0;
    v3.crc32 = ConfigManager::computeCrc32(&v3, offsetof(DeviceConfig, crc32));
    v3Storage.write(&v3, sizeof(v3));

    ConfigManager migV3;
    assert(migV3.load(v3Storage));
    assert(migV3.data().schemaVersion == 4);
    assert(migV3.data().ipMode == 1);
    assert(migV3.data().staticIp[3] == 20);
    assert(migV3.data().ledStealth == 0);
    assert(migV3.data().ledBrightness == 100);
}

} // namespace

int main() {
    std::printf("Running Garadget Local Firmware unit tests...\n");
    RUN_TEST(testTimerRollover);
    RUN_TEST(testJsonWriterAndParser);
    RUN_TEST(testTimezoneNorthAndSouth);
    RUN_TEST(testConfigPersistenceAndValidation);
    RUN_TEST(testLaserMathSafety);
    RUN_TEST(testDoorControllerStateMachine);
    RUN_TEST(testAlertEngine);
    RUN_TEST(testMqttAndHaDiscovery);
    RUN_TEST(testOtaValidator);
    RUN_TEST(testHttpSecurityAndAcl);
    std::printf("All %d test suites passed!\n", gTestsRun);
    return 0;
}
