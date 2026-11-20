#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/config.h"
#include "../core/door_controller.h"
#include "../net/ha_mqtt.h"
#include "../net/wifi_manager.h"

namespace garadget {

// Lightweight USB Serial diagnostic & setup CLI (115200 baud).
class SerialCli {
public:
    SerialCli(ConfigManager& config,
              ConfigStorage& storage,
              DoorController& door,
              WifiManager& wifi,
              HaMqttBridge& mqtt);

    void setHooks(ConfigSavedCallback onConfigSaved, SystemActionCallback onSystemAction, void* ctx) {
        onConfigSaved_ = onConfigSaved;
        onSystemAction_ = onSystemAction;
        hookCtx_ = ctx;
    }

    void init();
    void tick(uint32_t nowMs);

private:
    void handleLine(const char* line, uint32_t nowMs);

    ConfigManager& config_;
    ConfigStorage& storage_;
    DoorController& door_;
    WifiManager& wifi_;
    HaMqttBridge& mqtt_;

    char lineBuf_[160]{};
    size_t linePos_ = 0;

    ConfigSavedCallback onConfigSaved_ = nullptr;
    SystemActionCallback onSystemAction_ = nullptr;
    void* hookCtx_ = nullptr;
};

} // namespace garadget
