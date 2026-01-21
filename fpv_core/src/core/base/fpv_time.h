/* FunPay Vertex core time utilities. */

#ifndef FPV_TIME_H
#define FPV_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint64_t fpv_time_now_ms(void);
bool fpv_time_format_local(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len);
bool fpv_time_format_compact(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len);

#endif
