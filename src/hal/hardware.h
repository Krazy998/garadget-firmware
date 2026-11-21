#pragma once

#include <cstdint>

#include "../core/config.h"
#include "../core/door_controller.h"

namespace garadget {

class EepromStorage : public ConfigStorage {
public:
    bool read(void* dst, size_t len) override;
    bool write(const void* src, size_t len) override;
};

class P1DoorHardware : public DoorHardware {
public:
    void init(SensorMode initialMode);
    void configureSensorMode(SensorMode mode);
    void setMaxRelayPulseMs(uint32_t ms);

    void setRelay(bool active) override;
    SensorReading readSensor(SensorMode mode, uint8_t samples) override;

private:
    SensorMode activeMode_ = SensorMode::Laser;
    bool initialized_ = false;
    uint32_t maxRelayPulseMs_ = 350;
};

} // namespace garadget
