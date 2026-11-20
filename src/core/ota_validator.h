#pragma once

#include <cstddef>
#include <cstdint>

namespace garadget {

// Streaming validator for Particle P1 (Platform ID 8) user-part firmware binaries.
// Verifies module header (flash range, platform ID, module function) and IEEE 802.3 CRC32
// before committing an OTA update to the bootloader.
class OtaValidator {
public:
    static constexpr uint32_t kP1UserStartAddr = 0x080A0000u;
    static constexpr uint32_t kP1UserEndLimit  = 0x080C0000u;
    static constexpr uint16_t kP1PlatformId    = 8u;
    static constexpr uint8_t  kModuleFuncUser  = 5u;
    static constexpr size_t   kMinBinarySize   = 64u;
    static constexpr size_t   kMaxBinarySize   = 131072u; // 128 KB P1 user slot

    void reset(size_t totalLength) {
        totalLen_ = totalLength;
        processed_ = 0;
        crc_ = 0xFFFFFFFFu;
        footerLen_ = 0;
    }

    // Validates the 24-byte Particle module info prefix at offset 0.
    // Returns nullptr on success, or a short error code string on failure.
    static const char* validateHeader(const uint8_t hdr[24], size_t totalLength) {
        if (totalLength < kMinBinarySize || totalLength > kMaxBinarySize) {
            return "invalid_size";
        }
        auto rd32le = [](const uint8_t* p) -> uint32_t {
            return static_cast<uint32_t>(p[0]) |
                   (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24);
        };
        auto rd16le = [](const uint8_t* p) -> uint16_t {
            return static_cast<uint16_t>(p[0]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
        };

        uint32_t startAddr = rd32le(hdr + 0);
        uint32_t endAddr   = rd32le(hdr + 4);
        uint16_t platId    = rd16le(hdr + 12);
        uint8_t  modFunc   = hdr[14];

        if (startAddr != kP1UserStartAddr) {
            return "invalid_start_addr";
        }
        if (endAddr <= startAddr || endAddr > kP1UserEndLimit) {
            return "invalid_end_addr";
        }
        if (platId != kP1PlatformId) {
            return "wrong_platform";
        }
        if (modFunc != kModuleFuncUser) {
            return "not_user_firmware";
        }
        return nullptr;
    }

    // Feeds streaming binary bytes; updates CRC32 over [0 .. totalLen-4) and captures trailing 4-byte CRC.
    void feed(const uint8_t* data, size_t len) {
        if (!data || totalLen_ < 4) return;
        size_t crcEnd = totalLen_ - 4;
        for (size_t i = 0; i < len && processed_ < totalLen_; ++i, ++processed_) {
            uint8_t b = data[i];
            if (processed_ < crcEnd) {
                crc_ ^= b;
                for (int bit = 0; bit < 8; ++bit) {
                    uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc_ & 1u));
                    crc_ = (crc_ >> 1) ^ (0xEDB88320u & mask);
                }
            } else if (footerLen_ < 4) {
                footer_[footerLen_++] = b;
            }
        }
    }

    bool verifyCrc() const {
        if (processed_ != totalLen_ || footerLen_ != 4) return false;
        uint32_t stored = (static_cast<uint32_t>(footer_[0]) << 24) |
                          (static_cast<uint32_t>(footer_[1]) << 16) |
                          (static_cast<uint32_t>(footer_[2]) << 8) |
                          static_cast<uint32_t>(footer_[3]);
        return (~crc_) == stored;
    }

private:
    size_t totalLen_ = 0;
    size_t processed_ = 0;
    uint32_t crc_ = 0xFFFFFFFFu;
    uint8_t footer_[4]{};
    size_t footerLen_ = 0;
};

} // namespace garadget
