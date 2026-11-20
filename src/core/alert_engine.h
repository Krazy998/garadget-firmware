#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "timer.h"
#include "types.h"

namespace garadget {

using AlertCallback = void (*)(const char* alertType, const char* alertData, const char* jsonPayload, void* ctx);

// Evaluates Garadget alert rules:
//   - Status change alerts filtered by `aev` bitmap
//   - Open-door duration timeout (`aot` seconds)
//   - Night-time open window (`ans`..`ane` minutes from midnight)
//   - Maintains a fixed-size ring buffer of recent alerts for the Web UI
class AlertEngine {
public:
    static constexpr size_t kHistorySize = 8;

    explicit AlertEngine(const DeviceConfig& cfg);

    void setCallback(AlertCallback cb, void* ctx) {
        cb_ = cb;
        ctx_ = ctx;
    }

    // Call on boot with initial status (optionally fires `init` alert if enabled in `aev`).
    void onInit(DoorStatus initialStatus, uint32_t nowMs);

    // Call whenever door status changes.
    void onStatusChange(DoorStatus newStatus, uint32_t nowMs);

    // Call when configuration is updated (fires `config` alert if bit 8 is enabled).
    void onConfigUpdated(uint32_t nowMs);

    // Periodic tick for timeout & night-time alerts.
    // localMinuteOfDay: 0..1439 if timeValid is true.
    void tick(DoorStatus currentStatus, uint32_t nowMs, bool timeValid, uint16_t localMinuteOfDay);

    bool isNightMinute(uint16_t minuteOfDay) const;

    size_t historyCount() const { return historyCount_; }
    const AlertRecord& historyItem(size_t indexFromNewest) const;

private:
    void fire(const char* type, const char* data, uint32_t nowMs);

    const DeviceConfig& cfg_;
    AlertCallback cb_ = nullptr;
    void* ctx_ = nullptr;

    Timer openTimeoutTimer_;
    uint32_t openedAtMs_ = 0;
    bool timeoutFired_ = false;
    bool nightFired_ = false;

    AlertRecord history_[kHistorySize]{};
    size_t historyHead_ = 0;
    size_t historyCount_ = 0;
};

} // namespace garadget
