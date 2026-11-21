#include "ha_mqtt.h"

#include <cstdio>
#include <cstring>

#include "../core/json_utils.h"
#include "../version.h"

#ifdef PARTICLE
#include "Particle.h"
#endif

namespace garadget {

HaMqttBridge::HaMqttBridge(ConfigManager& config,
                           ConfigStorage& storage,
                           DoorController& door,
                           AlertEngine& alerts,
                           WifiManager& wifi)
    : config_(config), storage_(storage), door_(door), alerts_(alerts), wifi_(wifi) {}

bool HaMqttBridge::buildDiscoveryPayload(uint8_t entityIndex,
                                         const DeviceConfig& cfg,
                                         const char* deviceId,
                                         const char* topicId,
                                         const char* ipStr,
                                         char* outTopic, size_t topicCap,
                                         char* outPayload, size_t payloadCap) {
    if (entityIndex >= kDiscoveryEntityCount || !outTopic || !outPayload) return false;

    const char* prefix = (cfg.haPrefix[0] != '\0') ? cfg.haPrefix : "homeassistant";
    const char* devName = (cfg.deviceName[0] != '\0') ? cfg.deviceName : "Garadget";
    const char* uidBase = (deviceId && deviceId[0] != '\0') ? deviceId : topicId;

    char baseTopic[64];
    std::snprintf(baseTopic, sizeof(baseTopic), "garadget/%s", topicId);

    char availTopic[80];
    std::snprintf(availTopic, sizeof(availTopic), "%s/LWT", baseTopic);
    char stateTopic[80];
    std::snprintf(stateTopic, sizeof(stateTopic), "%s/status", baseTopic);
    char cmdTopic[80];
    std::snprintf(cmdTopic, sizeof(cmdTopic), "%s/command", baseTopic);
    char alertTopic[80];
    std::snprintf(alertTopic, sizeof(alertTopic), "%s/alert", baseTopic);
    char cfgTopic[80];
    std::snprintf(cfgTopic, sizeof(cfgTopic), "%s/config", baseTopic);
    char threshCmdTopic[80];
    std::snprintf(threshCmdTopic, sizeof(threshCmdTopic), "%s/set-threshold", baseTopic);
    char stealthCmdTopic[96];
    std::snprintf(stealthCmdTopic, sizeof(stealthCmdTopic), "%s/set-stealth", baseTopic);
    char ledBriCmdTopic[96];
    std::snprintf(ledBriCmdTopic, sizeof(ledBriCmdTopic), "%s/set-led-brightness", baseTopic);

    char uid[64];
    char cfgUrl[48];
    if (ipStr && ipStr[0] != '\0' && std::strcmp(ipStr, "0.0.0.0") != 0) {
        std::snprintf(cfgUrl, sizeof(cfgUrl), "http://%s/", ipStr);
    } else {
        cfgUrl[0] = '\0';
    }

    auto writeDeviceBlock = [&](JsonWriter& w, bool full) {
        w.keyObject("dev");
        w.keyArray("ids");
        w.stringValue(uidBase);
        w.endArray();
        if (full) {
            w.keyString("name", devName);
            w.keyString("mf", kManufacturer);
            w.keyString("mdl", kModelName);
            w.keyString("sw", kFirmwareVersion);
            if (cfgUrl[0] != '\0') {
                w.keyString("cu", cfgUrl);
            }
        }
        w.endObject();
    };

    JsonWriter w(outPayload, payloadCap);
    w.beginObject();

    switch (entityIndex) {
        case 0: // Main Garage Door Cover
            std::snprintf(outTopic, topicCap, "%s/cover/%s_door/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_door", uidBase);
            w.keyString("name", "Door");
            w.keyString("uniq_id", uid);
            w.keyString("dev_cla", "garage");
            w.keyString("cmd_t", cmdTopic);
            w.keyString("stat_t", stateTopic);
            w.keyString("val_tpl", "{{ value_json.status }}");
            w.keyString("pl_open", "open");
            w.keyString("pl_cls", "close");
            w.keyString("pl_stop", "stop");
            w.keyString("stat_open", "open");
            w.keyString("stat_clsd", "closed");
            w.keyString("stat_opening", "opening");
            w.keyString("stat_closing", "closing");
            w.keyString("stat_stopped", "stopped");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, true);
            break;

        case 1: // Laser Reflection Sensor
            std::snprintf(outTopic, topicCap, "%s/sensor/%s_reflection/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_reflection", uidBase);
            w.keyString("name", "Reflection");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", stateTopic);
            w.keyString("val_tpl", "{{ value_json.sensor }}");
            w.keyString("unit_of_meas", "%");
            w.keyString("stat_cla", "measurement");
            w.keyString("ic", "mdi:laser-pointer");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 2: // Ambient Brightness Sensor
            std::snprintf(outTopic, topicCap, "%s/sensor/%s_brightness/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_brightness", uidBase);
            w.keyString("name", "Ambient Brightness");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", stateTopic);
            w.keyString("val_tpl", "{{ value_json.bright }}");
            w.keyString("unit_of_meas", "%");
            w.keyString("stat_cla", "measurement");
            w.keyString("ent_cat", "diagnostic");
            w.keyString("ic", "mdi:brightness-6");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 3: // Wi-Fi Signal Strength
            std::snprintf(outTopic, topicCap, "%s/sensor/%s_rssi/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_rssi", uidBase);
            w.keyString("name", "Wi-Fi Signal");
            w.keyString("uniq_id", uid);
            w.keyString("dev_cla", "signal_strength");
            w.keyString("stat_t", stateTopic);
            w.keyString("val_tpl", "{{ value_json.signal }}");
            w.keyString("unit_of_meas", "dBm");
            w.keyString("stat_cla", "measurement");
            w.keyString("ent_cat", "diagnostic");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 4: // Time in Current State
            std::snprintf(outTopic, topicCap, "%s/sensor/%s_time/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_time", uidBase);
            w.keyString("name", "Time in State");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", stateTopic);
            w.keyString("val_tpl", "{{ value_json.time }}");
            w.keyString("ent_cat", "diagnostic");
            w.keyString("ic", "mdi:timer-outline");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 5: // Last Alert
            std::snprintf(outTopic, topicCap, "%s/sensor/%s_alert/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_alert", uidBase);
            w.keyString("name", "Last Alert");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", alertTopic);
            w.keyString("val_tpl", "{{ value_json.type }}: {{ value_json.data }}");
            w.keyString("ent_cat", "diagnostic");
            w.keyString("ic", "mdi:bell-alert-outline");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 6: // Sensor Threshold Number (configurable from HA!)
            std::snprintf(outTopic, topicCap, "%s/number/%s_threshold/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_threshold", uidBase);
            w.keyString("name", "Sensor Threshold");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", cfgTopic);
            w.keyString("val_tpl", "{{ value_json.srt }}");
            w.keyString("cmd_t", threshCmdTopic);
            w.keyInt("min", kMinSensorThreshold);
            w.keyInt("max", kMaxSensorThreshold);
            w.keyInt("step", 1);
            w.keyString("unit_of_meas", "%");
            w.keyString("ent_cat", "config");
            w.keyString("ic", "mdi:tune");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 7: // Reboot Button
            std::snprintf(outTopic, topicCap, "%s/button/%s_reboot/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_reboot", uidBase);
            w.keyString("name", "Reboot");
            w.keyString("uniq_id", uid);
            w.keyString("dev_cla", "restart");
            w.keyString("cmd_t", cmdTopic);
            w.keyString("pl_prs", "reboot");
            w.keyString("ent_cat", "config");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 8: // Stealth Mode Switch (turns RGB status LED off)
            std::snprintf(outTopic, topicCap, "%s/switch/%s_stealth/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_stealth", uidBase);
            w.keyString("name", "Stealth Mode");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", cfgTopic);
            w.keyString("val_tpl", "{{ 'ON' if value_json.leds == 1 else 'OFF' }}");
            w.keyString("cmd_t", stealthCmdTopic);
            w.keyString("pl_on", "ON");
            w.keyString("pl_off", "OFF");
            w.keyString("ent_cat", "config");
            w.keyString("ic", "mdi:led-off");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        case 9: // Status LED Brightness Number (0..100%)
            std::snprintf(outTopic, topicCap, "%s/number/%s_led_brightness/config", prefix, uidBase);
            std::snprintf(uid, sizeof(uid), "%s_led_brightness", uidBase);
            w.keyString("name", "LED Brightness");
            w.keyString("uniq_id", uid);
            w.keyString("stat_t", cfgTopic);
            w.keyString("val_tpl", "{{ value_json.ledb }}");
            w.keyString("cmd_t", ledBriCmdTopic);
            w.keyInt("min", 0);
            w.keyInt("max", 100);
            w.keyInt("step", 5);
            w.keyString("unit_of_meas", "%");
            w.keyString("ent_cat", "config");
            w.keyString("ic", "mdi:brightness-6");
            w.keyString("avty_t", availTopic);
            w.keyString("pl_avail", "Online");
            w.keyString("pl_not_avail", "Offline");
            writeDeviceBlock(w, false);
            break;

        default:
            return false;
    }

    w.endObject();
    return !w.truncated();
}

#ifdef PARTICLE

void HaMqttBridge::onConnectStatic(void* ctx) {
    static_cast<HaMqttBridge*>(ctx)->onConnected();
}

void HaMqttBridge::onMessageTrampoline(const char* topic, const char* payload, size_t len, void* ctx) {
    static_cast<HaMqttBridge*>(ctx)->onMessage(topic, payload, len);
}

void HaMqttBridge::refreshTopicId() {
    wifi_.formatDeviceId(deviceId_, sizeof(deviceId_));
    config_.buildTopicId(topicId_, sizeof(topicId_), deviceId_);
}

void HaMqttBridge::init(uint32_t nowMs) {
    client_.setCallbacks(&HaMqttBridge::onConnectStatic, &HaMqttBridge::onMessageTrampoline, this);
    refreshTopicId();
    backoffMs_ = 2000;
    reconnectTimer_.start(nowMs, 1000);
    periodicStatusTimer_.start(nowMs, 60000);
}

void HaMqttBridge::reconfigure(uint32_t nowMs) {
    client_.disconnect();
    refreshTopicId();
    backoffMs_ = 2000;
    discoveryStep_ = kDiscoveryEntityCount;
    reconnectTimer_.start(nowMs, 500);
}

void HaMqttBridge::triggerDiscoveryRefresh() {
    if (client_.isConnected() && config_.data().haDiscovery) {
        discoveryStep_ = 0;
    }
}

void HaMqttBridge::onConnected() {
    backoffMs_ = 2000;

    char topic[96];
    std::snprintf(topic, sizeof(topic), "garadget/%s/LWT", topicId_);
    client_.publish(topic, "Online", true);

    std::snprintf(topic, sizeof(topic), "garadget/%s/command", topicId_);
    client_.subscribe(topic, 0);

    std::snprintf(topic, sizeof(topic), "garadget/%s/set-config", topicId_);
    client_.subscribe(topic, 0);

    std::snprintf(topic, sizeof(topic), "garadget/%s/set-threshold", topicId_);
    client_.subscribe(topic, 0);

    std::snprintf(topic, sizeof(topic), "garadget/%s/set-stealth", topicId_);
    client_.subscribe(topic, 0);

    std::snprintf(topic, sizeof(topic), "garadget/%s/set-led-brightness", topicId_);
    client_.subscribe(topic, 0);

    if (config_.data().haDiscovery) {
        const char* pfx = (config_.data().haPrefix[0] != '\0') ? config_.data().haPrefix : "homeassistant";
        std::snprintf(topic, sizeof(topic), "%s/status", pfx);
        client_.subscribe(topic, 0);
        discoveryStep_ = 0; // Staggered publish in tick()
    }

    publishStatus(millis());
    publishConfig();
}

void HaMqttBridge::onMessage(const char* topic, const char* payload, size_t len) {
    if (!topic || !payload) return;
    if (std::strstr(topic, "/set-config") != nullptr) {
        Log.info("MQTT rx: %s -> [redacted %u bytes]", topic, static_cast<unsigned>(len));
    } else {
        Log.info("MQTT rx: %s -> %s", topic, payload);
    }

    // Home Assistant birth message -> re-publish discovery + current state
    if (config_.data().haDiscovery) {
        char haStatusTopic[64];
        const char* pfx = (config_.data().haPrefix[0] != '\0') ? config_.data().haPrefix : "homeassistant";
        std::snprintf(haStatusTopic, sizeof(haStatusTopic), "%s/status", pfx);
        if (std::strcmp(topic, haStatusTopic) == 0) {
            if (std::strcmp(payload, "online") == 0) {
                Log.info("MQTT: Home Assistant online, refreshing discovery");
                discoveryStep_ = 0;
                publishStatus(millis());
                publishConfig();
            }
            return;
        }
    }

    char expected[96];

    // Command topic
    std::snprintf(expected, sizeof(expected), "garadget/%s/command", topicId_);
    if (std::strcmp(topic, expected) == 0) {
        if (std::strcmp(payload, "get-status") == 0) {
            publishStatus(millis());
        } else if (std::strcmp(payload, "get-config") == 0) {
            publishConfig();
        } else if (std::strcmp(payload, "refresh-discovery") == 0) {
            triggerDiscoveryRefresh();
        } else if (std::strcmp(payload, "reboot") == 0) {
            if (onSystemAction_) onSystemAction_("reboot", hookCtx_);
        } else {
            DoorCommand cmd = parseDoorCommand(payload);
            if (cmd != DoorCommand::None) {
                door_.executeCommand(cmd, millis());
            } else {
                Log.warn("MQTT: unknown command '%s'", payload);
            }
        }
        return;
    }

    // Set-threshold topic (from Home Assistant Number entity)
    std::snprintf(expected, sizeof(expected), "garadget/%s/set-threshold", topicId_);
    if (std::strcmp(topic, expected) == 0) {
        int res = config_.setParam("srt", payload);
        if (res > 0) {
            config_.save(storage_);
            publishConfig();
            alerts_.onConfigUpdated(millis());
        }
        return;
    }

    // Set-stealth topic (from Home Assistant Switch entity: ON / OFF)
    std::snprintf(expected, sizeof(expected), "garadget/%s/set-stealth", topicId_);
    if (std::strcmp(topic, expected) == 0) {
        int res = config_.setParam("leds", payload);
        if (res >= 0) {
            if (res > 0) {
                config_.save(storage_);
                alerts_.onConfigUpdated(millis());
                if (onConfigSaved_) onConfigSaved_(hookCtx_);
            } else {
                publishConfig();
            }
        }
        return;
    }

    // Set-led-brightness topic (from Home Assistant Number entity: 0..100)
    std::snprintf(expected, sizeof(expected), "garadget/%s/set-led-brightness", topicId_);
    if (std::strcmp(topic, expected) == 0) {
        int res = config_.setParam("ledb", payload);
        if (res >= 0) {
            if (res > 0) {
                config_.save(storage_);
                alerts_.onConfigUpdated(millis());
                if (onConfigSaved_) onConfigSaved_(hookCtx_);
            } else {
                publishConfig();
            }
        }
        return;
    }

    // Set-config topic (supports JSON & pipe format; restricted to operational sensor/alert/LED settings)
    std::snprintf(expected, sizeof(expected), "garadget/%s/set-config", topicId_);
    if (std::strcmp(topic, expected) == 0) {
        int updates = (len > 0 && payload[0] == '{')
                          ? config_.applyJsonConfig(payload, len, ConfigScope::Operational)
                          : config_.applyPipeConfig(payload, ConfigScope::Operational);
        if (updates < 0) {
            Log.warn("MQTT: rejected set-config payload (invalid or privileged key)");
        } else if (updates > 0) {
            config_.save(storage_);
            alerts_.onConfigUpdated(millis());
            if (onConfigSaved_) {
                onConfigSaved_(hookCtx_);
            }
        }
    }
}

void HaMqttBridge::tick(uint32_t nowMs) {
    const DeviceConfig& cfg = config_.data();
    if (!cfg.mqttEnabled || cfg.mqttHost[0] == '\0' || !wifi_.isReady()) {
        if (client_.isConnected()) {
            client_.disconnect();
        }
        return;
    }

    if (client_.state() == MqttClient::State::Disconnected) {
        if (!reconnectTimer_.isRunning() || reconnectTimer_.expired(nowMs)) {
            refreshTopicId();
            char lwtTopic[96];
            std::snprintf(lwtTopic, sizeof(lwtTopic), "garadget/%s/LWT", topicId_);

            char clientId[48];
            std::snprintf(clientId, sizeof(clientId), "garadget-%s", topicId_);

            MqttClient::Options opts;
            opts.host = cfg.mqttHost;
            opts.port = cfg.mqttPort;
            opts.keepAliveSec = cfg.mqttKeepAliveSec;
            opts.clientId = clientId;
            opts.username = cfg.mqttUser;
            opts.password = cfg.mqttPass;
            opts.willTopic = lwtTopic;
            opts.willPayload = "Offline";
            opts.willRetain = true;

            if (!client_.connect(opts, nowMs)) {
                reconnectTimer_.start(nowMs, backoffMs_);
                backoffMs_ = (backoffMs_ < 30000u) ? (backoffMs_ * 2u) : 30000u;
            }
        }
        return;
    }

    client_.tick(nowMs);
    if (!client_.isConnected()) {
        // Just disconnected -> schedule reconnect with backoff
        if (client_.state() == MqttClient::State::Disconnected) {
            reconnectTimer_.start(nowMs, backoffMs_);
            backoffMs_ = (backoffMs_ < 30000u) ? (backoffMs_ * 2u) : 30000u;
        }
        return;
    }

    // Staggered Home Assistant Discovery publication (1 entity per tick)
    if (cfg.haDiscovery && discoveryStep_ < kDiscoveryEntityCount) {
        char discTopic[128];
        static char discPayload[768];
        char ipStr[20];
        wifi_.formatIp(ipStr, sizeof(ipStr));
        if (buildDiscoveryPayload(discoveryStep_, cfg, deviceId_, topicId_, ipStr,
                                  discTopic, sizeof(discTopic), discPayload, sizeof(discPayload))) {
            client_.publish(discTopic, discPayload, true);
        } else {
            Log.warn("MQTT: HA discovery payload %u truncated, skipped", static_cast<unsigned>(discoveryStep_));
        }
        discoveryStep_++;
    }

    // Periodic status heartbeat (every 60s, or debounced every >=5s if reflection changed notably)
    uint8_t curRefl = door_.lastReading().reflectionPct;
    bool reflDelta = (lastPublishedReflection_ == 255) ||
                     (curRefl > lastPublishedReflection_ ? (curRefl - lastPublishedReflection_) : (lastPublishedReflection_ - curRefl)) >= 5;
    bool reflShift = reflDelta && (!reflPublishCooldown_.isRunning() || reflPublishCooldown_.expired(nowMs));
    if (periodicStatusTimer_.expired(nowMs) || reflShift) {
        publishStatus(nowMs);
        periodicStatusTimer_.start(nowMs, 60000);
    }
}

void HaMqttBridge::publishStatus(uint32_t nowMs) {
    if (!client_.isConnected()) return;

    char topic[96];
    std::snprintf(topic, sizeof(topic), "garadget/%s/status", topicId_);

    char dur[16];
    formatDuration(door_.timeInStatusMs(nowMs), dur, sizeof(dur));
    char ip[20];
    wifi_.formatIp(ip, sizeof(ip));

    const SensorReading& r = door_.lastReading();
    lastPublishedReflection_ = r.reflectionPct;
    reflPublishCooldown_.start(nowMs, 5000);

    char payload[256];
    JsonWriter w(payload, sizeof(payload));
    w.beginObject();
    w.keyString("status", doorStatusToString(door_.status()));
    w.keyString("time", dur);
    w.keyUInt("sensor", r.reflectionPct);
    w.keyUInt("bright", r.brightnessPct);
    w.keyInt("signal", wifi_.rssiDbm());
    w.keyString("ip", ip);
    w.endObject();

    client_.publish(topic, payload, true);
}

void HaMqttBridge::publishConfig() {
    if (!client_.isConnected()) return;

    char topic[96];
    std::snprintf(topic, sizeof(topic), "garadget/%s/config", topicId_);

    char ssid[40];
    char ip[20];
    char mac[24];
    wifi_.formatSsid(ssid, sizeof(ssid));
    wifi_.formatIp(ip, sizeof(ip));
    wifi_.formatMac(mac, sizeof(mac));
    String sysVer = System.version();

    static char payload[1152];
    if (!config_.toJson(payload, sizeof(payload), deviceId_, sysVer.c_str(), ssid, ip, mac)) {
        Log.warn("MQTT: config JSON truncated, skipped publish");
        return;
    }
    client_.publish(topic, payload, true);
}

void HaMqttBridge::publishAlert(const char* alertJson) {
    if (!client_.isConnected() || !alertJson) return;
    char topic[96];
    std::snprintf(topic, sizeof(topic), "garadget/%s/alert", topicId_);
    client_.publish(topic, alertJson, false);
}

#endif // PARTICLE

} // namespace garadget
