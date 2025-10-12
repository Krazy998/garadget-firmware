#pragma once

#include "Particle.h"
#include "global.h"
#include "utils/timezones.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace garadget {

struct DoorConfiguration {
  std::uint8_t versionMajor = constants::kFirmwareMajor;
  std::uint8_t versionMinor = constants::kFirmwareMinor;
  std::uint16_t readTimeMs = 1000;
  std::uint16_t motionTimeMs = 10000;
  std::uint16_t relayTimeMs = 300;
  std::uint16_t relayPauseMs = 1000;
  std::uint8_t sensorReads = constants::kSensorReadsPerSample;
  std::uint8_t sensorThreshold = 10;
  std::uint16_t alertEvents = 0;
  std::uint16_t alertOpenTimeoutMin = 0;
  std::uint16_t alertNightStartMin = 0;
  std::uint16_t alertNightEndMin = 0;
  std::array<char, 25> timeZone{};
  std::array<char, constants::kMaxNameSize + 1> deviceName{};
  std::uint8_t protocols = 1;
  std::array<std::uint8_t, 4> mqttBrokerIp{};
  std::uint16_t mqttBrokerPort = 1883;
  std::array<char, 32> mqttBrokerUser{};
  std::array<char, 32> mqttBrokerPass{};
  std::uint16_t mqttTimeoutMs = 5000;
};

struct DeviceState {
  DoorStatus doorStatus = DoorStatus::Unknown;
  std::uint32_t lastEventMs = 0;
  std::uint8_t sensorReflection = 0;
  std::uint16_t sensorBase = 0;
};

class Config final {
 public:
  static Config& instance();

  bool init();
  void process();
  void save();

  int8_t parse(const String& config);
  int8_t setValue(const String& param, const String& value);
  void setName(const String& name);

  void getJsonConfig(char* buffer, std::size_t length) const;
  void getJsonStatus(char* buffer, std::size_t length) const;

  static const char* statusToString(DoorStatus status);
  static DoorStatus statusFromString(const String& status);

  static void formatTime(uint32_t millis, char* buffer, std::size_t length);
  void timeInStatus(char* buffer, std::size_t length) const;
  static String escapeJson(const String& value);

  DoorConfiguration config{};
  DeviceState state{};

 private:
  Config() = default;

  int8_t validate();
  void reset();
  void publishUpdate();
  void updateTimezone();

  bool updated_ = false;
  c_timezones timezones_;
};

}  // namespace garadget

