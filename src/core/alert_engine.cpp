#include "alert_engine.h"

#include <cstdio>
#include <cstring>

#include "json_utils.h"

namespace garadget {

AlertEngine::AlertEngine(const DeviceConfig& cfg) : cfg_(cfg) {}

void AlertEngine::onInit(DoorStatus initialStatus, uint32_t nowMs) {
    if (cfg_.alertEvents & kAlertInit) {
        fire("state", doorStatusToString(DoorStatus::Init), nowMs);
    }
    if (initialStatus != DoorStatus::Closed && initialStatus != DoorStatus::Unknown) {
        openedAtMs_ = nowMs;
        activeTimeoutSec_ = cfg_.alertOpenTimeout;
        if (cfg_.alertOpenTimeout > 0) {
            openTimeoutTimer_.start(nowMs, static_cast<uint32_t>(cfg_.alertOpenTimeout) * 1000u);
        }
    }
}

void AlertEngine::onStatusChange(DoorStatus newStatus, uint32_t nowMs) {
    uint16_t bit = statusToAlertBit(newStatus);
    if (bit != 0 && (cfg_.alertEvents & bit)) {
        fire("state", doorStatusToString(newStatus), nowMs);
    }

    switch (newStatus) {
        case DoorStatus::Opening:
        case DoorStatus::Closing:
        case DoorStatus::Open:
        case DoorStatus::Stopped:
            if (cfg_.alertOpenTimeout > 0 && !openTimeoutTimer_.isRunning() && !timeoutFired_) {
                openedAtMs_ = nowMs;
                activeTimeoutSec_ = cfg_.alertOpenTimeout;
                openTimeoutTimer_.start(nowMs, static_cast<uint32_t>(cfg_.alertOpenTimeout) * 1000u);
            }
            break;

        case DoorStatus::Closed:
            openTimeoutTimer_.stop();
            activeTimeoutSec_ = 0;
            timeoutFired_ = false;
            nightFired_ = false;
            break;

        default:
            break;
    }
}

void AlertEngine::onConfigUpdated(uint32_t nowMs) {
    if (cfg_.alertEvents & kAlertConfig) {
        fire("config", "updated", nowMs);
    }
}

bool AlertEngine::isNightMinute(uint16_t minuteOfDay) const {
    if (cfg_.alertNightStart == cfg_.alertNightEnd) {
        return false; // Disabled when start == end
    }
    if (cfg_.alertNightStart > cfg_.alertNightEnd) {
        // Overnight span (e.g., 22:00 = 1320 .. 06:00 = 360)
        return (minuteOfDay >= cfg_.alertNightStart) || (minuteOfDay < cfg_.alertNightEnd);
    }
    // Same-day span (e.g., 01:00 = 60 .. 05:00 = 300)
    return (minuteOfDay >= cfg_.alertNightStart) && (minuteOfDay < cfg_.alertNightEnd);
}

void AlertEngine::tick(DoorStatus currentStatus, uint32_t nowMs, bool timeValid, uint16_t localMinuteOfDay) {
    if (currentStatus == DoorStatus::Closed || currentStatus == DoorStatus::Unknown) {
        return;
    }

    // If user enabled, disabled, or changed timeout duration while door was already open, update timer cleanly
    if (cfg_.alertOpenTimeout == 0) {
        openTimeoutTimer_.stop();
        activeTimeoutSec_ = 0;
    } else if (!timeoutFired_ && (!openTimeoutTimer_.isRunning() || activeTimeoutSec_ != cfg_.alertOpenTimeout)) {
        if (!openTimeoutTimer_.isRunning()) {
            openedAtMs_ = nowMs;
        }
        activeTimeoutSec_ = cfg_.alertOpenTimeout;
        uint32_t targetMs = static_cast<uint32_t>(cfg_.alertOpenTimeout) * 1000u;
        uint32_t elapsedMs = static_cast<uint32_t>(nowMs - openedAtMs_);
        uint32_t remMs = (elapsedMs >= targetMs) ? 0u : (targetMs - elapsedMs);
        openTimeoutTimer_.start(nowMs, remMs);
    }

    if (!timeoutFired_ && openTimeoutTimer_.expired(nowMs)) {
        char dur[16];
        formatDuration(static_cast<uint32_t>(nowMs - openedAtMs_), dur, sizeof(dur));
        timeoutFired_ = true;
        fire("timeout", dur, nowMs);
    }

    if (timeValid) {
        if (isNightMinute(localMinuteOfDay)) {
            if (!nightFired_) {
                char range[16];
                std::snprintf(range, sizeof(range), "%u-%u",
                              static_cast<unsigned>(cfg_.alertNightStart),
                              static_cast<unsigned>(cfg_.alertNightEnd));
                nightFired_ = true;
                fire("night", range, nowMs);
            }
        } else {
            // Reset latch outside the night window so multi-day open doors alert once per night
            nightFired_ = false;
        }
    }
}

void AlertEngine::fire(const char* type, const char* data, uint32_t nowMs) {
    AlertRecord& rec = history_[historyHead_];
    std::strncpy(rec.type, type ? type : "", sizeof(rec.type) - 1);
    rec.type[sizeof(rec.type) - 1] = '\0';
    std::strncpy(rec.data, data ? data : "", sizeof(rec.data) - 1);
    rec.data[sizeof(rec.data) - 1] = '\0';
    rec.uptimeSec = nowMs / 1000u;

    historyHead_ = (historyHead_ + 1) % kHistorySize;
    if (historyCount_ < kHistorySize) {
        historyCount_++;
    }

    char payload[192];
    JsonWriter w(payload, sizeof(payload));
    w.beginObject();
    w.keyString("name", cfg_.deviceName);
    w.keyString("type", rec.type);
    w.keyString("data", rec.data);
    w.endObject();

    if (cb_) {
        cb_(rec.type, rec.data, payload, ctx_);
    }
}

const AlertRecord& AlertEngine::historyItem(size_t indexFromNewest) const {
    size_t idx = (historyHead_ + kHistorySize - 1 - (indexFromNewest % kHistorySize)) % kHistorySize;
    return history_[idx];
}

} // namespace garadget
