/* FunPay Vertex product storage helpers implementation. */

#include "core/data/fpv_products.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/io/fpv_fs.h"

#include "core/base/fpv_string.h"


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
  size_t length = data ? strlen(data) : 0;
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

static char* fpv_build_temp_path(const char* path) {
  size_t length = strlen(path);
  char* temp = (char*)malloc(length + 5);
  if (!temp) {
    return NULL;
  }
  memcpy(temp, path, length);
  memcpy(temp + length, ".tmp", 5);
  return temp;
}

static bool fpv_line_has_content(const char* line) {
  return line && line[0] != '\0';
}

void fpv_products_free(char** products, size_t count) {
  if (!products) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(products[i]);
  }
  fpv_free(products);
}

fpv_result_t fpv_products_take(
    const char* path,
    uint32_t amount,
    char*** out_products,
    size_t* out_count,
    size_t* out_remaining) {
  if (!path || !out_products || !out_count || amount == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_products = NULL;
  *out_count = 0;
  if (out_remaining) {
    *out_remaining = 0;
  }

  if (!fpv_fs_exists(path)) {
    return FPV_ERR_NOT_FOUND;
  }

  size_t size = 0;
  char* content = fpv_read_file(path, &size);
  if (!content) {
    return FPV_ERR_IO;
  }

  size_t lines_capacity = 0;
  size_t line_count = 0;
  char** lines = NULL;
  char* start = content;
  for (char* ptr = content; ; ptr++) {
    if (*ptr == '\n' || *ptr == '\0') {
      char* end = ptr;
      if (end > start && end[-1] == '\r') {
        end--;
      }
      if (end > start) {
        size_t len = (size_t)(end - start);
        char* line = fpv_strdup_n(start, len);
        if (line) {
          if (fpv_line_has_content(line)) {
            if (line_count + 1 > lines_capacity) {
              size_t next = lines_capacity == 0 ? 16 : lines_capacity * 2;
              char** grown = (char**)realloc(lines, next * sizeof(*grown));
              if (!grown) {
                fpv_free(line);
                fpv_products_free(lines, line_count);
                fpv_free(content);
                return FPV_ERR_OUT_OF_MEMORY;
              }
              lines = grown;
              lines_capacity = next;
            }
            lines[line_count++] = line;
          } else {
            fpv_free(line);
          }
        }
      }
      start = ptr + 1;
      if (*ptr == '\0') {
        break;
      }
    }
  }
  fpv_free(content);

  if (line_count == 0) {
    fpv_products_free(lines, line_count);
    return FPV_ERR_INVALID_STATE;
  }
  if (line_count < amount) {
    fpv_products_free(lines, line_count);
    return FPV_ERR_INVALID_STATE;
  }

  char** products = (char**)calloc(amount, sizeof(*products));
  if (!products) {
    fpv_products_free(lines, line_count);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (uint32_t i = 0; i < amount; i++) {
    products[i] = lines[i];
    lines[i] = NULL;
  }

  size_t remaining = line_count - amount;
  char* temp_path = fpv_build_temp_path(path);
  if (!temp_path) {
    fpv_products_free(products, amount);
    fpv_products_free(lines, line_count);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (size_t i = amount; i < line_count; i++) {
    if (i > amount) {
      fpv_buffer_append(&buffer, &length, &capacity, "\n", 1);
    }
    fpv_buffer_append(
        &buffer,
        &length,
        &capacity,
        lines[i],
        strlen(lines[i]));
  }

  bool write_ok = fpv_write_file(temp_path, buffer ? buffer : "");
  fpv_free(buffer);
  if (!write_ok || fpv_fs_rename(temp_path, path) != FPV_OK) {
    fpv_free(temp_path);
    fpv_products_free(products, amount);
    fpv_products_free(lines, line_count);
    return FPV_ERR_IO;
  }
  fpv_free(temp_path);
  fpv_products_free(lines, line_count);

  *out_products = products;
  *out_count = amount;
  if (out_remaining) {
    *out_remaining = remaining;
  }
  return FPV_OK;
}

fpv_result_t fpv_products_peek(
    const char* path,
    char** out_product,
    size_t* out_remaining) {
  if (!path || !out_product) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_product = NULL;
  if (out_remaining) {
    *out_remaining = 0;
  }

  if (!fpv_fs_exists(path)) {
    return FPV_ERR_NOT_FOUND;
  }

  size_t size = 0;
  char* content = fpv_read_file(path, &size);
  if (!content) {
    return FPV_ERR_IO;
  }

  size_t line_count = 0;
  char* first_line = NULL;
  char* start = content;
  for (char* ptr = content; ; ptr++) {
    if (*ptr == '\n' || *ptr == '\0') {
      char* end = ptr;
      if (end > start && end[-1] == '\r') {
        end--;
      }
      if (end > start) {
        size_t len = (size_t)(end - start);
        char* line = fpv_strdup_n(start, len);
        if (line) {
          if (fpv_line_has_content(line)) {
            if (!first_line) {
              first_line = line;
            } else {
              fpv_free(line);
            }
            line_count++;
          } else {
            fpv_free(line);
          }
        }
      }
      start = ptr + 1;
      if (*ptr == '\0') {
        break;
      }
    }
  }
  fpv_free(content);

  if (!first_line) {
    return FPV_ERR_INVALID_STATE;
  }
  *out_product = first_line;
  if (out_remaining) {
    *out_remaining = line_count > 0 ? line_count - 1 : 0;
  }
  return FPV_OK;
}

fpv_result_t fpv_products_restore(
    const char* path,
    char** products,
    size_t count) {
  if (!path || !products || count == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* existing = NULL;
  size_t existing_size = 0;
  if (fpv_fs_exists(path)) {
    existing = fpv_read_file(path, &existing_size);
  }

  char* temp_path = fpv_build_temp_path(path);
  if (!temp_path) {
    fpv_free(existing);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      fpv_buffer_append(&buffer, &length, &capacity, "\n", 1);
    }
    fpv_buffer_append(
        &buffer,
        &length,
        &capacity,
        products[i],
        strlen(products[i]));
  }
  if (existing && existing[0]) {
    if (length > 0) {
      fpv_buffer_append(&buffer, &length, &capacity, "\n", 1);
    }
    fpv_buffer_append(
        &buffer,
        &length,
        &capacity,
        existing,
        strlen(existing));
  }

  bool write_ok = fpv_write_file(temp_path, buffer ? buffer : "");
  fpv_free(buffer);
  fpv_free(existing);
  if (!write_ok || fpv_fs_rename(temp_path, path) != FPV_OK) {
    fpv_free(temp_path);
    return FPV_ERR_IO;
  }
  fpv_free(temp_path);
  return FPV_OK;
}
