#include "node-relay.h"

namespace garadget {

bool RelayNode::init() {
  auto& config = getConfig();
  status_ = &config.state.doorStatus;

  onTimer_.setDuration(&config.config.relayTimeMs);
  offTimer_.setDuration(&config.config.relayPauseMs);

  pinMode(constants::kRelayPin, OUTPUT);
  digitalWrite(constants::kRelayPin, LOW);

  enabled_ = true;
  return true;
}

void RelayNode::process() {
  if (onTimer_.isTimeout()) {
    release();
  } else if (offTimer_.isTimeout()) {
    engage();
  }
}

void RelayNode::engage(std::uint8_t clicks) {
  if (clicks > 0) {
    remainingClicks_ = clicks;
  }
  digitalWrite(constants::kRelayPin, HIGH);
  onTimer_.start();
}

void RelayNode::release() {
  digitalWrite(constants::kRelayPin, LOW);
  if (remainingClicks_ > 0) {
    --remainingClicks_;
  }
  if (remainingClicks_ > 0) {
    offTimer_.start();
  }
}

bool RelayNode::receive(const Message& message) {
  if (message.type != MessageType::Command || status_ == nullptr) {
    return true;
  }

  const auto command = *static_cast<const DoorStatus*>(message.payload);
  DoorStatus nextStatus = DoorStatus::Unknown;
  std::uint8_t clicks = 0;

  switch (command) {
    case DoorStatus::Open:
    case DoorStatus::Opening:
      switch (*status_) {
        case DoorStatus::Open:
        case DoorStatus::Opening:
          return true;
        case DoorStatus::Closing:
          nextStatus = DoorStatus::Opening;
          clicks = 1;
          break;
        case DoorStatus::Closed:
          if constexpr (build::kUseVirtualDoor) {
            nextStatus = DoorStatus::Opening;
          }
          clicks = 1;
          break;
        case DoorStatus::Stopped:
          nextStatus = DoorStatus::Opening;
          clicks = 2;
          break;
        default:
          break;
      }
      break;

    case DoorStatus::Closed:
    case DoorStatus::Closing:
      switch (*status_) {
        case DoorStatus::Closed:
        case DoorStatus::Closing:
          return true;
        case DoorStatus::Open:
        case DoorStatus::Stopped:
          clicks = 1;
          break;
        case DoorStatus::Opening:
          clicks = 2;
          break;
        default:
          break;
      }
      nextStatus = DoorStatus::Closing;
      break;

    case DoorStatus::Stopped:
      switch (*status_) {
        case DoorStatus::Stopped:
        case DoorStatus::Closed:
        case DoorStatus::Open:
          return true;
        case DoorStatus::Opening:
          clicks = 1;
          break;
        case DoorStatus::Closing:
          clicks = 2;
          break;
        default:
          break;
      }
      nextStatus = DoorStatus::Stopped;
      break;

    default:
      return true;
  }

  Log.info("Relay - clicks: %u", clicks);
  engage(clicks);
  if (nextStatus == DoorStatus::Unknown) {
    return true;
  }

  Log.info("Relay - new status: %s", Config::statusToString(nextStatus));
  *status_ = nextStatus;
  const Message statusMessage{"relay", MessageType::Status, status_};
  return handleMessage(statusMessage);
}

}  // namespace garadget

