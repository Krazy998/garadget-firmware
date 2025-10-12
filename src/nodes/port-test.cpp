#include "port-test.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace garadget {

namespace {
constexpr std::uint8_t kTestAddress = 0x11;
constexpr std::uint8_t kDeviceIdSize = 25;
}

const char* TestPort::source() const { return "test"; }

bool TestPort::init() {
  address_ = kTestAddress;
  if (!PortNode::init()) {
    return false;
  }

  sensor_ = &SensorNode::instance();

  Particle.disconnect();
  RGB.control(true);
  sendInfo();
  sendId();

  for (int loop = 0;; ++loop) {
    nextColor();
    if (loop % 2 == 1) {
      toggleRelay();
    }
    sendState();

    if (System.buttonPushed() > 500) {
      RGB.control(false);
      sendState();
      Particle.connect();
      WiFi.listen();
#if defined(SWITCH_NO) || defined(SWITCH_NC)
      close();
      return true;
#endif
      break;
    }
    delay(200);
  }

  for (int loop = 0; loop < 10; ++loop) {
    sendState();
    delay(1000);
  }
  return true;
}

void TestPort::sendInfo() {
  struct DeviceInfo {
    char type;
    std::uint32_t systemVersion;
    std::uint8_t firmwareMajor;
    std::uint8_t firmwareMinor;
  } __attribute__((packed));

  DeviceInfo info{'V', System.versionNumber(), constants::kFirmwareMajor,
                  constants::kFirmwareMinor};
  busSend(reinterpret_cast<std::uint8_t*>(&info), sizeof(info));
}

void TestPort::sendId() {
  const String deviceId = "I" + System.deviceID().toUpperCase();
  std::array<std::uint8_t, kDeviceIdSize> buffer{};
  std::memcpy(buffer.data(), deviceId.c_str(),
              std::min(deviceId.length(), buffer.size()));
  busSend(buffer.data(), buffer.size());
}

void TestPort::sendState() {
  struct DeviceState {
    char type;
    std::uint8_t sensorRate;
    std::uint16_t sensorBase;
    std::uint16_t buttonPushed;
    int8_t radioSignal;
    std::uint8_t ledStep;
    std::uint8_t relay;
    std::uint8_t padding;
  } __attribute__((packed));

  DeviceState state{};
  state.type = 'S';
#if defined(SWITCH_NO) || defined(SWITCH_NC)
  state.sensorRate = 100;
  state.sensorBase = analogRead(constants::kPhotoResistorPin);
#else
  state.sensorRate = sensor_->read();
  state.sensorBase = sensor_->baseValue();
#endif
  state.radioSignal = WiFi.RSSI();
  state.buttonPushed = System.buttonPushed();
  state.ledStep = ledStep_;
  state.relay = relayState_ ? 1 : 0;
  state.padding = 0;

  busSend(reinterpret_cast<std::uint8_t*>(&state), sizeof(state));
}

void TestPort::nextColor() {
  ledStep_ = (ledStep_ + 1) % 3;
  RGB.color(ledStep_ == 0 ? 0xFF : 0x00, ledStep_ == 1 ? 0xFF : 0x00,
            ledStep_ == 2 ? 0xFF : 0x00);
}

void TestPort::toggleRelay() {
  relayState_ = !relayState_;
  digitalWrite(constants::kRelayPin, relayState_);
  delay(20);
}

}  // namespace garadget

