#include "Particle.h"

#include "command.h"
#include "config.h"
#include "global.h"
#include "nodes/node-alert.h"
#include "nodes/node-mqtt.h"
#include "nodes/node-particle.h"
#include "nodes/node-relay.h"
#include "nodes/node-sensor.h"
#include "softap/webconfig.h"
#include "utils/timezones.h"

#if GARADGET_ENABLE_TESTPORT
#include "nodes/port-test.h"
#endif

SYSTEM_MODE(MANUAL);
// SYSTEM_THREAD(ENABLED);

STARTUP({
  if (garadget::build::kAntennaPreference != ANT_AUTO) {
    WiFi.selectAntenna(garadget::build::kAntennaPreference);
  }
  softap_set_application_page_handler(f_pageHandler, nullptr);
});

namespace garadget {

namespace {
Config& config = Config::instance();
SensorNode& sensor = SensorNode::instance();
RelayNode relay;
ParticleNode particle;
MqttNode mqtt;
AlertNode alert;
#if GARADGET_ENABLE_TESTPORT
TestPort testPort;
#endif

bool initialized = false;

void initLogging() {
  if (build::kEnableDebugLog) {
    static SerialLogHandler logHandler(
        LOG_LEVEL_WARN, {{"app", LOG_LEVEL_ALL}});
    (void)logHandler;
  }
}

void initNodes() {
  if (initialized) {
    return;
  }

  initLogging();
  config.init();
  sensor.init();
  relay.init();
#if GARADGET_ENABLE_TESTPORT
  testPort.init();
#endif
  WiFi.connect();
  particle.init();
  mqtt.init();
  alert.init();

  initialized = true;
}

}  // namespace

}  // namespace garadget

void setup() {
  garadget::initNodes();
}

void loop() {
  garadget::config.process();
  garadget::particle.process();
  garadget::mqtt.process();
  garadget::relay.process();
  garadget::sensor.process();
  garadget::alert.process();
}

namespace garadget {

bool handleMessage(const Message& message) {
  garadget::relay.receive(message);
  garadget::sensor.receive(message);
  garadget::alert.receive(message);
  garadget::particle.receive(message);
  garadget::mqtt.receive(message);
  return true;
}

}  // namespace garadget

