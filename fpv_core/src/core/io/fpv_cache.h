/* FunPay Vertex cache helpers. */

#ifndef FPV_CACHE_H
#define FPV_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_core/fpv_result.h"

typedef struct fpv_string_list {
  char** items;
  size_t count;
} fpv_string_list_t;

void fpv_string_list_destroy(fpv_string_list_t* list);

fpv_result_t fpv_cache_load_strings(
    const char* path,
    fpv_string_list_t* out_list);
fpv_result_t fpv_cache_save_strings(
    const char* path,
    const fpv_string_list_t* list);

fpv_result_t fpv_cache_load_uint64(
    const char* path,
    uint64_t** out_values,
    size_t* out_count);
fpv_result_t fpv_cache_save_uint64(
    const char* path,
    const uint64_t* values,
    size_t count);

#endif
