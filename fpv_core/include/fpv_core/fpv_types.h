/* FunPay Vertex core common types. */

#ifndef FPV_TYPES_H
#define FPV_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_core_status {
  FPV_CORE_STOPPED = 0,
  FPV_CORE_STARTING = 1,
  FPV_CORE_RUNNING = 2,
  FPV_CORE_STOPPING = 3,
  FPV_CORE_ERROR = 4
} fpv_core_status_t;

#ifdef __cplusplus
}
#endif

#endif
