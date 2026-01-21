/* FunPay Vertex database access utilities. */

#ifndef FPV_DB_H
#define FPV_DB_H

#include <stddef.h>

#include "fpv_core/fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_db_backend {
  FPV_DB_BACKEND_UNKNOWN = 0,
  FPV_DB_BACKEND_SQLITE = 1,
  FPV_DB_BACKEND_POSTGRES = 2
} fpv_db_backend_t;

typedef struct fpv_db_config {
  const char* url;
  const char* data_dir;
  const char* sqlite_path;
} fpv_db_config_t;

typedef struct fpv_db fpv_db_t;

typedef struct fpv_db_result {
  size_t row_count;
  size_t column_count;
  char*** rows;
} fpv_db_result_t;

fpv_result_t fpv_db_open(const fpv_db_config_t* config, fpv_db_t** out_db);
void fpv_db_close(fpv_db_t* db);
fpv_db_backend_t fpv_db_backend(const fpv_db_t* db);
const char* fpv_db_error(const fpv_db_t* db);

fpv_result_t fpv_db_exec(fpv_db_t* db, const char* sql);
fpv_result_t fpv_db_exec_params(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count);
fpv_result_t fpv_db_query(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count,
    fpv_db_result_t** out_result);
void fpv_db_result_destroy(fpv_db_result_t* result);

fpv_result_t fpv_db_begin(fpv_db_t* db);
fpv_result_t fpv_db_commit(fpv_db_t* db);
fpv_result_t fpv_db_rollback(fpv_db_t* db);

fpv_result_t fpv_db_migrate(fpv_db_t* db);
fpv_result_t fpv_db_backup(fpv_db_t* db, const char* path);
fpv_result_t fpv_db_restore(fpv_db_t* db, const char* path);

#ifdef __cplusplus
}
#endif

#endif
