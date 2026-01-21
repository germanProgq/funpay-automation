/* FunPay Vertex core time utilities. */

#include "core/base/fpv_time.h"


#include <stdio.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/time.h>
#endif

uint64_t fpv_time_now_ms(void) {
#if defined(_WIN32)
  FILETIME ft;
  ULARGE_INTEGER value;
  const uint64_t epoch_diff = 116444736000000000ULL;

  GetSystemTimeAsFileTime(&ft);
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  if (value.QuadPart < epoch_diff) {
    return 0;
  }
  return (value.QuadPart - epoch_diff) / 10000ULL;
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000ULL +
         (uint64_t)(ts.tv_nsec / 1000000ULL);
#endif
}

static bool fpv_time_to_local(time_t seconds, struct tm* out_tm) {
#if defined(_WIN32)
  return localtime_s(out_tm, &seconds) == 0;
#else
  return localtime_r(&seconds, out_tm) != NULL;
#endif
}

bool fpv_time_format_local(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len) {
  struct tm tm_value;
  const time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  const unsigned int millis = (unsigned int)(timestamp_ms % 1000ULL);
  int written = 0;

  if (!buffer || buffer_len == 0) {
    return false;
  }

  if (!fpv_time_to_local(seconds, &tm_value)) {
    return false;
  }

  written = snprintf(
      buffer,
      buffer_len,
      "%04d-%02d-%02d %02d:%02d:%02d.%03u",
      tm_value.tm_year + 1900,
      tm_value.tm_mon + 1,
      tm_value.tm_mday,
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec,
      millis);

  return written > 0 && (size_t)written < buffer_len;
}

bool fpv_time_format_compact(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len) {
  struct tm tm_value;
  const time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  int written = 0;

  if (!buffer || buffer_len == 0) {
    return false;
  }

  if (!fpv_time_to_local(seconds, &tm_value)) {
    return false;
  }

  written = snprintf(
      buffer,
      buffer_len,
      "%04d%02d%02d-%02d%02d%02d",
      tm_value.tm_year + 1900,
      tm_value.tm_mon + 1,
      tm_value.tm_mday,
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec);

  return written > 0 && (size_t)written < buffer_len;
}
