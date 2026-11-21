#include "softap_portal.h"

#ifdef PARTICLE
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../generated/softap_ui.h"

namespace garadget {

namespace {
ConfigManager* gSoftApConfig = nullptr;
ConfigStorage* gSoftApStorage = nullptr;
WifiManager* gSoftApWifi = nullptr;
ConfigSavedCallback gSoftApOnSaved = nullptr;
void* gSoftApHookCtx = nullptr;

enum class SoftApOp : uint8_t {
    None = 0,
    LoadConfig,
    SaveConfig,
    ClearWifi,
};

// Cross-thread staging slots: softapPageHandler (system thread) stages requests here and waits briefly
// while tickSoftApPortal() (application thread) exclusively accesses gSoftApConfig & EEPROM.
volatile SoftApOp gPendingOp = SoftApOp::None;
volatile bool gOpBusy = false;
volatile bool gOpDone = false;
volatile int gOpResult = -1;
char gJsonOutBuf[1152]{};
char gSaveInBuf[1025]{};
size_t gSaveInLen = 0;

bool runStagedOp(SoftApOp op, uint32_t timeoutMs = 500u) {
    if (gOpBusy) return false;
    gOpBusy = true;
    gOpResult = -1;
    gOpDone = false;
    gPendingOp = op;

    uint32_t start = millis();
    while (!gOpDone && static_cast<uint32_t>(millis() - start) < timeoutMs) {
        delay(1);
    }
    bool completed = gOpDone;
    if (!completed) {
        gPendingOp = SoftApOp::None;
    }
    gOpBusy = false;
    return completed;
}
} // namespace

void initSoftApPortal(ConfigManager* config,
                      ConfigStorage* storage,
                      WifiManager* wifi,
                      ConfigSavedCallback onConfigSaved,
                      void* hookCtx) {
    gSoftApConfig = config;
    gSoftApStorage = storage;
    gSoftApWifi = wifi;
    gSoftApOnSaved = onConfigSaved;
    gSoftApHookCtx = hookCtx;
}

void tickSoftApPortal(uint32_t /*nowMs*/) {
    SoftApOp op = gPendingOp;
    if (op == SoftApOp::None) return;
    gPendingOp = SoftApOp::None;

    int rc = -1;
    if (op == SoftApOp::LoadConfig && gSoftApConfig && gSoftApWifi) {
        char id[32], ssid[40], ip[20], mac[24];
        gSoftApWifi->formatDeviceId(id, sizeof(id));
        gSoftApWifi->formatSsid(ssid, sizeof(ssid));
        gSoftApWifi->formatIp(ip, sizeof(ip));
        gSoftApWifi->formatMac(mac, sizeof(mac));
        String sysVer = System.version();
        bool ok = gSoftApConfig->toJson(gJsonOutBuf, sizeof(gJsonOutBuf), id, sysVer.c_str(), ssid, ip, mac);
        if (!ok) {
            Log.warn("SoftAP: /load-config JSON truncated");
        }
        rc = ok ? 0 : -1;
    } else if (op == SoftApOp::ClearWifi && gSoftApWifi) {
        gSoftApWifi->clearCredentials();
        if (gSoftApConfig && gSoftApStorage && gSoftApConfig->data().ipMode != 0) {
            gSoftApConfig->mutableData().ipMode = 0;
            gSoftApConfig->save(*gSoftApStorage);
        }
        rc = 0;
    } else if (op == SoftApOp::SaveConfig && gSoftApConfig && gSoftApStorage) {
        DeviceConfig backup = gSoftApConfig->data();
        // Physical/local SoftAP setup is the sanctioned channel for initial Web Admin Password setup & reset
        rc = gSoftApConfig->applyJsonConfig(gSaveInBuf, gSaveInLen, ConfigScope::Full);
        if (rc >= 0 && !gSoftApConfig->hasAdminPassword()) {
            // Server-side enforcement: SoftAP setup must result in a configured Web Admin Password
            gSoftApConfig->mutableData() = backup;
            rc = -1;
        }
        if (rc >= 0) {
            gSoftApConfig->save(*gSoftApStorage);
            if (gSoftApWifi) {
                gSoftApWifi->applyIpConfig(gSoftApConfig->data());
            }
            if (gSoftApOnSaved) {
                gSoftApOnSaved(gSoftApHookCtx);
            }
        }
    }

    gOpResult = rc;
    gOpDone = true;
}

void softapPageHandler(const char* url,
                       ResponseCallback* cb,
                       void* cbArg,
                       Reader* request,
                       Writer* response,
                       void* /*reserved*/) {
    if (!url || !cb || !response) return;
    if (gSoftApWifi) {
        gSoftApWifi->touchSetupActivity();
    }

    if (std::strcmp(url, "/index") == 0 || std::strcmp(url, "/") == 0) {
        Header h("Location: /index.html\r\n");
        cb(cbArg, 0, 301, "text/plain", &h);
        return;
    }

    if (std::strcmp(url, "/index.html") == 0) {
        char hdr[64];
        size_t len = kSoftApIndexHtmlLen - 1;
        std::snprintf(hdr, sizeof(hdr), "Content-Length: %lu\r\n", static_cast<unsigned long>(len));
        Header h(hdr);
        cb(cbArg, 0, 200, "text/html", &h);
        response->write(reinterpret_cast<const uint8_t*>(kSoftApIndexHtml), len);
        return;
    }

    if (std::strcmp(url, "/rsa.js") == 0) {
        char hdr[64];
        size_t len = kSoftApRsaJsLen - 1;
        std::snprintf(hdr, sizeof(hdr), "Content-Length: %lu\r\n", static_cast<unsigned long>(len));
        Header h(hdr);
        cb(cbArg, 0, 200, "application/javascript", &h);
        response->write(reinterpret_cast<const uint8_t*>(kSoftApRsaJs), len);
        return;
    }

    if (std::strcmp(url, "/load-config") == 0 && gSoftApConfig && gSoftApWifi) {
        if (runStagedOp(SoftApOp::LoadConfig) && gOpResult == 0) {
            cb(cbArg, 0, 200, "application/json", nullptr);
            response->write(gJsonOutBuf);
        } else {
            cb(cbArg, 0, 503, "application/json", nullptr);
            response->write("{\"r\":-1}");
        }
        return;
    }

    if (std::strcmp(url, "/clear-wifi") == 0 && gSoftApWifi) {
        bool ok = runStagedOp(SoftApOp::ClearWifi) && gOpResult == 0;
        cb(cbArg, 0, ok ? 200 : 503, "application/json", nullptr);
        response->write(ok ? "{\"r\":0}" : "{\"r\":-1}");
        return;
    }

    if (std::strcmp(url, "/exit-setup") == 0 && gSoftApWifi) {
        cb(cbArg, 0, 200, "application/json", nullptr);
        response->write("{\"r\":0}");
        // Defer WiFi.listen(false) to main loop so the HTTP response flushes cleanly first
        gSoftApWifi->requestExitSetupMode();
        return;
    }

    if (std::strcmp(url, "/save-config") == 0 && gSoftApConfig && gSoftApStorage && request) {
        int rc = -1;
        if (!gOpBusy && request->bytes_left > 0 && request->bytes_left <= 1024) {
            char* body = request->fetch_as_string();
            if (body) {
                size_t blen = std::strlen(body);
                if (blen < sizeof(gSaveInBuf)) {
                    std::memcpy(gSaveInBuf, body, blen + 1);
                    gSaveInLen = blen;
                    if (runStagedOp(SoftApOp::SaveConfig)) {
                        rc = gOpResult;
                    }
                }
                std::free(body);
            }
        }
        cb(cbArg, 0, 200, "application/json", nullptr);
        response->write(rc >= 0 ? "{\"r\":0}" : "{\"r\":-1}");
        return;
    }

    cb(cbArg, 0, 404, nullptr, nullptr);
}

} // namespace garadget
#endif
