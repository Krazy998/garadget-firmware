#include "node-particle.h"

#include <cstdio>

namespace garadget {

bool ParticleNode::init() {
  auto& config = getConfig();
  enabled_ = (config.config.protocols & 0b01) != 0;

  if (!enabled_) {
    if (Particle.connected()) {
      Particle.disconnect();
    }
    Log.info("Cloud - disabled");
    return false;
  }

  Log.info("Cloud - enabled");
  boot_ = true;
  initialized_ = true;
  return true;
}

void ParticleNode::declare() {
  if (!initialized_ || !Particle.syncTimeDone()) {
    return;
  }

  if (!Time.isValid()) {
    Particle.syncTime();
    return;
  }

  updateTimer_.setDuration(constants::kVariablePublishIntervalMs);
  updateDoorConfig();

  if (boot_) {
    Particle.function("setState", &ParticleNode::receiveState, this);
    Particle.function("setConfig", &ParticleNode::receiveConfig, this);
    Particle.variable("doorStatus", doorStatus_.data(), STRING);
    Particle.variable("doorConfig", doorConfig_.data(), STRING);
    Particle.variable("netConfig", netConfig_.data(), STRING);
    Particle.subscribe("spark/", &ParticleNode::handleEvent, this, MY_DEVICES);
    Particle.publish("spark/device/name", PRIVATE);
    boot_ = false;
    Log.info("Cloud - ready");
  }
  initialized_ = false;
}

void ParticleNode::process() {
  if (!enabled_) {
    return;
  }

  if (!Particle.connected()) {
    Particle.connect();
    return;
  }

  declare();
  Particle.process();

  if (!updateTimer_.isRunning()) {
    updateDoorStatus();
    updateNetworkConfig();
    updateTimer_.start();

    if (millis() - Particle.timeSyncedLast() > constants::kTimeSyncIntervalMs) {
      Log.info("Cloud - syncing time");
      Particle.syncTime();
    }
  }
}

bool ParticleNode::receive(const Message& message) {
  switch (message.type) {
    case MessageType::Init:
    case MessageType::Config:
      init();
      break;
    case MessageType::Status:
      if (enabled_) {
        publishStatus(*static_cast<const DoorStatus*>(message.payload));
      }
      break;
    case MessageType::Alert:
      if (enabled_) {
        publishAlert(static_cast<const char*>(message.payload));
      }
      break;
    default:
      break;
  }
  return true;
}

int ParticleNode::receiveConfig(String config) {
  Log.info("Cloud - new config: %s", config.c_str());
  return getConfig().parse(config);
}

int ParticleNode::receiveState(String state) {
  const DoorStatus status = Config::statusFromString(state);
  if (status == DoorStatus::Unknown) {
    Log.error("Cloud - unknown command: %s", state.c_str());
    return false;
  }

  const Message message{"cloud", MessageType::Command, &status};
  Log.info("Cloud - command: %s", state.c_str());
  return handleMessage(message);
}

void ParticleNode::handleEvent(const char* topic, const char* data) {
  Log.info("Cloud - topic: %s, Value: %s", topic, data);
  if (String(topic).equals("spark/device/name")) {
    getConfig().setName(String(data));
    publishStatus(DoorStatus::Init);
  }
}

void ParticleNode::publishStatus(DoorStatus status) {
  const String payload = Config::statusToString(status);
  Particle.publish("state", payload, 60, PRIVATE);
  Log.info("Cloud - published status: %s", payload.c_str());
}

void ParticleNode::publishAlert(const char* alertData) {
  Particle.publish("alert", alertData, 60, PRIVATE);
  Log.info("Cloud - published alert: %s", alertData);
}

void ParticleNode::updateDoorStatus() {
  char timeBuffer[10];
  auto& config = getConfig();
  config.timeInStatus(timeBuffer, sizeof(timeBuffer));

  std::snprintf(doorStatus_.data(), doorStatus_.size(),
                "status=%s|time=%s|sensor=%u|base=%u|signal=%d",
                Config::statusToString(config.state.doorStatus), timeBuffer,
                config.state.sensorReflection, config.state.sensorBase,
                static_cast<int>(WiFi.RSSI()));
}

void ParticleNode::updateDoorConfig() {
  const auto& config = getConfig().config;
  std::snprintf(doorConfig_.data(), doorConfig_.size(),
                "sys=%s|ver=%u.%u|rdt=%u|mtt=%u|rlt=%u|rlp=%u|srr=%u|srt=%u|aev=%u|aot=%u|ans=%u|ane=%u|tzo=%s|nme=%s",
                System.version().c_str(), config.versionMajor,
                config.versionMinor, config.readTimeMs, config.motionTimeMs,
                config.relayTimeMs, config.relayPauseMs, config.sensorReads,
                config.sensorThreshold, config.alertEvents,
                config.alertOpenTimeoutMin, config.alertNightStartMin,
                config.alertNightEndMin, config.timeZone.data(),
                config.deviceName.data());
}

void ParticleNode::updateNetworkConfig() {
  if (!WiFi.ready()) {
    return;
  }

  const IPAddress localIp = WiFi.localIP();
  const IPAddress netMask = WiFi.subnetMask();
  const IPAddress gateway = WiFi.gatewayIP();

  uint8_t macAddress[6];
  WiFi.macAddress(macAddress);

  std::snprintf(netConfig_.data(), netConfig_.size(),
                "ip=%d.%d.%d.%d|snet=%d.%d.%d.%d|gway=%d.%d.%d.%d|mac=%02X:%02X:%02X:%02X:%02X:%02X|ssid=%s",
                localIp[0], localIp[1], localIp[2], localIp[3], netMask[0],
                netMask[1], netMask[2], netMask[3], gateway[0], gateway[1],
                gateway[2], gateway[3], macAddress[0], macAddress[1],
                macAddress[2], macAddress[3], macAddress[4], macAddress[5],
                WiFi.SSID().c_str());
}

}  // namespace garadget

