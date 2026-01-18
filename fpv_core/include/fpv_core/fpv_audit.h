/* FunPay Vertex audit log storage API. */

#ifndef FPV_AUDIT_H
#define FPV_AUDIT_H

#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_audit_store fpv_audit_store_t;

FPV_CORE_API fpv_audit_store_t* fpv_audit_store_open(
    const char* data_dir,
    const char* db_url,
    uint32_t retention_days,
    fpv_result_t* out_result);
FPV_CORE_API void fpv_audit_store_destroy(fpv_audit_store_t* store);

FPV_CORE_API fpv_result_t fpv_audit_store_append(
    const fpv_audit_store_t* store,
    const fpv_audit_log_entry_t* entry);
FPV_CORE_API fpv_result_t fpv_audit_store_prune(
    const fpv_audit_store_t* store,
    uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
