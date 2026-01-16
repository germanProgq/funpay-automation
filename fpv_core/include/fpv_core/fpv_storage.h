/* FunPay Vertex core storage layout API. */

#ifndef FPV_STORAGE_H
#define FPV_STORAGE_H

#include "fpv_export.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_storage_layout {
  char* data_dir;
  char* config_dir;
  char* logs_dir;
  char* cache_dir;
  char* products_dir;
  char* plugins_dir;
} fpv_storage_layout_t;

FPV_CORE_API fpv_result_t fpv_storage_prepare(
    const char* data_dir,
    const char* config_dir,
    const char* logs_dir,
    const char* plugins_dir,
    fpv_storage_layout_t* layout);
FPV_CORE_API void fpv_storage_layout_destroy(fpv_storage_layout_t* layout);

#ifdef __cplusplus
}
#endif

#endif
