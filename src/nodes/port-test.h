#pragma once

#include "port.h"
#include "node-sensor.h"

namespace garadget {

class TestPort : public PortNode {
 public:
  bool init() override;
  void sendInfo();
  void sendId();
  void sendState();

 private:
  std::uint8_t ledStep_ = 0;
  bool relayState_ = false;
  SensorNode* sensor_ = nullptr;

  void nextColor();
  void toggleRelay();
  const char* source() const;
};

}  // namespace garadget

