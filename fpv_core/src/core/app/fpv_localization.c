/* FunPay Vertex localization loader implementation. */

#include "core/app/fpv_localization.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "core/io/fpv_fs.h"

#include "core/base/fpv_string.h"


typedef struct fpv_locale_entry {
  char* key;
  char* value;
} fpv_locale_entry_t;

typedef struct fpv_locale_table {
  fpv_locale_entry_t* entries;
  size_t count;
  char* storage;
  size_t storage_size;
} fpv_locale_table_t;

struct fpv_localizer {
  char* language;
  char* fallback_language;
  fpv_locale_table_t current;
  fpv_locale_table_t fallback;
};

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

static bool fpv_buffer_append_char(
    char** buffer,
    size_t* length,
    size_t* capacity,
    char ch) {
  return fpv_buffer_append(buffer, length, capacity, &ch, 1);
}

static void fpv_locale_table_destroy(fpv_locale_table_t* table) {
  if (!table) {
    return;
  }
  bool owns_strings = table->storage == NULL;
  if (owns_strings) {
    for (size_t i = 0; i < table->count; i++) {
      fpv_free(table->entries[i].key);
      fpv_free(table->entries[i].value);
    }
  }
  fpv_free(table->entries);
  table->entries = NULL;
  table->count = 0;
  fpv_free(table->storage);
  table->storage = NULL;
  table->storage_size = 0;
}

static const char* fpv_locale_table_get(
    const fpv_locale_table_t* table,
    const char* key) {
  if (!table || !key) {
    return NULL;
  }
  for (size_t i = 0; i < table->count; i++) {
    if (table->entries[i].key &&
        strcmp(table->entries[i].key, key) == 0) {
      return table->entries[i].value;
    }
  }
  return NULL;
}

static void fpv_locale_table_set(
    fpv_locale_table_t* table,
    const char* key,
    const char* value) {
  if (!table || !key || !value) {
    return;
  }
  if (table->storage) {
    return;
  }
  for (size_t i = 0; i < table->count; i++) {
    if (table->entries[i].key &&
        strcmp(table->entries[i].key, key) == 0) {
      fpv_free(table->entries[i].value);
      table->entries[i].value = fpv_strdup(value);
      return;
    }
  }
  fpv_locale_entry_t* grown = (fpv_locale_entry_t*)realloc(
      table->entries,
      (table->count + 1) * sizeof(*grown));
  if (!grown) {
    return;
  }
  table->entries = grown;
  table->entries[table->count].key = fpv_strdup(key);
  table->entries[table->count].value = fpv_strdup(value);
  if (!table->entries[table->count].key ||
      !table->entries[table->count].value) {
    fpv_free(table->entries[table->count].key);
    fpv_free(table->entries[table->count].value);
    return;
  }
  table->count++;
}

static bool fpv_is_ident_start(int ch) {
  return isalpha(ch) || ch == '_';
}

static bool fpv_is_ident_char(int ch) {
  return isalnum(ch) || ch == '_';
}

static char fpv_parse_escape(const char** cursor) {
  const char* ptr = *cursor;
  if (!ptr || *ptr == '\0') {
    return '\0';
  }
  char ch = *ptr++;
  switch (ch) {
    case 'n':
      *cursor = ptr;
      return '\n';
    case 'r':
      *cursor = ptr;
      return '\r';
    case 't':
      *cursor = ptr;
      return '\t';
    case '\\':
    case '\'':
    case '"':
      *cursor = ptr;
      return ch;
    default:
      *cursor = ptr;
      return ch;
  }
}

static bool fpv_parse_python_string(const char** cursor, char** out) {
  if (!cursor || !*cursor || !out) {
    return false;
  }
  const char* ptr = *cursor;
  char quote = *ptr;
  if (quote != '"' && quote != '\'') {
    return false;
  }
  bool triple = false;
  if (ptr[1] == quote && ptr[2] == quote) {
    triple = true;
    ptr += 3;
  } else {
    ptr++;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;

  while (*ptr) {
    if (triple) {
      if (ptr[0] == quote && ptr[1] == quote && ptr[2] == quote) {
        ptr += 3;
        break;
      }
    } else if (*ptr == quote) {
      ptr++;
      break;
    }

    if (*ptr == '\\') {
      ptr++;
      if (*ptr == '\0') {
        break;
      }
      char escaped = fpv_parse_escape(&ptr);
      if (!fpv_buffer_append_char(&buffer, &length, &capacity, escaped)) {
        fpv_free(buffer);
        return false;
      }
      continue;
    }

    if (!fpv_buffer_append_char(&buffer, &length, &capacity, *ptr)) {
      fpv_free(buffer);
      return false;
    }
    ptr++;
  }

  if (!buffer) {
    buffer = fpv_strdup("");
    if (!buffer) {
      return false;
    }
  }

  *cursor = ptr;
  *out = buffer;
  return true;
}

static void fpv_skip_whitespace(const char** cursor) {
  const char* ptr = *cursor;
  while (*ptr && (*ptr == ' ' || *ptr == '\t')) {
    ptr++;
  }
  *cursor = ptr;
}

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

static void fpv_locale_parse_file(
    const char* path,
    fpv_locale_table_t* table) {
  size_t size = 0;
  char* content = fpv_read_file(path, &size);
  if (!content) {
    return;
  }

  const char* ptr = content;
  while (*ptr) {
    while (*ptr && (*ptr == '\r' || *ptr == '\n')) {
      ptr++;
    }
    if (!*ptr) {
      break;
    }

    if (*ptr == '#') {
      while (*ptr && *ptr != '\n') {
        ptr++;
      }
      continue;
    }

    const char* line_start = ptr;
    fpv_skip_whitespace(&ptr);
    if (!fpv_is_ident_start((unsigned char)*ptr)) {
      while (*ptr && *ptr != '\n') {
        ptr++;
      }
      continue;
    }

    const char* key_start = ptr;
    ptr++;
    while (fpv_is_ident_char((unsigned char)*ptr)) {
      ptr++;
    }
    const char* key_end = ptr;
    fpv_skip_whitespace(&ptr);
    if (*ptr != '=') {
      ptr = line_start;
      while (*ptr && *ptr != '\n') {
        ptr++;
      }
      continue;
    }
    ptr++;
    fpv_skip_whitespace(&ptr);

    if (*ptr != '"' && *ptr != '\'') {
      while (*ptr && *ptr != '\n') {
        ptr++;
      }
      continue;
    }

    char* key = fpv_strdup_n(key_start, (size_t)(key_end - key_start));
    if (!key) {
      break;
    }

    char* value = NULL;
    if (!fpv_parse_python_string(&ptr, &value)) {
      fpv_free(key);
      while (*ptr && *ptr != '\n') {
        ptr++;
      }
      continue;
    }

    char* combined = value;
    while (*ptr) {
      const char* checkpoint = ptr;
      fpv_skip_whitespace(&ptr);
      if (*ptr == '\\' && ptr[1] == '\n') {
        ptr += 2;
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r') {
          ptr++;
        }
        if (*ptr == '"' || *ptr == '\'') {
          char* next = NULL;
          if (fpv_parse_python_string(&ptr, &next)) {
            char* merged = NULL;
            size_t merged_len = 0;
            size_t merged_cap = 0;
            fpv_buffer_append(&merged, &merged_len, &merged_cap, combined, strlen(combined));
            fpv_buffer_append(&merged, &merged_len, &merged_cap, next, strlen(next));
            fpv_free(combined);
            fpv_free(next);
            combined = merged ? merged : fpv_strdup("");
            continue;
          }
        }
      }
      ptr = checkpoint;
      break;
    }

    fpv_locale_table_set(table, key, combined);
    fpv_free(key);
    fpv_free(combined);

    while (*ptr && *ptr != '\n') {
      ptr++;
    }
  }

  fpv_free(content);
}

static uint32_t fpv_read_u32_le(const unsigned char* data) {
  return (uint32_t)data[0] |
      ((uint32_t)data[1] << 8) |
      ((uint32_t)data[2] << 16) |
      ((uint32_t)data[3] << 24);
}

static bool fpv_locale_has_string(
    const char* base,
    size_t size,
    uint32_t offset) {
  if (!base || offset >= size) {
    return false;
  }
  const void* found = memchr(base + offset, '\0', size - offset);
  return found != NULL;
}

static bool fpv_locale_parse_loc(
    const char* path,
    fpv_locale_table_t* table) {
  if (!path || !table) {
    return false;
  }
  size_t size = 0;
  unsigned char* data = (unsigned char*)fpv_read_file(path, &size);
  if (!data || size < 16) {
    fpv_free(data);
    return false;
  }
  static const unsigned char magic[8] = {
      'F', 'P', 'V', 'L', 'O', 'C', '1', '\0'};
  if (memcmp(data, magic, sizeof(magic)) != 0) {
    fpv_free(data);
    return false;
  }
  uint32_t count = fpv_read_u32_le(data + 8);
  uint32_t blob_size = fpv_read_u32_le(data + 12);
  if (count == 0 || blob_size == 0) {
    fpv_free(data);
    return false;
  }
  size_t count_size = (size_t)count;
  if (count_size > (SIZE_MAX - 16) / 8) {
    fpv_free(data);
    return false;
  }
  size_t table_bytes = count_size * 8;
  size_t blob_offset = 16 + table_bytes;
  if (blob_offset > size) {
    fpv_free(data);
    return false;
  }
  if ((size_t)blob_size > size - blob_offset) {
    fpv_free(data);
    return false;
  }

  fpv_locale_entry_t* entries =
      (fpv_locale_entry_t*)calloc(count, sizeof(*entries));
  if (!entries) {
    fpv_free(data);
    return false;
  }

  const char* blob = (const char*)(data + blob_offset);
  for (uint32_t i = 0; i < count; i++) {
    const unsigned char* entry = data + 16 + i * 8;
    uint32_t key_offset = fpv_read_u32_le(entry);
    uint32_t value_offset = fpv_read_u32_le(entry + 4);
    if (!fpv_locale_has_string(blob, blob_size, key_offset) ||
        !fpv_locale_has_string(blob, blob_size, value_offset)) {
      fpv_free(entries);
      fpv_free(data);
      return false;
    }
    entries[i].key = (char*)(blob + key_offset);
    entries[i].value = (char*)(blob + value_offset);
  }

  table->entries = entries;
  table->count = count;
  table->storage = (char*)data;
  table->storage_size = size;
  return true;
}

fpv_localizer_t* fpv_localizer_create(
    const char* locales_dir,
    const char* language,
    const char* fallback_language) {
  if (!fallback_language || !fallback_language[0]) {
    return NULL;
  }
  fpv_localizer_t* localizer =
      (fpv_localizer_t*)calloc(1, sizeof(*localizer));
  if (!localizer) {
    return NULL;
  }
  localizer->language = language ? fpv_strdup(language) : NULL;
  localizer->fallback_language = fpv_strdup(fallback_language);
  if (!localizer->fallback_language) {
    fpv_localizer_destroy(localizer);
    return NULL;
  }

  char* fallback_loc = NULL;
  char* current_loc = NULL;
  char* fallback_py = NULL;
  char* current_py = NULL;
  if (locales_dir && locales_dir[0]) {
    char fallback_buf[64];
    snprintf(fallback_buf, sizeof(fallback_buf), "%s.loc", fallback_language);
    fallback_loc = fpv_path_join(locales_dir, fallback_buf);
    snprintf(fallback_buf, sizeof(fallback_buf), "%s.py", fallback_language);
    fallback_py = fpv_path_join(locales_dir, fallback_buf);
    if (language && language[0]) {
      char name_buf[64];
      snprintf(name_buf, sizeof(name_buf), "%s.loc", language);
      current_loc = fpv_path_join(locales_dir, name_buf);
      snprintf(name_buf, sizeof(name_buf), "%s.py", language);
      current_py = fpv_path_join(locales_dir, name_buf);
    }
  }
  if (!fallback_loc) {
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "locales/%s.loc", fallback_language);
    fallback_loc = fpv_strdup(name_buf);
  }
  if (!fallback_py) {
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "locales/%s.py", fallback_language);
    fallback_py = fpv_strdup(name_buf);
  }
  if (!current_loc && language && language[0]) {
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "locales/%s.loc", language);
    current_loc = fpv_strdup(name_buf);
  }
  if (!current_py && language && language[0]) {
    char name_buf[64];
    snprintf(name_buf, sizeof(name_buf), "locales/%s.py", language);
    current_py = fpv_strdup(name_buf);
  }

  bool fallback_loaded = false;
  if (fallback_loc && fpv_fs_exists(fallback_loc)) {
    fallback_loaded = fpv_locale_parse_loc(fallback_loc, &localizer->fallback);
  }
  if (!fallback_loaded && fallback_py && fpv_fs_exists(fallback_py)) {
    fpv_locale_parse_file(fallback_py, &localizer->fallback);
  }

  bool current_loaded = false;
  if (current_loc && fpv_fs_exists(current_loc)) {
    current_loaded = fpv_locale_parse_loc(current_loc, &localizer->current);
  }
  if (!current_loaded && current_py && fpv_fs_exists(current_py)) {
    fpv_locale_parse_file(current_py, &localizer->current);
  }

  fpv_free(fallback_loc);
  fpv_free(current_loc);
  fpv_free(fallback_py);
  fpv_free(current_py);
  return localizer;
}

void fpv_localizer_destroy(fpv_localizer_t* localizer) {
  if (!localizer) {
    return;
  }
  fpv_free(localizer->language);
  fpv_free(localizer->fallback_language);
  fpv_locale_table_destroy(&localizer->current);
  fpv_locale_table_destroy(&localizer->fallback);
  free(localizer);
}

const char* fpv_localizer_get(
    const fpv_localizer_t* localizer,
    const char* key) {
  if (!key) {
    return NULL;
  }
  if (!localizer) {
    return key;
  }
  const char* value = fpv_locale_table_get(&localizer->current, key);
  if (value) {
    return value;
  }
  value = fpv_locale_table_get(&localizer->fallback, key);
  return value ? value : key;
}

char* fpv_localizer_format(
    const fpv_localizer_t* localizer,
    const char* key,
    const char* const* args,
    size_t arg_count) {
  const char* template = fpv_localizer_get(localizer, key);
  if (!template) {
    return NULL;
  }

  size_t length = 0;
  size_t capacity = 0;
  char* buffer = NULL;

  size_t arg_index = 0;
  for (size_t i = 0; template[i] != '\0'; i++) {
    if (template[i] == '{' && template[i + 1] == '}') {
      const char* replacement = "{}";
      if (args && arg_index < arg_count && args[arg_index]) {
        replacement = args[arg_index];
      }
      arg_index++;
      fpv_buffer_append(
          &buffer,
          &length,
          &capacity,
          replacement,
          strlen(replacement));
      i++;
      continue;
    }
    fpv_buffer_append_char(&buffer, &length, &capacity, template[i]);
  }

  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}
