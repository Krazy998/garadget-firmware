#include <jsmn.h>

#include <cstddef>

extern "C" {

static jsmntok_t* jsmn_alloc_token(jsmn_parser* parser, jsmntok_t* tokens,
                                   unsigned int num_tokens) {
  if (!parser || !tokens) {
    return nullptr;
  }

  if (parser->toknext >= num_tokens) {
    return nullptr;
  }

  jsmntok_t* token = &tokens[parser->toknext++];
  token->start = -1;
  token->end = -1;
  token->size = 0;
#ifdef JSMN_PARENT_LINKS
  token->parent = -1;
#endif
  return token;
}

static void jsmn_fill_token(jsmntok_t* token, jsmntype_t type, int start, int end) {
  if (!token) {
    return;
  }

  token->type = type;
  token->start = start;
  token->end = end;
  token->size = 0;
}

static int jsmn_parse_primitive(jsmn_parser* parser, const char* js, std::size_t len,
                                jsmntok_t* tokens, unsigned int num_tokens) {
  int start = static_cast<int>(parser->pos);

  for (; parser->pos < len; ++parser->pos) {
    char c = js[parser->pos];

    switch (c) {
      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ',':
      case ']':
      case '}':
        goto found;
      default:
        if (c < 32 || c == '"' || c == ':' || c == '[' || c == '{') {
          return JSMN_ERROR_INVAL;
        }
    }
  }

#ifdef JSMN_STRICT
  return JSMN_ERROR_PART;
#endif

found:
  if (tokens) {
    jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (!token) {
      parser->pos = start;
      return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, static_cast<int>(parser->pos));
#ifdef JSMN_PARENT_LINKS
    token->parent = parser->toksuper;
#endif
  }
  parser->pos--;
  return 0;
}

static int jsmn_parse_string(jsmn_parser* parser, const char* js, std::size_t len,
                             jsmntok_t* tokens, unsigned int num_tokens) {
  int start = static_cast<int>(parser->pos);

  parser->pos++;
  for (; parser->pos < len; ++parser->pos) {
    char c = js[parser->pos];

    if (c == '"') {
      if (tokens) {
        jsmntok_t* token = jsmn_alloc_token(parser, tokens, num_tokens);
        if (!token) {
          parser->pos = start;
          return JSMN_ERROR_NOMEM;
        }
        jsmn_fill_token(token, JSMN_STRING, start + 1, static_cast<int>(parser->pos));
#ifdef JSMN_PARENT_LINKS
        token->parent = parser->toksuper;
#endif
      }
      return 0;
    }

    if (c == '\\' && parser->pos + 1 < len) {
      ++parser->pos;
      char esc = js[parser->pos];
      switch (esc) {
        case '"':
        case '\\':
        case '/':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
          break;
        case 'u':
          for (int i = 0; i < 4 && parser->pos < len; ++i) {
            ++parser->pos;
            char u = js[parser->pos];
            if (!((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f'))) {
              return JSMN_ERROR_INVAL;
            }
          }
          break;
        default:
          return JSMN_ERROR_INVAL;
      }
    }
  }

  parser->pos = start;
  return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser* parser, void*) {
  if (!parser) {
    return;
  }
  parser->pos = 0;
  parser->toknext = 0;
  parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser* parser, const char* js, std::size_t len, jsmntok_t* tokens,
               unsigned int num_tokens, void*) {
  if (!parser || !js) {
    return JSMN_ERROR_INVAL;
  }

  int count = 0;

  for (; parser->pos < len; ++parser->pos) {
    char c = js[parser->pos];
    jsmntok_t* token;

    switch (c) {
      case '{':
      case '[':
        ++count;
        token = tokens ? jsmn_alloc_token(parser, tokens, num_tokens) : nullptr;
        if (token == nullptr && tokens != nullptr) {
          return JSMN_ERROR_NOMEM;
        }
        if (token) {
          token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
          token->start = static_cast<int>(parser->pos);
          token->end = -1;
          token->size = 0;
#ifdef JSMN_PARENT_LINKS
          token->parent = parser->toksuper;
#endif
        }
        parser->toksuper = static_cast<int>(parser->toknext - 1);
        break;

      case '}':
      case ']':
        if (tokens) {
          int i = static_cast<int>(parser->toknext) - 1;
          for (; i >= 0; --i) {
            token = &tokens[i];
            if (token->start != -1 && token->end == -1) {
              if ((token->type == JSMN_OBJECT && c == '}') ||
                  (token->type == JSMN_ARRAY && c == ']')) {
                token->end = static_cast<int>(parser->pos) + 1;
#ifdef JSMN_PARENT_LINKS
                parser->toksuper = token->parent;
#else
                parser->toksuper = -1;
                if (i > 0) {
                  for (int j = i - 1; j >= 0; --j) {
                    if (tokens[j].start != -1 && tokens[j].end == -1) {
                      parser->toksuper = j;
                      break;
                    }
                  }
                }
#endif
                break;
              } else {
                return JSMN_ERROR_INVAL;
              }
            }
          }
        }
        break;

      case '"': {
        int result = jsmn_parse_string(parser, js, len, tokens, num_tokens);
        if (result < 0) {
          return result;
        }
        ++count;
        break;
      }

      case '\t':
      case '\r':
      case '\n':
      case ' ':
      case ':':
      case ',':
        break;

      default: {
        int result = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
        if (result < 0) {
          return result;
        }
        ++count;
        break;
      }
    }
  }

  if (tokens) {
    for (unsigned int i = 0; i < parser->toknext; ++i) {
      jsmntok_t* tok = &tokens[i];
      if (tok->start != -1 && tok->end == -1) {
        return JSMN_ERROR_PART;
      }
    }
  }

  return count;
}

}  // extern "C"

