/* FunPay Vertex core INI configuration loader API. */

#ifndef FPV_INI_H
#define FPV_INI_H

#include <stddef.h>

#include "fpv_export.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_ini fpv_ini_t;

typedef struct fpv_ini_error {
  size_t line;
  fpv_result_t code;
} fpv_ini_error_t;

FPV_CORE_API fpv_ini_t* fpv_ini_load(
    const char* path,
    fpv_ini_error_t* error);
FPV_CORE_API void fpv_ini_destroy(fpv_ini_t* ini);

FPV_CORE_API const char* fpv_ini_get(
    const fpv_ini_t* ini,
    const char* section,
    const char* key);
FPV_CORE_API size_t fpv_ini_section_count(const fpv_ini_t* ini);
FPV_CORE_API const char* fpv_ini_section_name(
    const fpv_ini_t* ini,
    size_t index);
FPV_CORE_API size_t fpv_ini_entry_count(
    const fpv_ini_t* ini,
    const char* section);
FPV_CORE_API const char* fpv_ini_entry_key(
    const fpv_ini_t* ini,
    const char* section,
    size_t index);
FPV_CORE_API const char* fpv_ini_entry_value(
    const fpv_ini_t* ini,
    const char* section,
    size_t index);
FPV_CORE_API fpv_result_t fpv_ini_set(
    fpv_ini_t* ini,
    const char* section,
    const char* key,
    const char* value);
FPV_CORE_API fpv_result_t fpv_ini_remove_section(
    fpv_ini_t* ini,
    const char* section);
FPV_CORE_API fpv_result_t fpv_ini_remove_entry(
    fpv_ini_t* ini,
    const char* section,
    const char* key);
FPV_CORE_API fpv_result_t fpv_ini_save(
    const fpv_ini_t* ini,
    const char* path);

#ifdef __cplusplus
}
#endif

#endif
