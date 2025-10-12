#include "config.h"

#include "global.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace garadget {

namespace {
constexpr std::size_t kTimeBufferSize = 10;
constexpr std::size_t kTimezoneBufferSize = 25;
constexpr char kConfigSource[] = "config";
}

Config& Config::instance() {
  static Config instance;
  return instance;
}

bool Config::init() {
  state.lastEventMs = millis();

  EEPROM.get(0, config);
  if (validate() < 0) {
    reset();
    return false;
  }

  const auto savedVersionId = static_cast<std::uint16_t>(
      config.versionMajor * 100 + config.versionMinor);
  if (savedVersionId != constants::kFirmwareId) {
    if (savedVersionId < constants::kFirmwareCompatId ||
        savedVersionId > constants::kFirmwareId) {
      reset();
      return false;
    }
    config.versionMajor = constants::kFirmwareMajor;
    config.versionMinor = constants::kFirmwareMinor;
    save();
  }

  updateTimezone();
  return true;
}

void Config::process() {
  if (!updated_) {
    return;
  }
  publishUpdate();
  updated_ = false;
}

void Config::save() {
  EEPROM.put(0, config);
  updated_ = true;
}

int8_t Config::parse(const String& incomingConfig) {
  if (incomingConfig.isEmpty()) {
    return -1;
  }

  String working = incomingConfig;
  int updates = 0;
  if (working.equalsIgnoreCase("defaults")) {
    Log.info("Config - loading defaults");
    config.versionMajor = constants::kFirmwareMajor;
    config.versionMinor = constants::kFirmwareMinor;
    working = String(constants::kDefaultConfig);
    updates = 5;  // parity with legacy firmware
  }

  int start = 0;
  while (start >= 0 && start < working.length()) {
    const int equals = working.indexOf('=', start);
    if (equals < 0) {
      break;
    }

    const int delimiter = working.indexOf('|', equals + 1);
    const String key = working.substring(start, equals);
    const String value =
        delimiter < 0 ? working.substring(equals + 1)
                      : working.substring(equals + 1, delimiter);

    start = delimiter < 0 ? working.length() : delimiter + 1;

    const int8_t result = setValue(key, value);
    if (result < 0) {
      continue;
    }
    if (result > 0) {
      ++updates;
    }
  }

  if (updates > 0) {
    Log.info("Config - parameters updated: %d", updates);
    save();
  }

  return updates;
}

int8_t Config::setValue(const String& param, const String& value) {
  int numericValue = value.toInt();

  if (param.equals("ver")) {
    return 0;
  }

  if (param.equals("nme")) {
    if (value.equals(config.deviceName.data())) {
      return 0;
    }
    setName(value);
    return 1;
  }

  if (param.equals("rdt")) {
    if (numericValue < 200 || numericValue > 60000) {
      return -1;
    }
    if (numericValue == config.readTimeMs) {
      return 0;
    }
    config.readTimeMs = numericValue;
    return 1;
  }

  if (param.equals("mtt")) {
    if (numericValue < 5000 || numericValue > 60000) {
      return -1;
    }
    if (numericValue == config.motionTimeMs) {
      return 0;
    }
    config.motionTimeMs = numericValue;
    return 1;
  }

  if (param.equals("rlt")) {
    if (numericValue < 10 || numericValue > 2000) {
      return -1;
    }
    if (numericValue == config.relayTimeMs) {
      return 0;
    }
    config.relayTimeMs = numericValue;
    return 1;
  }

  if (param.equals("rlp")) {
    if (numericValue < 10 || numericValue > 5000) {
      return -1;
    }
    if (numericValue == config.relayPauseMs) {
      return 0;
    }
    config.relayPauseMs = numericValue;
    return 1;
  }

  if (param.equals("srt")) {
    if (numericValue < 1 || numericValue > 80) {
      return -1;
    }
    if (numericValue == config.sensorThreshold) {
      return 0;
    }
    config.sensorThreshold = numericValue;
    return 1;
  }

  if (param.equals("srr")) {
    if (numericValue < 1 || numericValue > 10) {
      return -1;
    }
    if (numericValue == config.sensorReads) {
      return 0;
    }
    config.sensorReads = numericValue;
    return 1;
  }

  if (param.equals("aev")) {
    if (numericValue > 0b111111111) {
      return -1;
    }
    if (numericValue == config.alertEvents) {
      return 0;
    }
    config.alertEvents = numericValue;
    return 1;
  }

  if (param.equals("aot")) {
    if (numericValue > 43200) {
      return -1;
    }
    if (numericValue == config.alertOpenTimeoutMin) {
      return 0;
    }
    config.alertOpenTimeoutMin = numericValue;
    return 1;
  }

  if (param.equals("ans")) {
    if (numericValue < 0 || numericValue >= 1440) {
      return -1;
    }
    if (numericValue == config.alertNightStartMin) {
      return 0;
    }
    config.alertNightStartMin = numericValue;
    return 1;
  }

  if (param.equals("ane")) {
    if (numericValue < 0 || numericValue >= 1440) {
      return -1;
    }
    if (numericValue == config.alertNightEndMin) {
      return 0;
    }
    config.alertNightEndMin = numericValue;
    return 1;
  }

  if (param.equals("tzo")) {
    if (!timezones_.f_setConfig(value)) {
      return -1;
    }

    if (value.equals(config.timeZone.data())) {
      return 0;
    }
    Log.info("Config - updated Timezone, time now: %s", Time.timeStr().c_str());
    value.toCharArray(config.timeZone.data(), kTimezoneBufferSize);
    updateTimezone();
    return 1;
  }

  if (param.equals("mqtt")) {
    if (numericValue > 0b011) {
      return -1;
    }
    if (numericValue == config.protocols) {
      return 0;
    }
    config.protocols = numericValue;
    return 1;
  }

  if (param.equals("mqip")) {
    std::uint8_t start = 0;
    std::uint8_t updated = 0;
    for (std::uint8_t index = 0; index < 4; ++index) {
      const auto end = value.indexOf('.', start);
      const int ipPart = value.substring(start, end).toInt();
      if (!value.length() || ipPart < 0 || ipPart > 0xFF) {
        return -1;
      }
      if (ipPart != config.mqttBrokerIp[index]) {
        updated = 1;
      }
      config.mqttBrokerIp[index] = static_cast<std::uint8_t>(ipPart);
      start = end + 1;
    }
    return updated;
  }

  if (param.equals("mqpt")) {
    if (numericValue <= 0 || numericValue > 0xFFFF) {
      return -1;
    }
    if (numericValue == config.mqttBrokerPort) {
      return 0;
    }
    config.mqttBrokerPort = numericValue;
    return 1;
  }

  if (param.equals("mqus")) {
    if (value.equals(config.mqttBrokerUser.data())) {
      return 0;
    }
    const String sanitized = Config::escapeJson(value);
    sanitized.toCharArray(config.mqttBrokerUser.data(),
                          config.mqttBrokerUser.size());
    return 1;
  }

  if (param.equals("mqpw")) {
    if (value.equals(config.mqttBrokerPass.data())) {
      return 0;
    }
    const String sanitized = Config::escapeJson(value);
    sanitized.toCharArray(config.mqttBrokerPass.data(),
                          config.mqttBrokerPass.size());
    return 1;
  }

  if (param.equals("mqto")) {
    if (numericValue < 0 || numericValue > 0xFFFF) {
      return -1;
    }
    if (numericValue == config.mqttTimeoutMs) {
      return 0;
    }
    config.mqttTimeoutMs = numericValue;
    return 1;
  }

  return -1;
}

void Config::getJsonConfig(char* buffer, std::size_t length) const {
  WiFiAccessPoint accessPoints[1];
  const int accessPointCount = WiFi.getCredentials(accessPoints, 1);

  const char* ssid = accessPointCount > 0 ? accessPoints[0].ssid : "";

  snprintf(
      buffer, length,
      "{\"sys\":\"%s\",\"ver\":\"%u.%u\",\"id\":\"%s\",\"ssid\":\"%s\",\"rdt\":%u,\"mtt\":%u,\"rlt\":%u,\"rlp\":%u,\"srt\":%u,\"nme\":\"%s\",\"mqtt\":%u,\"mqip\":\"%u.%u.%u.%u\",\"mqpt\":%u,\"mqus\":\"%s\",\"mqto\":%u}",
      System.version().c_str(), config.versionMajor, config.versionMinor,
      System.deviceID().c_str(), ssid, config.readTimeMs, config.motionTimeMs,
      config.relayTimeMs, config.relayPauseMs, config.sensorThreshold,
      Config::escapeJson(String(config.deviceName.data())).c_str(),
      config.protocols, config.mqttBrokerIp[0], config.mqttBrokerIp[1],
      config.mqttBrokerIp[2], config.mqttBrokerIp[3], config.mqttBrokerPort,
      Config::escapeJson(String(config.mqttBrokerUser.data())).c_str(),
      config.mqttTimeoutMs);
}

void Config::getJsonStatus(char* buffer, std::size_t length) const {
  char timeBuffer[kTimeBufferSize] = {0};
  timeInStatus(timeBuffer, sizeof(timeBuffer));

  snprintf(buffer, length,
           "{\"status\":\"%s\",\"time\":\"%s\",\"sensor\":%u,\"bright\":%u,\"signal\":%d}",
           Config::statusToString(state.doorStatus), timeBuffer,
           state.sensorReflection, 99 - state.sensorBase * 100 / 4096,
           static_cast<int>(WiFi.RSSI()));
}

void Config::setName(const String& name) {
  String sanitized = Config::escapeJson(name);
  sanitized.replace('_', ' ');
  sanitized.toCharArray(config.deviceName.data(), config.deviceName.size());
  Log.info("Config - name changed to %s", sanitized.c_str());
}

const char* Config::statusToString(DoorStatus status) {
  switch (status) {
    case DoorStatus::Closed:
      return "closed";
    case DoorStatus::Open:
      return "open";
    case DoorStatus::Closing:
      return "closing";
    case DoorStatus::Opening:
      return "opening";
    case DoorStatus::Stopped:
      return "stopped";
    case DoorStatus::Init:
      return "initialized";
    case DoorStatus::Unknown:
    case DoorStatus::None:
    default:
      return "unknown";
  }
}

DoorStatus Config::statusFromString(const String& status) {
  if (status.startsWith("close")) {
    return DoorStatus::Closed;
  }
  if (status.equals("open")) {
    return DoorStatus::Open;
  }
  if (status.equals("closing")) {
    return DoorStatus::Closing;
  }
  if (status.equals("opening")) {
    return DoorStatus::Opening;
  }
  if (status.startsWith("stop")) {
    return DoorStatus::Stopped;
  }
  return DoorStatus::Unknown;
}

void Config::formatTime(uint32_t timeMs, char* buffer, std::size_t length) {
  if (!buffer || length == 0) {
    return;
  }

  uint32_t value = timeMs / 1000;
  char unit = 's';
  if (value >= 120) {
    unit = 'm';
    value /= 60;
    if (value >= 120) {
      unit = 'h';
      value /= 60;
      if (value >= 48) {
        unit = 'd';
        value /= 24;
      }
    }
  }

  snprintf(buffer, length, "%lu%c", static_cast<unsigned long>(value), unit);
}

void Config::timeInStatus(char* buffer, std::size_t length) const {
  Config::formatTime(millis() - state.lastEventMs, buffer, length);
}

String Config::escapeJson(const String& value) {
  String result;
  result.reserve(value.length());
  for (uint8_t index = 0; index < value.length(); ++index) {
    const char character = value.charAt(index);
    if (character < 0x20 || character > 0x7E) {
      continue;
    }
    result.concat(character);
  }
  result.replace("\\", "\\\\");
  result.replace("\"", "\\\"");
  return result;
}

int8_t Config::validate() {
  return static_cast<int8_t>(
      (setValue("rdt", String(config.readTimeMs)) != -1) &&
      (setValue("mtt", String(config.motionTimeMs)) != -1) &&
      (setValue("rlt", String(config.relayTimeMs)) != -1) &&
      (setValue("rlp", String(config.relayPauseMs)) != -1) &&
      (setValue("srt", String(config.sensorThreshold)) != -1) &&
      (setValue("srr", String(config.sensorReads)) != -1) &&
      (setValue("aev", String(config.alertEvents)) != -1) &&
      (setValue("aot", String(config.alertOpenTimeoutMin)) != -1) &&
      (setValue("ans", String(config.alertNightStartMin)) != -1) &&
      (setValue("ane", String(config.alertNightEndMin)) != -1) &&
      (setValue("tzo", String(config.timeZone.data())) != -1) &&
      (setValue("mqtt", String(config.protocols)) != -1) &&
      (setValue("mqpt", String(config.mqttBrokerPort)) != -1) &&
      (setValue("mqto", String(config.mqttTimeoutMs)) != -1));
}

void Config::reset() {
  parse(String("defaults"));
}

void Config::publishUpdate() {
  const Message message{kConfigSource, MessageType::Config, nullptr};
  handleMessage(message);
}

void Config::updateTimezone() {
  timezones_.f_setConfig(String(config.timeZone.data()));
}

}  // namespace garadget

