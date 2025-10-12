#pragma once

#include "Particle.h"
#include "config.h"
#include "softap_http.h"

#include <cstddef>

#include "lib/jsmn/jsmn.h"

namespace garadget {

class Command {
 public:
  virtual ~Command() = default;
  virtual int execute(Reader& reader, Writer& writer);
};

class JsonCommand : public Command {
 protected:
  jsmntok_t* tokenise(char* json);
  bool tokenEquals(const char* json, jsmntok_t* token, const char* literal);
  char* tokenToString(char* json, jsmntok_t* token);
  void writeResultCode(Writer& writer, int result);
  virtual int parseRequest(Reader& reader);
  virtual void produceResponse(Writer& writer, int result);
  virtual int process();
  void writeChar(Writer& writer, char c);
  void writeQuotedString(Writer& writer, const char* value);
  void writeJsonString(Writer& writer, const char* name, const char* value);
  char* toAscii(int value, char* buffer, int length);
  void writeJsonInt(Writer& writer, const char* name, int value);

 public:
  int execute(Reader& reader, Writer& writer) override;
};

class JsonRequestCommand : public JsonCommand {
 protected:
  virtual bool parsedValue(unsigned index, jsmntok_t* token, char* value);
  int parseJsonString(char* json, const char* const keys[],
                      const jsmntype_t types[], std::size_t count);
  int parseJsonRequest(Reader& reader, const char* const keys[],
                       const jsmntype_t types[], std::size_t count);
};

class SetConfigCommand : public JsonRequestCommand {
 public:
  int execute(Reader& reader, Writer& writer) override;
  int execute(char* configPayload);

 protected:
  bool parsedValue(unsigned index, jsmntok_t* token, char* value) override;

 private:
  static const char* kKeys[13];
  static const jsmntype_t kTypes[];
  Config& config_;

 public:
  SetConfigCommand();
};

}  // namespace garadget

