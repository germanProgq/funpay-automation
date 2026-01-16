/* FunPay Vertex cache helpers implementation. */

#include "fpv_cache.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_fs.h"
#include "fpv_json.h"
#include "fpv_string.h"

static char* fpv_read_file(const char* path, size_t* out_size) {
  if (out_size) {
    *out_size = 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  char* buffer = (char*)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(file);
    return NULL;
  }
  size_t read_count = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read_count] = '\0';
  if (out_size) {
    *out_size = read_count;
  }
  return buffer;
}

static bool fpv_write_file(const char* path, const char* data) {
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  size_t length = strlen(data);
  if (length > 0 && fwrite(data, 1, length, file) != length) {
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

static bool fpv_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len);

static bool fpv_json_escape_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value) {
  const char* ptr = value;
  while (*ptr) {
    unsigned char ch = (unsigned char)*ptr++;
    if (ch == '\\' || ch == '"') {
      char escaped[2] = {'\\', (char)ch};
      if (!fpv_buffer_append(buffer, length, capacity, escaped, 2)) {
        return false;
      }
      continue;
    }
    switch (ch) {
      case '\n':
        if (!fpv_buffer_append(buffer, length, capacity, "\\n", 2)) {
          return false;
        }
        continue;
      case '\r':
        if (!fpv_buffer_append(buffer, length, capacity, "\\r", 2)) {
          return false;
        }
        continue;
      case '\t':
        if (!fpv_buffer_append(buffer, length, capacity, "\\t", 2)) {
          return false;
        }
        continue;
      default:
        break;
    }
    if (ch < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04X", ch);
      if (!fpv_buffer_append(buffer, length, capacity, escaped, 6)) {
        return false;
      }
      continue;
    }
    if (!fpv_buffer_append(buffer, length, capacity, (const char*)&ch, 1)) {
      return false;
    }
  }
  return true;
}

static bool fpv_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len) {
  if (*length + data_len + 1 > *capacity) {
    size_t next = *capacity == 0 ? 64 : *capacity;
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

void fpv_string_list_destroy(fpv_string_list_t* list) {
  if (!list) {
    return;
  }
  for (size_t i = 0; i < list->count; i++) {
    fpv_free(list->items[i]);
  }
  fpv_free(list->items);
  list->items = NULL;
  list->count = 0;
}

fpv_result_t fpv_cache_load_strings(
    const char* path,
    fpv_string_list_t* out_list) {
  if (!path || !out_list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  out_list->items = NULL;
  out_list->count = 0;
  if (!fpv_fs_exists(path)) {
    return FPV_OK;
  }

  size_t size = 0;
  char* content = fpv_read_file(path, &size);
  if (!content) {
    return FPV_ERR_IO;
  }

  fpv_json_error_t error;
  fpv_json_value_t* json = NULL;
  fpv_result_t parse_result =
      fpv_json_parse(content, size, &json, &error);
  fpv_free(content);
  if (parse_result != FPV_OK || !json) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  if (!fpv_json_is_type(json, FPV_JSON_ARRAY)) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  size_t count = fpv_json_array_size(json);
  if (count == 0) {
    fpv_json_destroy(json);
    return FPV_OK;
  }

  char** items = (char**)calloc(count, sizeof(*items));
  if (!items) {
    fpv_json_destroy(json);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t out_count = 0;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* entry = fpv_json_array_get(json, i);
    const char* value = fpv_json_string(entry);
    if (!value) {
      continue;
    }
    items[out_count] = fpv_strdup(value);
    if (!items[out_count]) {
      for (size_t j = 0; j < out_count; j++) {
        fpv_free(items[j]);
      }
      fpv_free(items);
      fpv_json_destroy(json);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    out_count++;
  }

  fpv_json_destroy(json);
  out_list->items = items;
  out_list->count = out_count;
  return FPV_OK;
}

fpv_result_t fpv_cache_save_strings(
    const char* path,
    const fpv_string_list_t* list) {
  if (!path || !list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;

  fpv_buffer_append(&buffer, &length, &capacity, "[", 1);
  for (size_t i = 0; i < list->count; i++) {
    if (i > 0) {
      fpv_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    fpv_buffer_append(&buffer, &length, &capacity, "\"", 1);
    if (!fpv_json_escape_append(
            &buffer,
            &length,
            &capacity,
            list->items[i] ? list->items[i] : "")) {
      fpv_free(buffer);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_buffer_append(&buffer, &length, &capacity, "\"", 1);
  }
  fpv_buffer_append(&buffer, &length, &capacity, "]", 1);

  if (!buffer) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_result_t result = fpv_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  fpv_free(buffer);
  return result;
}

fpv_result_t fpv_cache_load_uint64(
    const char* path,
    uint64_t** out_values,
    size_t* out_count) {
  if (!path || !out_values || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_values = NULL;
  *out_count = 0;
  if (!fpv_fs_exists(path)) {
    return FPV_OK;
  }

  size_t size = 0;
  char* content = fpv_read_file(path, &size);
  if (!content) {
    return FPV_ERR_IO;
  }

  fpv_json_error_t error;
  fpv_json_value_t* json = NULL;
  fpv_result_t parse_result =
      fpv_json_parse(content, size, &json, &error);
  fpv_free(content);
  if (parse_result != FPV_OK || !json) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  if (!fpv_json_is_type(json, FPV_JSON_ARRAY)) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  size_t count = fpv_json_array_size(json);
  if (count == 0) {
    fpv_json_destroy(json);
    return FPV_OK;
  }

  uint64_t* values = (uint64_t*)calloc(count, sizeof(*values));
  if (!values) {
    fpv_json_destroy(json);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t stored = 0;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* entry = fpv_json_array_get(json, i);
    uint64_t value = 0;
    if (!fpv_json_number_to_uint64(entry, &value)) {
      continue;
    }
    values[stored++] = value;
  }

  fpv_json_destroy(json);
  *out_values = values;
  *out_count = stored;
  return FPV_OK;
}

fpv_result_t fpv_cache_save_uint64(
    const char* path,
    const uint64_t* values,
    size_t count) {
  if (!path || (!values && count > 0)) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_buffer_append(&buffer, &length, &capacity, "[", 1);
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      fpv_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    char num_buf[32];
    snprintf(num_buf, sizeof(num_buf), "%" PRIu64, values[i]);
    fpv_buffer_append(&buffer, &length, &capacity, num_buf, strlen(num_buf));
  }
  fpv_buffer_append(&buffer, &length, &capacity, "]", 1);

  if (!buffer) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_result_t result = fpv_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  fpv_free(buffer);
  return result;
}
