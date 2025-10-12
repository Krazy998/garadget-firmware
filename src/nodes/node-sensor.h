#pragma once

#include "node.h"

namespace garadget {

class SensorNode : public Node {
 public:
  static SensorNode& instance();

  bool init() override;
  void process() override;
  DoorStatus getState();
  bool receive(const Message& message) override;
  std::uint8_t read();
  std::uint16_t baseValue() const;

 private:
  SensorNode() = default;

  bool isTripping();
  bool onChange(DoorStatus newStatus);
  void onMotionEnd();

  Timeout scanTimer_;
  Timeout motionTimer_;
  DoorStatus* status_ = nullptr;
  std::uint8_t* reflection_ = nullptr;
  std::uint16_t* base_ = nullptr;
  std::uint8_t* threshold_ = nullptr;
};

}  // namespace garadget

