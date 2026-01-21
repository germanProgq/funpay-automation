/* FunPay Vertex JSON utilities. */

#ifndef FPV_JSON_H
#define FPV_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_core/fpv_result.h"

typedef enum fpv_json_type {
  FPV_JSON_NULL = 0,
  FPV_JSON_BOOL = 1,
  FPV_JSON_NUMBER = 2,
  FPV_JSON_STRING = 3,
  FPV_JSON_ARRAY = 4,
  FPV_JSON_OBJECT = 5
} fpv_json_type_t;

typedef struct fpv_json_value fpv_json_value_t;

typedef struct fpv_json_error {
  size_t line;
  size_t column;
  char message[128];
} fpv_json_error_t;

fpv_result_t fpv_json_parse(
    const char* input,
    size_t length,
    fpv_json_value_t** out,
    fpv_json_error_t* error);
void fpv_json_destroy(fpv_json_value_t* value);

const fpv_json_value_t* fpv_json_object_get(
    const fpv_json_value_t* value,
    const char* key);
size_t fpv_json_object_size(const fpv_json_value_t* value);
const char* fpv_json_object_key(
    const fpv_json_value_t* value,
    size_t index);
const fpv_json_value_t* fpv_json_object_value(
    const fpv_json_value_t* value,
    size_t index);
const fpv_json_value_t* fpv_json_array_get(
    const fpv_json_value_t* value,
    size_t index);
size_t fpv_json_array_size(const fpv_json_value_t* value);

const char* fpv_json_string(const fpv_json_value_t* value);
const char* fpv_json_number_raw(const fpv_json_value_t* value);
bool fpv_json_bool(const fpv_json_value_t* value, bool fallback);
bool fpv_json_is_type(const fpv_json_value_t* value, fpv_json_type_t type);

bool fpv_json_number_to_int64(const fpv_json_value_t* value, int64_t* out);
bool fpv_json_number_to_uint64(const fpv_json_value_t* value, uint64_t* out);
bool fpv_json_number_to_double(const fpv_json_value_t* value, double* out);

#endif
