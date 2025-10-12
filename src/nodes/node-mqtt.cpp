#include "node-mqtt.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace garadget {

namespace {
constexpr char kTopicRoot[] = "garadget";
constexpr char kCommandSuffix[] = "command";
constexpr char kConfigSuffix[] = "config";
constexpr char kStatusSuffix[] = "status";
constexpr char kSetConfigSuffix[] = "set-config";
constexpr char kAvailabilitySuffix[] = "LWT";
constexpr char kDiscoveryPrefix[] = "homeassistant/cover";
constexpr char kAvailabilityOnline[] = "Online";
constexpr char kAvailabilityOffline[] = "Offline";

String makeTopic(const char* clientId, const char* suffix) {
  String topic = String::format("%s/%s/%s", kTopicRoot, clientId, suffix);
  return topic;
}

}  // namespace

MqttNode::MqttNode() { enabled_ = false; }

bool MqttNode::init() {
  auto& config = getConfig();
  enabled_ = (config.config.protocols & 0b10) != 0;
  discoveryPublished_ = false;
  f_disconnect();

  if (!enabled_) {
    Log.info("MQTT - disabled");
    return false;
  }

  Log.info("MQTT - enabled");
  MQTT::f_init(nullptr, config.config.mqttBrokerIp.data(),
               config.config.mqttBrokerPort, config.config.mqttTimeoutMs,
               MQTT_MAX_PACKET_SIZE);
  return true;
}

bool MqttNode::connectBroker() {
  if (!enabled_ || !WiFi.ready()) {
    return false;
  }
  if (f_isConnected()) {
    return true;
  }

  const char* id = clientId();
  auto& config = getConfig();

  String availabilityTopic = makeTopic(id, kAvailabilitySuffix);

  const bool connected = MQTT::f_connect(
      id, config.config.mqttBrokerUser.data(),
      config.config.mqttBrokerPass.data(), availabilityTopic.c_str(), QOS0,
      true, kAvailabilityOffline, true);
  if (!connected) {
    return false;
  }

  MQTT::f_publish(availabilityTopic.c_str(),
                  reinterpret_cast<const uint8_t*>(kAvailabilityOnline),
                  std::strlen(kAvailabilityOnline), true);
  return true;
}

void MqttNode::process() {
  if (!connectBroker()) {
    return;
  }
  MQTT::f_process();
}

bool MqttNode::receive(const Message& message) {
  switch (message.type) {
    case MessageType::Config:
      init();
      discoveryPublished_ = false;
      break;
    case MessageType::Status:
      publishStatus();
      break;
    default:
      break;
  }
  return true;
}

void MqttNode::subscribeTopics() {
  const char* id = clientId();
  std::array<char, sizeof(kTopicRoot) + constants::kMaxNameSize + 32> topic{};

  std::snprintf(topic.data(), topic.size(), "%s/%s/%s", kTopicRoot, id,
                kCommandSuffix);
  MQTT::f_subscribe(topic.data(), QOS0);
  Log.info("MQTT - subscribed to topic: %s", topic.data());

  std::snprintf(topic.data(), topic.size(), "%s/%s/%s", kTopicRoot, id,
                kSetConfigSuffix);
  MQTT::f_subscribe(topic.data(), QOS0);
  Log.info("MQTT - subscribed to topic: %s", topic.data());
}

void MqttNode::publishStatus() {
  if (!enabled_) {
    return;
  }

  const char* id = clientId();
  std::array<char, sizeof(kTopicRoot) + constants::kMaxNameSize + 32> topic{};
  std::snprintf(topic.data(), topic.size(), "%s/%s/%s", kTopicRoot, id,
                kStatusSuffix);

  std::array<char, 128> payload{};
  getConfig().getJsonStatus(payload.data(), payload.size());
  MQTT::f_publish(topic.data(),
                  reinterpret_cast<uint8_t*>(payload.data()),
                  std::strlen(payload.data()), true);
  Log.info("MQTT - published status: %s", payload.data());
}

void MqttNode::publishConfig() {
  if (!enabled_) {
    return;
  }
  const char* id = clientId();
  std::array<char, sizeof(kTopicRoot) + constants::kMaxNameSize + 32> topic{};
  std::snprintf(topic.data(), topic.size(), "%s/%s/%s", kTopicRoot, id,
                kConfigSuffix);

  std::array<char, MQTT_MAX_PACKET_SIZE> payload{};
  getConfig().getJsonConfig(payload.data(), payload.size());
  MQTT::f_publish(topic.data(),
                  reinterpret_cast<uint8_t*>(payload.data()),
                  std::strlen(payload.data()), true);
  Log.info("MQTT - published config: %s", payload.data());
}

void MqttNode::publishDiscovery() {
  if (discoveryPublished_) {
    return;
  }

  const char* id = clientId();
  auto& config = getConfig();

  const String discoveryTopic =
      String::format("%s/%s/%s/config", kDiscoveryPrefix, id, id);

  std::array<char, MQTT_MAX_PACKET_SIZE> payload{};
  std::snprintf(
      payload.data(), payload.size(),
      "{\"name\":\"%s\",\"unique_id\":\"garadget_%s\",\"command_topic\":\"%s/%s/%s\",\"state_topic\":\"%s/%s/%s\",\"availability_topic\":\"%s/%s/%s\",\"payload_open\":\"open\",\"payload_close\":\"closed\",\"payload_stop\":\"stopped\",\"state_open\":\"open\",\"state_closed\":\"closed\",\"payload_available\":\"%s\",\"payload_not_available\":\"%s\",\"value_template\":\"{{ value_json.status }}\",\"device_class\":\"garage\",\"device\":{\"identifiers\":[\"garadget_%s\"],\"manufacturer\":\"Softcomplex\",\"model\":\"Garadget P1\",\"sw_version\":\"%u.%u\"}}",
      Config::escapeJson(String(config.config.deviceName.data())).c_str(), id,
      kTopicRoot, id, kCommandSuffix, kTopicRoot, id, kStatusSuffix,
      kTopicRoot, id, kAvailabilitySuffix, kAvailabilityOnline,
      kAvailabilityOffline, id, constants::kFirmwareMajor,
      constants::kFirmwareMinor);

  MQTT::f_publish(discoveryTopic.c_str(),
                  reinterpret_cast<uint8_t*>(payload.data()),
                  std::strlen(payload.data()), true);
  discoveryPublished_ = true;
  Log.info("MQTT - published discovery payload: %s", payload.data());
}

const char* MqttNode::clientId() const {
  auto& config = Config::instance();
  String id = String(config.config.deviceName.data());
  if (id.length() == 0) {
    id = System.deviceID();
  }
  id.replace(' ', '_');
  id.replace('/', '_');
  clientIdCache_ = id;
  return clientIdCache_.c_str();
}

void MqttNode::f_onConnectHanlder(uint8_t result) {
  if (result != CONN_ACCEPT) {
    Log.warn("MQTT - connection refused: %u", result);
    return;
  }
  subscribeTopics();
  publishStatus();
  publishConfig();
  publishDiscovery();
  Log.info("MQTT - ready");
}

void MqttNode::f_onReceiveHandler(char* topic, uint8_t* payload,
                                  unsigned int length) {
  if (!topic || !payload) {
    return;
  }

  payload[length] = '\0';
  const String topicString = String(topic);
  const String payloadString = String(reinterpret_cast<char*>(payload));

  Log.info("MQTT - topic: %s, payload: %s", topic, payload);

  if (topicString.endsWith(kCommandSuffix)) {
    if (payloadString.equalsIgnoreCase("get-status")) {
      publishStatus();
      return;
    }
    if (payloadString.equalsIgnoreCase("get-config")) {
      publishConfig();
      return;
    }
    if (payloadString.equalsIgnoreCase("reboot")) {
      System.reset();
      return;
    }

    const DoorStatus requested = Config::statusFromString(payloadString);
    if (requested == DoorStatus::Unknown) {
      Log.error("MQTT - unknown command: %s", payload);
      return;
    }

    const Message message{"mqtt", MessageType::Command, &requested};
    handleMessage(message);
    Log.info("MQTT - command dispatched: %s", payload);
  } else if (topicString.endsWith(kSetConfigSuffix)) {
    SetConfigCommand command;
    command.execute(reinterpret_cast<char*>(payload));
    discoveryPublished_ = false;
  }
}

}  // namespace garadget

