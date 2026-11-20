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
} // namespace

void initSoftApPortal(ConfigManager* config, ConfigStorage* storage, WifiManager* wifi) {
    gSoftApConfig = config;
    gSoftApStorage = storage;
    gSoftApWifi = wifi;
}

void softapPageHandler(const char* url,
                       ResponseCallback* cb,
                       void* cbArg,
                       Reader* request,
                       Writer* response,
                       void* /*reserved*/) {
    if (!url || !cb || !response) return;

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
        char buf[768];
        char id[32], ssid[40], ip[20], mac[24];
        gSoftApWifi->formatDeviceId(id, sizeof(id));
        gSoftApWifi->formatSsid(ssid, sizeof(ssid));
        gSoftApWifi->formatIp(ip, sizeof(ip));
        gSoftApWifi->formatMac(mac, sizeof(mac));
        String sysVer = System.version();
        gSoftApConfig->toJson(buf, sizeof(buf), id, sysVer.c_str(), ssid, ip, mac);
        cb(cbArg, 0, 200, "application/json", nullptr);
        response->write(buf);
        return;
    }

    if (std::strcmp(url, "/save-config") == 0 && gSoftApConfig && gSoftApStorage && request) {
        char* body = request->fetch_as_string();
        int rc = -1;
        if (body) {
            rc = gSoftApConfig->applyJsonConfig(body, std::strlen(body));
            if (rc >= 0) {
                gSoftApConfig->save(*gSoftApStorage);
            }
            std::free(body);
        }
        cb(cbArg, 0, 200, "application/json", nullptr);
        response->write(rc >= 0 ? "{\"r\":0}" : "{\"r\":-1}");
        return;
    }

    cb(cbArg, 0, 404, nullptr, nullptr);
}

} // namespace garadget
#endif
