#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace garadget {

enum class DoorStatus : uint8_t {
    Unknown = 0,
    Closed  = 1,
    Open    = 2,
    Closing = 3,
    Opening = 4,
    Stopped = 5,
    Init    = 6,
};

enum class DoorCommand : uint8_t {
    None   = 0,
    Open   = 1,
    Close  = 2,
    Stop   = 3,
    Toggle = 4,
};

enum class SensorMode : uint8_t {
    Laser    = 0, // Optical laser + phototransistor (default Garadget hardware)
    SwitchNO = 1, // Dry-contact Normally Open switch on D0 (closed to GND when door is closed)
    SwitchNC = 2, // Dry-contact Normally Closed switch on D0 (open when door is closed)
};

// Alert event bitmap bits (backwards compatible with Garadget `aev` parameter)
enum AlertBit : uint16_t {
    kAlertClosed  = 1u << 0,
    kAlertOpen    = 1u << 1,
    kAlertClosing = 1u << 2,
    kAlertOpening = 1u << 3,
    kAlertStopped = 1u << 4,
    kAlertInit    = 1u << 5,
    kAlertOnline  = 1u << 6,
    kAlertOffline = 1u << 7,
    kAlertConfig  = 1u << 8,
};

struct AlertRecord {
    char type[16];
    char data[48];
    uint32_t uptimeSec;
};

inline const char* doorStatusToString(DoorStatus status) {
    switch (status) {
        case DoorStatus::Closed:  return "closed";
        case DoorStatus::Open:    return "open";
        case DoorStatus::Closing: return "closing";
        case DoorStatus::Opening: return "opening";
        case DoorStatus::Stopped: return "stopped";
        case DoorStatus::Init:    return "initialized";
        default:                  return "unknown";
    }
}

inline DoorCommand parseDoorCommand(const char* text) {
    if (!text || text[0] == '\0') {
        return DoorCommand::None;
    }
    // Case-insensitive prefix/exact matching for compatibility with Garadget & Home Assistant
    auto eqIgnoreCase = [](const char* a, const char* b) -> bool {
        while (*a && *b) {
            char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
            char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
            if (ca != cb) return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    };
    auto startsIgnoreCase = [](const char* str, const char* prefix) -> bool {
        while (*prefix) {
            if (*str == '\0') return false;
            char ca = (*str >= 'A' && *str <= 'Z') ? static_cast<char>(*str + 32) : *str;
            char cb = (*prefix >= 'A' && *prefix <= 'Z') ? static_cast<char>(*prefix + 32) : *prefix;
            if (ca != cb) return false;
            ++str;
            ++prefix;
        }
        return true;
    };

    if (startsIgnoreCase(text, "close")) return DoorCommand::Close;
    if (eqIgnoreCase(text, "open") || eqIgnoreCase(text, "opening")) return DoorCommand::Open;
    if (startsIgnoreCase(text, "stop")) return DoorCommand::Stop;
    if (eqIgnoreCase(text, "toggle")) return DoorCommand::Toggle;
    return DoorCommand::None;
}

inline const char* sensorModeToString(SensorMode mode) {
    switch (mode) {
        case SensorMode::Laser:    return "laser";
        case SensorMode::SwitchNO: return "switch_no";
        case SensorMode::SwitchNC: return "switch_nc";
        default:                   return "laser";
    }
}

// Formats milliseconds into compact Garadget duration string: 0-119s, 2-119m, 2-47h, 2+d
inline void formatDuration(uint32_t elapsedMs, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    uint32_t value = elapsedMs / 1000u;
    char unit = 's';
    if (value >= 120u) {
        unit = 'm';
        value /= 60u;
        if (value >= 120u) {
            unit = 'h';
            value /= 60u;
            if (value >= 48u) {
                unit = 'd';
                value /= 24u;
            }
        }
    }
    std::snprintf(out, outSize, "%lu%c", static_cast<unsigned long>(value), unit);
}

inline uint16_t statusToAlertBit(DoorStatus status) {
    switch (status) {
        case DoorStatus::Closed:  return kAlertClosed;
        case DoorStatus::Open:    return kAlertOpen;
        case DoorStatus::Closing: return kAlertClosing;
        case DoorStatus::Opening: return kAlertOpening;
        case DoorStatus::Stopped: return kAlertStopped;
        case DoorStatus::Init:    return kAlertInit;
        default:                  return 0;
    }
}

} // namespace garadget
