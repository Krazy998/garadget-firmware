#include "mqtt_client.h"

#include <cstring>

#ifdef PARTICLE
#include "Particle.h"
#endif

namespace garadget {

namespace {
constexpr uint8_t kPktConnect    = 0x10;
constexpr uint8_t kPktConnAck    = 0x20;
constexpr uint8_t kPktPublish    = 0x30;
constexpr uint8_t kPktPubAck     = 0x40;
constexpr uint8_t kPktSubscribe  = 0x82; // Fixed header with QoS 1 required by MQTT 3.1.1
constexpr uint8_t kPktSubAck     = 0x90;
constexpr uint8_t kPktPingReq    = 0xC0;
constexpr uint8_t kPktPingResp   = 0xD0;
constexpr uint8_t kPktDisconnect = 0xE0;

#ifdef PARTICLE
TCPClient gMqttTcp;
#endif
} // namespace

size_t MqttClient::encodeRemainingLength(uint32_t remLen, uint8_t out[4]) {
    size_t count = 0;
    do {
        uint8_t digit = static_cast<uint8_t>(remLen & 0x7Fu);
        remLen >>= 7;
        if (remLen > 0) {
            digit |= 0x80u;
        }
        out[count++] = digit;
    } while (remLen > 0 && count < 4);
    return count;
}

bool MqttClient::parseIpv4(const char* str, uint8_t out[4]) {
    if (!str || str[0] == '\0') return false;
    int octet = 0;
    int val = 0;
    int digits = 0;
    for (size_t i = 0; ; ++i) {
        char c = str[i];
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
}

bool MqttClient::writeUtf8String(uint8_t* buf, size_t cap, size_t& pos, const char* str) {
    size_t slen = str ? std::strlen(str) : 0;
    if (slen > 0xFFFF || pos + 2 + slen > cap) return false;
    buf[pos++] = static_cast<uint8_t>((slen >> 8) & 0xFF);
    buf[pos++] = static_cast<uint8_t>(slen & 0xFF);
    if (slen > 0) {
        std::memcpy(buf + pos, str, slen);
        pos += slen;
    }
    return true;
}

void MqttClient::resetRx() {
    rxStage_ = RxStage::Header;
    rxHeader_ = 0;
    rxRemLen_ = 0;
    rxMult_ = 1;
    rxPos_ = 0;
    rxOverflow_ = false;
    rxPacketTimer_.stop();
}

#ifdef PARTICLE

void MqttClient::closeTransport() {
    gMqttTcp.stop();
    state_ = State::Disconnected;
    activityTimer_.stop();
    responseTimer_.stop();
    resetRx();
}

bool MqttClient::sendPacket(uint8_t fixedHeader, const uint8_t* body, size_t bodyLen) {
    if (!gMqttTcp.connected()) return false;
    uint8_t hdr[5];
    hdr[0] = fixedHeader;
    size_t remBytes = encodeRemainingLength(static_cast<uint32_t>(bodyLen), hdr + 1);
    size_t hdrLen = 1 + remBytes;

    int w1 = gMqttTcp.write(hdr, hdrLen);
    if (w1 != static_cast<int>(hdrLen)) {
        closeTransport();
        return false;
    }
    if (bodyLen > 0) {
        int w2 = gMqttTcp.write(body, bodyLen);
        if (w2 != static_cast<int>(bodyLen)) {
            closeTransport();
            return false;
        }
    }
    if (state_ == State::Connected) {
        // Send PINGREQ at 75% of keepalive interval to stay well ahead of 1.5x broker timeouts
        activityTimer_.start(millis(), static_cast<uint32_t>(keepAliveSec_) * 750u);
    }
    return true;
}

bool MqttClient::connect(const Options& opts, uint32_t nowMs) {
    closeTransport();
    if (!opts.host || opts.host[0] == '\0' || opts.port == 0) {
        return false;
    }

    keepAliveSec_ = (opts.keepAliveSec < 5) ? 5 : opts.keepAliveSec;

    uint8_t ip[4];
    int rc = 0;
    if (parseIpv4(opts.host, ip)) {
        Log.info("MQTT: connecting to %u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], opts.port);
        rc = gMqttTcp.connect(IPAddress(ip[0], ip[1], ip[2], ip[3]), opts.port);
    } else {
        Log.info("MQTT: connecting to %s:%u", opts.host, opts.port);
        rc = gMqttTcp.connect(opts.host, opts.port);
    }

    if (!rc) {
        Log.warn("MQTT: TCP connect failed");
        closeTransport();
        return false;
    }

    // Build MQTT 3.1.1 CONNECT variable header + payload
    size_t pos = 0;
    if (!writeUtf8String(txBuf_, kBufferSize, pos, "MQTT")) {
        closeTransport();
        return false;
    }
    if (pos + 4 > kBufferSize) {
        closeTransport();
        return false;
    }
    txBuf_[pos++] = 0x04; // Protocol level 4 (MQTT 3.1.1)

    uint8_t flags = 0x02; // Clean Session
    bool hasWill = opts.willTopic && opts.willTopic[0] != '\0';
    bool hasUser = opts.username && opts.username[0] != '\0';
    bool hasPass = hasUser && opts.password && opts.password[0] != '\0';
    if (hasWill) {
        flags |= 0x04; // Will Flag
        if (opts.willRetain) flags |= 0x20;
    }
    if (hasUser) flags |= 0x80;
    if (hasPass) flags |= 0x40;

    txBuf_[pos++] = flags;
    txBuf_[pos++] = static_cast<uint8_t>((keepAliveSec_ >> 8) & 0xFF);
    txBuf_[pos++] = static_cast<uint8_t>(keepAliveSec_ & 0xFF);

    if (!writeUtf8String(txBuf_, kBufferSize, pos, opts.clientId ? opts.clientId : "garadget")) {
        closeTransport();
        return false;
    }
    if (hasWill) {
        if (!writeUtf8String(txBuf_, kBufferSize, pos, opts.willTopic) ||
            !writeUtf8String(txBuf_, kBufferSize, pos, opts.willPayload ? opts.willPayload : "")) {
            closeTransport();
            return false;
        }
    }
    if (hasUser) {
        if (!writeUtf8String(txBuf_, kBufferSize, pos, opts.username)) {
            closeTransport();
            return false;
        }
        if (hasPass && !writeUtf8String(txBuf_, kBufferSize, pos, opts.password)) {
            closeTransport();
            return false;
        }
    }

    if (!sendPacket(kPktConnect, txBuf_, pos)) {
        return false;
    }

    state_ = State::WaitingConnAck;
    responseTimer_.start(nowMs, 5000);
    return true;
}

void MqttClient::disconnect() {
    if (gMqttTcp.connected() && isConnected()) {
        uint8_t pkt[2] = {kPktDisconnect, 0x00};
        gMqttTcp.write(pkt, 2);
    }
    closeTransport();
}

bool MqttClient::publish(const char* topic, const char* payload, bool retain) {
    return publish(topic, reinterpret_cast<const uint8_t*>(payload ? payload : ""),
                   payload ? std::strlen(payload) : 0, retain);
}

bool MqttClient::publish(const char* topic, const uint8_t* payload, size_t len, bool retain) {
    if (!isConnected() || !topic) return false;
    size_t pos = 0;
    if (!writeUtf8String(txBuf_, kBufferSize, pos, topic)) return false;
    if (pos + len > kBufferSize) {
        Log.warn("MQTT: publish payload too large (%u bytes)", static_cast<unsigned>(pos + len));
        return false;
    }
    if (len > 0 && payload) {
        std::memcpy(txBuf_ + pos, payload, len);
        pos += len;
    }
    uint8_t hdr = kPktPublish | (retain ? 0x01u : 0x00u);
    return sendPacket(hdr, txBuf_, pos);
}

bool MqttClient::subscribe(const char* topic, uint8_t qos) {
    if (!isConnected() || !topic) return false;
    if (++nextPacketId_ == 0) nextPacketId_ = 1;

    size_t pos = 0;
    txBuf_[pos++] = static_cast<uint8_t>((nextPacketId_ >> 8) & 0xFF);
    txBuf_[pos++] = static_cast<uint8_t>(nextPacketId_ & 0xFF);
    if (!writeUtf8String(txBuf_, kBufferSize, pos, topic)) return false;
    if (pos + 1 > kBufferSize) return false;
    txBuf_[pos++] = qos & 0x01u;
    return sendPacket(kPktSubscribe, txBuf_, pos);
}

bool MqttClient::sendPing(uint32_t nowMs) {
    if (!sendPacket(kPktPingReq, nullptr, 0)) return false;
    state_ = State::WaitingPingResp;
    responseTimer_.start(nowMs, 5000);
    return true;
}

bool MqttClient::sendPubAck(uint16_t packetId) {
    uint8_t body[2] = {
        static_cast<uint8_t>((packetId >> 8) & 0xFF),
        static_cast<uint8_t>(packetId & 0xFF),
    };
    return sendPacket(kPktPubAck, body, 2);
}

void MqttClient::tick(uint32_t nowMs) {
    if (state_ == State::Disconnected) return;

    if (!gMqttTcp.connected()) {
        Log.warn("MQTT: TCP connection closed by peer");
        closeTransport();
        return;
    }

    pumpIncoming(nowMs);
    if (state_ == State::Disconnected) return;

    // Abort stalled partial packet after 3s
    if (rxStage_ != RxStage::Header && rxPacketTimer_.expired(nowMs)) {
        Log.warn("MQTT: stalled incoming packet, resetting connection");
        closeTransport();
        return;
    }

    switch (state_) {
        case State::WaitingConnAck:
            if (responseTimer_.expired(nowMs)) {
                Log.warn("MQTT: CONNACK timeout");
                closeTransport();
            }
            break;

        case State::Connected:
            if (activityTimer_.expired(nowMs)) {
                sendPing(nowMs);
            }
            break;

        case State::WaitingPingResp:
            if (responseTimer_.expired(nowMs)) {
                Log.warn("MQTT: PINGRESP timeout");
                closeTransport();
            }
            break;

        default:
            break;
    }
}

void MqttClient::pumpIncoming(uint32_t nowMs) {
    // Process up to 512 bytes per tick to keep loop latency tiny
    int budget = 512;
    while (budget-- > 0 && gMqttTcp.available() > 0 && state_ != State::Disconnected) {
        int raw = gMqttTcp.read();
        if (raw < 0) break;
        uint8_t byte = static_cast<uint8_t>(raw);

        switch (rxStage_) {
            case RxStage::Header:
                rxHeader_ = byte;
                rxRemLen_ = 0;
                rxMult_ = 1;
                rxPos_ = 0;
                rxOverflow_ = false;
                rxStage_ = RxStage::RemLen;
                rxPacketTimer_.start(nowMs, 3000);
                break;

            case RxStage::RemLen:
                rxRemLen_ += static_cast<uint32_t>(byte & 0x7Fu) * rxMult_;
                if ((byte & 0x80u) == 0) {
                    if (rxRemLen_ == 0) {
                        handlePacket(nowMs);
                        resetRx();
                    } else {
                        rxOverflow_ = (rxRemLen_ >= kBufferSize);
                        rxStage_ = RxStage::Body;
                    }
                } else {
                    rxMult_ *= 128u;
                    if (rxMult_ > 128u * 128u * 128u) {
                        Log.error("MQTT: malformed remaining length");
                        closeTransport();
                        return;
                    }
                }
                break;

            case RxStage::Body:
                if (!rxOverflow_ && rxPos_ < kBufferSize - 1) {
                    rxBuf_[rxPos_] = byte;
                }
                rxPos_++;
                if (rxPos_ >= rxRemLen_) {
                    if (!rxOverflow_) {
                        rxBuf_[rxPos_] = '\0';
                        handlePacket(nowMs);
                    } else {
                        Log.warn("MQTT: dropped oversized incoming packet (%lu bytes)",
                                 static_cast<unsigned long>(rxRemLen_));
                    }
                    resetRx();
                }
                break;
        }
    }
}

void MqttClient::handlePacket(uint32_t nowMs) {
    uint8_t type = rxHeader_ & 0xF0u;

    if (state_ == State::WaitingConnAck) {
        if (type == kPktConnAck && rxPos_ >= 2) {
            lastConnAck_ = rxBuf_[1];
            if (lastConnAck_ == 0) {
                Log.info("MQTT: connected (CONNACK accepted)");
                state_ = State::Connected;
                responseTimer_.stop();
                activityTimer_.start(nowMs, static_cast<uint32_t>(keepAliveSec_) * 750u);
                if (onConnect_) {
                    onConnect_(cbCtx_);
                }
            } else {
                Log.error("MQTT: CONNACK rejected (code=%u)", lastConnAck_);
                closeTransport();
            }
        } else {
            Log.error("MQTT: expected CONNACK, got 0x%02X", type);
            closeTransport();
        }
        return;
    }

    // Any valid packet refreshes keepalive window (75% of keepAliveSec)
    activityTimer_.start(nowMs, static_cast<uint32_t>(keepAliveSec_) * 750u);

    switch (type) {
        case kPktPublish:
            handlePublishPacket(rxHeader_, rxBuf_, rxPos_);
            break;

        case kPktPingResp:
            if (state_ == State::WaitingPingResp) {
                state_ = State::Connected;
                responseTimer_.stop();
            }
            break;

        case kPktPingReq:
            sendPacket(kPktPingResp, nullptr, 0);
            break;

        case kPktSubAck:
        case kPktPubAck:
            break;

        default:
            break;
    }
}

void MqttClient::handlePublishPacket(uint8_t header, const uint8_t* body, size_t bodyLen) {
    if (bodyLen < 2) return;
    uint16_t topicLen = static_cast<uint16_t>((body[0] << 8) | body[1]);
    size_t offset = 2 + topicLen;
    if (offset > bodyLen) return;

    uint8_t qos = (header >> 1) & 0x03u;
    uint16_t packetId = 0;
    if (qos > 0) {
        if (offset + 2 > bodyLen) return;
        packetId = static_cast<uint16_t>((body[offset] << 8) | body[offset + 1]);
        offset += 2;
    }

    char topic[128];
    size_t copyLen = (topicLen < sizeof(topic) - 1) ? topicLen : (sizeof(topic) - 1);
    std::memcpy(topic, body + 2, copyLen);
    topic[copyLen] = '\0';

    size_t payloadLen = bodyLen - offset;
    char payload[512];
    size_t pCopy = (payloadLen < sizeof(payload) - 1) ? payloadLen : (sizeof(payload) - 1);
    if (pCopy > 0) {
        std::memcpy(payload, body + offset, pCopy);
    }
    payload[pCopy] = '\0';

    if (qos == 1) {
        sendPubAck(packetId);
    }

    if (onMessage_) {
        onMessage_(topic, payload, pCopy, cbCtx_);
    }
}

#endif // PARTICLE

} // namespace garadget
