#include "node-sensor.h"

namespace garadget {

SensorNode& SensorNode::instance() {
  static SensorNode instance;
  return instance;
}

bool SensorNode::init() {
  auto& config = getConfig();
  status_ = &config.state.doorStatus;
  reflection_ = &config.state.sensorReflection;
  base_ = &config.state.sensorBase;
  threshold_ = &config.config.sensorThreshold;
  scanTimer_.setDuration(&config.config.readTimeMs);
  motionTimer_.setDuration(&config.config.motionTimeMs);
  *status_ = DoorStatus::Open;

#if defined(SWITCH_NO) || defined(SWITCH_NC)
  pinMode(constants::kSensorPin, INPUT_PULLUP);
#else
  pinMode(constants::kLaserPin, OUTPUT);
  digitalWrite(constants::kLaserPin, LOW);
#endif

  getState();
  enabled_ = true;
  return true;
}

void SensorNode::process() {
  if (!scanTimer_.isRunning()) {
    getState();
    scanTimer_.start();
  }
  if (motionTimer_.isTimeout()) {
    onMotionEnd();
  }
}

std::uint8_t SensorNode::read() {
#if defined(SWITCH_NO)
  *reflection_ = digitalRead(constants::kSensorPin) ? 0 : 100;
#elif defined(SWITCH_NC)
  *reflection_ = digitalRead(constants::kSensorPin) ? 100 : 0;
#else
  std::uint32_t sumBase = 0;
  std::uint32_t sumScan = 0;

  for (std::uint8_t count = constants::kSensorReadsPerSample; count > 0; --count) {
    *base_ = analogRead(constants::kPhotoResistorPin);
    sumBase += *base_ - constants::kSensorBias;
    digitalWriteFast(constants::kLaserPin, HIGH);
    delay(1);
    sumScan += *base_ - analogRead(constants::kPhotoResistorPin);
    digitalWriteFast(constants::kLaserPin, LOW);
    delay(1);
  }

  if (sumBase != 0 && sumScan <= sumBase) {
    *reflection_ = sumScan * 100 / sumBase;
  }
#endif
  return *reflection_;
}

bool SensorNode::isTripping() { return read() > *threshold_; }

std::uint16_t SensorNode::baseValue() const { return *base_; }

DoorStatus SensorNode::getState() {
  if constexpr (build::kUseVirtualDoor) {
    return *status_ == DoorStatus::Closed ? DoorStatus::Closed : DoorStatus::Open;
  }

  const bool closed = isTripping();
  if (closed && *status_ != DoorStatus::Closed) {
    onChange(DoorStatus::Closed);
  } else if (!closed && *status_ == DoorStatus::Closed) {
    onChange(DoorStatus::Opening);
  }
  return *status_;
}

void SensorNode::onMotionEnd() {
  switch (*status_) {
    case DoorStatus::Opening:
      onChange(DoorStatus::Open);
      break;
    case DoorStatus::Closing:
      if constexpr (build::kUseVirtualDoor) {
        onChange(DoorStatus::Closed);
      } else if (getState() != DoorStatus::Closed) {
        onChange(DoorStatus::Stopped);
      }
      break;
    default:
      break;
  }
}

bool SensorNode::onChange(DoorStatus newStatus) {
  Log.info("Sensor - new status: %s", Config::statusToString(newStatus));
  *status_ = newStatus;
  getConfig().state.lastEventMs = millis();
  const Message statusMessage{"sensor", MessageType::Status, status_};
  return handleMessage(statusMessage);
}

bool SensorNode::receive(const Message& message) {
  if (message.type != MessageType::Status) {
    return true;
  }

  switch (*status_) {
    case DoorStatus::Opening:
    case DoorStatus::Closing:
      getConfig().state.lastEventMs = millis();
      motionTimer_.start();
      break;
    case DoorStatus::Closed:
    case DoorStatus::Open:
    case DoorStatus::Stopped:
      motionTimer_.stop();
      break;
    default:
      break;
  }
  return true;
}

}  // namespace garadget

