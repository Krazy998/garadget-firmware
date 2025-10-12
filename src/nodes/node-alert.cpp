#include "node-alert.h"

#include <array>
#include <cstdio>

namespace garadget {

bool AlertNode::init() {
  auto& config = getConfig();
  status_ = &config.state.doorStatus;
  events_ = &config.config.alertEvents;
  timeoutMinutes_ = &config.config.alertOpenTimeoutMin;
  nightStartMinutes_ = &config.config.alertNightStartMin;
  nightEndMinutes_ = &config.config.alertNightEndMin;

  handleStatus();
  enabled_ = true;
  return true;
}

void AlertNode::process() {
  if (*status_ == DoorStatus::Closed) {
    return;
  }

  if (!timeoutAlertsFired_ && timeoutTimer_.isTimeout()) {
    timeoutAlert();
  }

  if (!nightAlertsFired_ && isNight()) {
    nightAlert();
  }
}

bool AlertNode::receive(const Message& message) {
  if (message.type != MessageType::Status) {
    return true;
  }
  handleStatus();
  return true;
}

bool AlertNode::isNight() const {
  if (!Time.isValid() || *nightStartMinutes_ == *nightEndMinutes_) {
    return false;
  }

  const std::uint16_t nowMinutes =
      static_cast<std::uint16_t>(Time.hour() * 60 + Time.minute());

  if (*nightStartMinutes_ > *nightEndMinutes_ &&
      nowMinutes < *nightStartMinutes_ && nowMinutes > *nightEndMinutes_) {
    return false;
  }

  if (*nightStartMinutes_ < *nightEndMinutes_ &&
      (nowMinutes < *nightStartMinutes_ || nowMinutes > *nightEndMinutes_)) {
    return false;
  }

  return true;
}

void AlertNode::fireAlert(const char* type, const char* data) {
  Log.info("Alert - type: %s, data: %s", type, data);

  std::array<char, constants::kMaxVariableSize + 128> payload{};
  std::snprintf(payload.data(), payload.size(),
                "{\"name\": \"%s\", \"type\": \"%s\", \"data\": \"%s\"}",
                getConfig().config.deviceName.data(), type, data);

  const Message message{"alert", MessageType::Alert, payload.data()};
  handleMessage(message);
}

void AlertNode::statusAlert() {
  fireAlert("state", Config::statusToString(*status_));
}

void AlertNode::timeoutAlert() {
  char buffer[10];
  Config::formatTime(millis() - openedAtMs_, buffer, sizeof(buffer));
  fireAlert("timeout", buffer);
  timeoutAlertsFired_ = 1;
}

void AlertNode::nightAlert() {
  char buffer[12];
  std::snprintf(buffer, sizeof(buffer), "%u-%u", *nightStartMinutes_,
                *nightEndMinutes_);
  fireAlert("night", buffer);
  nightAlertsFired_ = 1;
}

void AlertNode::handleStatus() {
  const auto statusIndex = static_cast<std::uint8_t>(*status_);
  if (statusIndex > 0 && (*events_ & (0x01 << (statusIndex - 1)))) {
    statusAlert();
  }

  switch (*status_) {
    case DoorStatus::Opening:
    case DoorStatus::Closing:
    case DoorStatus::Open:
    case DoorStatus::Stopped:
      if (*timeoutMinutes_ && !timeoutTimer_.isRunning()) {
        timeoutTimer_.setDuration(static_cast<std::uint32_t>(*timeoutMinutes_) *
                                  1000);
        timeoutTimer_.start();
        openedAtMs_ = millis();
      }
      break;
    case DoorStatus::Closed:
      timeoutTimer_.stop();
      timeoutAlertsFired_ = 0;
      nightAlertsFired_ = 0;
      break;
    default:
      break;
  }
}

}  // namespace garadget

