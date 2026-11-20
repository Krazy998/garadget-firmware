#include "sntp_client.h"

#ifdef PARTICLE
#include "Particle.h"
#include <cstring>

namespace garadget {

namespace {
UDP gNtpUdp;
} // namespace

SntpClient::SntpClient(const ConfigManager& config) : config_(config) {}

void SntpClient::init() {
    syncTimer_.setDuration(1000);
    dstTimer_.setDuration(kDstCheckIntervalMs);
}

void SntpClient::requestSync(uint32_t nowMs) {
    waitingResponse_ = false;
    syncTimer_.start(nowMs, 50);
}

void SntpClient::applyTimezoneNow() {
    if (Time.isValid()) {
        time_t utcNow = Time.now();
        Time.zone(config_.timezone().offsetHoursForUtc(utcNow));
    }
}

void SntpClient::tick(uint32_t nowMs, bool wifiReady) {
    if (!wifiReady) {
        waitingResponse_ = false;
        if (udpStarted_) {
            gNtpUdp.stop();
            udpStarted_ = false;
        }
        return;
    }

    if (synced_ && (!dstTimer_.isRunning() || dstTimer_.expired(nowMs))) {
        applyTimezoneNow();
        dstTimer_.start(nowMs, kDstCheckIntervalMs);
    }

    if (waitingResponse_) {
        int packetLen = gNtpUdp.parsePacket();
        if (packetLen >= 48) {
            uint8_t buf[48] = {};
            gNtpUdp.read(buf, sizeof(buf));
            waitingResponse_ = false;
            responseTimer_.stop();

            uint32_t secsSince1900 =
                (static_cast<uint32_t>(buf[40]) << 24) |
                (static_cast<uint32_t>(buf[41]) << 16) |
                (static_cast<uint32_t>(buf[42]) << 8) |
                static_cast<uint32_t>(buf[43]);

            if (secsSince1900 > kNtpUnixOffset) {
                time_t unixUtc = static_cast<time_t>(secsSince1900 - kNtpUnixOffset);
                Time.setTime(unixUtc);
                Time.zone(config_.timezone().offsetHoursForUtc(unixUtc));
                synced_ = true;
                lastSyncUptimeSec_ = nowMs / 1000u;
                Log.info("SNTP: synced local time: %s", Time.timeStr().c_str());
                syncTimer_.start(nowMs, kSyncIntervalMs);
                dstTimer_.start(nowMs, kDstCheckIntervalMs);
                return;
            }
        }

        if (responseTimer_.expired(nowMs)) {
            Log.warn("SNTP: response timeout");
            waitingResponse_ = false;
            syncTimer_.start(nowMs, kRetryIntervalMs);
        }
        return;
    }

    if (!syncTimer_.isRunning() || syncTimer_.expired(nowMs)) {
        const char* server = config_.data().ntpServer;
        if (!server || server[0] == '\0') {
            syncTimer_.start(nowMs, kSyncIntervalMs);
            return;
        }

        IPAddress serverIp = WiFi.resolve(server);
        if (!serverIp) {
            Log.warn("SNTP: failed to resolve %s", server);
            syncTimer_.start(nowMs, kRetryIntervalMs);
            return;
        }

        if (!udpStarted_) {
            gNtpUdp.begin(kLocalPort);
            udpStarted_ = true;
        }

        uint8_t req[48] = {};
        req[0] = 0x23; // LI=0, VN=4, Mode=3 (Client)
        int sent = gNtpUdp.sendPacket(req, sizeof(req), serverIp, kNtpPort);
        if (sent == static_cast<int>(sizeof(req))) {
            waitingResponse_ = true;
            responseTimer_.start(nowMs, kResponseTimeoutMs);
        } else {
            Log.warn("SNTP: sendPacket failed (%d)", sent);
            syncTimer_.start(nowMs, kRetryIntervalMs);
        }
    }
}

} // namespace garadget
#endif
