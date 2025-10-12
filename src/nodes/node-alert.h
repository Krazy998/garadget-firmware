#pragma once

#include "node.h"

namespace garadget {

class AlertNode : public Node {
 public:
  bool init() override;
  void process() override;
  bool receive(const Message& message) override;

 private:
  bool isNight() const;
  void handleStatus();
  void fireAlert(const char* type, const char* data);
  void statusAlert();
  void timeoutAlert();
  void nightAlert();

  DoorStatus* status_ = nullptr;
  std::uint16_t* events_ = nullptr;
  std::uint16_t* timeoutMinutes_ = nullptr;
  std::uint16_t* nightStartMinutes_ = nullptr;
  std::uint16_t* nightEndMinutes_ = nullptr;
  Timeout timeoutTimer_;

  std::uint32_t openedAtMs_ = 0;
  std::uint8_t timeoutAlertsFired_ = 0;
  std::uint8_t nightAlertsFired_ = 0;
};

}  // namespace garadget

