#include "jsmn.h"

static jsmntok_t* jsmn_alloc_token(jsmn_parser* parser, jsmntok_t* tokens,
                                   unsigned int num_tokens) {
  if (parser->toknext >= num_tokens) {
    return NULL;
  }
  jsmntok_t* tok = &tokens[parser->toknext++];
  tok->start = tok->end = -1;
  tok->size = 0;
  tok->type = JSMN_UNDEFINED;
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

static int jsmn_parse_primitive(jsmn_parser* parser, const char* js, size_t len,
                                jsmntok_t* tokens, unsigned int num_tokens) {
  int start = parser->pos;
  for (; parser->pos < len; parser->pos++) {
    switch (js[parser->pos]) {
      case ' ': case '\t': case '\r': case '\n': case ',': case ']': case '}':
        goto found;
      default:
        if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
          parser->pos = start;
          return JSMN_ERROR_INVAL;
        }
        break;
    }
  }
found:
  if (tokens == NULL) {
    parser->pos--;
    return 0;
  }
  jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
  if (token == NULL) {
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

static int jsmn_parse_string(jsmn_parser* parser, const char* js, size_t len,
                             jsmntok_t* tokens, unsigned int num_tokens) {
  int start = parser->pos;
  parser->pos++;
  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];

    if (c == '"') {
      if (tokens == NULL) {
        return 0;
      }
      jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
      if (token == NULL) {
        parser->pos = start;
        return JSMN_ERROR_NOMEM;
      }
      jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
#ifdef JSMN_PARENT_LINKS
      token->parent = parser->toksuper;
#endif
      return 0;
    }

    if (c == '\\' && parser->pos + 1 < len) {
      int i;
      parser->pos++;
      switch (js[parser->pos]) {
        case '"':
        case '/':
        case '\\':
        case 'b':
        case 'f':
        case 'r':
        case 'n':
        case 't':
          break;
        case 'u':
          for (i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++) {
            parser->pos++;
            if (!((js[parser->pos] >= '0' && js[parser->pos] <= '9') ||
                  (js[parser->pos] >= 'A' && js[parser->pos] <= 'F') ||
                  (js[parser->pos] >= 'a' && js[parser->pos] <= 'f'))) {
              parser->pos = start;
              return JSMN_ERROR_INVAL;
            }
          }
          parser->pos--;
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

int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens,
               unsigned int num_tokens) {
  int count = parser->toknext;

  for (; parser->pos < len; parser->pos++) {
    char c = js[parser->pos];
    jsmntok_t* token;
    int result;

    switch (c) {
      case '{':
      case '[':
        count++;
        if (tokens == NULL) {
          break;
        }
        token = jsmn_alloc_token(parser, tokens, num_tokens);
        if (token == NULL) {
          return JSMN_ERROR_NOMEM;
        }
        if (parser->toksuper != -1) {
          tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
          token->parent = parser->toksuper;
#endif
        }
        token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
        token->start = parser->pos;
        parser->toksuper = parser->toknext - 1;
        break;

      case '}':
      case ']':
        if (tokens == NULL) {
          break;
        }
        for (int i = parser->toknext - 1; i >= 0; i--) {
          token = &tokens[i];
          if (token->start != -1 && token->end == -1) {
            if ((token->type == JSMN_OBJECT && c == '}') ||
                (token->type == JSMN_ARRAY && c == ']')) {
              token->end = parser->pos + 1;
              parser->toksuper = token->parent;
              break;
            } else {
              return JSMN_ERROR_INVAL;
            }
          }
        }
        break;

      case '"':
        result = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (result < 0) {
          return result;
        }
        if (parser->toksuper != -1 && tokens != NULL) {
          tokens[parser->toksuper].size++;
        }
        break;

      case '\t':
      case '\r':
      case '\n':
      case ' ': 
        break;

      case ':':
        parser->toksuper = parser->toknext - 1;
        break;

      case ',':
        if (tokens != NULL && parser->toksuper != -1 &&
            tokens[parser->toksuper].type != JSMN_ARRAY &&
            tokens[parser->toksuper].type != JSMN_OBJECT) {
          parser->toksuper = tokens[parser->toksuper].parent;
        }
        break;

      case '-':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case 't':
      case 'f':
      case 'n':
        result = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (result < 0) {
          return result;
        }
        if (parser->toksuper != -1 && tokens != NULL) {
          tokens[parser->toksuper].size++;
        }
        break;

      default:
        return JSMN_ERROR_INVAL;
    }
  }

  if (tokens != NULL) {
    for (int i = parser->toknext - 1; i >= 0; i--) {
      if (tokens[i].start != -1 && tokens[i].end == -1) {
        return JSMN_ERROR_PART;
      }
    }
  }

  return count;
}

void jsmn_init(jsmn_parser* parser) {
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}
