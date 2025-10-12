#include "command.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace garadget {

int Command::execute(Reader&, Writer&) { return 0; }

jsmntok_t* JsonCommand::tokenise(char* json) {
  if (!json) {
    return nullptr;
  }

  jsmn_parser parser;
  parser.size = sizeof(parser);
  jsmn_init(&parser, nullptr);

  unsigned count = 64;
  jsmntok_t* tokens = static_cast<jsmntok_t*>(
      std::malloc(sizeof(jsmntok_t) * count));
  if (!tokens) {
    return nullptr;
  }

  int result = jsmn_parse(&parser, json, std::strlen(json), tokens, count, nullptr);
  while (result == JSMN_ERROR_NOMEM) {
    count = count * 2 + 1;
    jsmntok_t* previous = tokens;
    tokens = static_cast<jsmntok_t*>(
        std::realloc(tokens, sizeof(jsmntok_t) * count));
    if (!tokens) {
      std::free(previous);
      return nullptr;
    }
    result = jsmn_parse(&parser, json, std::strlen(json), tokens, count, nullptr);
  }

  return tokens;
}

bool JsonCommand::tokenEquals(const char* json, jsmntok_t* token,
                              const char* literal) {
  return std::strncmp(json + token->start, literal, token->end - token->start) == 0 &&
         std::strlen(literal) == static_cast<std::size_t>(token->end - token->start);
}

char* JsonCommand::tokenToString(char* json, jsmntok_t* token) {
  json[token->end] = '\0';
  return json + token->start;
}

void JsonCommand::writeResultCode(Writer& writer, int result) {
  writeChar(writer, '{');
  writeJsonInt(writer, "r", result);
  writeChar(writer, '}');
}

int JsonCommand::parseRequest(Reader&) { return 0; }

void JsonCommand::produceResponse(Writer& writer, int result) {
  writeResultCode(writer, result);
}

int JsonCommand::process() { return 0; }

void JsonCommand::writeChar(Writer& writer, char c) { writer.write(&c, 1); }

void JsonCommand::writeQuotedString(Writer& writer, const char* value) {
  writeChar(writer, '"');
  writer.write(value);
  writeChar(writer, '"');
}

void JsonCommand::writeJsonString(Writer& writer, const char* name,
                                  const char* value) {
  writeQuotedString(writer, name);
  writeChar(writer, ':');
  writeQuotedString(writer, value);
}

char* JsonCommand::toAscii(int value, char* buffer, int length) {
  buffer[--length] = 0;
  bool negative = value < 0;
  if (negative) {
    value = -value;
  }
  buffer[--length] = static_cast<char>((value % 10) + '0');
  value /= 10;
  for (; value && length; value /= 10) {
    buffer[--length] = static_cast<char>((value % 10) + '0');
  }
  if (negative) {
    buffer[--length] = '-';
  }
  return &buffer[length];
}

void JsonCommand::writeJsonInt(Writer& writer, const char* name, int value) {
  char buffer[20];
  writeQuotedString(writer, name);
  writeChar(writer, ':');
  writer.write(toAscii(value, buffer, sizeof(buffer)));
}

int JsonCommand::execute(Reader& reader, Writer& writer) {
  int result = parseRequest(reader);
  if (!result) {
    result = process();
    produceResponse(writer, result);
  }
  return result;
}

bool JsonRequestCommand::parsedValue(unsigned, jsmntok_t*, char*) { return false; }

int JsonRequestCommand::parseJsonString(char* json, const char* const keys[],
                                        const jsmntype_t types[],
                                        std::size_t count) {
  if (!json) {
    return -1;
  }

  int result = -1;
  jsmntok_t* tokens = tokenise(json);
  if (!tokens) {
    return result;
  }

  enum class ParseState { Start, Key, Value, Skip, Stop };
  ParseState state = ParseState::Start;
  jsmntype_t expectedType = JSMN_OBJECT;
  result = 0;
  int keyIndex = -1;

  for (std::size_t tokenIndex = 0, j = 1; j > 0; ++tokenIndex, --j) {
    jsmntok_t* token = &tokens[tokenIndex];

    if (token->type == JSMN_ARRAY || token->type == JSMN_OBJECT) {
      j += token->size * 2;
    }

    switch (state) {
      case ParseState::Start:
        state = ParseState::Key;
        break;

      case ParseState::Key:
        state = ParseState::Value;
        keyIndex = -1;
        for (std::size_t typeIndex = 0; typeIndex < count; ++typeIndex) {
          if (tokenEquals(json, token, keys[typeIndex])) {
            expectedType = types[typeIndex];
            keyIndex = static_cast<int>(typeIndex);
          }
        }
        if (keyIndex == -1) {
          Log.warn("JSON Unknown Key: %s", tokenToString(json, token));
          result = -1;
        }
        break;

      case ParseState::Value:
        if (keyIndex != -1) {
          if (token->type != expectedType) {
            result = -1;
            Log.error("JSON Type Mismatch");
          } else {
            char* str = tokenToString(json, token);
            if (!parsedValue(keyIndex, token, str)) {
              result = -1;
            }
          }
        }
        state = ParseState::Key;
        break;

      case ParseState::Skip:
      case ParseState::Stop:
      default:
        break;
    }
  }

  std::free(tokens);
  return result;
}

int JsonRequestCommand::parseJsonRequest(Reader& reader, const char* const keys[],
                                         const jsmntype_t types[],
                                         std::size_t count) {
  char* json = reader.fetch_as_string();
  const int result = parseJsonString(json, keys, types, count);
  std::free(json);
  return result;
}

SetConfigCommand::SetConfigCommand() : config_(Config::instance()) {}

int SetConfigCommand::execute(Reader& reader, Writer& writer) {
  constexpr std::size_t keyCount = sizeof(kKeys) / sizeof(kKeys[0]);
  const int result = parseJsonRequest(reader, kKeys, kTypes, keyCount);
  config_.save();
  produceResponse(writer, result);
  return result;
}

int SetConfigCommand::execute(char* configPayload) {
  constexpr std::size_t keyCount = sizeof(kKeys) / sizeof(kKeys[0]);
  const int result =
      parseJsonString(configPayload, kKeys, kTypes, keyCount);
  config_.save();
  return result;
}

bool SetConfigCommand::parsedValue(unsigned index, jsmntok_t*, char* value) {
  Log.info("JSON Key: %s, Value: %s", kKeys[index], value);
  const int8_t result = config_.setValue(String(kKeys[index]), String(value));
  return result != -1;
}

const char* SetConfigCommand::kKeys[13] = {"nme", "rdt", "mtt", "rlt", "rlp",
                                           "srr", "srt", "mqtt", "mqip", "mqpt",
                                           "mqus", "mqpw", "mqto"};

const jsmntype_t SetConfigCommand::kTypes[] = {
    JSMN_STRING,    JSMN_PRIMITIVE, JSMN_PRIMITIVE, JSMN_PRIMITIVE,
    JSMN_PRIMITIVE, JSMN_PRIMITIVE, JSMN_PRIMITIVE, JSMN_PRIMITIVE,
    JSMN_STRING,    JSMN_PRIMITIVE, JSMN_STRING,    JSMN_STRING,
    JSMN_PRIMITIVE};

}  // namespace garadget

