#include "serial_cli.h"

#ifdef PARTICLE
#include "Particle.h"
#include <cstring>

#include "../version.h"

namespace garadget {

SerialCli::SerialCli(ConfigManager& config,
                     ConfigStorage& storage,
                     DoorController& door,
                     WifiManager& wifi,
                     HaMqttBridge& mqtt)
    : config_(config), storage_(storage), door_(door), wifi_(wifi), mqtt_(mqtt) {}

void SerialCli::init() {
    Serial.begin(115200);
}

void SerialCli::tick(uint32_t nowMs) {
    while (Serial.available() > 0) {
        int ch = Serial.read();
        if (ch < 0) break;
        if (ch == '\r' || ch == '\n') {
            if (linePos_ > 0) {
                lineBuf_[linePos_] = '\0';
                handleLine(lineBuf_, nowMs);
                linePos_ = 0;
            }
        } else if (linePos_ + 1 < sizeof(lineBuf_)) {
            lineBuf_[linePos_++] = static_cast<char>(ch);
        }
    }
}

void SerialCli::handleLine(const char* line, uint32_t nowMs) {
    if (std::strcmp(line, "help") == 0) {
        Serial.println("Garadget Local CLI v" + String(kFirmwareVersion));
        Serial.println("Commands: status | config | set key=val[|k=v] | open | close | stop | toggle | scan | wifi-setup | reboot | factory-reset");
        return;
    }

    if (std::strcmp(line, "status") == 0) {
        char ip[20], dur[16];
        wifi_.formatIp(ip, sizeof(ip));
        formatDuration(door_.timeInStatusMs(nowMs), dur, sizeof(dur));
        const SensorReading& r = door_.lastReading();
        Serial.printlnf("door=%s time=%s refl=%u%% thresh=%u%% bright=%u%% raw=%u ip=%s rssi=%d mqtt=%s",
                        doorStatusToString(door_.status()), dur, r.reflectionPct,
                        config_.data().sensorThreshold, r.brightnessPct, r.rawBase,
                        ip, wifi_.rssiDbm(), mqtt_.isConnected() ? "connected" : "disconnected");
        return;
    }

    if (std::strcmp(line, "config") == 0) {
        char buf[768], id[32], ssid[40], ip[20], mac[24];
        wifi_.formatDeviceId(id, sizeof(id));
        wifi_.formatSsid(ssid, sizeof(ssid));
        wifi_.formatIp(ip, sizeof(ip));
        wifi_.formatMac(mac, sizeof(mac));
        config_.toJson(buf, sizeof(buf), id, System.version().c_str(), ssid, ip, mac);
        Serial.println(buf);
        return;
    }

    if (std::strncmp(line, "set ", 4) == 0) {
        int res = config_.applyPipeConfig(line + 4);
        if (res < 0) {
            Serial.println("ERR: invalid config parameter");
        } else {
            if (res > 0) {
                config_.save(storage_);
                if (onConfigSaved_) onConfigSaved_(hookCtx_);
            }
            Serial.printlnf("OK: updated %d parameter(s)", res);
        }
        return;
    }

    if (std::strcmp(line, "scan") == 0) {
        SensorReading r = door_.forceScan(nowMs);
        Serial.printlnf("scan: refl=%u%% bright=%u%% raw=%u", r.reflectionPct, r.brightnessPct, r.rawBase);
        return;
    }

    if (std::strcmp(line, "wifi-setup") == 0 ||
        std::strcmp(line, "reboot") == 0 ||
        std::strcmp(line, "factory-reset") == 0) {
        if (onSystemAction_) onSystemAction_(line, hookCtx_);
        return;
    }

    DoorCommand cmd = parseDoorCommand(line);
    if (cmd != DoorCommand::None) {
        bool ok = door_.executeCommand(cmd, nowMs);
        Serial.println(ok ? "OK" : "BUSY");
        return;
    }

    Serial.println("Unknown command. Type 'help'.");
}

} // namespace garadget
#endif
