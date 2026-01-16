/* FunPay Vertex core logging API. */

#ifndef FPV_LOG_H
#define FPV_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "fpv_event_bus.h"
#include "fpv_export.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_logger fpv_logger_t;

typedef struct fpv_log_config {
  const char* logs_dir;
  const char* base_name;
  fpv_log_level_t min_level;
  bool console;
} fpv_log_config_t;

FPV_CORE_API fpv_logger_t* fpv_logger_create(const fpv_log_config_t* config);
FPV_CORE_API void fpv_logger_destroy(fpv_logger_t* logger);

FPV_CORE_API fpv_result_t fpv_logger_log(
    fpv_logger_t* logger,
    fpv_log_level_t level,
    const char* component,
    const char* message,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_result_t fpv_logger_logf(
    fpv_logger_t* logger,
    fpv_log_level_t level,
    const char* component,
    const char* format,
    ...);

#ifdef __cplusplus
}
#endif

#endif
