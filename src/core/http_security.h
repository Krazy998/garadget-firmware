#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace garadget {

// Constant-time equality comparison (returns true iff both buffers have identical length & bytes).
inline bool constantTimeEquals(const char* a, size_t aLen, const char* b, size_t bLen) {
    if (!a || !b || aLen != bLen) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < aLen; ++i) {
        diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
    }
    return diff == 0;
}

// Finds an HTTP header value strictly within the header block [0 .. headerLen).
// Never scans past the blank line separating headers from body.
// Writes the trimmed value length into outLen if non-null.
inline const char* findHttpHeaderValue(const char* headers, size_t headerLen,
                                       const char* headerName, size_t* outLen = nullptr) {
    if (!headers || !headerName || headerLen == 0) return nullptr;
    size_t nameLen = std::strlen(headerName);
    if (nameLen == 0) return nullptr;

    size_t pos = 0;
    while (pos < headerLen) {
        size_t lineEnd = pos;
        while (lineEnd < headerLen && headers[lineEnd] != '\n') {
            lineEnd++;
        }
        size_t contentEnd = lineEnd;
        if (contentEnd > pos && headers[contentEnd - 1] == '\r') {
            contentEnd--;
        }

        // Empty line marks end of HTTP headers
        if (contentEnd == pos) {
            break;
        }

        if (contentEnd - pos > nameLen && headers[pos + nameLen] == ':') {
            bool match = true;
            for (size_t i = 0; i < nameLen; ++i) {
                char c1 = headers[pos + i];
                char c2 = headerName[i];
                if (c1 >= 'A' && c1 <= 'Z') c1 = static_cast<char>(c1 + 32);
                if (c2 >= 'A' && c2 <= 'Z') c2 = static_cast<char>(c2 + 32);
                if (c1 != c2) {
                    match = false;
                    break;
                }
            }
            if (match) {
                size_t valStart = pos + nameLen + 1;
                while (valStart < contentEnd && (headers[valStart] == ' ' || headers[valStart] == '\t')) {
                    valStart++;
                }
                size_t valEnd = contentEnd;
                while (valEnd > valStart && (headers[valEnd - 1] == ' ' || headers[valEnd - 1] == '\t')) {
                    valEnd--;
                }
                if (outLen) *outLen = valEnd - valStart;
                return headers + valStart;
            }
        }

        pos = (lineEnd < headerLen) ? (lineEnd + 1) : headerLen;
    }
    return nullptr;
}

// Verifies X-Auth-Token header strictly against adminPass in constant time.
inline bool verifyAuthToken(const char* headers, size_t headerLen, const char* adminPass) {
    if (!adminPass || adminPass[0] == '\0') return true;
    size_t tokLen = 0;
    const char* tok = findHttpHeaderValue(headers, headerLen, "x-auth-token", &tokLen);
    if (!tok) return false;
    return constantTimeEquals(tok, tokLen, adminPass, std::strlen(adminPass));
}

// Validates same-origin & non-simple Content-Type on state-changing HTTP requests to block drive-by CSRF.
inline bool verifyCsrfProtection(const char* headers, size_t headerLen,
                                 const char* method, const char* path) {
    if (!method || std::strcmp(method, "GET") == 0 || std::strcmp(method, "HEAD") == 0) {
        return true;
    }

    // 1. If Origin header is present, its host must match Host header (blocks cross-origin browser requests)
    size_t originLen = 0;
    const char* origin = findHttpHeaderValue(headers, headerLen, "origin", &originLen);
    if (origin) {
        size_t hostLen = 0;
        const char* host = findHttpHeaderValue(headers, headerLen, "host", &hostLen);
        if (!host || hostLen == 0) return false;

        // Strip "http://" or "https://" prefix from Origin
        const char* originHost = origin;
        size_t originHostLen = originLen;
        if (originHostLen > 7 && std::strncmp(originHost, "http://", 7) == 0) {
            originHost += 7;
            originHostLen -= 7;
        } else if (originHostLen > 8 && std::strncmp(originHost, "https://", 8) == 0) {
            originHost += 8;
            originHostLen -= 8;
        } else {
            return false; // e.g., "Origin: null"
        }

        // Compare case-insensitively
        if (originHostLen != hostLen) return false;
        for (size_t i = 0; i < hostLen; ++i) {
            char c1 = originHost[i];
            char c2 = host[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 = static_cast<char>(c1 + 32);
            if (c2 >= 'A' && c2 <= 'Z') c2 = static_cast<char>(c2 + 32);
            if (c1 != c2) return false;
        }
    }

    // 2. Require a non-simple Content-Type (browsers cannot send these cross-origin without CORS preflight)
    size_t ctLen = 0;
    const char* ct = findHttpHeaderValue(headers, headerLen, "content-type", &ctLen);
    if (!ct || ctLen == 0) return false;

    auto startsWithCi = [](const char* s, size_t sLen, const char* prefix) -> bool {
        size_t pLen = std::strlen(prefix);
        if (sLen < pLen) return false;
        for (size_t i = 0; i < pLen; ++i) {
            char c1 = s[i];
            char c2 = prefix[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 = static_cast<char>(c1 + 32);
            if (c2 >= 'A' && c2 <= 'Z') c2 = static_cast<char>(c2 + 32);
            if (c1 != c2) return false;
        }
        return true;
    };

    if (path && std::strcmp(path, "/api/ota") == 0) {
        return startsWithCi(ct, ctLen, "application/octet-stream");
    }
    return startsWithCi(ct, ctLen, "application/json");
}

// Brute-force rate limiter for Web UI authentication (locks out for 10s after 5 consecutive failures).
class AuthRateLimiter {
public:
    static constexpr uint8_t  kMaxFailures = 5;
    static constexpr uint32_t kLockoutMs   = 10000u;

    bool isLockedOut(uint32_t nowMs) {
        if (!locked_) return false;
        if (static_cast<uint32_t>(nowMs - lockStartMs_) >= kLockoutMs) {
            locked_ = false;
            failures_ = 0;
            return false;
        }
        return true;
    }

    void recordFailure(uint32_t nowMs) {
        if (failures_ < kMaxFailures) {
            failures_++;
        }
        if (failures_ >= kMaxFailures) {
            locked_ = true;
            lockStartMs_ = nowMs;
        }
    }

    void recordSuccess() {
        failures_ = 0;
        locked_ = false;
    }

    uint8_t failures() const { return failures_; }

private:
    uint8_t failures_ = 0;
    bool locked_ = false;
    uint32_t lockStartMs_ = 0;
};

} // namespace garadget
