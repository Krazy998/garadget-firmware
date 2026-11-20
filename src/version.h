#pragma once

#include <cstdint>

#ifdef PARTICLE
#include "Particle.h"
#endif

namespace garadget {

inline constexpr uint8_t kFirmwareMajor = 3;
inline constexpr uint8_t kFirmwareMinor = 2;
inline constexpr uint8_t kFirmwarePatch = 2;
inline constexpr const char* kFirmwareVersion = "3.2.2";
inline constexpr const char* kModelName = "Garadget P1";
inline constexpr const char* kManufacturer = "Garadget";

// Hardware pin mapping for Garadget (Particle P1 / STM32F205)
// Note: PIN_SENSOR shares D0 with I2C SDA; only configured as INPUT_PULLUP when dry-contact switch mode is active.
#ifdef PARTICLE
inline constexpr uint16_t kPinSwitchSensor = D0;
inline constexpr uint16_t kPinLaser        = D2;
inline constexpr uint16_t kPinRelay        = D3;
inline constexpr uint16_t kPinPhoto        = A0;
#endif

// Wi-Fi antenna selection (ANT_AUTO, ANT_INTERNAL, or ANT_EXTERNAL)
#ifndef GARADGET_ANT_MODE
#define GARADGET_ANT_MODE ANT_AUTO
#endif

// Default sensor bias subtracted from raw 12-bit ADC baseline to improve contrast in bright environments
inline constexpr int32_t kSensorBias = 100;

// Safety bounds
inline constexpr uint16_t kMinReadIntervalMs   = 200;
inline constexpr uint16_t kMaxReadIntervalMs   = 60000;
inline constexpr uint32_t kMinMotionTimeMs     = 1000;
inline constexpr uint32_t kMaxMotionTimeMs     = 120000;
inline constexpr uint16_t kMinRelayOnMs        = 10;
inline constexpr uint16_t kMaxRelayOnMs        = 2000;
inline constexpr uint16_t kMinRelayPauseMs     = 10;
inline constexpr uint16_t kMaxRelayPauseMs     = 5000;
inline constexpr uint8_t  kMinSensorReads      = 1;
inline constexpr uint8_t  kMaxSensorReads      = 20;
inline constexpr uint8_t  kMinSensorThreshold  = 1;
inline constexpr uint8_t  kMaxSensorThreshold  = 80;
inline constexpr uint16_t kMaxAlertBitmap      = 0x01FF; // 9 bits (0..8)
inline constexpr uint16_t kMaxAlertTimeoutSec  = 43200;  // 12 hours
inline constexpr uint16_t kMinutesPerDay       = 1440;
inline constexpr uint16_t kMinMqttKeepAliveSec = 5;
inline constexpr uint16_t kMaxMqttKeepAliveSec = 300;

// String field capacities (including NUL terminator)
inline constexpr size_t kDeviceNameCap   = 32;
inline constexpr size_t kTimeZoneCap     = 32;
inline constexpr size_t kMqttHostCap     = 64;
inline constexpr size_t kMqttUserCap     = 32;
inline constexpr size_t kMqttPassCap     = 48;
inline constexpr size_t kHaPrefixCap     = 32;
inline constexpr size_t kNtpServerCap    = 64;
inline constexpr size_t kAdminPassCap    = 32;
inline constexpr size_t kMinAdminPassLen = 4;

} // namespace garadget
