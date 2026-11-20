#pragma once

#include <cstdint>
#include <ctime>

namespace garadget {

// DST-aware timezone calculator supporting Garadget's `tzo` rule syntax:
//   - Fixed offset (no DST): "-7", "-7.00", "+10.00", "5.50", optional trailing region letter "-7.00A"
//   - DST rules (2 comma-separated rules: standard-time start, daylight-time start, optional 3rd disambiguator):
//       "<week><wday><MM><H><offset>"
//       week: 0=last, 1=first, 2=second, 3=third, 4=fourth
//       wday: 0=Sunday .. 6=Saturday
//       MM:   01..12
//       H:    0..9 (single digit hour, e.g. 2 for 2am)
//       offset: float hours from UTC (e.g. -7, -6.00, 10, 11)
//     Example (US Mountain): "10112-7.00,20032-6.00"
//     Example (Sydney, Australia - Southern Hemisphere): "10043+10.00,10102+11.00"
class Timezone {
public:
    struct DstRule {
        uint8_t weekNum = 0;   // 0=last, 1..4=nth
        uint8_t weekDay = 0;   // 0=Sun..6=Sat
        uint8_t month = 0;     // 0=disabled, 1..12
        uint8_t hour = 0;      // 0..23
        int16_t offsetMin = 0; // minutes from UTC (-780..+840)
    };

    Timezone() = default;

    // Parses and activates a timezone specification. Returns false if invalid.
    bool setConfig(const char* spec);

    // Computes the active UTC offset in minutes for a given UTC Unix timestamp.
    int16_t offsetMinutesForUtc(time_t utcNow) const;

    // Convenience: returns offset in hours as float (for Particle Time.zone(float)).
    float offsetHoursForUtc(time_t utcNow) const {
        return static_cast<float>(offsetMinutesForUtc(utcNow)) / 60.0f;
    }

    bool hasDst() const { return stdRule_.month != 0 && dstRule_.month != 0; }

private:
    static bool parseOffsetHours(const char* str, int16_t& outMinutes);
    static bool parseRule(const char* str, DstRule& outRule);
    static time_t computeRuleUtc(int year, const DstRule& rule, int16_t priorOffsetMin);
    static int daysInMonth(int year, int month);
    static int dayOfWeek(int year, int month, int day); // 0=Sun..6=Sat
    static time_t makeUtcTimestamp(int year, int month, int day, int hour);

    DstRule stdRule_;
    DstRule dstRule_;
};

} // namespace garadget
