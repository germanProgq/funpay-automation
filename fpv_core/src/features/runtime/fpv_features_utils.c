#include "features/runtime/fpv_features_internal.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

void fpv_sleep_ms(uint32_t delay_ms) {
#if defined(_WIN32)
  Sleep(delay_ms);
#else
  struct timespec ts;
  ts.tv_sec = delay_ms / 1000U;
  ts.tv_nsec = (long)(delay_ms % 1000U) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

void fpv_features_log(
    fpv_feature_state_t* state,
    fpv_log_level_t level,
    const char* message) {
  if (!state || !message || !state->logger) {
    return;
  }
  fpv_logger_log(state->logger, level, "features", message, fpv_time_now_ms());
}


char* fpv_features_format_delivery_error(
    fpv_feature_state_t* state,
    const char* order_id) {
  if (!order_id || !order_id[0]) {
    return fpv_strdup("Failed to deliver goods for order.");
  }
  if (state && state->localizer) {
    char* formatted = fpv_localizer_format(
        state->localizer,
        "ntfc_delivery_failed",
        (const char*[]){order_id},
        1);
    if (formatted) {
      return formatted;
    }
  }
  const char* format = "Failed to deliver goods for order %s.";
  size_t size = strlen(format) + strlen(order_id) + 8;
  char* text = (char*)malloc(size);
  if (text) {
    snprintf(text, size, format, order_id);
  }
  return text;
}


void fpv_features_notify_delivery_error(
    fpv_feature_state_t* state,
    const char* order_id,
    const char* buyer_username) {
  if (!state || !state->telegram || !order_id) {
    return;
  }
  char* text = fpv_features_format_delivery_error(state, order_id);
  if (!text) {
    return;
  }
  fpv_telegram_service_notify_delivery(
      state->telegram, order_id, buyer_username, text, 0, false);
  fpv_free(text);
}


bool fpv_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len) {
  if (*length + data_len + 1 > *capacity) {
    size_t next = *capacity == 0 ? 64 : *capacity;
    while (next < *length + data_len + 1) {
      next *= 2;
    }
    char* grown = (char*)realloc(*buffer, next);
    if (!grown) {
      return false;
    }
    *buffer = grown;
    *capacity = next;
  }
  memcpy(*buffer + *length, data, data_len);
  *length += data_len;
  (*buffer)[*length] = '\0';
  return true;
}


bool fpv_buffer_append_char(
    char** buffer,
    size_t* length,
    size_t* capacity,
    char ch) {
  return fpv_buffer_append(buffer, length, capacity, &ch, 1);
}


static char* fpv_escape_html(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (const char* ptr = text; *ptr; ptr++) {
    switch (*ptr) {
      case '&':
        fpv_buffer_append(&buffer, &length, &capacity, "&amp;", 5);
        break;
      case '<':
        fpv_buffer_append(&buffer, &length, &capacity, "&lt;", 4);
        break;
      case '>':
        fpv_buffer_append(&buffer, &length, &capacity, "&gt;", 4);
        break;
      default:
        fpv_buffer_append_char(&buffer, &length, &capacity, *ptr);
        break;
    }
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}


bool fpv_features_is_sras_error(const fpv_funpay_error_t* error) {
  if (!error) {
    return false;
  }
  if (error->code == FPV_FUNPAY_ERR_NOT_FOUND) {
    return true;
  }
  if (error->code == FPV_FUNPAY_ERR_REQUEST_FAILED &&
      error->http_status == 404 &&
      error->url &&
      strstr(error->url, "/lots/offer") != NULL) {
    return true;
  }
  return error->code == FPV_FUNPAY_ERR_PARSE &&
      error->message &&
      strcmp(error->message, "Balance selector missing") == 0;
}
