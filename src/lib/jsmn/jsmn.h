#ifndef JSMN_H
#define JSMN_H

#ifndef JSMN_PARENT_LINKS
#define JSMN_PARENT_LINKS
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * JSON type identifier. Basic types are:
 *  o Object
 *  o Array
 *  o String
 *  o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
  JSMN_UNDEFINED = 0,
  JSMN_OBJECT = 1,
  JSMN_ARRAY = 2,
  JSMN_STRING = 3,
  JSMN_PRIMITIVE = 4
} jsmntype_t;

enum jsmnerr {
  JSMN_ERROR_NOMEM = -1,
  JSMN_ERROR_INVAL = -2,
  JSMN_ERROR_PART = -3
};

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

void jsmn_init(jsmn_parser* parser);
int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens,
               unsigned int num_tokens);

#ifdef __cplusplus
}
#endif

#endif  // JSMN_H
