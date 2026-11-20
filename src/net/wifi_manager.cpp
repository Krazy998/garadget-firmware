#include "wifi_manager.h"

#ifdef PARTICLE
#include "Particle.h"
#include <cstdio>
#include <cstring>

#include "../version.h"

namespace garadget {

void WifiManager::applyIpConfig(const DeviceConfig& cfg) {
    if (cfg.ipMode == 1) {
        IPAddress ip(cfg.staticIp[0], cfg.staticIp[1], cfg.staticIp[2], cfg.staticIp[3]);
        IPAddress mask(cfg.staticMask[0], cfg.staticMask[1], cfg.staticMask[2], cfg.staticMask[3]);
        IPAddress gw(cfg.staticGw[0], cfg.staticGw[1], cfg.staticGw[2], cfg.staticGw[3]);
        bool hasDns = (cfg.staticDns[0] | cfg.staticDns[1] | cfg.staticDns[2] | cfg.staticDns[3]) != 0;
        IPAddress dns = hasDns
            ? IPAddress(cfg.staticDns[0], cfg.staticDns[1], cfg.staticDns[2], cfg.staticDns[3])
            : gw;
        WiFi.setStaticIP(ip, mask, gw, dns);
        WiFi.useStaticIP();
        Log.info("WiFi: using Static IP %u.%u.%u.%u (GW %u.%u.%u.%u, DNS %u.%u.%u.%u)",
                 ip[0], ip[1], ip[2], ip[3], gw[0], gw[1], gw[2], gw[3], dns[0], dns[1], dns[2], dns[3]);
    } else {
        WiFi.useDynamicIP();
        Log.info("WiFi: using DHCP");
    }
}

void WifiManager::init(uint32_t nowMs, const DeviceConfig& cfg) {
    WiFi.selectAntenna(GARADGET_ANT_MODE);
    WiFi.on();
    applyIpConfig(cfg);

    if (!WiFi.hasCredentials()) {
        Log.info("WiFi: no stored credentials, entering SoftAP setup mode");
        WiFi.listen(true);
        return;
    }

    Log.info("WiFi: connecting to saved network...");
    WiFi.connect();
    connecting_ = true;
    reconnectTimer_.start(nowMs, 15000);
}

void WifiManager::tick(uint32_t nowMs) {
    if (WiFi.listening()) {
        connecting_ = false;
        wasReady_ = false;
        return;
    }

    bool ready = WiFi.ready();
    if (ready) {
        if (!wasReady_) {
            char ip[20];
            formatIp(ip, sizeof(ip));
            Log.info("WiFi: connected! IP=%s RSSI=%d dBm", ip, rssiDbm());
            wasReady_ = true;
        }
        connecting_ = false;
        reconnectTimer_.stop();
        return;
    }

    if (wasReady_) {
        Log.warn("WiFi: connection lost, scheduling reconnect");
        wasReady_ = false;
        reconnectTimer_.start(nowMs, 3000);
    }

    if (!WiFi.hasCredentials()) {
        WiFi.listen(true);
        return;
    }

    if (!connecting_ && (!reconnectTimer_.isRunning() || reconnectTimer_.expired(nowMs))) {
        Log.info("WiFi: reconnecting...");
        WiFi.connect();
        connecting_ = true;
        reconnectTimer_.start(nowMs, 20000);
    } else if (connecting_ && reconnectTimer_.expired(nowMs)) {
        // Retry if stuck connecting
        Log.warn("WiFi: connect timeout, retrying...");
        WiFi.disconnect();
        connecting_ = false;
        reconnectTimer_.start(nowMs, 2000);
    }
}

void WifiManager::enterSetupMode() {
    Log.info("WiFi: entering SoftAP setup mode (192.168.0.1)");
    connecting_ = false;
    WiFi.listen(true);
}

void WifiManager::clearCredentials() {
    WiFi.clearCredentials();
    WiFi.useDynamicIP();
}

bool WifiManager::isReady() const {
    return WiFi.ready();
}

bool WifiManager::isListening() const {
    return WiFi.listening();
}

int WifiManager::rssiDbm() const {
    if (!WiFi.ready()) return 0;
    WiFiSignal sig = WiFi.RSSI();
    return static_cast<int>(sig.getStrengthValue());
}

void WifiManager::formatIp(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    if (!WiFi.ready()) {
        std::snprintf(out, outCap, "0.0.0.0");
        return;
    }
    IPAddress ip = WiFi.localIP();
    std::snprintf(out, outCap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void WifiManager::formatSubnet(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    if (!WiFi.ready()) {
        std::snprintf(out, outCap, "0.0.0.0");
        return;
    }
    IPAddress ip = WiFi.subnetMask();
    std::snprintf(out, outCap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void WifiManager::formatGateway(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    if (!WiFi.ready()) {
        std::snprintf(out, outCap, "0.0.0.0");
        return;
    }
    IPAddress ip = WiFi.gatewayIP();
    std::snprintf(out, outCap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void WifiManager::formatDns(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    if (!WiFi.ready()) {
        std::snprintf(out, outCap, "0.0.0.0");
        return;
    }
    IPAddress ip = WiFi.dnsServerIP();
    std::snprintf(out, outCap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void WifiManager::formatMac(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    uint8_t mac[6] = {};
    WiFi.macAddress(mac);
    std::snprintf(out, outCap, "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void WifiManager::formatSsid(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    if (WiFi.ready()) {
        const char* ssid = WiFi.SSID();
        std::snprintf(out, outCap, "%s", ssid ? ssid : "");
        return;
    }
    WiFiAccessPoint ap[1];
    int count = WiFi.getCredentials(ap, 1);
    if (count > 0) {
        std::snprintf(out, outCap, "%s", ap[0].ssid);
    } else {
        out[0] = '\0';
    }
}

void WifiManager::formatDeviceId(char* out, size_t outCap) const {
    if (!out || outCap == 0) return;
    String id = System.deviceID();
    std::snprintf(out, outCap, "%s", id.c_str());
}

} // namespace garadget
#endif
