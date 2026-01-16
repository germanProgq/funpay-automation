/* FunPay Vertex core string utilities. */

#include "fpv_string.h"

#include <stdlib.h>
#include <string.h>

char* fpv_strdup(const char* value) {
  size_t length = 0;
  char* copy = NULL;

  if (!value) {
    return NULL;
  }

  length = strlen(value);
  copy = (char*)malloc(length + 1);
  if (!copy) {
    return NULL;
  }

  memcpy(copy, value, length);
  copy[length] = '\0';
  return copy;
}

char* fpv_strdup_n(const char* value, size_t max_len) {
  size_t length = 0;
  char* copy = NULL;

  if (!value) {
    return NULL;
  }

  while (length < max_len && value[length] != '\0') {
    length++;
  }

  copy = (char*)malloc(length + 1);
  if (!copy) {
    return NULL;
  }

  memcpy(copy, value, length);
  copy[length] = '\0';
  return copy;
}

void fpv_free(void* ptr) {
  free(ptr);
}
