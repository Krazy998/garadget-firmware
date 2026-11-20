#pragma once

#include <cstdint>

namespace garadget {

// Rollover-safe monotonic millisecond timer (uses unsigned subtraction: now - start >= duration).
class Timer {
public:
    Timer() = default;
    explicit Timer(uint32_t durationMs) : durationMs_(durationMs) {}

    void setDuration(uint32_t durationMs) { durationMs_ = durationMs; }
    uint32_t duration() const { return durationMs_; }

    void start(uint32_t nowMs) {
        if (durationMs_ == 0) {
            running_ = false;
            return;
        }
        startMs_ = nowMs;
        running_ = true;
    }

    void start(uint32_t nowMs, uint32_t durationMs) {
        durationMs_ = durationMs;
        start(nowMs);
    }

    void stop() { running_ = false; }

    bool isRunning() const { return running_; }

    // Returns true once when the timer transitions from running to expired.
    bool expired(uint32_t nowMs) {
        if (!running_) {
            return false;
        }
        if (static_cast<uint32_t>(nowMs - startMs_) >= durationMs_) {
            running_ = false;
            return true;
        }
        return false;
    }

    uint32_t remaining(uint32_t nowMs) const {
        if (!running_) {
            return 0;
        }
        uint32_t elapsed = static_cast<uint32_t>(nowMs - startMs_);
        return (elapsed >= durationMs_) ? 0 : (durationMs_ - elapsed);
    }

private:
    uint32_t startMs_ = 0;
    uint32_t durationMs_ = 0;
    bool running_ = false;
};

} // namespace garadget
