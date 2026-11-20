#include "json_utils.h"

#include <cstdio>
#include <cstring>

namespace garadget {

JsonWriter::JsonWriter(char* buf, size_t cap) : buf_(buf), cap_(cap) {
    if (buf_ && cap_ > 0) {
        buf_[0] = '\0';
    }
}

void JsonWriter::appendChar(char c) {
    if (!buf_ || cap_ == 0) {
        truncated_ = true;
        return;
    }
    if (len_ + 1 < cap_) {
        buf_[len_++] = c;
        buf_[len_] = '\0';
    } else {
        truncated_ = true;
    }
}

void JsonWriter::appendRaw(const char* s) {
    if (!s) return;
    while (*s) {
        appendChar(*s++);
    }
}

void JsonWriter::appendEscapedString(const char* s) {
    appendChar('"');
    if (s) {
        while (*s) {
            unsigned char c = static_cast<unsigned char>(*s++);
            switch (c) {
                case '"':  appendRaw("\\\""); break;
                case '\\': appendRaw("\\\\"); break;
                case '\b': appendRaw("\\b");  break;
                case '\f': appendRaw("\\f");  break;
                case '\n': appendRaw("\\n");  break;
                case '\r': appendRaw("\\r");  break;
                case '\t': appendRaw("\\t");  break;
                default:
                    if (c < 0x20) {
                        char esc[7];
                        std::snprintf(esc, sizeof(esc), "\\u%04x", c);
                        appendRaw(esc);
                    } else {
                        appendChar(static_cast<char>(c));
                    }
                    break;
            }
        }
    }
    appendChar('"');
}

void JsonWriter::maybeComma() {
    if (expectingValue_) {
        expectingValue_ = false;
        return;
    }
    if (depth_ > 0 && depth_ <= kMaxDepth) {
        if (needComma_[depth_ - 1]) {
            appendChar(',');
        }
        needComma_[depth_ - 1] = true;
    }
}

void JsonWriter::beginObject() {
    maybeComma();
    appendChar('{');
    if (depth_ < kMaxDepth) {
        needComma_[depth_] = false;
    }
    depth_++;
}

void JsonWriter::endObject() {
    if (depth_ > 0) depth_--;
    appendChar('}');
}

void JsonWriter::beginArray() {
    maybeComma();
    appendChar('[');
    if (depth_ < kMaxDepth) {
        needComma_[depth_] = false;
    }
    depth_++;
}

void JsonWriter::endArray() {
    if (depth_ > 0) depth_--;
    appendChar(']');
}

void JsonWriter::key(const char* name) {
    maybeComma();
    appendEscapedString(name);
    appendChar(':');
    expectingValue_ = true;
}

void JsonWriter::keyString(const char* name, const char* value) {
    key(name);
    stringValue(value);
}

void JsonWriter::keyInt(const char* name, int32_t value) {
    key(name);
    intValue(value);
}

void JsonWriter::keyUInt(const char* name, uint32_t value) {
    key(name);
    uintValue(value);
}

void JsonWriter::keyBool(const char* name, bool value) {
    key(name);
    boolValue(value);
}

void JsonWriter::keyObject(const char* name) {
    key(name);
    beginObject();
}

void JsonWriter::keyArray(const char* name) {
    key(name);
    beginArray();
}

void JsonWriter::stringValue(const char* value) {
    maybeComma();
    appendEscapedString(value);
}

void JsonWriter::intValue(int32_t value) {
    maybeComma();
    char num[16];
    std::snprintf(num, sizeof(num), "%ld", static_cast<long>(value));
    appendRaw(num);
}

void JsonWriter::uintValue(uint32_t value) {
    maybeComma();
    char num[16];
    std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(value));
    appendRaw(num);
}

void JsonWriter::boolValue(bool value) {
    maybeComma();
    appendRaw(value ? "true" : "false");
}

namespace {

void skipWs(const char* s, size_t len, size_t& pos) {
    while (pos < len) {
        char c = s[pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            pos++;
        } else {
            break;
        }
    }
}

bool parseJsonString(const char* s, size_t len, size_t& pos, char* out, size_t outCap) {
    if (pos >= len || s[pos] != '"') return false;
    pos++; // skip opening quote
    size_t outLen = 0;
    while (pos < len) {
        char c = s[pos++];
        if (c == '"') {
            if (outCap > 0) out[outLen] = '\0';
            return true;
        }
        if (c == '\\') {
            if (pos >= len) return false;
            char esc = s[pos++];
            switch (esc) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u':
                    // Skip 4 hex digits safely
                    if (pos + 4 > len) return false;
                    pos += 4;
                    continue;
                default:
                    return false;
            }
        }
        if (outLen + 1 < outCap) {
            out[outLen++] = c;
        }
    }
    return false;
}

bool skipNestedValue(const char* s, size_t len, size_t& pos) {
    if (pos >= len) return false;
    char open = s[pos];
    char close = (open == '{') ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    while (pos < len) {
        char c = s[pos++];
        if (inStr) {
            if (c == '\\') {
                if (pos < len) pos++;
            } else if (c == '"') {
                inStr = false;
            }
        } else {
            if (c == '"') {
                inStr = true;
            } else if (c == open) {
                depth++;
            } else if (c == close) {
                depth--;
                if (depth == 0) return true;
            }
        }
    }
    return false;
}

} // namespace

bool parseFlatJsonObject(const char* json, size_t len, JsonFieldCallback cb, void* ctx) {
    if (!json || !cb) return false;
    size_t pos = 0;
    skipWs(json, len, pos);
    if (pos >= len || json[pos] != '{') return false;
    pos++;

    skipWs(json, len, pos);
    if (pos < len && json[pos] == '}') {
        return true; // empty object
    }

    char keyBuf[40];
    char valBuf[128];

    while (pos < len) {
        skipWs(json, len, pos);
        if (!parseJsonString(json, len, pos, keyBuf, sizeof(keyBuf))) {
            return false;
        }

        skipWs(json, len, pos);
        if (pos >= len || json[pos] != ':') return false;
        pos++;
        skipWs(json, len, pos);
        if (pos >= len) return false;

        bool isStr = false;
        if (json[pos] == '"') {
            isStr = true;
            if (!parseJsonString(json, len, pos, valBuf, sizeof(valBuf))) {
                return false;
            }
            if (!cb(keyBuf, valBuf, isStr, ctx)) {
                return false;
            }
        } else if (json[pos] == '{' || json[pos] == '[') {
            if (!skipNestedValue(json, len, pos)) return false;
        } else {
            size_t start = pos;
            while (pos < len) {
                char c = json[pos];
                if (c == ',' || c == '}' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                    break;
                }
                pos++;
            }
            size_t vlen = pos - start;
            if (vlen == 0) return false;
            if (vlen >= sizeof(valBuf)) vlen = sizeof(valBuf) - 1;
            std::memcpy(valBuf, json + start, vlen);
            valBuf[vlen] = '\0';
            if (!cb(keyBuf, valBuf, false, ctx)) {
                return false;
            }
        }

        skipWs(json, len, pos);
        if (pos >= len) return false;
        if (json[pos] == '}') {
            return true;
        }
        if (json[pos] != ',') {
            return false;
        }
        pos++;
    }
    return false;
}

void sanitizeAscii(char* dst, size_t dstCap, const char* src) {
    if (!dst || dstCap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dstCap; ++i) {
        unsigned char c = static_cast<unsigned char>(src[i]);
        if (c >= 0x20 && c <= 0x7E) {
            dst[out++] = static_cast<char>(c);
        }
    }
    dst[out] = '\0';
}

} // namespace garadget
