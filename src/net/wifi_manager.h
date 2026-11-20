#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/config.h"
#include "../core/timer.h"

namespace garadget {

class WifiManager {
public:
    void init(uint32_t nowMs, const DeviceConfig& cfg);
    void applyIpConfig(const DeviceConfig& cfg);
    void tick(uint32_t nowMs);

    void enterSetupMode();
    void clearCredentials();

    bool isReady() const;
    bool isListening() const;

    int rssiDbm() const;
    void formatIp(char* out, size_t outCap) const;
    void formatSubnet(char* out, size_t outCap) const;
    void formatGateway(char* out, size_t outCap) const;
    void formatDns(char* out, size_t outCap) const;
    void formatMac(char* out, size_t outCap) const;
    void formatSsid(char* out, size_t outCap) const;
    void formatDeviceId(char* out, size_t outCap) const;

private:
    Timer reconnectTimer_;
    bool connecting_ = false;
    bool wasReady_ = false;
};

} // namespace garadget
