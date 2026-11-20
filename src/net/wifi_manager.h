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
    void requestExitSetupMode();
    void touchSetupActivity();
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
    static constexpr uint32_t kSetupIdleTimeoutMs = 10UL * 60UL * 1000UL; // 10 minutes

    Timer reconnectTimer_;
    Timer setupTimeoutTimer_;
    Timer exitSetupTimer_;
    bool connecting_ = false;
    bool wasReady_ = false;
    bool wasListening_ = false;
    volatile bool exitSetupRequested_ = false;
    volatile bool setupActivityTouched_ = false;
};

} // namespace garadget
