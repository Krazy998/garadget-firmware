#include "port.h"

#include <cstring>

namespace garadget {

std::uint8_t PortNode::deviceCount = 0;

const char* PortNode::source() { return "port"; }

bool PortNode::init() {
  Log.info("%s - scanning for connected devices", source());
  savedSdaMode_ = getPinMode(SDA);
  savedSclMode_ = getPinMode(SCL);
  savedSdaLevel_ = digitalRead(SDA);
  savedSclLevel_ = digitalRead(SCL);

  Wire.begin();
  std::array<std::uint8_t, constants::kPortBufferSize> buffer{};
  if (busRequest(buffer.data()) > 0) {
    enabled_ = true;
    scanTimer_.setDuration(scanPeriodMs_);
    ++deviceCount;
    Log.info("%s - device connected", source());
  }

  if (deviceCount == 0) {
    close();
  }

  return enabled_;
}

void PortNode::process() {
  if (!enabled_ || !input_ || scanTimer_.isRunning()) {
    return;
  }

  std::array<std::uint8_t, constants::kPortBufferSize> buffer{};
  const std::uint8_t length =
      busRequest(buffer.data(), constants::kPortBufferSize);

  if (length == lastResponseLength_ &&
      std::memcmp(lastResponse_.data(), buffer.data(), length) == 0) {
    return;
  }

  std::memcpy(lastResponse_.data(), buffer.data(), length);
  lastResponseLength_ = length;

  const Message message{source(), MessageType::Port, lastResponse_.data()};
  handleMessage(message);
  scanTimer_.start();
}

int8_t PortNode::busSend(std::uint8_t* buffer, std::uint8_t length) {
  Wire.beginTransmission(address_);
  Wire.write(buffer, length);
  Wire.endTransmission();
  return length;
}

int8_t PortNode::busRequest(std::uint8_t* buffer, std::uint8_t maxLength) {
  Log.info("%s - requesting data from %u", source(), address_);
  Wire.requestFrom(address_, maxLength);
  const std::uint32_t timeout = millis() + constants::kPortTimeoutMs;
  while (!Wire.available()) {
    if (millis() > timeout) {
      Log.info("%s - no response from %u", source(), address_);
      return -1;
    }
  }
  Log.info("%s - received response from %u", source(), address_);
  std::uint8_t length = 0;
  for (; Wire.available() && length < maxLength; ++length) {
    buffer[length] = Wire.read();
  }
  return length;
}

void PortNode::close() {
  Wire.end();
  pinMode(SDA, savedSdaMode_);
  digitalWrite(SDA, savedSdaLevel_);
  pinMode(SCL, savedSclMode_);
  digitalWrite(SCL, savedSclLevel_);
}

}  // namespace garadget

