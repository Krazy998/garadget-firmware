#pragma once

#include "node.h"
#include "../command.h"
#include "../lib/MQTT.h"

namespace garadget {

class MqttNode : public Node, public MQTT {
 public:
  MqttNode();

  bool init() override;
  void process() override;
  bool receive(const Message& message) override;

  void f_onConnectHanlder(uint8_t result) override;
  void f_onReceiveHandler(char* topic, uint8_t* payload, unsigned int length) override;

 private:
  bool connectBroker();
  void subscribeTopics();
  void publishStatus();
  void publishConfig();
  void publishDiscovery();
  const char* clientId() const;

  bool discoveryPublished_ = false;
  mutable String clientIdCache_;
};

}  // namespace garadget

