#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/alert_engine.h"
#include "../core/config.h"
#include "../core/door_controller.h"
#include "../core/timer.h"
#include "mqtt_client.h"
#include "wifi_manager.h"

namespace garadget {

using ConfigSavedCallback = void (*)(void* ctx);
using SystemActionCallback = void (*)(const char* action, void* ctx);

// Garadget MQTT Bridge + Home Assistant MQTT Auto-Discovery Engine.
class HaMqttBridge {
public:
    HaMqttBridge(ConfigManager& config,
                 ConfigStorage& storage,
                 DoorController& door,
                 AlertEngine& alerts,
                 WifiManager& wifi);

    void setHooks(ConfigSavedCallback onConfigSaved, SystemActionCallback onSystemAction, void* ctx) {
        onConfigSaved_ = onConfigSaved;
        onSystemAction_ = onSystemAction;
        hookCtx_ = ctx;
    }

    void init(uint32_t nowMs);
    void reconfigure(uint32_t nowMs);
    void tick(uint32_t nowMs);

    void publishStatus(uint32_t nowMs);
    void publishConfig();
    void publishAlert(const char* alertJson);
    void triggerDiscoveryRefresh();

    bool isConnected() const { return client_.isConnected(); }
    const char* topicId() const { return topicId_; }

    // Pure builder exposed for unit testing HA Discovery JSON payloads
    static bool buildDiscoveryPayload(uint8_t entityIndex,
                                      const DeviceConfig& cfg,
                                      const char* deviceId,
                                      const char* topicId,
                                      const char* ipStr,
                                      char* outTopic, size_t topicCap,
                                      char* outPayload, size_t payloadCap);
    static constexpr uint8_t kDiscoveryEntityCount = 8;

private:
    static void onConnectStatic(void* ctx);
    static void onMessageStatic(void* ctx, const char* topic, const char* payload, size_t len);
    static void onMessageTrampoline(const char* topic, const char* payload, size_t len, void* ctx);

    void onConnected();
    void onMessage(const char* topic, const char* payload, size_t len);
    void refreshTopicId();

    ConfigManager& config_;
    ConfigStorage& storage_;
    DoorController& door_;
    AlertEngine& alerts_;
    WifiManager& wifi_;
    MqttClient client_;

    char topicId_[kDeviceNameCap]{};
    char deviceId_[32]{};

    Timer reconnectTimer_;
    Timer periodicStatusTimer_;
    uint32_t backoffMs_ = 2000;
    uint8_t discoveryStep_ = kDiscoveryEntityCount; // < kDiscoveryEntityCount means publishing in progress
    uint8_t lastPublishedReflection_ = 255;

    ConfigSavedCallback onConfigSaved_ = nullptr;
    SystemActionCallback onSystemAction_ = nullptr;
    void* hookCtx_ = nullptr;
};

} // namespace garadget
