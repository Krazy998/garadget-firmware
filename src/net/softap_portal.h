#pragma once

#include "../core/config.h"
#include "wifi_manager.h"

#ifdef PARTICLE
#include "Particle.h"
#include "softap_http.h"
#endif

namespace garadget {

void initSoftApPortal(ConfigManager* config, ConfigStorage* storage, WifiManager* wifi);

#ifdef PARTICLE
void softapPageHandler(const char* url,
                       ResponseCallback* cb,
                       void* cbArg,
                       Reader* request,
                       Writer* response,
                       void* reserved);
#endif

} // namespace garadget
