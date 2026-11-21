#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "http_security.h"

namespace garadget {

// Pure, zero-heap incremental HTTP/1.1 request parser shared between WebServer (P1) and host unit tests.
// Enforces header buffer limits (431), non-OTA JSON body limits (413), body timeouts (408), and
// stops immediately at \r\n\r\n for streaming POST /api/ota uploads without consuming binary body bytes.
class HttpRequestParser {
public:
    static constexpr size_t kBufSize     = 2560;
    static constexpr size_t kMaxJsonBody = 1280;

    enum class Status : uint8_t {
        Incomplete = 0,
        Ready,
        BadRequest,       // 400
        BodyTimeout,      // 408
        PayloadTooLarge,  // 413
        HeadersTooLarge,  // 431
    };

    void reset() {
        len_ = 0;
        headerEnd_ = 0;
        contentLen_ = 0;
        isOtaStream_ = false;
        status_ = Status::Incomplete;
        buf_[0] = '\0';
        method_[0] = '\0';
        path_[0] = '\0';
    }

    // Feeds one byte from the TCP stream. Returns true while more bytes should be read.
    bool feedByte(uint8_t byte) {
        if (status_ != Status::Incomplete) {
            return false;
        }
        if (len_ + 1 >= kBufSize) {
            status_ = (headerEnd_ == 0) ? Status::HeadersTooLarge : Status::PayloadTooLarge;
            return false;
        }

        buf_[len_++] = static_cast<char>(byte);
        buf_[len_] = '\0';

        if (headerEnd_ == 0 && len_ >= 4 &&
            buf_[len_ - 4] == '\r' && buf_[len_ - 3] == '\n' &&
            buf_[len_ - 2] == '\r' && buf_[len_ - 1] == '\n') {
            headerEnd_ = len_;
            if (!parseRequestLine()) {
                status_ = Status::BadRequest;
                return false;
            }

            size_t clLen = 0;
            const char* cl = findHttpHeaderValue(buf_, headerEnd_, "content-length", &clLen);
            if (cl) {
                if (!parseContentLength(cl, clLen, contentLen_)) {
                    status_ = Status::BadRequest;
                    return false;
                }
            }

            if (std::strcmp(method_, "POST") == 0 && std::strcmp(path_, "/api/ota") == 0) {
                isOtaStream_ = true;
                status_ = Status::Ready;
                return false; // Stop reading: OTA binary body is streamed directly from socket
            }

            if (contentLen_ > kMaxJsonBody || headerEnd_ + contentLen_ >= kBufSize) {
                status_ = Status::PayloadTooLarge;
                return false;
            }
        }

        if (headerEnd_ > 0 && len_ >= headerEnd_ + contentLen_) {
            status_ = Status::Ready;
            return false;
        }
        return true;
    }

    // Call when read loop ends (timeout or EOF) before Ready/Error to classify final status.
    Status finish() {
        if (status_ != Status::Incomplete) {
            return status_;
        }
        if (headerEnd_ == 0) {
            status_ = (len_ + 1 >= kBufSize) ? Status::HeadersTooLarge : Status::BadRequest;
            return status_;
        }
        if (!isOtaStream_ && contentLen_ > 0 && bodyLen() < contentLen_) {
            status_ = Status::BodyTimeout;
            return status_;
        }
        status_ = Status::Ready;
        return status_;
    }

    Status status() const { return status_; }
    size_t rawLen() const { return len_; }
    const char* rawHeaders() const { return buf_; }
    size_t headerLen() const { return headerEnd_; }
    size_t contentLength() const { return contentLen_; }
    bool isOtaStream() const { return isOtaStream_; }
    const char* method() const { return method_; }
    const char* path() const { return path_; }

    const char* body() const {
        return (headerEnd_ > 0 && len_ > headerEnd_) ? (buf_ + headerEnd_) : "";
    }
    size_t bodyLen() const {
        return (headerEnd_ > 0 && len_ > headerEnd_) ? (len_ - headerEnd_) : 0;
    }

private:
    bool parseRequestLine() {
        const char* lineEnd = std::strstr(buf_, "\r\n");
        if (!lineEnd) return false;
        const char* sp1 = static_cast<const char*>(std::memchr(buf_, ' ', static_cast<size_t>(lineEnd - buf_)));
        if (!sp1 || sp1 == buf_) return false;

        size_t mlen = static_cast<size_t>(sp1 - buf_);
        if (mlen >= sizeof(method_)) return false;
        std::memcpy(method_, buf_, mlen);
        method_[mlen] = '\0';

        const char* pStart = sp1 + 1;
        if (pStart >= lineEnd || *pStart != '/') return false;
        const char* sp2 = static_cast<const char*>(std::memchr(pStart, ' ', static_cast<size_t>(lineEnd - pStart)));
        if (!sp2 || sp2 == pStart) return false;

        const char* qm = static_cast<const char*>(std::memchr(pStart, '?', static_cast<size_t>(sp2 - pStart)));
        const char* pEnd = qm ? qm : sp2;
        size_t plen = static_cast<size_t>(pEnd - pStart);
        if (plen == 0 || plen >= sizeof(path_)) return false;
        std::memcpy(path_, pStart, plen);
        path_[plen] = '\0';
        return true;
    }

    static bool parseContentLength(const char* s, size_t len, size_t& out) {
        if (!s || len == 0) return false;
        uint64_t val = 0;
        for (size_t i = 0; i < len; ++i) {
            char c = s[i];
            if (c < '0' || c > '9') return false;
            val = val * 10u + static_cast<uint64_t>(c - '0');
            if (val > 0x1000000ULL) return false; // 16 MB sanity cap
        }
        out = static_cast<size_t>(val);
        return true;
    }

    char buf_[kBufSize]{};
    char method_[8]{};
    char path_[64]{};
    size_t len_ = 0;
    size_t headerEnd_ = 0;
    size_t contentLen_ = 0;
    bool isOtaStream_ = false;
    Status status_ = Status::Incomplete;
};

} // namespace garadget
