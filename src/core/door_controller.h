#pragma once

#include <cstdint>

#include "config.h"
#include "timer.h"
#include "types.h"

namespace garadget {

struct SensorReading {
    uint8_t reflectionPct = 0; // 0..100
    uint16_t rawBase = 0;      // 0..4095 (12-bit ADC baseline)
    uint8_t brightnessPct = 0; // 0..100 ambient brightness
};

// Pure hardware-abstraction interface for DoorController (mockable in unit tests).
class DoorHardware {
public:
    virtual ~DoorHardware() = default;
    virtual void setRelay(bool active) = 0;
    virtual SensorReading readSensor(SensorMode mode, uint8_t samples) = 0;
};

using DoorStatusCallback = void (*)(DoorStatus oldStatus, DoorStatus newStatus, void* ctx);

// Pure, deterministic state machine controlling the garage door sensor, motion timers, and relay click sequencer.
class DoorController {
public:
    DoorController(DoorHardware& hw, const DeviceConfig& cfg);

    // Performs initial hardware-safe boot scan (relay forced OFF; detects initial Open/Closed without false transition).
    void init(uint32_t nowMs);

    // Non-blocking periodic tick (call every loop()).
    void tick(uint32_t nowMs);

    // Executes a door command (Open, Close, Stop, Toggle).
    // Returns true if accepted (or already in target state), false if busy or invalid.
    bool executeCommand(DoorCommand cmd, uint32_t nowMs);

    // Forces an immediate sensor reading (useful for laser aiming / calibration from Web UI).
    SensorReading forceScan(uint32_t nowMs);

    void setStatusCallback(DoorStatusCallback cb, void* ctx) {
        statusCb_ = cb;
        statusCtx_ = ctx;
    }

    DoorStatus status() const { return status_; }
    uint32_t lastEventMs() const { return lastEventMs_; }
    uint32_t timeInStatusMs(uint32_t nowMs) const { return static_cast<uint32_t>(nowMs - lastEventMs_); }
    const SensorReading& lastReading() const { return lastReading_; }
    bool relayActive() const { return relayActive_; }
    bool isClickSequenceBusy() const { return relayActive_ || clicksRemaining_ > 0 || relayPauseTimer_.isRunning(); }

    // Safe reflection percentage calculation (exposed for direct unit testing).
    static uint8_t computeLaserReflection(const int32_t* baseSamples, const int32_t* litSamples,
                                          uint8_t count, int32_t bias = kSensorBias);
    static uint8_t computeAmbientBrightness(uint16_t rawBase);

private:
    void evaluateSensor(uint32_t nowMs, bool isInitial);
    void startRelayClicks(uint8_t clicks, uint32_t nowMs);
    void transitionTo(DoorStatus next, uint32_t nowMs);

    DoorHardware& hw_;
    const DeviceConfig& cfg_;

    DoorStatus status_ = DoorStatus::Unknown;
    uint32_t lastEventMs_ = 0;
    SensorReading lastReading_{};

    Timer scanTimer_;
    Timer motionTimer_;
    Timer relayOnTimer_;
    Timer relayPauseTimer_;

    bool relayActive_ = false;
    uint8_t clicksRemaining_ = 0;

    DoorStatusCallback statusCb_ = nullptr;
    void* statusCtx_ = nullptr;
};

} // namespace garadget
