#include "timeout.h"

namespace garadget {

Timeout::Timeout(uint32_t durationMs) { setDuration(durationMs); }

Timeout::Timeout(uint32_t* durationMs) { setDuration(durationMs); }

Timeout::Timeout(uint16_t* durationMs) { setDuration(durationMs); }

void Timeout::setDuration(uint32_t durationMs) {
  staticDurationMs_ = durationMs;
  durationLong_ = nullptr;
  durationShort_ = nullptr;
}

void Timeout::setDuration(uint32_t* durationMs) {
  durationLong_ = durationMs;
  durationShort_ = nullptr;
}

void Timeout::setDuration(uint16_t* durationMs) {
  durationShort_ = durationMs;
  durationLong_ = nullptr;
}

uint32_t Timeout::resolveDuration() const {
  if (durationShort_) {
    return *durationShort_;
  }
  if (durationLong_) {
    return *durationLong_;
  }
  return staticDurationMs_;
}

void Timeout::start() {
  start_ = millis();
  end_ = start_ + resolveDuration();
  running_ = true;
}

void Timeout::stop() { running_ = false; }

bool Timeout::isRunning() {
  if (!running_) {
    return false;
  }
  const uint32_t now = millis();
  if (now >= end_ && (end_ >= start_ || now < start_)) {
    running_ = false;
    return false;
  }
  return true;
}

bool Timeout::isTimeout() {
  if (!running_) {
    return false;
  }
  if (millis() >= end_) {
    running_ = false;
    return true;
  }
  return false;
}

uint32_t Timeout::timeLeft() const {
  if (!running_) {
    return 0;
  }
  const uint32_t now = millis();
  if (now >= end_) {
    return 0;
  }
  return end_ - now;
}

}  // namespace garadget

