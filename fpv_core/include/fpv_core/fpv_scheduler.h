/* FunPay Vertex core scheduler API. */

#ifndef FPV_SCHEDULER_H
#define FPV_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fpv_task_fn)(void* user_data);

typedef struct fpv_scheduler fpv_scheduler_t;

FPV_CORE_API fpv_scheduler_t* fpv_scheduler_create(size_t worker_count);
FPV_CORE_API void fpv_scheduler_destroy(fpv_scheduler_t* scheduler);

FPV_CORE_API fpv_result_t fpv_scheduler_enqueue(
    fpv_scheduler_t* scheduler,
    fpv_task_fn fn,
    void* user_data);
FPV_CORE_API fpv_result_t fpv_scheduler_schedule(
    fpv_scheduler_t* scheduler,
    uint64_t run_at_ms,
    fpv_task_fn fn,
    void* user_data);
FPV_CORE_API fpv_result_t fpv_scheduler_schedule_delay(
    fpv_scheduler_t* scheduler,
    uint64_t delay_ms,
    fpv_task_fn fn,
    void* user_data);
FPV_CORE_API size_t fpv_scheduler_worker_count(
    const fpv_scheduler_t* scheduler);

#ifdef __cplusplus
}
#endif

#endif
