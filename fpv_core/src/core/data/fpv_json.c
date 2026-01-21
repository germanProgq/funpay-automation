/* FunPay Vertex JSON utilities. */

#include "core/data/fpv_json.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fpv_json_kv {
  char* key;
  struct fpv_json_value* value;
} fpv_json_kv_t;

struct fpv_json_value {
  fpv_json_type_t type;
  union {
    bool boolean;
    char* number;
    char* string;
    struct {
      struct fpv_json_value** items;
      size_t count;
    } array;
    struct {
      fpv_json_kv_t* items;
      size_t count;
    } object;
  } as;
};

typedef struct fpv_json_reader {
  const char* ptr;
  const char* end;
  size_t line;
  size_t column;
  fpv_json_error_t* error;
} fpv_json_reader_t;

static void fpv_json_set_error(fpv_json_reader_t* reader, const char* message) {
  if (!reader || !reader->error) {
    return;
  }
  reader->error->line = reader->line;
  reader->error->column = reader->column;
  if (message) {
    snprintf(reader->error->message, sizeof(reader->error->message), "%s", message);
  } else {
    reader->error->message[0] = '\0';
  }
}

static int fpv_json_peek(fpv_json_reader_t* reader) {
  if (!reader || reader->ptr >= reader->end) {
    return EOF;
  }
  return (unsigned char)*reader->ptr;
}

static int fpv_json_next(fpv_json_reader_t* reader) {
  if (!reader || reader->ptr >= reader->end) {
    return EOF;
  }
  const char c = *reader->ptr++;
  if (c == '\n') {
    reader->line++;
    reader->column = 1;
  } else {
    reader->column++;
  }
  return (unsigned char)c;
}

static void fpv_json_skip_whitespace(fpv_json_reader_t* reader) {
  while (true) {
    int c = fpv_json_peek(reader);
    if (c == EOF || !isspace(c)) {
      return;
    }
    fpv_json_next(reader);
  }
}

static fpv_json_value_t* fpv_json_value_create(fpv_json_type_t type) {
  fpv_json_value_t* value = (fpv_json_value_t*)calloc(1, sizeof(*value));
  if (!value) {
    return NULL;
  }
  value->type = type;
  return value;
}

static bool fpv_json_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len) {
  if (*length + data_len + 1 > *capacity) {
    size_t next = *capacity == 0 ? 32 : *capacity;
    while (next < *length + data_len + 1) {
      next *= 2;
    }
    char* grown = (char*)realloc(*buffer, next);
    if (!grown) {
      return false;
    }
    *buffer = grown;
    *capacity = next;
  }
  memcpy(*buffer + *length, data, data_len);
  *length += data_len;
  (*buffer)[*length] = '\0';
  return true;
}

static bool fpv_json_append_utf8(
    char** buffer,
    size_t* length,
    size_t* capacity,
    uint32_t codepoint) {
  char encoded[4];
  size_t encoded_len = 0;

  if (codepoint <= 0x7F) {
    encoded[0] = (char)codepoint;
    encoded_len = 1;
  } else if (codepoint <= 0x7FF) {
    encoded[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
    encoded[1] = (char)(0x80 | (codepoint & 0x3F));
    encoded_len = 2;
  } else if (codepoint <= 0xFFFF) {
    encoded[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
    encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[2] = (char)(0x80 | (codepoint & 0x3F));
    encoded_len = 3;
  } else if (codepoint <= 0x10FFFF) {
    encoded[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
    encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    encoded[3] = (char)(0x80 | (codepoint & 0x3F));
    encoded_len = 4;
  } else {
    return false;
  }

  return fpv_json_buffer_append(buffer, length, capacity, encoded, encoded_len);
}

static bool fpv_json_parse_hex(fpv_json_reader_t* reader, uint32_t* out) {
  uint32_t value = 0;
  for (int i = 0; i < 4; i++) {
    int c = fpv_json_next(reader);
    if (c == EOF) {
      return false;
    }
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= (uint32_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= (uint32_t)(10 + (c - 'a'));
    } else if (c >= 'A' && c <= 'F') {
      value |= (uint32_t)(10 + (c - 'A'));
    } else {
      return false;
    }
  }
  *out = value;
  return true;
}

static fpv_result_t fpv_json_parse_string(
    fpv_json_reader_t* reader,
    char** out) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;

  if (fpv_json_next(reader) != '"') {
    fpv_json_set_error(reader, "Expected string opening quote.");
    return FPV_ERR_PARSE;
  }

  while (true) {
    int c = fpv_json_next(reader);
    if (c == EOF) {
      fpv_json_set_error(reader, "Unexpected end of input in string.");
      free(buffer);
      return FPV_ERR_PARSE;
    }
    if (c == '"') {
      break;
    }
    if (c == '\\') {
      int esc = fpv_json_next(reader);
      if (esc == EOF) {
        fpv_json_set_error(reader, "Unexpected end of input in escape.");
        free(buffer);
        return FPV_ERR_PARSE;
      }
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, (char*)&esc, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        case 'b': {
          const char backspace = '\b';
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, &backspace, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        case 'f': {
          const char formfeed = '\f';
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, &formfeed, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        case 'n': {
          const char newline = '\n';
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, &newline, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        case 'r': {
          const char carriage = '\r';
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, &carriage, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        case 't': {
          const char tab = '\t';
          if (!fpv_json_buffer_append(&buffer, &length, &capacity, &tab, 1)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        case 'u': {
          uint32_t codepoint = 0;
          if (!fpv_json_parse_hex(reader, &codepoint)) {
            fpv_json_set_error(reader, "Invalid unicode escape.");
            free(buffer);
            return FPV_ERR_PARSE;
          }
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (fpv_json_next(reader) != '\\' || fpv_json_next(reader) != 'u') {
              fpv_json_set_error(reader, "Invalid surrogate pair.");
              free(buffer);
              return FPV_ERR_PARSE;
            }
            uint32_t low = 0;
            if (!fpv_json_parse_hex(reader, &low) ||
                low < 0xDC00 || low > 0xDFFF) {
              fpv_json_set_error(reader, "Invalid surrogate pair.");
              free(buffer);
              return FPV_ERR_PARSE;
            }
            codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
          }
          if (!fpv_json_append_utf8(&buffer, &length, &capacity, codepoint)) {
            free(buffer);
            return FPV_ERR_OUT_OF_MEMORY;
          }
          break;
        }
        default:
          fpv_json_set_error(reader, "Invalid escape.");
          free(buffer);
          return FPV_ERR_PARSE;
      }
      continue;
    }
    const char ch = (char)c;
    if (!fpv_json_buffer_append(&buffer, &length, &capacity, &ch, 1)) {
      free(buffer);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  if (!buffer) {
    buffer = (char*)calloc(1, 1);
    if (!buffer) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  *out = buffer;
  return FPV_OK;
}

static fpv_result_t fpv_json_parse_number(
    fpv_json_reader_t* reader,
    char** out) {
  const char* start = reader->ptr;
  bool has_digit = false;

  int c = fpv_json_peek(reader);
  if (c == '-') {
    fpv_json_next(reader);
  }

  c = fpv_json_peek(reader);
  if (c == '0') {
    fpv_json_next(reader);
    has_digit = true;
  } else if (c >= '1' && c <= '9') {
    while (true) {
      c = fpv_json_peek(reader);
      if (c < '0' || c > '9') {
        break;
      }
      fpv_json_next(reader);
      has_digit = true;
    }
  }

  if (!has_digit) {
    fpv_json_set_error(reader, "Invalid number.");
    return FPV_ERR_PARSE;
  }

  c = fpv_json_peek(reader);
  if (c == '.') {
    fpv_json_next(reader);
    bool frac_digit = false;
    while (true) {
      c = fpv_json_peek(reader);
      if (c < '0' || c > '9') {
        break;
      }
      fpv_json_next(reader);
      frac_digit = true;
    }
    if (!frac_digit) {
      fpv_json_set_error(reader, "Invalid number fraction.");
      return FPV_ERR_PARSE;
    }
  }

  c = fpv_json_peek(reader);
  if (c == 'e' || c == 'E') {
    fpv_json_next(reader);
    c = fpv_json_peek(reader);
    if (c == '+' || c == '-') {
      fpv_json_next(reader);
    }
    bool exp_digit = false;
    while (true) {
      c = fpv_json_peek(reader);
      if (c < '0' || c > '9') {
        break;
      }
      fpv_json_next(reader);
      exp_digit = true;
    }
    if (!exp_digit) {
      fpv_json_set_error(reader, "Invalid number exponent.");
      return FPV_ERR_PARSE;
    }
  }

  const char* end = reader->ptr;
  size_t length = (size_t)(end - start);
  char* copy = (char*)malloc(length + 1);
  if (!copy) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  memcpy(copy, start, length);
  copy[length] = '\0';
  *out = copy;
  return FPV_OK;
}

static fpv_result_t fpv_json_parse_value(
    fpv_json_reader_t* reader,
    fpv_json_value_t** out);

static fpv_result_t fpv_json_parse_array(
    fpv_json_reader_t* reader,
    fpv_json_value_t** out) {
  fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_ARRAY);
  if (!value) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_json_next(reader);
  fpv_json_skip_whitespace(reader);
  if (fpv_json_peek(reader) == ']') {
    fpv_json_next(reader);
    *out = value;
    return FPV_OK;
  }

  while (true) {
    fpv_json_value_t* item = NULL;
    fpv_result_t result = fpv_json_parse_value(reader, &item);
    if (result != FPV_OK) {
      fpv_json_destroy(value);
      return result;
    }

    fpv_json_value_t** grown = (fpv_json_value_t**)realloc(
        value->as.array.items,
        (value->as.array.count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_json_destroy(item);
      fpv_json_destroy(value);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    value->as.array.items = grown;
    value->as.array.items[value->as.array.count++] = item;

    fpv_json_skip_whitespace(reader);
    int c = fpv_json_next(reader);
    if (c == ']') {
      break;
    }
    if (c != ',') {
      fpv_json_set_error(reader, "Expected ',' or ']'.");
      fpv_json_destroy(value);
      return FPV_ERR_PARSE;
    }
    fpv_json_skip_whitespace(reader);
  }

  *out = value;
  return FPV_OK;
}

static fpv_result_t fpv_json_parse_object(
    fpv_json_reader_t* reader,
    fpv_json_value_t** out) {
  fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_OBJECT);
  if (!value) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_json_next(reader);
  fpv_json_skip_whitespace(reader);
  if (fpv_json_peek(reader) == '}') {
    fpv_json_next(reader);
    *out = value;
    return FPV_OK;
  }

  while (true) {
    char* key = NULL;
    if (fpv_json_peek(reader) != '"') {
      fpv_json_set_error(reader, "Expected string key.");
      fpv_json_destroy(value);
      return FPV_ERR_PARSE;
    }

    fpv_result_t result = fpv_json_parse_string(reader, &key);
    if (result != FPV_OK) {
      fpv_json_destroy(value);
      return result;
    }

    fpv_json_skip_whitespace(reader);
    if (fpv_json_next(reader) != ':') {
      fpv_json_set_error(reader, "Expected ':'.");
      free(key);
      fpv_json_destroy(value);
      return FPV_ERR_PARSE;
    }
    fpv_json_skip_whitespace(reader);

    fpv_json_value_t* item = NULL;
    result = fpv_json_parse_value(reader, &item);
    if (result != FPV_OK) {
      free(key);
      fpv_json_destroy(value);
      return result;
    }

    fpv_json_kv_t* grown = (fpv_json_kv_t*)realloc(
        value->as.object.items,
        (value->as.object.count + 1) * sizeof(*grown));
    if (!grown) {
      free(key);
      fpv_json_destroy(item);
      fpv_json_destroy(value);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    value->as.object.items = grown;
    value->as.object.items[value->as.object.count].key = key;
    value->as.object.items[value->as.object.count].value = item;
    value->as.object.count++;

    fpv_json_skip_whitespace(reader);
    int c = fpv_json_next(reader);
    if (c == '}') {
      break;
    }
    if (c != ',') {
      fpv_json_set_error(reader, "Expected ',' or '}'.");
      fpv_json_destroy(value);
      return FPV_ERR_PARSE;
    }
    fpv_json_skip_whitespace(reader);
  }

  *out = value;
  return FPV_OK;
}

static fpv_result_t fpv_json_parse_value(
    fpv_json_reader_t* reader,
    fpv_json_value_t** out) {
  fpv_json_skip_whitespace(reader);
  int c = fpv_json_peek(reader);
  if (c == EOF) {
    fpv_json_set_error(reader, "Unexpected end of input.");
    return FPV_ERR_PARSE;
  }

  if (c == '"') {
    fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_STRING);
    if (!value) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_result_t result = fpv_json_parse_string(reader, &value->as.string);
    if (result != FPV_OK) {
      fpv_json_destroy(value);
      return result;
    }
    *out = value;
    return FPV_OK;
  }

  if (c == '{') {
    return fpv_json_parse_object(reader, out);
  }

  if (c == '[') {
    return fpv_json_parse_array(reader, out);
  }

  if (c == 't') {
    if (reader->end - reader->ptr < 4 ||
        strncmp(reader->ptr, "true", 4) != 0) {
      fpv_json_set_error(reader, "Invalid literal.");
      return FPV_ERR_PARSE;
    }
    reader->ptr += 4;
    reader->column += 4;
    fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_BOOL);
    if (!value) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    value->as.boolean = true;
    *out = value;
    return FPV_OK;
  }

  if (c == 'f') {
    if (reader->end - reader->ptr < 5 ||
        strncmp(reader->ptr, "false", 5) != 0) {
      fpv_json_set_error(reader, "Invalid literal.");
      return FPV_ERR_PARSE;
    }
    reader->ptr += 5;
    reader->column += 5;
    fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_BOOL);
    if (!value) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    value->as.boolean = false;
    *out = value;
    return FPV_OK;
  }

  if (c == 'n') {
    if (reader->end - reader->ptr < 4 ||
        strncmp(reader->ptr, "null", 4) != 0) {
      fpv_json_set_error(reader, "Invalid literal.");
      return FPV_ERR_PARSE;
    }
    reader->ptr += 4;
    reader->column += 4;
    fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_NULL);
    if (!value) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    *out = value;
    return FPV_OK;
  }

  if (c == '-' || (c >= '0' && c <= '9')) {
    fpv_json_value_t* value = fpv_json_value_create(FPV_JSON_NUMBER);
    if (!value) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_result_t result = fpv_json_parse_number(reader, &value->as.number);
    if (result != FPV_OK) {
      fpv_json_destroy(value);
      return result;
    }
    *out = value;
    return FPV_OK;
  }

  fpv_json_set_error(reader, "Unexpected token.");
  return FPV_ERR_PARSE;
}

fpv_result_t fpv_json_parse(
    const char* input,
    size_t length,
    fpv_json_value_t** out,
    fpv_json_error_t* error) {
  fpv_json_reader_t reader;
  if (!input || !out) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  reader.ptr = input;
  reader.end = input + length;
  reader.line = 1;
  reader.column = 1;
  reader.error = error;
  if (error) {
    error->line = 1;
    error->column = 1;
    error->message[0] = '\0';
  }

  fpv_json_value_t* value = NULL;
  fpv_result_t result = fpv_json_parse_value(&reader, &value);
  if (result != FPV_OK) {
    fpv_json_destroy(value);
    return result;
  }

  fpv_json_skip_whitespace(&reader);
  if (reader.ptr != reader.end) {
    fpv_json_set_error(&reader, "Trailing data.");
    fpv_json_destroy(value);
    return FPV_ERR_PARSE;
  }

  *out = value;
  return FPV_OK;
}

void fpv_json_destroy(fpv_json_value_t* value) {
  if (!value) {
    return;
  }
  switch (value->type) {
    case FPV_JSON_STRING:
      free(value->as.string);
      break;
    case FPV_JSON_NUMBER:
      free(value->as.number);
      break;
    case FPV_JSON_ARRAY:
      for (size_t i = 0; i < value->as.array.count; i++) {
        fpv_json_destroy(value->as.array.items[i]);
      }
      free(value->as.array.items);
      break;
    case FPV_JSON_OBJECT:
      for (size_t i = 0; i < value->as.object.count; i++) {
        free(value->as.object.items[i].key);
        fpv_json_destroy(value->as.object.items[i].value);
      }
      free(value->as.object.items);
      break;
    default:
      break;
  }
  free(value);
}

const fpv_json_value_t* fpv_json_object_get(
    const fpv_json_value_t* value,
    const char* key) {
  if (!value || value->type != FPV_JSON_OBJECT || !key) {
    return NULL;
  }
  for (size_t i = 0; i < value->as.object.count; i++) {
    if (strcmp(value->as.object.items[i].key, key) == 0) {
      return value->as.object.items[i].value;
    }
  }
  return NULL;
}

size_t fpv_json_object_size(const fpv_json_value_t* value) {
  if (!value || value->type != FPV_JSON_OBJECT) {
    return 0;
  }
  return value->as.object.count;
}

const char* fpv_json_object_key(
    const fpv_json_value_t* value,
    size_t index) {
  if (!value || value->type != FPV_JSON_OBJECT) {
    return NULL;
  }
  if (index >= value->as.object.count) {
    return NULL;
  }
  return value->as.object.items[index].key;
}

const fpv_json_value_t* fpv_json_object_value(
    const fpv_json_value_t* value,
    size_t index) {
  if (!value || value->type != FPV_JSON_OBJECT) {
    return NULL;
  }
  if (index >= value->as.object.count) {
    return NULL;
  }
  return value->as.object.items[index].value;
}

const fpv_json_value_t* fpv_json_array_get(
    const fpv_json_value_t* value,
    size_t index) {
  if (!value || value->type != FPV_JSON_ARRAY) {
    return NULL;
  }
  if (index >= value->as.array.count) {
    return NULL;
  }
  return value->as.array.items[index];
}

size_t fpv_json_array_size(const fpv_json_value_t* value) {
  if (!value || value->type != FPV_JSON_ARRAY) {
    return 0;
  }
  return value->as.array.count;
}

const char* fpv_json_string(const fpv_json_value_t* value) {
  if (!value || value->type != FPV_JSON_STRING) {
    return NULL;
  }
  return value->as.string;
}

const char* fpv_json_number_raw(const fpv_json_value_t* value) {
  if (!value || value->type != FPV_JSON_NUMBER) {
    return NULL;
  }
  return value->as.number;
}

bool fpv_json_bool(const fpv_json_value_t* value, bool fallback) {
  if (!value || value->type != FPV_JSON_BOOL) {
    return fallback;
  }
  return value->as.boolean;
}

bool fpv_json_is_type(const fpv_json_value_t* value, fpv_json_type_t type) {
  return value && value->type == type;
}

bool fpv_json_number_to_int64(const fpv_json_value_t* value, int64_t* out) {
  const char* raw = fpv_json_number_raw(value);
  if (!raw || !out) {
    return false;
  }
  char* end = NULL;
  long long parsed = strtoll(raw, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (int64_t)parsed;
  return true;
}

bool fpv_json_number_to_uint64(const fpv_json_value_t* value, uint64_t* out) {
  const char* raw = fpv_json_number_raw(value);
  if (!raw || !out) {
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(raw, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (uint64_t)parsed;
  return true;
}

bool fpv_json_number_to_double(const fpv_json_value_t* value, double* out) {
  const char* raw = fpv_json_number_raw(value);
  if (!raw || !out) {
    return false;
  }
  char* end = NULL;
  double parsed = strtod(raw, &end);
  if (!end || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}
