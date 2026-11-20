#include "web_server.h"

#ifdef PARTICLE
#include "Particle.h"
#include "system_update.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/http_security.h"
#include "../core/json_utils.h"
#include "../core/ota_validator.h"
#include "../generated/web_ui_gz.h"
#include "../version.h"

namespace garadget {

namespace {

TCPServer gHttpServer(80);
AuthRateLimiter gAuthLimiter;

void sendTextResponse(TCPClient& client, int statusCode, const char* statusText,
                      const char* contentType, const char* body, size_t bodyLen) {
    char hdr[320];
    int hlen = std::snprintf(
        hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Connection: close\r\n\r\n",
        statusCode, statusText, contentType, static_cast<unsigned long>(bodyLen));
    client.write(reinterpret_cast<const uint8_t*>(hdr), static_cast<size_t>(hlen));
    if (body && bodyLen > 0) {
        size_t sent = 0;
        while (sent < bodyLen && client.connected()) {
            size_t chunk = (bodyLen - sent > 512) ? 512 : (bodyLen - sent);
            int w = client.write(reinterpret_cast<const uint8_t*>(body + sent), chunk);
            if (w <= 0) break;
            sent += static_cast<size_t>(w);
        }
    }
}

void sendGzipHtml(TCPClient& client, const unsigned char* data, size_t len) {
    char hdr[420];
    int hlen = std::snprintf(
        hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Encoding: gzip\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-cache\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Content-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'\r\n"
        "Connection: close\r\n\r\n",
        static_cast<unsigned long>(len));
    client.write(reinterpret_cast<const uint8_t*>(hdr), static_cast<size_t>(hlen));

    size_t sent = 0;
    while (sent < len && client.connected()) {
        size_t chunk = (len - sent > 512) ? 512 : (len - sent);
        int w = client.write(data + sent, chunk);
        if (w <= 0) break;
        sent += static_cast<size_t>(w);
    }
}

// Reads up to `want` bytes from TCPClient with an idle timeout.
size_t readStreamBytes(TCPClient& client, uint8_t* dst, size_t want, uint32_t idleTimeoutMs) {
    size_t got = 0;
    uint32_t lastRx = millis();
    while (got < want) {
        ApplicationWatchdog::checkin();
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = want - got;
            if (static_cast<size_t>(avail) < toRead) {
                toRead = static_cast<size_t>(avail);
            }
            int n = client.read(dst + got, toRead);
            if (n > 0) {
                got += static_cast<size_t>(n);
                lastRx = millis();
                continue;
            }
        }
        if (!client.connected()) {
            // Drain any remaining buffered bytes after peer half-close
            if (client.available() <= 0) break;
        }
        if (static_cast<uint32_t>(millis() - lastRx) >= idleTimeoutMs) {
            break;
        }
        delay(1);
    }
    return got;
}

void sendOtaError(TCPClient& client, int code, const char* statusText, const char* errCode) {
    char msg[128];
    int len = std::snprintf(msg, sizeof(msg), "{\"ok\":false,\"error\":\"%s\"}", errCode);
    sendTextResponse(client, code, statusText, "application/json", msg, static_cast<size_t>(len));
}

struct OtaLedGuard {
    const DeviceConfig& cfg;
    bool boosted = false;
    void boost() {
        RGB.brightness(128);
        boosted = true;
    }
    void release() { boosted = false; }
    ~OtaLedGuard() {
        if (!boosted) return;
        if (cfg.ledStealth) {
            RGB.brightness(0);
        } else {
            uint8_t level = static_cast<uint8_t>((static_cast<uint16_t>(cfg.ledBrightness) * 255u) / 100u);
            RGB.brightness(level);
        }
    }
};

void handleOtaUpload(TCPClient& client, const DeviceConfig& cfg,
                     const char* reqHeaders, size_t headerLen, size_t contentLen) {
    if (contentLen < OtaValidator::kMinBinarySize || contentLen > OtaValidator::kMaxBinarySize) {
        Log.error("OTA: invalid Content-Length (%lu)", static_cast<unsigned long>(contentLen));
        sendOtaError(client, 400, "Bad Request", "invalid_size");
        return;
    }

    // Handle HTTP/1.1 Expect: 100-continue (sent by curl for uploads > 1 KB)
    if (findHttpHeaderValue(reqHeaders, headerLen, "expect") != nullptr) {
        const char* cont = "HTTP/1.1 100 Continue\r\n\r\n";
        client.write(reinterpret_cast<const uint8_t*>(cont), std::strlen(cont));
    }

    static constexpr size_t kChunkSize = 512;
    static constexpr uint32_t kTotalOtaTimeoutMs = 30000u;
    uint32_t otaStartMs = millis();
    uint8_t chunk[kChunkSize];

    // Read full first chunk with tight 2s idle timeout and validate P1 header BEFORE touching flash
    size_t firstWant = (contentLen < kChunkSize) ? contentLen : kChunkSize;
    size_t firstRead = readStreamBytes(client, chunk, firstWant, 2000);
    if (firstRead != firstWant || firstRead < 24) {
        Log.error("OTA: truncated header read (%u/%u bytes)",
                  static_cast<unsigned>(firstRead), static_cast<unsigned>(firstWant));
        sendOtaError(client, 400, "Bad Request", "truncated_upload");
        return;
    }

    const char* hdrErr = OtaValidator::validateHeader(chunk, contentLen);
    if (hdrErr != nullptr) {
        Log.error("OTA: header validation failed (%s)", hdrErr);
        sendOtaError(client, 400, "Bad Request", hdrErr);
        return;
    }

    // Temporarily restore LED visibility during OTA even if Stealth Mode is active
    OtaLedGuard ledGuard{cfg, false};
    ledGuard.boost();

    // Prepare Device OS OTA staging slot in flash
    FileTransfer::Descriptor desc;
    desc.store = FileTransfer::Store::FIRMWARE;
    desc.file_length = static_cast<uint32_t>(contentLen);
    desc.chunk_address = 0;
    desc.chunk_size = static_cast<uint16_t>(kChunkSize);

    Log.info("OTA: starting local firmware update (%lu bytes)", static_cast<unsigned long>(contentLen));
    int prepRc = Spark_Prepare_For_Firmware_Update(desc, 0, nullptr);
    if (prepRc != 0) {
        Log.error("OTA: prepare failed (rc=%d)", prepRc);
        sendOtaError(client, 500, "Internal Server Error", "flash_prepare_failed");
        return;
    }

    OtaValidator validator;
    validator.reset(contentLen);

    // Save first chunk (guaranteed 512B aligned unless total binary < 512B)
    size_t written = 0;
    desc.chunk_address = desc.file_address;
    desc.chunk_size = static_cast<uint16_t>(firstRead);
    if (Spark_Save_Firmware_Chunk(desc, chunk, nullptr) != 0) {
        Log.error("OTA: failed saving chunk 0");
        Spark_Finish_Firmware_Update(desc, 0 /* CANCEL */, nullptr);
        sendOtaError(client, 500, "Internal Server Error", "flash_write_failed");
        return;
    }
    validator.feed(chunk, firstRead);
    written += firstRead;

    // Stream remaining chunks; enforce exact full reads so flash writes stay 4-byte word aligned
    while (written < contentLen) {
        if (static_cast<uint32_t>(millis() - otaStartMs) >= kTotalOtaTimeoutMs) {
            Log.error("OTA: total upload timeout exceeded");
            Spark_Finish_Firmware_Update(desc, 0 /* CANCEL */, nullptr);
            sendOtaError(client, 408, "Request Timeout", "upload_timeout");
            return;
        }
        size_t want = contentLen - written;
        if (want > kChunkSize) want = kChunkSize;
        size_t got = readStreamBytes(client, chunk, want, 5000);
        if (got != want) {
            Log.error("OTA: stream truncated/stalled at %lu/%lu bytes (got %u/%u)",
                      static_cast<unsigned long>(written), static_cast<unsigned long>(contentLen),
                      static_cast<unsigned>(got), static_cast<unsigned>(want));
            Spark_Finish_Firmware_Update(desc, 0 /* CANCEL */, nullptr);
            sendOtaError(client, 400, "Bad Request", "upload_timeout");
            return;
        }
        desc.chunk_address = desc.file_address + static_cast<uint32_t>(written);
        desc.chunk_size = static_cast<uint16_t>(got);
        if (Spark_Save_Firmware_Chunk(desc, chunk, nullptr) != 0) {
            Log.error("OTA: flash write error at offset %lu", static_cast<unsigned long>(written));
            Spark_Finish_Firmware_Update(desc, 0 /* CANCEL */, nullptr);
            sendOtaError(client, 500, "Internal Server Error", "flash_write_failed");
            return;
        }
        validator.feed(chunk, got);
        written += got;
    }

    // Verify full IEEE 802.3 CRC32 before committing to bootloader
    if (!validator.verifyCrc()) {
        Log.error("OTA: CRC32 verification failed — aborting safely");
        Spark_Finish_Firmware_Update(desc, 0 /* CANCEL */, nullptr);
        sendOtaError(client, 400, "Bad Request", "crc_mismatch");
        return;
    }

    // Defer reset until HTTP 200 response is flushed cleanly to client
    System.disableReset();
    int finishRc = Spark_Finish_Firmware_Update(desc, 1 /* SUCCESS */, nullptr);
    if (finishRc != 0) {
        Log.error("OTA: Device OS finish/validate failed (rc=%d)", finishRc);
        sendOtaError(client, 400, "Bad Request", "os_validation_failed");
        client.flush();
        client.stop();
        delay(300);
        System.enableReset();
        return;
    }

    ledGuard.release();
    Log.info("OTA: firmware verified & staged (%lu bytes), rebooting to apply!",
             static_cast<unsigned long>(written));
    char okMsg[96];
    int okLen = std::snprintf(okMsg, sizeof(okMsg),
                              "{\"ok\":true,\"size\":%lu,\"rebooting\":true}",
                              static_cast<unsigned long>(written));
    sendTextResponse(client, 200, "OK", "application/json", okMsg, static_cast<size_t>(okLen));
    client.flush();
    client.stop();

    for (int i = 0; i < 10; ++i) {
        ApplicationWatchdog::checkin();
        delay(50);
    }
    System.enableReset();
    System.reset();
}

bool isKnownSystemAction(const char* action) {
    if (!action || action[0] == '\0') return false;
    return std::strcmp(action, "scan") == 0 ||
           std::strcmp(action, "sync-time") == 0 ||
           std::strcmp(action, "ha-rediscover") == 0 ||
           std::strcmp(action, "wifi-setup") == 0 ||
           std::strcmp(action, "clear-wifi") == 0 ||
           std::strcmp(action, "reboot") == 0 ||
           std::strcmp(action, "factory-reset") == 0;
}

} // namespace

WebServer::WebServer(ConfigManager& config,
                     ConfigStorage& storage,
                     DoorController& door,
                     AlertEngine& alerts,
                     WifiManager& wifi,
                     SntpClient& sntp,
                     HaMqttBridge& mqtt)
    : config_(config),
      storage_(storage),
      door_(door),
      alerts_(alerts),
      wifi_(wifi),
      sntp_(sntp),
      mqtt_(mqtt) {}

void WebServer::init() {
    started_ = false;
}

void WebServer::tick(uint32_t nowMs) {
    if (!wifi_.isReady() || wifi_.isListening()) {
        if (started_) {
            gHttpServer.stop();
            started_ = false;
        }
        return;
    }

    if (!started_) {
        gHttpServer.begin();
        started_ = true;
        Log.info("HTTP: LAN management server listening on port 80");
    }

    TCPClient client = gHttpServer.available();
    if (!client.connected()) {
        return;
    }

    // Read request headers (and JSON bodies up to 1280B) with strict non-blocking budget
    static constexpr size_t kMaxJsonBody = 1280;
    static char reqBuf[2560];
    size_t reqLen = 0;
    size_t headerEnd = 0;
    size_t contentLen = 0;
    bool isOtaStream = false;
    bool payloadTooLarge = false;
    uint32_t deadline = millis() + 300u;

    while (client.connected() && static_cast<int32_t>(deadline - millis()) > 0) {
        while (client.available() > 0 && reqLen + 1 < sizeof(reqBuf)) {
            int b = client.read();
            if (b < 0) break;
            reqBuf[reqLen++] = static_cast<char>(b);
            reqBuf[reqLen] = '\0';

            if (headerEnd == 0 && reqLen >= 4 &&
                reqBuf[reqLen - 4] == '\r' && reqBuf[reqLen - 3] == '\n' &&
                reqBuf[reqLen - 2] == '\r' && reqBuf[reqLen - 1] == '\n') {
                headerEnd = reqLen;
                const char* cl = findHttpHeaderValue(reqBuf, headerEnd, "content-length");
                if (cl) {
                    long v = std::strtol(cl, nullptr, 10);
                    if (v > 0) contentLen = static_cast<size_t>(v);
                }
                if (std::strncmp(reqBuf, "POST /api/ota ", 14) == 0 ||
                    std::strncmp(reqBuf, "POST /api/ota?", 14) == 0) {
                    isOtaStream = true;
                    break;
                }
                if (contentLen > kMaxJsonBody || headerEnd + contentLen >= sizeof(reqBuf)) {
                    payloadTooLarge = true;
                    break;
                }
            }
        }
        if (isOtaStream || payloadTooLarge ||
            (headerEnd > 0 && reqLen >= headerEnd + contentLen) ||
            (headerEnd == 0 && reqLen + 1 >= sizeof(reqBuf))) {
            break;
        }
        delay(1);
    }

    if (reqLen == 0) {
        client.stop();
        return;
    }

    if (headerEnd == 0) {
        if (reqLen + 1 >= sizeof(reqBuf)) {
            const char* err = "{\"ok\":false,\"error\":\"headers_too_large\"}";
            sendTextResponse(client, 431, "Request Header Fields Too Large", "application/json", err, std::strlen(err));
        } else {
            const char* err = "{\"ok\":false,\"error\":\"malformed_request\"}";
            sendTextResponse(client, 400, "Bad Request", "application/json", err, std::strlen(err));
        }
        client.flush();
        client.stop();
        return;
    }

    if (payloadTooLarge) {
        const char* err = "{\"ok\":false,\"error\":\"payload_too_large\"}";
        sendTextResponse(client, 413, "Payload Too Large", "application/json", err, std::strlen(err));
        client.flush();
        client.stop();
        return;
    }

    // Parse request line: "METHOD /path HTTP/1.1"
    char method[8] = {};
    char path[64] = {};
    {
        const char* sp1 = std::strchr(reqBuf, ' ');
        if (sp1) {
            size_t mlen = static_cast<size_t>(sp1 - reqBuf);
            if (mlen >= sizeof(method)) mlen = sizeof(method) - 1;
            std::memcpy(method, reqBuf, mlen);
            method[mlen] = '\0';

            const char* pStart = sp1 + 1;
            const char* sp2 = std::strchr(pStart, ' ');
            const char* qm = std::strchr(pStart, '?');
            const char* pEnd = sp2;
            if (qm && (!pEnd || qm < pEnd)) pEnd = qm;
            if (pEnd) {
                size_t plen = static_cast<size_t>(pEnd - pStart);
                if (plen >= sizeof(path)) plen = sizeof(path) - 1;
                std::memcpy(path, pStart, plen);
                path[plen] = '\0';
            }
        }
    }

    const char* body = (headerEnd > 0 && reqLen > headerEnd) ? (reqBuf + headerEnd) : "";
    size_t bodyLen = (headerEnd > 0 && reqLen > headerEnd) ? (reqLen - headerEnd) : 0;

    // Serve root dashboard UI without auth (JS handles 401 modal cleanly)
    if (std::strcmp(method, "GET") == 0 &&
        (std::strcmp(path, "/") == 0 || std::strcmp(path, "/index.html") == 0)) {
        sendGzipHtml(client, kWebUiIndexGz, kWebUiIndexGzLen);
        client.flush();
        client.stop();
        return;
    }

    size_t hdrLen = (headerEnd > 0) ? headerEnd : reqLen;

    // CSRF & Origin protection on all state-changing API requests (blocks cross-origin simple POSTs)
    if (std::strncmp(path, "/api/", 5) == 0 &&
        !verifyCsrfProtection(reqBuf, hdrLen, method, path)) {
        const char* err = "{\"ok\":false,\"error\":\"csrf_rejected\"}";
        sendTextResponse(client, 403, "Forbidden", "application/json", err, std::strlen(err));
        client.flush();
        client.stop();
        return;
    }

    // Mandatory first-time Web Admin Password setup endpoint (only allowed while no password is set)
    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/setup-password") == 0) {
        if (config_.hasAdminPassword()) {
            const char* err = "{\"ok\":false,\"error\":\"already_configured\"}";
            sendTextResponse(client, 403, "Forbidden", "application/json", err, std::strlen(err));
        } else {
            struct PwCtx {
                char pass[kAdminPassCap] = {};
            } ctx;
            auto cb = [](const char* k, const char* v, bool, void* u) -> bool {
                if (std::strcmp(k, "apass") == 0) {
                    auto* c = static_cast<PwCtx*>(u);
                    std::strncpy(c->pass, v, sizeof(c->pass) - 1);
                }
                return true;
            };
            bool okJson = parseFlatJsonObject(body, bodyLen, cb, &ctx);
            int res = okJson ? config_.setParam("apass", ctx.pass, true) : -1;
            if (res > 0) {
                config_.save(storage_);
                alerts_.onConfigUpdated(nowMs);
                if (onConfigSaved_) onConfigSaved_(hookCtx_);
                const char* ok = "{\"ok\":true}";
                sendTextResponse(client, 200, "OK", "application/json", ok, std::strlen(ok));
            } else {
                const char* err = "{\"ok\":false,\"error\":\"password_too_short\"}";
                sendTextResponse(client, 400, "Bad Request", "application/json", err, std::strlen(err));
            }
        }
        client.flush();
        client.stop();
        return;
    }

    // All API endpoints require an admin password to be configured first, then constant-time auth + per-IP lockout
    if (std::strncmp(path, "/api/", 5) == 0) {
        if (!config_.hasAdminPassword()) {
            const char* err = "{\"ok\":false,\"error\":\"password_setup_required\"}";
            sendTextResponse(client, 428, "Precondition Required", "application/json", err, std::strlen(err));
            client.flush();
            client.stop();
            return;
        }
        IPAddress rip = client.remoteIP();
        uint32_t clientIp = (static_cast<uint32_t>(rip[0]) << 24) |
                            (static_cast<uint32_t>(rip[1]) << 16) |
                            (static_cast<uint32_t>(rip[2]) << 8) |
                            static_cast<uint32_t>(rip[3]);
        if (gAuthLimiter.isLockedOut(nowMs, clientIp)) {
            const char* err = "{\"ok\":false,\"error\":\"rate_limited\"}";
            sendTextResponse(client, 429, "Too Many Requests", "application/json", err, std::strlen(err));
            client.flush();
            client.stop();
            return;
        }
        if (!verifyAuthToken(reqBuf, hdrLen, config_.data().adminPass)) {
            gAuthLimiter.recordFailure(nowMs, clientIp);
            const char* err = "{\"ok\":false,\"error\":\"unauthorized\"}";
            sendTextResponse(client, 401, "Unauthorized", "application/json", err, std::strlen(err));
            client.flush();
            client.stop();
            return;
        }
        gAuthLimiter.recordSuccess(clientIp);
    }

    // Streaming binary OTA firmware upload
    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/ota") == 0) {
        handleOtaUpload(client, config_.data(), reqBuf, hdrLen, contentLen);
        client.flush();
        client.stop();
        return;
    }

    static char respBuf[1400];

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/status") == 0) {
        char dur[16];
        char up[16];
        char id[32], ssid[40], ip[20], snet[20], gway[20], dns[20], mac[24];
        formatDuration(door_.timeInStatusMs(nowMs), dur, sizeof(dur));
        formatDuration(nowMs, up, sizeof(up));
        wifi_.formatDeviceId(id, sizeof(id));
        wifi_.formatSsid(ssid, sizeof(ssid));
        wifi_.formatIp(ip, sizeof(ip));
        wifi_.formatSubnet(snet, sizeof(snet));
        wifi_.formatGateway(gway, sizeof(gway));
        wifi_.formatDns(dns, sizeof(dns));
        wifi_.formatMac(mac, sizeof(mac));
        String sysVer = System.version();

        char timeStr[24] = "";
        if (Time.isValid()) {
            std::snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d",
                          Time.year(), Time.month(), Time.day(),
                          Time.hour(), Time.minute(), Time.second());
        }

        const SensorReading& r = door_.lastReading();
        JsonWriter w(respBuf, sizeof(respBuf));
        w.beginObject();
        w.keyString("status", doorStatusToString(door_.status()));
        w.keyString("time", dur);
        w.keyUInt("sensor", r.reflectionPct);
        w.keyUInt("threshold", config_.data().sensorThreshold);
        w.keyUInt("bright", r.brightnessPct);
        w.keyUInt("raw", r.rawBase);
        w.keyInt("signal", wifi_.rssiDbm());
        w.keyUInt("smode", config_.data().sensorMode);
        w.keyBool("wifiReady", wifi_.isReady());
        w.keyBool("mqttEnabled", config_.data().mqttEnabled != 0);
        w.keyBool("mqttConnected", mqtt_.isConnected());
        w.keyBool("ntpSynced", sntp_.isSynced());
        w.keyString("localTime", timeStr);
        w.keyString("ver", kFirmwareVersion);
        w.keyString("sys", sysVer.c_str());
        w.keyString("id", id);
        w.keyString("ssid", ssid);
        w.keyString("ip", ip);
        w.keyString("snet", snet);
        w.keyString("gway", gway);
        w.keyString("dns", dns);
        w.keyString("mac", mac);
        w.keyString("uptime", up);
        w.keyUInt("freeMem", System.freeMemory());

        w.keyArray("alerts");
        size_t acount = alerts_.historyCount();
        for (size_t i = 0; i < acount; ++i) {
            const AlertRecord& rec = alerts_.historyItem(i);
            w.beginObject();
            w.keyString("type", rec.type);
            w.keyString("data", rec.data);
            w.keyUInt("up", rec.uptimeSec);
            w.endObject();
        }
        w.endArray();
        w.endObject();

        sendTextResponse(client, 200, "OK", "application/json", w.c_str(), w.length());
    } else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/config") == 0) {
        char id[32], ssid[40], ip[20], mac[24];
        wifi_.formatDeviceId(id, sizeof(id));
        wifi_.formatSsid(ssid, sizeof(ssid));
        wifi_.formatIp(ip, sizeof(ip));
        wifi_.formatMac(mac, sizeof(mac));
        String sysVer = System.version();
        config_.toJson(respBuf, sizeof(respBuf), id, sysVer.c_str(), ssid, ip, mac);
        sendTextResponse(client, 200, "OK", "application/json", respBuf, std::strlen(respBuf));
    } else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/config") == 0) {
        int res = config_.applyJsonConfig(body, bodyLen);
        if (res < 0) {
            const char* err = "{\"ok\":false,\"error\":\"invalid_config\"}";
            sendTextResponse(client, 400, "Bad Request", "application/json", err, std::strlen(err));
        } else {
            if (res > 0) {
                config_.save(storage_);
                alerts_.onConfigUpdated(nowMs);
                if (onConfigSaved_) onConfigSaved_(hookCtx_);
            }
            const char* ok = "{\"ok\":true}";
            sendTextResponse(client, 200, "OK", "application/json", ok, std::strlen(ok));
        }
    } else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/command") == 0) {
        struct CmdCtx {
            char cmd[24] = {};
        } ctx;
        auto cb = [](const char* k, const char* v, bool, void* u) -> bool {
            if (std::strcmp(k, "command") == 0) {
                auto* c = static_cast<CmdCtx*>(u);
                std::strncpy(c->cmd, v, sizeof(c->cmd) - 1);
            }
            return true;
        };
        parseFlatJsonObject(body, bodyLen, cb, &ctx);
        DoorCommand dc = parseDoorCommand(ctx.cmd);
        if (dc != DoorCommand::None && door_.executeCommand(dc, nowMs)) {
            const char* ok = "{\"ok\":true}";
            sendTextResponse(client, 200, "OK", "application/json", ok, std::strlen(ok));
        } else {
            const char* err = "{\"ok\":false,\"error\":\"invalid_or_busy\"}";
            sendTextResponse(client, 400, "Bad Request", "application/json", err, std::strlen(err));
        }
    } else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/system") == 0) {
        struct ActCtx {
            char action[24] = {};
        } ctx;
        auto cb = [](const char* k, const char* v, bool, void* u) -> bool {
            if (std::strcmp(k, "action") == 0) {
                auto* c = static_cast<ActCtx*>(u);
                std::strncpy(c->action, v, sizeof(c->action) - 1);
            }
            return true;
        };
        parseFlatJsonObject(body, bodyLen, cb, &ctx);
        if (!isKnownSystemAction(ctx.action)) {
            const char* err = "{\"ok\":false,\"error\":\"unknown_action\"}";
            sendTextResponse(client, 400, "Bad Request", "application/json", err, std::strlen(err));
            client.flush();
            client.stop();
            return;
        }
        const char* ok = "{\"ok\":true}";
        sendTextResponse(client, 200, "OK", "application/json", ok, std::strlen(ok));
        client.flush();
        client.stop();
        if (onSystemAction_) {
            onSystemAction_(ctx.action, hookCtx_);
        }
        return;
    } else {
        const char* nf = "{\"ok\":false,\"error\":\"not_found\"}";
        sendTextResponse(client, 404, "Not Found", "application/json", nf, std::strlen(nf));
    }

    client.flush();
    client.stop();
}

} // namespace garadget
#endif
