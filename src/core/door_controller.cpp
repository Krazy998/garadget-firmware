#include "door_controller.h"

namespace garadget {

DoorController::DoorController(DoorHardware& hw, const DeviceConfig& cfg)
    : hw_(hw), cfg_(cfg) {}

uint8_t DoorController::computeLaserReflection(const int32_t* baseSamples, const int32_t* litSamples,
                                               uint8_t count, int32_t bias) {
    if (!baseSamples || !litSamples || count == 0) return 0;

    int64_t sumBase = 0;
    int64_t sumDelta = 0;
    for (uint8_t i = 0; i < count; ++i) {
        int32_t base = baseSamples[i];
        int32_t lit = litSamples[i];
        if (base < 0) base = 0;
        if (lit < 0) lit = 0;

        int32_t effBase = (base > bias) ? (base - bias) : 0;
        int32_t delta = (base > lit) ? (base - lit) : 0;
        if (delta > effBase) delta = effBase;

        sumBase += effBase;
        sumDelta += delta;
    }

    if (sumBase <= 0) return 0;
    int64_t pct = (sumDelta * 100) / sumBase;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return static_cast<uint8_t>(pct);
}

uint8_t DoorController::computeAmbientBrightness(uint16_t rawBase) {
    if (rawBase >= 4096) return 0;
    int32_t bright = 99 - (static_cast<int32_t>(rawBase) * 100) / 4096;
    if (bright < 0) bright = 0;
    if (bright > 100) bright = 100;
    return static_cast<uint8_t>(bright);
}

void DoorController::init(uint32_t nowMs) {
    relayActive_ = false;
    clicksRemaining_ = 0;
    hw_.setRelay(false);
    lastEventMs_ = nowMs;
    evaluateSensor(nowMs, true);
    scanTimer_.start(nowMs, cfg_.readTimeMs);
}

SensorReading DoorController::forceScan(uint32_t nowMs) {
    evaluateSensor(nowMs, false);
    scanTimer_.start(nowMs, cfg_.readTimeMs);
    return lastReading_;
}

void DoorController::tick(uint32_t nowMs) {
    // 1. Handle relay pulse sequencer first (highest priority for hardware safety)
    if (relayActive_ && relayOnTimer_.expired(nowMs)) {
        hw_.setRelay(false);
        relayActive_ = false;
        if (clicksRemaining_ > 0) {
            clicksRemaining_--;
        }
        if (clicksRemaining_ > 0) {
            relayPauseTimer_.start(nowMs, cfg_.relayPauseMs);
        }
    } else if (!relayActive_ && clicksRemaining_ > 0 && relayPauseTimer_.expired(nowMs)) {
        hw_.setRelay(true);
        relayActive_ = true;
        relayOnTimer_.start(nowMs, cfg_.relayTimeMs);
    }

    // 2. Periodic sensor scan
    if (!scanTimer_.isRunning() || scanTimer_.expired(nowMs)) {
        evaluateSensor(nowMs, false);
        scanTimer_.start(nowMs, cfg_.readTimeMs);
    }

    // 3. Motion completion timer
    if (motionTimer_.expired(nowMs)) {
        if (status_ == DoorStatus::Opening) {
            transitionTo(DoorStatus::Open, nowMs);
        } else if (status_ == DoorStatus::Closing) {
            // Re-scan once before declaring Stopped
            evaluateSensor(nowMs, false);
            if (status_ != DoorStatus::Closed) {
                transitionTo(DoorStatus::Stopped, nowMs);
            }
        }
    }
}

void DoorController::evaluateSensor(uint32_t nowMs, bool isInitial) {
    SensorMode mode = static_cast<SensorMode>(cfg_.sensorMode);
    lastReading_ = hw_.readSensor(mode, cfg_.sensorReads);
    bool isClosed = (lastReading_.reflectionPct > cfg_.sensorThreshold);

    if (isInitial) {
        status_ = isClosed ? DoorStatus::Closed : DoorStatus::Open;
        lastEventMs_ = nowMs;
        return;
    }

    if (isClosed && status_ != DoorStatus::Closed) {
        transitionTo(DoorStatus::Closed, nowMs);
    } else if (!isClosed && status_ == DoorStatus::Closed) {
        transitionTo(DoorStatus::Opening, nowMs);
    }
}

void DoorController::startRelayClicks(uint8_t clicks, uint32_t nowMs) {
    if (clicks == 0) return;
    clicksRemaining_ = clicks;
    relayActive_ = true;
    relayPauseTimer_.stop();
    hw_.setRelay(true);
    relayOnTimer_.start(nowMs, cfg_.relayTimeMs);
}

void DoorController::transitionTo(DoorStatus next, uint32_t nowMs) {
    DoorStatus prev = status_;
    if (prev == next) return;
    status_ = next;
    lastEventMs_ = nowMs;

    switch (next) {
        case DoorStatus::Opening:
        case DoorStatus::Closing:
            motionTimer_.start(nowMs, cfg_.motionTimeMs);
            break;
        case DoorStatus::Closed:
        case DoorStatus::Open:
        case DoorStatus::Stopped:
            motionTimer_.stop();
            break;
        default:
            break;
    }

    if (statusCb_) {
        statusCb_(prev, next, statusCtx_);
    }
}

bool DoorController::executeCommand(DoorCommand cmd, uint32_t nowMs) {
    if (cmd == DoorCommand::None) return false;

    // Resolve Toggle into concrete Open / Close / Stop
    if (cmd == DoorCommand::Toggle) {
        switch (status_) {
            case DoorStatus::Closed:  cmd = DoorCommand::Open;  break;
            case DoorStatus::Open:
            case DoorStatus::Stopped: cmd = DoorCommand::Close; break;
            case DoorStatus::Opening:
            case DoorStatus::Closing: cmd = DoorCommand::Stop;  break;
            default:                  cmd = DoorCommand::Open;  break;
        }
    }

    // Prevent overlapping click sequences
    if (isClickSequenceBusy()) {
        return false;
    }

    uint8_t clicks = 0;
    DoorStatus nextStatus = DoorStatus::Unknown;

    switch (cmd) {
        case DoorCommand::Open:
            switch (status_) {
                case DoorStatus::Open:
                case DoorStatus::Opening:
                    return true; // Already open or opening
                case DoorStatus::Closing:
                    clicks = 1;
                    nextStatus = DoorStatus::Opening;
                    break;
                case DoorStatus::Closed:
                    // Trigger opener; wait for sensor to confirm door actually moves off reflector
                    clicks = 1;
                    break;
                case DoorStatus::Stopped:
                    // Single-button opener typically closes on 1st click after stop, reverses to open on 2nd
                    clicks = 2;
                    nextStatus = DoorStatus::Opening;
                    break;
                default:
                    clicks = 1;
                    nextStatus = DoorStatus::Opening;
                    break;
            }
            break;

        case DoorCommand::Close:
            switch (status_) {
                case DoorStatus::Closed:
                case DoorStatus::Closing:
                    return true; // Already closed or closing
                case DoorStatus::Open:
                case DoorStatus::Stopped:
                    clicks = 1;
                    nextStatus = DoorStatus::Closing;
                    break;
                case DoorStatus::Opening:
                    // 1st click stops opening, 2nd click starts closing
                    clicks = 2;
                    nextStatus = DoorStatus::Closing;
                    break;
                default:
                    clicks = 1;
                    nextStatus = DoorStatus::Closing;
                    break;
            }
            break;

        case DoorCommand::Stop:
            switch (status_) {
                case DoorStatus::Stopped:
                case DoorStatus::Closed:
                case DoorStatus::Open:
                    return true; // Not moving
                case DoorStatus::Opening:
                    clicks = 1;
                    nextStatus = DoorStatus::Stopped;
                    break;
                case DoorStatus::Closing:
                    // 1st click reverses (safety), 2nd click stops
                    clicks = 2;
                    nextStatus = DoorStatus::Stopped;
                    break;
                default:
                    return true;
            }
            break;

        default:
            return false;
    }

    startRelayClicks(clicks, nowMs);
    if (nextStatus != DoorStatus::Unknown) {
        transitionTo(nextStatus, nowMs);
    }
    return true;
}

} // namespace garadget
