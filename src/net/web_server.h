#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/alert_engine.h"
#include "../core/config.h"
#include "../core/door_controller.h"
#include "ha_mqtt.h"
#include "sntp_client.h"
#include "wifi_manager.h"

namespace garadget {

class WebServer {
public:
    WebServer(ConfigManager& config,
              ConfigStorage& storage,
              DoorController& door,
              AlertEngine& alerts,
              WifiManager& wifi,
              SntpClient& sntp,
              HaMqttBridge& mqtt);

    void setHooks(ConfigSavedCallback onConfigSaved, SystemActionCallback onSystemAction, void* ctx) {
        onConfigSaved_ = onConfigSaved;
        onSystemAction_ = onSystemAction;
        hookCtx_ = ctx;
    }

    void init();
    void tick(uint32_t nowMs);

private:
    ConfigManager& config_;
    ConfigStorage& storage_;
    DoorController& door_;
    AlertEngine& alerts_;
    WifiManager& wifi_;
    SntpClient& sntp_;
    HaMqttBridge& mqtt_;

    bool started_ = false;
    ConfigSavedCallback onConfigSaved_ = nullptr;
    SystemActionCallback onSystemAction_ = nullptr;
    void* hookCtx_ = nullptr;
};

} // namespace garadget
