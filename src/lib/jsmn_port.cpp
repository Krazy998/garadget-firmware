#include <cstddef>
#include <cstdint>

extern "C" {
#include "jsmn_compat.h"
}

extern "C" {

static jsmntok_t* jsmn_alloc_token(jsmn_parser* parser, jsmntok_t* tokens,
                                   unsigned int num_tokens) {
  if (!parser || !tokens || parser->toknext >= num_tokens) {
    return nullptr;
  }
  jsmntok_t* tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
#ifdef JSMN_PARENT_LINKS
  tok->parent = -1;
#endif
  return tok;
}

static void jsmn_fill_token(jsmntok_t* token, jsmntype_t type, int start, int end) {
  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser* parser, const char* js, std::size_t len,
                                jsmntok_t* tokens, unsigned int num_tokens) {
  int start = parser->pos;
  const unsigned int limit = static_cast<unsigned int>(len);
  for (; parser->pos < limit; parser->pos++) {
    char c = js[parser->pos];
    switch (c) {
      case '\t':
      case '\r':
      case '\n':
      case ' ': 
      case ',':
      case ']':
      case '}': {
        jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!token) {
          parser->pos = start;
          return JSMN_ERROR_NOMEM;
        }
        jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
#ifdef JSMN_PARENT_LINKS
        token->parent = parser->toksuper;
#endif
        parser->pos--;
        return 0;
      }
      default:
        if (c < 32 || c >= 127) {
          parser->pos = start;
          return JSMN_ERROR_INVAL;
        }
    }
  }
  parser->pos = start;
  return JSMN_ERROR_PART;
}

static int jsmn_parse_string(jsmn_parser* parser, const char* js, std::size_t len,
                             jsmntok_t* tokens, unsigned int num_tokens) {
  int start = parser->pos;
  parser->pos++;

  const unsigned int limit = static_cast<unsigned int>(len);
  for (; parser->pos < limit; parser->pos++) {
    char c = js[parser->pos];

    if (c == '"') {
      jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
      if (!token) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
      }
      jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
#ifdef JSMN_PARENT_LINKS
      token->parent = parser->toksuper;
#endif
      return 0;
    }

    if (c == '\\' && parser->pos + 1 < limit) {
      parser->pos++;
      switch (js[parser->pos]) {
        case '"':
        case '/':
        case '\\':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
          break;
        case 'u':
          parser->pos += 4;
          break;
        default:
          parser->pos = start;
          return JSMN_ERROR_INVAL;
      }
    }
  }
  parser->pos = start;
  return JSMN_ERROR_PART;
}

__attribute__((weak)) void jsmn_init(jsmn_parser* parser, void* reserved) {
  (void)reserved;
  if (!parser) {
    return;
  }
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

static int jsmn_parse_internal(jsmn_parser* parser, const char* js, std::size_t len,
                               jsmntok_t* tokens, unsigned int num_tokens) {
  int r;
  int i;
  jsmntok_t* token;

  const unsigned int limit = static_cast<unsigned int>(len);
  for (; parser->pos < limit; parser->pos++) {
    char c = js[parser->pos];
    switch (c) {
      case '{':
      case '[':
        parser->toksuper = parser->toknext;
        token = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!token) {
          return JSMN_ERROR_NOMEM;
        }
        token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
        token->start = parser->pos;
#ifdef JSMN_PARENT_LINKS
        token->parent = parser->toksuper;
#endif
        continue;

      case '}':
      case ']':
        token = nullptr;
        for (i = parser->toknext - 1; i >= 0; i--) {
          token = &tokens[i];
          if (token->start != -1 && token->end == -1) {
            if ((token->type == JSMN_OBJECT && c == '}') ||
                (token->type == JSMN_ARRAY && c == ']')) {
              token->end = parser->pos + 1;
#ifdef JSMN_PARENT_LINKS
              parser->toksuper = token->parent;
#else
              parser->toksuper = -1;
#endif
              break;
            }
            return JSMN_ERROR_INVAL;
          }
        }
        if (!token) {
          return JSMN_ERROR_INVAL;
        }
#ifndef JSMN_PARENT_LINKS
        for (i = parser->toknext - 1; i >= 0; i--) {
          token = &tokens[i];
          if (token->start != -1 && token->end == -1) {
            parser->toksuper = i;
            break;
          }
        }
#endif
        continue;

      case '"':
        r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        token = &tokens[parser->toknext - 1];
        if (parser->toksuper != -1 && tokens[parser->toksuper].type == JSMN_OBJECT &&
            token->start != -1 && token->end == -1) {
#ifdef JSMN_PARENT_LINKS
          tokens[parser->toknext - 1].parent = parser->toksuper;
#else
          parser->toksuper = parser->toknext - 1;
#endif
        }
        break;

      case '\t':
      case '\r':
      case '\n':
      case ' ': {
        break;
      }

      case ':':
        parser->toksuper = parser->toknext - 1;
        break;

      case ',': {
        if (parser->toksuper != -1 && tokens[parser->toksuper].type != JSMN_ARRAY) {
#ifdef JSMN_PARENT_LINKS
          parser->toksuper = tokens[parser->toksuper].parent;
#else
          parser->toksuper = -1;
          for (i = parser->toknext - 1; i >= 0; --i) {
            if (tokens[i].start != -1 && tokens[i].end == -1) {
              parser->toksuper = i;
              break;
            }
          }
#endif
        }
        break;
      }

      default: {
        r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (r < 0) {
          return r;
        }
        break;
      }
    }
  }

  for (i = parser->toknext - 1; i >= 0; i--) {
    if (tokens[i].start != -1 && tokens[i].end == -1) {
      return JSMN_ERROR_PART;
    }
  }
  return parser->toknext;
}

__attribute__((weak)) int jsmn_parse(jsmn_parser* parser, const char* js, std::size_t len,
                                     jsmntok_t* tokens, unsigned int num_tokens,
                                     void* reserved) {
  (void)reserved;
  return jsmn_parse_internal(parser, js, len, tokens, num_tokens);
}

}

