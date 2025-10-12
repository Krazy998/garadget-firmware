#pragma once

#include "node.h"

#include <array>

namespace garadget {

class PortNode : public Node {
 public:
  static std::uint8_t deviceCount;

  bool init() override;
  void process() override;
  void close();

 protected:
  std::uint8_t address_ = 0x00;
  const bool input_ = false;
  const bool output_ = false;
  const std::uint16_t scanPeriodMs_ = 1000;

  std::array<std::uint8_t, constants::kPortBufferSize> lastResponse_{};
  std::uint8_t lastResponseLength_ = 0;
  PinMode savedSdaMode_;
  PinMode savedSclMode_;
  bool savedSdaLevel_ = false;
  bool savedSclLevel_ = false;

  Timeout scanTimer_;
  int8_t busSend(std::uint8_t* buffer, std::uint8_t length);
  int8_t busRequest(std::uint8_t* buffer,
                    std::uint8_t maxLength = constants::kPortBufferSize);
  const char* source();
};

}  // namespace garadget

