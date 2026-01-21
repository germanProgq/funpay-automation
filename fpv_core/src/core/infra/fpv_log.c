/* FunPay Vertex core logging implementation. */

#include "fpv_core/fpv_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/io/fpv_fs.h"

#include "core/base/fpv_platform.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


struct fpv_logger {
  fpv_mutex_t mutex;
  FILE* file;
  fpv_log_level_t min_level;
  bool console;
};

static const char* fpv_log_level_label(fpv_log_level_t level) {
  switch (level) {
    case FPV_LOG_DEBUG:
      return "DEBUG";
    case FPV_LOG_INFO:
      return "INFO";
    case FPV_LOG_WARNING:
      return "WARN";
    case FPV_LOG_ERROR:
      return "ERROR";
    default:
      return "LOG";
  }
}

static void fpv_log_write_escaped(FILE* file, const char* message) {
  if (!file || !message) {
    return;
  }

  for (const char* ptr = message; *ptr; ptr++) {
    char value = *ptr;
    if (value == '\n' || value == '\r') {
      value = ' ';
    } else if (value == '"') {
      value = '\'';
    }
    fputc(value, file);
  }
}

fpv_logger_t* fpv_logger_create(const fpv_log_config_t* config) {
  fpv_logger_t* logger = NULL;
  char timestamp[32];
  char filename[128];
  char* path = NULL;
  uint64_t now_ms = 0;
  int written = 0;

  if (!config || !config->logs_dir || !config->logs_dir[0]) {
    return NULL;
  }

  if (fpv_fs_ensure_dir(config->logs_dir) != FPV_OK) {
    return NULL;
  }

  now_ms = fpv_time_now_ms();
  if (!fpv_time_format_compact(now_ms, timestamp, sizeof(timestamp))) {
    return NULL;
  }

  written = snprintf(
      filename,
      sizeof(filename),
      "%s-%s.log",
      config->base_name ? config->base_name : "fpv",
      timestamp);
  if (written <= 0 || (size_t)written >= sizeof(filename)) {
    return NULL;
  }

  path = fpv_path_join(config->logs_dir, filename);
  if (!path) {
    return NULL;
  }

  logger = (fpv_logger_t*)calloc(1, sizeof(*logger));
  if (!logger) {
    fpv_free(path);
    return NULL;
  }

  logger->file = fopen(path, "a");
  fpv_free(path);
  if (!logger->file) {
    free(logger);
    return NULL;
  }

  if (!fpv_mutex_init(&logger->mutex)) {
    fclose(logger->file);
    free(logger);
    return NULL;
  }

  logger->min_level = config->min_level;
  logger->console = config->console;
  return logger;
}

void fpv_logger_destroy(fpv_logger_t* logger) {
  if (!logger) {
    return;
  }

  fpv_mutex_lock(&logger->mutex);
  if (logger->file) {
    fclose(logger->file);
    logger->file = NULL;
  }
  fpv_mutex_unlock(&logger->mutex);

  fpv_mutex_destroy(&logger->mutex);
  free(logger);
}

fpv_result_t fpv_logger_log(
    fpv_logger_t* logger,
    fpv_log_level_t level,
    const char* component,
    const char* message,
    uint64_t timestamp_ms) {
  char timestamp[32];
  const char* label = fpv_log_level_label(level);
  const char* scope = component ? component : "core";
  FILE* console = NULL;
  uint64_t now_ms = timestamp_ms;

  if (!logger || !message) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (level < logger->min_level) {
    return FPV_OK;
  }

  if (now_ms == 0) {
    now_ms = fpv_time_now_ms();
  }

  if (!fpv_time_format_local(now_ms, timestamp, sizeof(timestamp))) {
    snprintf(timestamp, sizeof(timestamp), "0000-00-00 00:00:00.000");
  }

  if (logger->console) {
    console = (level >= FPV_LOG_WARNING) ? stderr : stdout;
  }

  fpv_mutex_lock(&logger->mutex);
  if (logger->file) {
    fprintf(
        logger->file,
        "%s level=%s component=%s msg=\"",
        timestamp,
        label,
        scope);
    fpv_log_write_escaped(logger->file, message);
    fprintf(logger->file, "\"\n");
    fflush(logger->file);
  }

  if (console) {
    fprintf(
        console,
        "%s [%s] %s: ",
        timestamp,
        label,
        scope);
    fpv_log_write_escaped(console, message);
    fprintf(console, "\n");
    fflush(console);
  }
  fpv_mutex_unlock(&logger->mutex);

  return FPV_OK;
}

fpv_result_t fpv_logger_logf(
    fpv_logger_t* logger,
    fpv_log_level_t level,
    const char* component,
    const char* format,
    ...) {
  va_list args;
  va_list copy;
  int needed = 0;
  fpv_result_t result = FPV_OK;
  char stack_buffer[512];
  char* buffer = NULL;

  if (!logger || !format) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  va_start(args, format);
  va_copy(copy, args);
  needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);

  if (needed < 0) {
    va_end(args);
    return FPV_ERR_INTERNAL;
  }

  if ((size_t)needed < sizeof(stack_buffer)) {
    vsnprintf(stack_buffer, sizeof(stack_buffer), format, args);
    va_end(args);
    return fpv_logger_log(
        logger,
        level,
        component,
        stack_buffer,
        fpv_time_now_ms());
  }

  buffer = (char*)malloc((size_t)needed + 1);
  if (!buffer) {
    va_end(args);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  vsnprintf(buffer, (size_t)needed + 1, format, args);
  va_end(args);

  result = fpv_logger_log(
      logger,
      level,
      component,
      buffer,
      fpv_time_now_ms());
  free(buffer);
  return result;
}
