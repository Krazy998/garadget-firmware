#pragma once

#include "../core/config.h"
#include "ha_mqtt.h"
#include "wifi_manager.h"

#ifdef PARTICLE
#include "Particle.h"
#include "softap_http.h"
#endif

namespace garadget {

void initSoftApPortal(ConfigManager* config,
                      ConfigStorage* storage,
                      WifiManager* wifi,
                      ConfigSavedCallback onConfigSaved = nullptr,
                      void* hookCtx = nullptr);

// Processes staged SoftAP config load/save/clear-wifi requests on the application thread (loop()).
void tickSoftApPortal(uint32_t nowMs);

#ifdef PARTICLE
void softapPageHandler(const char* url,
                       ResponseCallback* cb,
                       void* cbArg,
                       Reader* request,
                       Writer* response,
                       void* reserved);
#endif

} // namespace garadget
