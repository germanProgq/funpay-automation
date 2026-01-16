/* FunPay Vertex core INI configuration loader implementation. */

#include "fpv_core/fpv_ini.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_string.h"

typedef struct fpv_ini_entry {
  char* key;
  char* value;
  struct fpv_ini_entry* next;
} fpv_ini_entry_t;

typedef struct fpv_ini_section {
  char* name;
  fpv_ini_entry_t* entries;
  size_t entry_count;
  struct fpv_ini_section* next;
} fpv_ini_section_t;

struct fpv_ini {
  fpv_ini_section_t* sections;
  size_t section_count;
};

static bool fpv_write_file(const char* path, const char* data) {
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  size_t length = strlen(data ? data : "");
  if (length > 0 && fwrite(data, 1, length, file) != length) {
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

static char* fpv_trim(char* value) {
  char* end = NULL;

  if (!value) {
    return value;
  }

  while (*value && isspace((unsigned char)*value)) {
    value++;
  }

  if (*value == '\0') {
    return value;
  }

  end = value + strlen(value) - 1;
  while (end > value && isspace((unsigned char)*end)) {
    *end-- = '\0';
  }
  return value;
}

static fpv_ini_section_t* fpv_ini_find_section(
    fpv_ini_t* ini,
    const char* name) {
  fpv_ini_section_t* section = NULL;

  if (!ini || !name) {
    return NULL;
  }

  for (section = ini->sections; section; section = section->next) {
    if (strcmp(section->name, name) == 0) {
      return section;
    }
  }

  return NULL;
}

static fpv_ini_entry_t* fpv_ini_find_entry(
    fpv_ini_section_t* section,
    const char* key) {
  fpv_ini_entry_t* entry = NULL;

  if (!section || !key) {
    return NULL;
  }

  for (entry = section->entries; entry; entry = entry->next) {
    if (strcmp(entry->key, key) == 0) {
      return entry;
    }
  }

  return NULL;
}

static fpv_result_t fpv_ini_set_entry(
    fpv_ini_section_t* section,
    const char* key,
    const char* value) {
  fpv_ini_entry_t* entry = NULL;
  fpv_ini_entry_t* node = NULL;

  if (!section || !key) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  entry = fpv_ini_find_entry(section, key);
  if (entry) {
    fpv_free(entry->value);
    entry->value = fpv_strdup(value ? value : "");
    return entry->value ? FPV_OK : FPV_ERR_OUT_OF_MEMORY;
  }

  node = (fpv_ini_entry_t*)calloc(1, sizeof(*node));
  if (!node) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  node->key = fpv_strdup(key);
  node->value = fpv_strdup(value ? value : "");
  if (!node->key || !node->value) {
    fpv_free(node->key);
    fpv_free(node->value);
    free(node);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  node->next = section->entries;
  section->entries = node;
  section->entry_count++;
  return FPV_OK;
}

static fpv_result_t fpv_ini_append_entry_value(
    fpv_ini_entry_t* entry,
    const char* value) {
  size_t base_len = 0;
  size_t suffix_len = 0;
  size_t extra = 0;
  char* merged = NULL;

  if (!entry || !value) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (entry->value) {
    base_len = strlen(entry->value);
  }
  suffix_len = strlen(value);
  extra = base_len > 0 ? 1 : 0;

  merged = (char*)malloc(base_len + extra + suffix_len + 1);
  if (!merged) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (base_len > 0) {
    memcpy(merged, entry->value, base_len);
    merged[base_len] = '\n';
    memcpy(merged + base_len + 1, value, suffix_len);
    merged[base_len + 1 + suffix_len] = '\0';
  } else {
    memcpy(merged, value, suffix_len);
    merged[suffix_len] = '\0';
  }

  fpv_free(entry->value);
  entry->value = merged;
  return FPV_OK;
}

static void fpv_ini_section_destroy(fpv_ini_section_t* section) {
  fpv_ini_entry_t* entry = NULL;

  if (!section) {
    return;
  }

  entry = section->entries;
  while (entry) {
    fpv_ini_entry_t* next = entry->next;
    fpv_free(entry->key);
    fpv_free(entry->value);
    free(entry);
    entry = next;
  }

  fpv_free(section->name);
  free(section);
}

static char* fpv_read_line(FILE* file, bool* out_oom) {
  char buffer[512];
  char* line = NULL;
  size_t length = 0;

  if (out_oom) {
    *out_oom = false;
  }

  while (fgets(buffer, sizeof(buffer), file)) {
    size_t chunk = strlen(buffer);
    char* next = (char*)realloc(line, length + chunk + 1);
    if (!next) {
      fpv_free(line);
      if (out_oom) {
        *out_oom = true;
      }
      return NULL;
    }
    line = next;
    memcpy(line + length, buffer, chunk);
    length += chunk;
    line[length] = '\0';
    if (chunk > 0 && buffer[chunk - 1] == '\n') {
      break;
    }
  }

  if (length == 0) {
    return NULL;
  }

  return line;
}

fpv_ini_t* fpv_ini_load(const char* path, fpv_ini_error_t* error) {
  fpv_ini_t* ini = NULL;
  fpv_ini_section_t* current = NULL;
  fpv_ini_entry_t* last_entry = NULL;
  FILE* file = NULL;
  size_t line_number = 0;
  char* line = NULL;

  if (error) {
    error->line = 0;
    error->code = FPV_OK;
  }

  if (!path) {
    if (error) {
      error->code = FPV_ERR_INVALID_ARGUMENT;
    }
    return NULL;
  }

  file = fopen(path, "r");
  if (!file) {
    if (error) {
      error->code = FPV_ERR_IO;
    }
    return NULL;
  }

  ini = (fpv_ini_t*)calloc(1, sizeof(*ini));
  if (!ini) {
    fclose(file);
    if (error) {
      error->code = FPV_ERR_OUT_OF_MEMORY;
    }
    return NULL;
  }

  while (true) {
    bool oom = false;
    bool leading_space = false;
    line = fpv_read_line(file, &oom);
    if (!line) {
      if (oom) {
        if (error) {
          error->line = line_number + 1;
          error->code = FPV_ERR_OUT_OF_MEMORY;
        }
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }
      break;
    }
    char* trimmed = NULL;
    char* value = NULL;
    line_number++;

    leading_space = (line[0] == ' ' || line[0] == '\t');
    if (line_number == 1) {
      size_t line_length = strlen(line);
      if (line_length >= 3 &&
          (unsigned char)line[0] == 0xEF &&
          (unsigned char)line[1] == 0xBB &&
          (unsigned char)line[2] == 0xBF) {
        memmove(line, line + 3, strlen(line + 3) + 1);
      }
    }

    trimmed = fpv_trim(line);
    if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
      fpv_free(line);
      continue;
    }

    if (leading_space && current && last_entry) {
      if (fpv_ini_append_entry_value(last_entry, trimmed) != FPV_OK) {
        fpv_free(line);
        if (error) {
          error->line = line_number;
          error->code = FPV_ERR_OUT_OF_MEMORY;
        }
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }
      fpv_free(line);
      continue;
    }

    if (*trimmed == '[') {
      char* closing = strchr(trimmed, ']');
      char* name = NULL;
      if (!closing) {
        fpv_free(line);
        if (error) {
          error->line = line_number;
          error->code = FPV_ERR_PARSE;
        }
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }

      *closing = '\0';
      name = fpv_trim(trimmed + 1);
      value = fpv_trim(closing + 1);
      if (*name == '\0' ||
          (*value != '\0' && *value != ';' && *value != '#')) {
        fpv_free(line);
        if (error) {
          error->line = line_number;
          error->code = FPV_ERR_PARSE;
        }
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }

      fpv_ini_section_t* existing = fpv_ini_find_section(ini, name);
      if (existing) {
        current = existing;
        last_entry = NULL;
        fpv_free(line);
        continue;
      }

      current = (fpv_ini_section_t*)calloc(1, sizeof(*current));
      if (!current) {
        fpv_free(line);
        if (error) {
          error->line = line_number;
          error->code = FPV_ERR_OUT_OF_MEMORY;
        }
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }

      current->name = fpv_strdup(name);
      if (!current->name) {
        fpv_free(line);
        if (error) {
          error->line = line_number;
          error->code = FPV_ERR_OUT_OF_MEMORY;
        }
        fpv_ini_section_destroy(current);
        fpv_ini_destroy(ini);
        fclose(file);
        return NULL;
      }

      current->next = ini->sections;
      ini->sections = current;
      ini->section_count++;
      last_entry = NULL;
      fpv_free(line);
      continue;
    }

    if (!current) {
      fpv_free(line);
      if (error) {
        error->line = line_number;
        error->code = FPV_ERR_PARSE;
      }
      fpv_ini_destroy(ini);
      fclose(file);
      return NULL;
    }

    value = strpbrk(trimmed, ":=");
    if (!value) {
      fpv_free(line);
      if (error) {
        error->line = line_number;
        error->code = FPV_ERR_PARSE;
      }
      fpv_ini_destroy(ini);
      fclose(file);
      return NULL;
    }

    *value = '\0';
    value++;

    trimmed = fpv_trim(trimmed);
    value = fpv_trim(value);
    if (*trimmed == '\0') {
      fpv_free(line);
      if (error) {
        error->line = line_number;
        error->code = FPV_ERR_PARSE;
      }
      fpv_ini_destroy(ini);
      fclose(file);
      return NULL;
    }

    if (fpv_ini_set_entry(current, trimmed, value) != FPV_OK) {
      fpv_free(line);
      if (error) {
        error->line = line_number;
        error->code = FPV_ERR_OUT_OF_MEMORY;
      }
      fpv_ini_destroy(ini);
      fclose(file);
      return NULL;
    }
    last_entry = fpv_ini_find_entry(current, trimmed);

    fpv_free(line);
  }

  fclose(file);
  return ini;
}

void fpv_ini_destroy(fpv_ini_t* ini) {
  fpv_ini_section_t* section = NULL;

  if (!ini) {
    return;
  }

  section = ini->sections;
  while (section) {
    fpv_ini_section_t* next = section->next;
    fpv_ini_section_destroy(section);
    section = next;
  }

  free(ini);
}

const char* fpv_ini_get(
    const fpv_ini_t* ini,
    const char* section,
    const char* key) {
  fpv_ini_section_t* current = NULL;
  fpv_ini_entry_t* entry = NULL;

  if (!ini || !section || !key) {
    return NULL;
  }

  current = fpv_ini_find_section((fpv_ini_t*)ini, section);
  if (!current) {
    return NULL;
  }

  entry = fpv_ini_find_entry(current, key);
  return entry ? entry->value : NULL;
}

size_t fpv_ini_section_count(const fpv_ini_t* ini) {
  if (!ini) {
    return 0;
  }
  return ini->section_count;
}

const char* fpv_ini_section_name(const fpv_ini_t* ini, size_t index) {
  size_t current_index = 0;
  fpv_ini_section_t* section = NULL;

  if (!ini) {
    return NULL;
  }

  for (section = ini->sections; section; section = section->next) {
    if (current_index == index) {
      return section->name;
    }
    current_index++;
  }

  return NULL;
}

size_t fpv_ini_entry_count(const fpv_ini_t* ini, const char* section) {
  fpv_ini_section_t* current = NULL;

  if (!ini || !section) {
    return 0;
  }

  current = fpv_ini_find_section((fpv_ini_t*)ini, section);
  return current ? current->entry_count : 0;
}

const char* fpv_ini_entry_key(
    const fpv_ini_t* ini,
    const char* section,
    size_t index) {
  fpv_ini_section_t* current = NULL;
  fpv_ini_entry_t* entry = NULL;
  size_t current_index = 0;

  if (!ini || !section) {
    return NULL;
  }

  current = fpv_ini_find_section((fpv_ini_t*)ini, section);
  if (!current) {
    return NULL;
  }

  for (entry = current->entries; entry; entry = entry->next) {
    if (current_index == index) {
      return entry->key;
    }
    current_index++;
  }

  return NULL;
}

const char* fpv_ini_entry_value(
    const fpv_ini_t* ini,
    const char* section,
    size_t index) {
  fpv_ini_section_t* current = NULL;
  fpv_ini_entry_t* entry = NULL;
  size_t current_index = 0;

  if (!ini || !section) {
    return NULL;
  }

  current = fpv_ini_find_section((fpv_ini_t*)ini, section);
  if (!current) {
    return NULL;
  }

  for (entry = current->entries; entry; entry = entry->next) {
    if (current_index == index) {
      return entry->value;
    }
    current_index++;
  }

  return NULL;
}

fpv_result_t fpv_ini_set(
    fpv_ini_t* ini,
    const char* section,
    const char* key,
    const char* value) {
  if (!ini || !section || !key) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_ini_section_t* target = fpv_ini_find_section(ini, section);
  if (!target) {
    target = (fpv_ini_section_t*)calloc(1, sizeof(*target));
    if (!target) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    target->name = fpv_strdup(section);
    if (!target->name) {
      fpv_ini_section_destroy(target);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    target->next = ini->sections;
    ini->sections = target;
    ini->section_count++;
  }

  return fpv_ini_set_entry(target, key, value ? value : "");
}

fpv_result_t fpv_ini_remove_section(
    fpv_ini_t* ini,
    const char* section) {
  if (!ini || !section) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_ini_section_t* prev = NULL;
  fpv_ini_section_t* current = ini->sections;
  while (current) {
    if (strcmp(current->name, section) == 0) {
      if (prev) {
        prev->next = current->next;
      } else {
        ini->sections = current->next;
      }
      ini->section_count--;
      fpv_ini_section_destroy(current);
      return FPV_OK;
    }
    prev = current;
    current = current->next;
  }

  return FPV_OK;
}

fpv_result_t fpv_ini_remove_entry(
    fpv_ini_t* ini,
    const char* section,
    const char* key) {
  if (!ini || !section || !key) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_ini_section_t* target = fpv_ini_find_section(ini, section);
  if (!target) {
    return FPV_OK;
  }

  fpv_ini_entry_t* prev = NULL;
  fpv_ini_entry_t* entry = target->entries;
  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      if (prev) {
        prev->next = entry->next;
      } else {
        target->entries = entry->next;
      }
      target->entry_count--;
      fpv_free(entry->key);
      fpv_free(entry->value);
      free(entry);
      return FPV_OK;
    }
    prev = entry;
    entry = entry->next;
  }
  return FPV_OK;
}

static bool fpv_ini_save_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len) {
  if (*length + data_len + 1 > *capacity) {
    size_t next = *capacity == 0 ? 256 : *capacity;
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

static bool fpv_ini_save_append_str(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value) {
  return fpv_ini_save_append(
      buffer,
      length,
      capacity,
      value ? value : "",
      value ? strlen(value) : 0);
}

static bool fpv_ini_save_entry(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* key,
    const char* value) {
  if (!fpv_ini_save_append_str(buffer, length, capacity, key)) {
    return false;
  }
  if (!fpv_ini_save_append(buffer, length, capacity, " : ", 3)) {
    return false;
  }
  if (!value) {
    return fpv_ini_save_append(buffer, length, capacity, "\n", 1);
  }

  const char* line = value;
  const char* next = strchr(line, '\n');
  if (!next) {
    if (!fpv_ini_save_append_str(buffer, length, capacity, line)) {
      return false;
    }
    return fpv_ini_save_append(buffer, length, capacity, "\n", 1);
  }

  while (next) {
    if (!fpv_ini_save_append(
            buffer,
            length,
            capacity,
            line,
            (size_t)(next - line))) {
      return false;
    }
    if (!fpv_ini_save_append(buffer, length, capacity, "\n ", 2)) {
      return false;
    }
    line = next + 1;
    next = strchr(line, '\n');
  }
  if (*line) {
    if (!fpv_ini_save_append_str(buffer, length, capacity, line)) {
      return false;
    }
  }
  return fpv_ini_save_append(buffer, length, capacity, "\n", 1);
}

fpv_result_t fpv_ini_save(
    const fpv_ini_t* ini,
    const char* path) {
  if (!ini || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;

  size_t section_count = 0;
  for (const fpv_ini_section_t* section = ini->sections;
       section;
       section = section->next) {
    section_count++;
  }

  fpv_ini_section_t** sections = NULL;
  if (section_count > 0) {
    sections = (fpv_ini_section_t**)calloc(section_count, sizeof(*sections));
    if (!sections) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    size_t idx = 0;
    for (fpv_ini_section_t* section = ini->sections;
         section;
         section = section->next) {
      sections[idx++] = section;
    }
  }

  for (size_t s = section_count; s-- > 0;) {
    fpv_ini_section_t* section = sections ? sections[s] : NULL;
    if (!section || !section->name) {
      continue;
    }
    if (!fpv_ini_save_append(&buffer, &length, &capacity, "[", 1) ||
        !fpv_ini_save_append_str(&buffer, &length, &capacity, section->name) ||
        !fpv_ini_save_append(&buffer, &length, &capacity, "]\n", 2)) {
      fpv_free(sections);
      fpv_free(buffer);
      return FPV_ERR_OUT_OF_MEMORY;
    }

    size_t entry_count = 0;
    for (fpv_ini_entry_t* entry = section->entries;
         entry;
         entry = entry->next) {
      entry_count++;
    }

    fpv_ini_entry_t** entries = NULL;
    if (entry_count > 0) {
      entries = (fpv_ini_entry_t**)calloc(entry_count, sizeof(*entries));
      if (!entries) {
        fpv_free(sections);
        fpv_free(buffer);
        return FPV_ERR_OUT_OF_MEMORY;
      }
      size_t idx = 0;
      for (fpv_ini_entry_t* entry = section->entries;
           entry;
           entry = entry->next) {
        entries[idx++] = entry;
      }
    }

    for (size_t e = entry_count; e-- > 0;) {
      fpv_ini_entry_t* entry = entries ? entries[e] : NULL;
      if (!entry || !entry->key) {
        continue;
      }
      if (!fpv_ini_save_entry(
              &buffer,
              &length,
              &capacity,
              entry->key,
              entry->value ? entry->value : "")) {
        fpv_free(entries);
        fpv_free(sections);
        fpv_free(buffer);
        return FPV_ERR_OUT_OF_MEMORY;
      }
    }

    fpv_free(entries);

    if (s > 0) {
      if (!fpv_ini_save_append(&buffer, &length, &capacity, "\n", 1)) {
        fpv_free(sections);
        fpv_free(buffer);
        return FPV_ERR_OUT_OF_MEMORY;
      }
    }
  }

  fpv_free(sections);

  if (!buffer) {
    buffer = fpv_strdup("");
    if (!buffer) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  fpv_result_t result = fpv_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  fpv_free(buffer);
  return result;
}
