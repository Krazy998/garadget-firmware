#pragma once

#include "Particle.h"

#include <cstdint>

namespace garadget {

class Timeout {
 public:
  Timeout() = default;
  explicit Timeout(uint32_t durationMs);
  explicit Timeout(uint32_t* durationMs);
  explicit Timeout(uint16_t* durationMs);

  void setDuration(uint32_t durationMs);
  void setDuration(uint32_t* durationMs);
  void setDuration(uint16_t* durationMs);

  void start();
  void stop();
  bool isRunning();
  bool isTimeout();
  uint32_t timeLeft() const;

 private:
  uint32_t resolveDuration() const;

  uint32_t staticDurationMs_ = 0;
  uint16_t* durationShort_ = nullptr;
  uint32_t* durationLong_ = nullptr;
  uint32_t start_ = 0;
  uint32_t end_ = 0;
  bool running_ = false;
};

}  // namespace garadget

