#pragma once

#include <cstddef>
#include <cstdint>

namespace garadget {

// Zero-heap, bounds-checked JSON writer targeting a caller-supplied char buffer.
class JsonWriter {
public:
    JsonWriter(char* buf, size_t cap);

    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    void key(const char* name);
    void keyString(const char* name, const char* value);
    void keyInt(const char* name, int32_t value);
    void keyUInt(const char* name, uint32_t value);
    void keyBool(const char* name, bool value);
    void keyObject(const char* name);
    void keyArray(const char* name);

    void stringValue(const char* value);
    void intValue(int32_t value);
    void uintValue(uint32_t value);
    void boolValue(bool value);

    size_t length() const { return len_; }
    bool truncated() const { return truncated_; }
    const char* c_str() const { return buf_; }

private:
    void appendChar(char c);
    void appendRaw(const char* s);
    void appendEscapedString(const char* s);
    void maybeComma();

    static constexpr int kMaxDepth = 8;
    char* buf_;
    size_t cap_;
    size_t len_ = 0;
    bool truncated_ = false;
    bool needComma_[kMaxDepth] = {};
    int depth_ = 0;
    bool expectingValue_ = false;
};

// Lightweight flat JSON object parser (supports strings with escapes, integers, booleans).
// Invokes callback(key, value, isString) for every top-level key/value pair.
using JsonFieldCallback = bool (*)(const char* key, const char* value, bool isString, void* ctx);

// Returns true if valid JSON object was parsed (callback returning false marks error but stops safely).
bool parseFlatJsonObject(const char* json, size_t len, JsonFieldCallback cb, void* ctx);

// Sanitizes a string in-place, removing control characters (<0x20 or >0x7E) and trimming to maxLen.
void sanitizeAscii(char* dst, size_t dstCap, const char* src);

} // namespace garadget
