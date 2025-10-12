#pragma once

#include "Particle.h"
#include <cstddef>
#include <cstdint>

#ifndef GARADGET_ORG_MODE
#define GARADGET_ORG_MODE 1
#endif

#ifndef GARADGET_ENABLE_DEBUG_LOG
#define GARADGET_ENABLE_DEBUG_LOG 0
#endif

#ifndef GARADGET_ENABLE_TESTPORT
#define GARADGET_ENABLE_TESTPORT 1
#endif

#ifndef GARADGET_USE_VIRTUAL_DOOR
#define GARADGET_USE_VIRTUAL_DOOR 0
#endif

namespace garadget {

namespace build {
inline constexpr bool kOrgMode = GARADGET_ORG_MODE != 0;
inline constexpr bool kEnableDebugLog = GARADGET_ENABLE_DEBUG_LOG != 0;
inline constexpr bool kEnableTestPort = GARADGET_ENABLE_TESTPORT != 0;
inline constexpr bool kUseVirtualDoor = GARADGET_USE_VIRTUAL_DOOR != 0;
inline constexpr WiFiAntSelect kAntennaPreference = ANT_AUTO;
}  // namespace build

namespace constants {
inline constexpr std::uint16_t kProductId = 355;
inline constexpr std::uint8_t kFirmwareMajor = 1;
inline constexpr std::uint8_t kFirmwareMinor = 26;
inline constexpr std::uint16_t kFirmwareId =
    static_cast<std::uint16_t>(kFirmwareMajor * 100 + kFirmwareMinor);
inline constexpr std::uint16_t kFirmwareCompatId = 108;

inline constexpr std::size_t kMaxVariableSize = 622;
inline constexpr std::size_t kMaxNameSize = 31;

inline constexpr pin_t kSensorPin = D0;
inline constexpr pin_t kLaserPin = D2;
inline constexpr pin_t kRelayPin = D3;
inline constexpr pin_t kPhotoResistorPin = A0;

inline constexpr std::uint8_t kPortAddress = 0x10;
inline constexpr std::uint8_t kPortRange = 0x10;
inline constexpr std::uint8_t kPortBufferSize = 0x20;
inline constexpr std::uint16_t kPortTimeoutMs = 30;

inline constexpr char kDefaultConfig[] =
    "rdt=1000|mtt=10000|rlt=300|rlp=1000|srr=3|srt=10|aev=0|aot=0|ans=0|ane=0|tzo=10112-7,20032-6|nme=Garage|mqtt=1|mqip=0.0.0.0|mqpt=1883|mqus=|mqpw=|mqto=5000";

inline constexpr std::uint32_t kVariablePublishIntervalMs = 1000;
inline constexpr std::uint32_t kTimeSyncIntervalMs = 24 * 60 * 60 * 1000;
inline constexpr std::uint8_t kSensorReadsPerSample = 3;
inline constexpr std::uint16_t kSensorBias = 100;
}  // namespace constants

enum class DoorStatus : std::uint8_t {
  Unknown = 0,
  None = 0,
  Closed,
  Open,
  Closing,
  Opening,
  Stopped,
  Init,
};

enum class MessageType : std::uint8_t {
  None = 0,
  Init,
  Command,
  Status,
  Config,
  Alert,
  Port,
};

struct Message {
  const char* source = nullptr;
  MessageType type = MessageType::None;
  const void* payload = nullptr;
};

bool handleMessage(const Message& message);

}  // namespace garadget

