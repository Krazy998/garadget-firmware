#pragma once

#include "node.h"

namespace garadget {

class RelayNode : public Node {
 public:
  bool init() override;
  void process() override;
  bool receive(const Message& message) override;

 private:
  void engage(std::uint8_t clicks = 0);
  void release();

  Timeout onTimer_;
  Timeout offTimer_;
  DoorStatus* status_ = nullptr;
  std::uint8_t remainingClicks_ = 0;
};

}  // namespace garadget

