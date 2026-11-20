#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/timer.h"

namespace garadget {

// Safe, zero-heap, non-blocking MQTT 3.1.1 client for Particle Device OS.
class MqttClient {
public:
    static constexpr size_t kBufferSize = 1280;

    using ConnectCallback = void (*)(void* ctx);
    using MessageCallback = void (*)(const char* topic, const char* payload, size_t payloadLen, void* ctx);

    struct Options {
        const char* host = "";
        uint16_t port = 1883;
        uint16_t keepAliveSec = 15;
        const char* clientId = "garadget";
        const char* username = "";
        const char* password = "";
        const char* willTopic = "";
        const char* willPayload = "Offline";
        bool willRetain = true;
    };

    enum class State : uint8_t {
        Disconnected = 0,
        WaitingConnAck,
        Connected,
        WaitingPingResp,
    };

    MqttClient() = default;

    void setCallbacks(ConnectCallback onConnect, MessageCallback onMessage, void* ctx) {
        onConnect_ = onConnect;
        onMessage_ = onMessage;
        cbCtx_ = ctx;
    }

    // Attempts TCP + MQTT CONNECT (non-blocking CONNACK wait).
    bool connect(const Options& opts, uint32_t nowMs);
    void disconnect();

    // Non-blocking pump: processes incoming bytes, keepalive pings, and timeouts.
    void tick(uint32_t nowMs);

    bool isConnected() const { return state_ == State::Connected || state_ == State::WaitingPingResp; }
    State state() const { return state_; }
    uint8_t lastConnAckCode() const { return lastConnAck_; }

    bool publish(const char* topic, const char* payload, bool retain = false);
    bool publish(const char* topic, const uint8_t* payload, size_t len, bool retain = false);
    bool subscribe(const char* topic, uint8_t qos = 0);

    // Pure helpers (also unit-testable on host)
    static size_t encodeRemainingLength(uint32_t remLen, uint8_t out[4]);
    static bool parseIpv4(const char* str, uint8_t out[4]);

private:
    enum class RxStage : uint8_t {
        Header = 0,
        RemLen,
        Body,
    };

    void resetRx();
    void closeTransport();
    bool sendPacket(uint8_t fixedHeader, const uint8_t* body, size_t bodyLen);
    void pumpIncoming(uint32_t nowMs);
    void handlePacket(uint32_t nowMs);
    void handlePublishPacket(uint8_t header, const uint8_t* body, size_t bodyLen);
    bool sendPing(uint32_t nowMs);
    bool sendPubAck(uint16_t packetId);

    static bool writeUtf8String(uint8_t* buf, size_t cap, size_t& pos, const char* str);

    State state_ = State::Disconnected;
    uint16_t keepAliveSec_ = 15;
    uint16_t nextPacketId_ = 1;
    uint8_t lastConnAck_ = 0;

    Timer activityTimer_;
    Timer responseTimer_;
    Timer rxPacketTimer_;

    RxStage rxStage_ = RxStage::Header;
    uint8_t rxHeader_ = 0;
    uint32_t rxRemLen_ = 0;
    uint32_t rxMult_ = 1;
    size_t rxPos_ = 0;
    bool rxOverflow_ = false;

    uint8_t txBuf_[kBufferSize]{};
    uint8_t rxBuf_[kBufferSize]{};

    ConnectCallback onConnect_ = nullptr;
    MessageCallback onMessage_ = nullptr;
    void* cbCtx_ = nullptr;
};

} // namespace garadget
