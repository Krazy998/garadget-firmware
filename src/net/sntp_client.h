#pragma once

#include <cstdint>

#include "../core/config.h"
#include "../core/timer.h"

namespace garadget {

// Lightweight non-blocking UDP SNTP client (RFC 4330).
// Syncs local RTC without Particle Cloud so night alerts and DST work 100% locally.
class SntpClient {
public:
    explicit SntpClient(const ConfigManager& config);

    void init();
    void tick(uint32_t nowMs, bool wifiReady);
    void requestSync(uint32_t nowMs);
    void applyTimezoneNow();

    bool isSynced() const { return synced_; }
    uint32_t lastSyncUptimeSec() const { return lastSyncUptimeSec_; }

private:
    static constexpr uint16_t kNtpPort = 123;
    static constexpr uint16_t kLocalPort = 2390;
    static constexpr uint32_t kNtpUnixOffset = 2208988800UL;
    static constexpr uint32_t kSyncIntervalMs = 6UL * 60UL * 60UL * 1000UL; // 6 hours
    static constexpr uint32_t kRetryIntervalMs = 30UL * 1000UL;            // 30 seconds
    static constexpr uint32_t kResponseTimeoutMs = 3000UL;
    static constexpr uint32_t kDstCheckIntervalMs = 15UL * 60UL * 1000UL;  // 15 minutes

    const ConfigManager& config_;
    Timer syncTimer_;
    Timer responseTimer_;
    Timer dstTimer_;
    bool waitingResponse_ = false;
    bool synced_ = false;
    bool udpStarted_ = false;
    uint32_t lastSyncUptimeSec_ = 0;
};

} // namespace garadget
