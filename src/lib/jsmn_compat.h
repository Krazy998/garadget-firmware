#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#if defined(__has_include)
#if __has_include(<jsmn.h>)
#include <jsmn.h>
#define GARADGET_HAS_PLATFORM_JSMN 1
#endif
#endif

#ifndef GARADGET_HAS_PLATFORM_JSMN

typedef enum {
  JSMN_UNDEFINED = 0,
  JSMN_OBJECT = 1,
  JSMN_ARRAY = 2,
  JSMN_STRING = 3,
  JSMN_PRIMITIVE = 4
} jsmntype_t;

typedef enum {
  JSMN_ERROR_NOMEM = -1,
  JSMN_ERROR_INVAL = -2,
  JSMN_ERROR_PART = -3
} jsmnerr_t;

typedef struct {
  jsmntype_t type;
  int start;
  int end;
  int size;
#ifdef JSMN_PARENT_LINKS
  int parent;
#endif
} jsmntok_t;

typedef struct {
  unsigned int pos;
  unsigned int toknext;
  int toksuper;
} jsmn_parser;

void jsmn_init(jsmn_parser* parser, void* reserved);
int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens,
               unsigned int num_tokens, void* reserved);
int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens,
               unsigned int num_tokens);

#endif  // GARADGET_HAS_PLATFORM_JSMN

#ifdef __cplusplus
}  // extern "C"
#endif

#undef GARADGET_HAS_PLATFORM_JSMN
