#include "timezone.h"

#include <cstdlib>
#include <cstring>

namespace garadget {

bool Timezone::parseOffsetHours(const char* str, int16_t& outMinutes) {
    if (!str || str[0] == '\0') return false;
    char* endPtr = nullptr;
    double hours = std::strtod(str, &endPtr);
    if (endPtr == str) return false;
    // Allow optional single trailing disambiguation letter (e.g., "-7.00A")
    if (*endPtr != '\0') {
        char trailing = *endPtr;
        bool isLetter = (trailing >= 'A' && trailing <= 'Z') || (trailing >= 'a' && trailing <= 'z');
        if (!isLetter || endPtr[1] != '\0') {
            return false;
        }
    }
    int32_t mins = static_cast<int32_t>(hours >= 0.0 ? (hours * 60.0 + 0.5) : (hours * 60.0 - 0.5));
    if (mins < -780 || mins > 840) {
        return false;
    }
    outMinutes = static_cast<int16_t>(mins);
    return true;
}

bool Timezone::parseRule(const char* str, DstRule& outRule) {
    if (!str || std::strlen(str) < 6) return false;
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (!isDigit(str[0]) || !isDigit(str[1]) || !isDigit(str[2]) || !isDigit(str[3]) || !isDigit(str[4])) {
        return false;
    }

    uint8_t weekNum = static_cast<uint8_t>(str[0] - '0');
    uint8_t weekDay = static_cast<uint8_t>(str[1] - '0');
    uint8_t month   = static_cast<uint8_t>((str[2] - '0') * 10 + (str[3] - '0'));
    uint8_t hour    = static_cast<uint8_t>(str[4] - '0');

    if (weekNum > 4 || weekDay > 6 || month < 1 || month > 12) {
        return false;
    }

    int16_t offsetMin = 0;
    if (!parseOffsetHours(str + 5, offsetMin)) {
        return false;
    }

    outRule.weekNum = weekNum;
    outRule.weekDay = weekDay;
    outRule.month = month;
    outRule.hour = hour;
    outRule.offsetMin = offsetMin;
    return true;
}

bool Timezone::setConfig(const char* spec) {
    if (!spec || spec[0] == '\0') return false;

    const char* comma1 = std::strchr(spec, ',');
    if (!comma1) {
        int16_t offsetMin = 0;
        if (!parseOffsetHours(spec, offsetMin)) {
            return false;
        }
        stdRule_ = DstRule{};
        dstRule_ = DstRule{};
        stdRule_.offsetMin = offsetMin;
        return true;
    }

    size_t len1 = static_cast<size_t>(comma1 - spec);
    if (len1 == 0 || len1 >= 24) return false;
    char part1[24];
    std::memcpy(part1, spec, len1);
    part1[len1] = '\0';

    const char* p2Start = comma1 + 1;
    const char* comma2 = std::strchr(p2Start, ',');
    size_t len2 = comma2 ? static_cast<size_t>(comma2 - p2Start) : std::strlen(p2Start);
    if (len2 == 0 || len2 >= 24) return false;
    char part2[24];
    std::memcpy(part2, p2Start, len2);
    part2[len2] = '\0';

    DstRule parsedStd;
    DstRule parsedDst;
    if (!parseRule(part1, parsedStd) || !parseRule(part2, parsedDst)) {
        return false;
    }

    stdRule_ = parsedStd;
    dstRule_ = parsedDst;
    return true;
}

int Timezone::daysInMonth(int year, int month) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

// Sakamoto's algorithm: returns 0=Sunday .. 6=Saturday
int Timezone::dayOfWeek(int year, int month, int day) {
    static const int kT[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (year + year / 4 - year / 100 + year / 400 + kT[month - 1] + day) % 7;
}

// Civil date to Unix epoch seconds (Howard Hinnant's algorithm, no mktime TZ issues)
time_t Timezone::makeUtcTimestamp(int year, int month, int day, int hour) {
    int y = year - (month <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(day) - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    return static_cast<time_t>(days * 86400LL + static_cast<int64_t>(hour) * 3600LL);
}

time_t Timezone::computeRuleUtc(int year, const DstRule& rule, int16_t priorOffsetMin) {
    int day = 1;
    if (rule.weekNum == 0) {
        // Last occurrence of weekDay in month
        int lastDay = daysInMonth(year, rule.month);
        int lastDow = dayOfWeek(year, rule.month, lastDay);
        int diff = (lastDow - rule.weekDay + 7) % 7;
        day = lastDay - diff;
    } else {
        // Nth occurrence (1..4) of weekDay in month
        int firstDow = dayOfWeek(year, rule.month, 1);
        int diff = (rule.weekDay - firstDow + 7) % 7;
        day = 1 + diff + 7 * (rule.weekNum - 1);
        int maxDay = daysInMonth(year, rule.month);
        if (day > maxDay) {
            day -= 7;
        }
    }
    time_t localEpoch = makeUtcTimestamp(year, rule.month, day, rule.hour);
    return localEpoch - static_cast<time_t>(priorOffsetMin) * 60;
}

int16_t Timezone::offsetMinutesForUtc(time_t utcNow) const {
    if (!hasDst()) {
        return stdRule_.offsetMin;
    }

    // Extract UTC year from utcNow using civil-from-days
    int64_t z = (static_cast<int64_t>(utcNow) / 86400LL) + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = static_cast<unsigned>(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int year = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned m = mp + (mp < 10u ? 3u : -9u);
    year += (m <= 2u ? 1 : 0);

    // Transition from standard -> DST happens while clock is on standard offset
    time_t dstStartUtc = computeRuleUtc(year, dstRule_, stdRule_.offsetMin);
    // Transition from DST -> standard happens while clock is on DST offset
    time_t stdStartUtc = computeRuleUtc(year, stdRule_, dstRule_.offsetMin);

    if (dstStartUtc < stdStartUtc) {
        // Northern Hemisphere (e.g., March -> November): DST is active in [dstStartUtc, stdStartUtc)
        return (utcNow >= dstStartUtc && utcNow < stdStartUtc) ? dstRule_.offsetMin : stdRule_.offsetMin;
    } else {
        // Southern Hemisphere (e.g., October -> April): Standard is active in [stdStartUtc, dstStartUtc)
        return (utcNow >= stdStartUtc && utcNow < dstStartUtc) ? stdRule_.offsetMin : dstRule_.offsetMin;
    }
}

} // namespace garadget
