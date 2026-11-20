#include "sntp_client.h"

#ifdef PARTICLE
#include "Particle.h"
#include <cstring>

#include "mqtt_client.h"

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
    // Keep DST timezone offset updated whenever RTC holds a valid clock (even across warm reboots/offline LAN)
    if (Time.isValid() && (!dstTimer_.isRunning() || dstTimer_.expired(nowMs))) {
        applyTimezoneNow();
        dstTimer_.start(nowMs, kDstCheckIntervalMs);
    }

    if (!wifiReady) {
        waitingResponse_ = false;
        if (udpStarted_) {
            gNtpUdp.stop();
            udpStarted_ = false;
        }
        return;
    }

    if (waitingResponse_) {
        int packetLen = gNtpUdp.parsePacket();
        if (packetLen > 0) {
            uint8_t buf[48] = {};
            int got = gNtpUdp.read(buf, sizeof(buf));
            IPAddress fromIp = gNtpUdp.remoteIP();
            uint16_t fromPort = static_cast<uint16_t>(gNtpUdp.remotePort());

            IPAddress expectedIp(expectedServerIp_[0], expectedServerIp_[1],
                                 expectedServerIp_[2], expectedServerIp_[3]);
            uint8_t mode = buf[0] & 0x07u;
            uint8_t stratum = buf[1];
            uint32_t origSec =
                (static_cast<uint32_t>(buf[24]) << 24) |
                (static_cast<uint32_t>(buf[25]) << 16) |
                (static_cast<uint32_t>(buf[26]) << 8) |
                static_cast<uint32_t>(buf[27]);
            uint32_t origFrac =
                (static_cast<uint32_t>(buf[28]) << 24) |
                (static_cast<uint32_t>(buf[29]) << 16) |
                (static_cast<uint32_t>(buf[30]) << 8) |
                static_cast<uint32_t>(buf[31]);

            bool validReply = (got >= 48) &&
                              (fromIp == expectedIp) &&
                              (fromPort == kNtpPort) &&
                              (mode == 4u) &&
                              (stratum != 0u) &&
                              (origSec == txSec_) &&
                              (origFrac == txFrac_);

            if (!validReply) {
                Log.warn("SNTP: ignored unverified UDP packet (len=%d port=%u mode=%u stratum=%u)",
                         got, static_cast<unsigned>(fromPort),
                         static_cast<unsigned>(mode), static_cast<unsigned>(stratum));
            } else {
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

        IPAddress serverIp;
        uint8_t ipBytes[4];
        if (MqttClient::parseIpv4(server, ipBytes)) {
            serverIp = IPAddress(ipBytes[0], ipBytes[1], ipBytes[2], ipBytes[3]);
        } else {
            serverIp = WiFi.resolve(server);
        }
        if (!serverIp) {
            Log.warn("SNTP: failed to resolve %s", server);
            syncTimer_.start(nowMs, kRetryIntervalMs);
            return;
        }

        expectedServerIp_[0] = serverIp[0];
        expectedServerIp_[1] = serverIp[1];
        expectedServerIp_[2] = serverIp[2];
        expectedServerIp_[3] = serverIp[3];

        if (!udpStarted_) {
            gNtpUdp.begin(kLocalPort);
            udpStarted_ = true;
        }

        // Embed a transmit timestamp nonce so replies must echo it in Originate Timestamp
        txSec_ = (Time.isValid() ? static_cast<uint32_t>(Time.now()) : 0u) + kNtpUnixOffset;
        txFrac_ = nowMs ^ (nowMs << 13) ^ 0xA5C3F19Du;

        uint8_t req[48] = {};
        req[0] = 0x23; // LI=0, VN=4, Mode=3 (Client)
        req[40] = static_cast<uint8_t>((txSec_ >> 24) & 0xFFu);
        req[41] = static_cast<uint8_t>((txSec_ >> 16) & 0xFFu);
        req[42] = static_cast<uint8_t>((txSec_ >> 8) & 0xFFu);
        req[43] = static_cast<uint8_t>(txSec_ & 0xFFu);
        req[44] = static_cast<uint8_t>((txFrac_ >> 24) & 0xFFu);
        req[45] = static_cast<uint8_t>((txFrac_ >> 16) & 0xFFu);
        req[46] = static_cast<uint8_t>((txFrac_ >> 8) & 0xFFu);
        req[47] = static_cast<uint8_t>(txFrac_ & 0xFFu);

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
