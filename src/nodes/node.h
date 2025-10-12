#pragma once

#include "Particle.h"
#include "../config.h"
#include "../global.h"
#include "../utils/timeout.h"

namespace garadget {

class Node {
 public:
  virtual ~Node() = default;

  virtual bool init();
  virtual void process();
  virtual bool receive(const Message& message);

 protected:
  static Config& getConfig();
  bool enabled_ = false;
};

}  // namespace garadget

