#pragma once

#include "node.h"

#include <array>

namespace garadget {

class ParticleNode : public Node {
 public:
  bool init() override;
  void process() override;
  bool receive(const Message& message) override;

  int receiveConfig(String config);
  int receiveState(String state);
  void handleEvent(const char* topic, const char* data);

 private:
  void declare();
  void updateDoorStatus();
  void updateDoorConfig();
  void updateNetworkConfig();
  void publishStatus(DoorStatus status);
  void publishAlert(const char* alertData);

  Timeout updateTimer_;
  std::array<char, constants::kMaxVariableSize> doorConfig_{};
  std::array<char, constants::kMaxVariableSize> doorStatus_{};
  std::array<char, constants::kMaxVariableSize> netConfig_{};
  bool boot_ = true;
  bool initialized_ = true;
};

}  // namespace garadget

