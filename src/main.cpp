/**
 * Garadget Local Firmware v3.2.2
 * Target: Particle P1 (STM32F205 + BCM43362) running Particle Device OS 3.3.1
 *
 * Completely local, cloud-free rewrite:
 *  - Zero Particle Cloud dependencies (SYSTEM_MODE(MANUAL) + SYSTEM_THREAD(ENABLED))
 *  - Safe, non-blocking door/laser/switch/relay state machine + ApplicationWatchdog
 *  - Local LAN modern Web UI + REST JSON API (port 80)
 *  - SoftAP Wi-Fi provisioning portal (192.168.0.1)
 *  - Non-blocking MQTT 3.1.1 + Home Assistant Auto-Discovery
 *  - Local SNTP time synchronization + DST-aware timezones + Alerts
 */

#include "Particle.h"

#include <cstring>

#include "cli/serial_cli.h"
#include "core/alert_engine.h"
#include "core/config.h"
#include "core/door_controller.h"
#include "hal/hardware.h"
#include "net/ha_mqtt.h"
#include "net/sntp_client.h"
#include "net/softap_portal.h"
#include "net/web_server.h"
#include "net/wifi_manager.h"
#include "version.h"

SYSTEM_MODE(MANUAL);
SYSTEM_THREAD(ENABLED);

STARTUP(
    WiFi.selectAntenna(GARADGET_ANT_MODE);
    softap_set_application_page_handler(garadget::softapPageHandler, nullptr);
);

SerialLogHandler gLogHandler(LOG_LEVEL_WARN, {{"app", LOG_LEVEL_INFO}});

// Hardware/Application watchdog: automatically resets P1 if loop() ever stalls > 60s
static void onWatchdogTimeout() {
    System.reset();
}
ApplicationWatchdog gWatchdog(60000, onWatchdogTimeout);

namespace {

garadget::EepromStorage gStorage;
garadget::ConfigManager gConfig;
garadget::P1DoorHardware gHardware;
garadget::DoorController gDoor(gHardware, gConfig.data());
garadget::AlertEngine gAlerts(gConfig.data());
garadget::WifiManager gWifi;
garadget::SntpClient gSntp(gConfig);
garadget::HaMqttBridge gMqtt(gConfig, gStorage, gDoor, gAlerts, gWifi);
garadget::WebServer gWeb(gConfig, gStorage, gDoor, gAlerts, gWifi, gSntp, gMqtt);
garadget::SerialCli gCli(gConfig, gStorage, gDoor, gWifi, gMqtt);

bool gPendingReboot = false;
garadget::Timer gRebootTimer;
uint8_t gLastIpMode = 0;
uint8_t gLastIpCfg[16]{};
uint32_t gLastMqttHash = 0;
bool gLastWifiListening = false;

uint32_t hashMqttCfg(const garadget::DeviceConfig& c) {
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    mix(&c.mqttEnabled, sizeof(c.mqttEnabled));
    mix(&c.haDiscovery, sizeof(c.haDiscovery));
    mix(&c.mqttPort, sizeof(c.mqttPort));
    mix(&c.mqttKeepAliveSec, sizeof(c.mqttKeepAliveSec));
    mix(c.mqttHost, std::strlen(c.mqttHost));
    mix(c.mqttUser, std::strlen(c.mqttUser));
    mix(c.mqttPass, std::strlen(c.mqttPass));
    mix(c.haPrefix, std::strlen(c.haPrefix));
    mix(c.deviceName, std::strlen(c.deviceName));
    return h;
}

void applyLedSettings() {
    if (gWifi.isListening()) {
        // Keep LED clearly visible during SoftAP Wi-Fi setup mode
        RGB.brightness(128);
        return;
    }
    const auto& cfg = gConfig.data();
    if (cfg.ledStealth) {
        RGB.brightness(0);
    } else {
        uint8_t level = static_cast<uint8_t>((static_cast<uint16_t>(cfg.ledBrightness) * 255u) / 100u);
        RGB.brightness(level);
    }
}

void snapshotIpCfg(const garadget::DeviceConfig& c) {
    gLastIpMode = c.ipMode;
    std::memcpy(gLastIpCfg + 0, c.staticIp, 4);
    std::memcpy(gLastIpCfg + 4, c.staticMask, 4);
    std::memcpy(gLastIpCfg + 8, c.staticGw, 4);
    std::memcpy(gLastIpCfg + 12, c.staticDns, 4);
}

bool ipCfgChanged(const garadget::DeviceConfig& c) {
    return c.ipMode != gLastIpMode ||
           std::memcmp(gLastIpCfg + 0, c.staticIp, 4) != 0 ||
           std::memcmp(gLastIpCfg + 4, c.staticMask, 4) != 0 ||
           std::memcmp(gLastIpCfg + 8, c.staticGw, 4) != 0 ||
           std::memcmp(gLastIpCfg + 12, c.staticDns, 4) != 0;
}

void onDoorStatusChanged(garadget::DoorStatus oldStatus, garadget::DoorStatus newStatus, void*) {
    uint32_t now = millis();
    Log.info("Door: %s -> %s",
             garadget::doorStatusToString(oldStatus),
             garadget::doorStatusToString(newStatus));
    gAlerts.onStatusChange(newStatus, now);
    gMqtt.publishStatus(now);
}

void onAlertFired(const char* type, const char* data, const char* jsonPayload, void*) {
    Log.info("Alert [%s]: %s", type, data);
    gMqtt.publishAlert(jsonPayload);
}

void onConfigSaved(void*) {
    uint32_t now = millis();
    gHardware.configureSensorMode(static_cast<garadget::SensorMode>(gConfig.data().sensorMode));
    gSntp.applyTimezoneNow();
    applyLedSettings();
    uint32_t newMqttHash = hashMqttCfg(gConfig.data());
    if (newMqttHash != gLastMqttHash) {
        gLastMqttHash = newMqttHash;
        gMqtt.reconfigure(now);
    } else {
        gMqtt.publishConfig();
    }
    if (ipCfgChanged(gConfig.data())) {
        snapshotIpCfg(gConfig.data());
        gWifi.applyIpConfig(gConfig.data());
        // Cleanly reboot after HTTP response flushes so new Static IP / DHCP lease takes effect
        gPendingReboot = true;
        gRebootTimer.start(now, 500);
    }
}

void onSystemAction(const char* action, void*) {
    if (!action) return;
    uint32_t now = millis();
    if (std::strcmp(action, "scan") == 0) {
        gDoor.forceScan(now);
        gMqtt.publishStatus(now);
    } else if (std::strcmp(action, "sync-time") == 0) {
        gSntp.requestSync(now);
    } else if (std::strcmp(action, "ha-rediscover") == 0) {
        gMqtt.triggerDiscoveryRefresh();
    } else if (std::strcmp(action, "wifi-setup") == 0) {
        gWifi.enterSetupMode();
    } else if (std::strcmp(action, "clear-wifi") == 0) {
        Log.warn("System: clearing saved Wi-Fi credentials and entering setup mode");
        if (gConfig.data().ipMode != 0) {
            gConfig.mutableData().ipMode = 0;
            gConfig.save(gStorage);
            snapshotIpCfg(gConfig.data());
        }
        gWifi.clearCredentials();
        gWifi.enterSetupMode();
    } else if (std::strcmp(action, "reboot") == 0) {
        Log.info("System: reboot requested");
        gPendingReboot = true;
        gRebootTimer.start(now, 400);
    } else if (std::strcmp(action, "factory-reset") == 0) {
        Log.warn("System: factory reset requested");
        gConfig.resetDefaults();
        gConfig.save(gStorage);
        gWifi.clearCredentials();
        gPendingReboot = true;
        gRebootTimer.start(now, 500);
    }
}

void checkModeButton() {
    if (gPendingReboot) return;
    uint32_t heldMs = System.buttonPushed();
    if (heldMs >= 10000u) {
        onSystemAction("factory-reset", nullptr);
    } else if (heldMs >= 3000u && !gWifi.isListening()) {
        gWifi.enterSetupMode();
    }
}

} // namespace

void setup() {
    uint32_t now = millis();

    gCli.init();
    Log.info("Garadget Local v%s booting (Device OS %s)",
             garadget::kFirmwareVersion, System.version().c_str());

    if (!gConfig.load(gStorage)) {
        Log.warn("Config: EEPROM empty or CRC mismatch, initialized defaults");
        gConfig.save(gStorage);
    }

    gHardware.init(static_cast<garadget::SensorMode>(gConfig.data().sensorMode));

    gDoor.setStatusCallback(onDoorStatusChanged, nullptr);
    gDoor.init(now);

    gAlerts.setCallback(onAlertFired, nullptr);
    gAlerts.onInit(gDoor.status(), now);

    garadget::initSoftApPortal(&gConfig, &gStorage, &gWifi);
    snapshotIpCfg(gConfig.data());
    gLastMqttHash = hashMqttCfg(gConfig.data());
    gWifi.init(now, gConfig.data());
    gLastWifiListening = gWifi.isListening();
    applyLedSettings();
    gSntp.init();
    gSntp.applyTimezoneNow();

    gMqtt.setHooks(onConfigSaved, onSystemAction, nullptr);
    gMqtt.init(now);

    gWeb.setHooks(onConfigSaved, onSystemAction, nullptr);
    gWeb.init();

    gCli.setHooks(onConfigSaved, onSystemAction, nullptr);
}

void loop() {
    uint32_t now = millis();
    ApplicationWatchdog::checkin();

    if (gPendingReboot && gRebootTimer.expired(now)) {
        System.reset();
    }

    checkModeButton();

    // 1. Hardware safety & door control always runs first
    gDoor.tick(now);

    // 2. Alerts evaluation (uses local SNTP-synced RTC if valid)
    bool timeValid = Time.isValid();
    uint16_t localMin = timeValid ? static_cast<uint16_t>(Time.hour() * 60 + Time.minute()) : 0;
    gAlerts.tick(gDoor.status(), now, timeValid, localMin);

    // 3. Local network services (never Particle Cloud)
    gWifi.tick(now);
    bool listeningNow = gWifi.isListening();
    if (listeningNow != gLastWifiListening) {
        gLastWifiListening = listeningNow;
        if (!listeningNow) {
            // Sync config change snapshots in case settings were updated during SoftAP setup
            snapshotIpCfg(gConfig.data());
            gLastMqttHash = hashMqttCfg(gConfig.data());
            gSntp.applyTimezoneNow();
        }
        applyLedSettings();
    }
    gSntp.tick(now, gWifi.isReady());
    gMqtt.tick(now);
    gWeb.tick(now);

    // 4. USB Serial CLI
    gCli.tick(now);
}
