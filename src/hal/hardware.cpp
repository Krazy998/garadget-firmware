#include "hardware.h"

#ifdef PARTICLE
#include "Particle.h"

namespace garadget {

bool EepromStorage::read(void* dst, size_t len) {
    if (!dst || len == 0 || len > EEPROM.length()) return false;
    uint8_t* out = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < len; ++i) {
        out[i] = EEPROM.read(static_cast<int>(i));
    }
    return true;
}

bool EepromStorage::write(const void* src, size_t len) {
    if (!src || len == 0 || len > EEPROM.length()) return false;
    const uint8_t* in = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < len; ++i) {
        EEPROM.write(static_cast<int>(i), in[i]);
    }
    return true;
}

void P1DoorHardware::init(SensorMode initialMode) {
    // Safety first: ensure relay output is LOW before enabling output driver
    pinMode(kPinRelay, OUTPUT);
    digitalWrite(kPinRelay, LOW);

    pinMode(kPinLaser, OUTPUT);
    digitalWrite(kPinLaser, LOW);

    initialized_ = true;
    configureSensorMode(initialMode);
}

void P1DoorHardware::configureSensorMode(SensorMode mode) {
    activeMode_ = mode;
    if (!initialized_) return;

    if (mode == SensorMode::Laser) {
        digitalWrite(kPinLaser, LOW);
        pinMode(kPinLaser, OUTPUT);
    } else {
        digitalWrite(kPinLaser, LOW);
        pinMode(kPinSwitchSensor, INPUT_PULLUP);
    }
}

void P1DoorHardware::setRelay(bool active) {
    digitalWrite(kPinRelay, active ? HIGH : LOW);
}

SensorReading P1DoorHardware::readSensor(SensorMode mode, uint8_t samples) {
    if (mode != activeMode_) {
        configureSensorMode(mode);
    }

    SensorReading reading{};
    if (mode == SensorMode::SwitchNO) {
        int val = digitalRead(kPinSwitchSensor);
        reading.reflectionPct = (val == LOW) ? 100 : 0;
        reading.rawBase = static_cast<uint16_t>(analogRead(kPinPhoto));
        reading.brightnessPct = DoorController::computeAmbientBrightness(reading.rawBase);
        return reading;
    }

    if (mode == SensorMode::SwitchNC) {
        int val = digitalRead(kPinSwitchSensor);
        reading.reflectionPct = (val == HIGH) ? 100 : 0;
        reading.rawBase = static_cast<uint16_t>(analogRead(kPinPhoto));
        reading.brightnessPct = DoorController::computeAmbientBrightness(reading.rawBase);
        return reading;
    }

    // Laser optical reflection scan
    if (samples < kMinSensorReads) samples = kMinSensorReads;
    if (samples > kMaxSensorReads) samples = kMaxSensorReads;

    int32_t baseSamples[kMaxSensorReads];
    int32_t litSamples[kMaxSensorReads];
    int32_t lastBase = 0;

    for (uint8_t i = 0; i < samples; ++i) {
        lastBase = analogRead(kPinPhoto);
        baseSamples[i] = lastBase;
        digitalWriteFast(kPinLaser, HIGH);
        delay(1);
        litSamples[i] = analogRead(kPinPhoto);
        digitalWriteFast(kPinLaser, LOW);
        delay(1);
    }

    reading.rawBase = static_cast<uint16_t>(lastBase < 0 ? 0 : (lastBase > 4095 ? 4095 : lastBase));
    reading.reflectionPct = DoorController::computeLaserReflection(baseSamples, litSamples, samples, kSensorBias);
    reading.brightnessPct = DoorController::computeAmbientBrightness(reading.rawBase);
    return reading;
}

} // namespace garadget
#endif
