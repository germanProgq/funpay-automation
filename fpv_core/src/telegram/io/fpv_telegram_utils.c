/* FunPay Vertex Telegram service helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

bool fpv_tg_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len) {
  if (*length + data_len + 1 > *capacity) {
    size_t next = *capacity == 0 ? 128 : *capacity;
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

bool fpv_tg_buffer_append_str(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value) {
  return fpv_tg_buffer_append(
      buffer,
      length,
      capacity,
      value ? value : "",
      value ? strlen(value) : 0);
}

char* fpv_tg_read_file(const char* path, size_t* out_size) {
  if (out_size) {
    *out_size = 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  char* buffer = (char*)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(file);
    return NULL;
  }
  size_t read_count = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read_count] = '\0';
  if (out_size) {
    *out_size = read_count;
  }
  return buffer;
}

bool fpv_tg_write_file(const char* path, const char* data) {
  if (!path) {
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  size_t length = data ? strlen(data) : 0;
  if (length > 0 && fwrite(data, 1, length, file) != length) {
    fclose(file);
    return false;
  }
  fclose(file);
  return true;
}

bool fpv_tg_write_file_data(
    const char* path,
    const void* data,
    size_t size) {
  if (!path || !data) {
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  bool ok = size == 0 || fwrite(data, 1, size, file) == size;
  fclose(file);
  return ok;
}

bool fpv_tg_json_escape_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value) {
  const char* ptr = value ? value : "";
  while (*ptr) {
    unsigned char ch = (unsigned char)*ptr++;
    if (ch == '\\' || ch == '"') {
      char escaped[2] = {'\\', (char)ch};
      if (!fpv_tg_buffer_append(buffer, length, capacity, escaped, 2)) {
        return false;
      }
      continue;
    }
    switch (ch) {
      case '\n':
        if (!fpv_tg_buffer_append(buffer, length, capacity, "\\n", 2)) {
          return false;
        }
        continue;
      case '\r':
        if (!fpv_tg_buffer_append(buffer, length, capacity, "\\r", 2)) {
          return false;
        }
        continue;
      case '\t':
        if (!fpv_tg_buffer_append(buffer, length, capacity, "\\t", 2)) {
          return false;
        }
        continue;
      default:
        break;
    }
    if (ch < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04X", ch);
      if (!fpv_tg_buffer_append(buffer, length, capacity, escaped, 6)) {
        return false;
      }
      continue;
    }
    if (!fpv_tg_buffer_append(buffer, length, capacity, (const char*)&ch, 1)) {
      return false;
    }
  }
  return true;
}

char* fpv_tg_make_valid_utf8(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const unsigned char* ptr = (const unsigned char*)text;
  while (*ptr) {
    unsigned char lead = *ptr;
    size_t seq_len = 0;
    if (lead < 0x80) {
      seq_len = 1;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      seq_len = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      seq_len = 3;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      seq_len = 4;
    } else {
      if (!fpv_tg_buffer_append(&buffer, &length, &capacity, "?", 1)) {
        fpv_free(buffer);
        return NULL;
      }
      ptr++;
      continue;
    }

    if (seq_len == 1) {
      if (!fpv_tg_buffer_append(&buffer, &length, &capacity, (const char*)ptr, 1)) {
        fpv_free(buffer);
        return NULL;
      }
      ptr++;
      continue;
    }

    if (!ptr[1] || (ptr[1] & 0xC0) != 0x80 ||
        (seq_len > 2 && (!ptr[2] || (ptr[2] & 0xC0) != 0x80)) ||
        (seq_len > 3 && (!ptr[3] || (ptr[3] & 0xC0) != 0x80))) {
      if (!fpv_tg_buffer_append(&buffer, &length, &capacity, "?", 1)) {
        fpv_free(buffer);
        return NULL;
      }
      ptr++;
      continue;
    }

    if ((lead == 0xE0 && ptr[1] < 0xA0) ||
        (lead == 0xED && ptr[1] >= 0xA0) ||
        (lead == 0xF0 && ptr[1] < 0x90) ||
        (lead == 0xF4 && ptr[1] > 0x8F)) {
      if (!fpv_tg_buffer_append(&buffer, &length, &capacity, "?", 1)) {
        fpv_free(buffer);
        return NULL;
      }
      ptr++;
      continue;
    }

    if (!fpv_tg_buffer_append(&buffer, &length, &capacity, (const char*)ptr,
                              seq_len)) {
      fpv_free(buffer);
      return NULL;
    }
    ptr += seq_len;
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

char* fpv_tg_strip_html_tags(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const char* ptr = text;
  while (*ptr) {
    if (*ptr == '<') {
      if (strncmp(ptr, "<b>", 3) == 0) {
        ptr += 3;
        continue;
      }
      if (strncmp(ptr, "</b>", 4) == 0) {
        ptr += 4;
        continue;
      }
      if (strncmp(ptr, "<i>", 3) == 0) {
        ptr += 3;
        continue;
      }
      if (strncmp(ptr, "</i>", 4) == 0) {
        ptr += 4;
        continue;
      }
      if (strncmp(ptr, "<u>", 3) == 0) {
        ptr += 3;
        continue;
      }
      if (strncmp(ptr, "</u>", 4) == 0) {
        ptr += 4;
        continue;
      }
      if (strncmp(ptr, "<code>", 6) == 0) {
        ptr += 6;
        continue;
      }
      if (strncmp(ptr, "</code>", 7) == 0) {
        ptr += 7;
        continue;
      }
      if (strncmp(ptr, "</a>", 4) == 0) {
        ptr += 4;
        continue;
      }
      if (strncmp(ptr, "<a", 2) == 0) {
        const char* end = strchr(ptr, '>');
        if (end) {
          ptr = end + 1;
          continue;
        }
      }
      const char* end = strchr(ptr, '>');
      if (end) {
        ptr = end + 1;
        continue;
      }
    }
    if (!fpv_tg_buffer_append(&buffer, &length, &capacity, ptr, 1)) {
      fpv_free(buffer);
      return NULL;
    }
    ptr++;
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

char* fpv_tg_escape_html(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* safe = fpv_tg_make_valid_utf8(text);
  const char* src = safe ? safe : text;
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (const char* ptr = src; *ptr; ptr++) {
    switch (*ptr) {
      case '&':
        fpv_tg_buffer_append(&buffer, &length, &capacity, "&amp;", 5);
        break;
      case '<':
        fpv_tg_buffer_append(&buffer, &length, &capacity, "&lt;", 4);
        break;
      case '>':
        fpv_tg_buffer_append(&buffer, &length, &capacity, "&gt;", 4);
        break;
      default:
        fpv_tg_buffer_append(&buffer, &length, &capacity, ptr, 1);
        break;
    }
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  fpv_free(safe);
  return buffer;
}

size_t fpv_tg_count_substr(const char* text, const char* needle) {
  if (!text || !needle || !needle[0]) {
    return 0;
  }
  size_t count = 0;
  const char* ptr = text;
  size_t needle_len = strlen(needle);
  while ((ptr = strstr(ptr, needle)) != NULL) {
    count++;
    ptr += needle_len;
  }
  return count;
}

size_t fpv_tg_count_anchor_tags(const char* text) {
  if (!text) {
    return 0;
  }
  size_t count = 0;
  const char* ptr = text;
  while ((ptr = strstr(ptr, "<a")) != NULL) {
    char next = ptr[2];
    if (next == ' ' || next == '>') {
      count++;
    }
    ptr += 2;
  }
  return count;
}

bool fpv_tg_html_is_balanced(const char* text) {
  if (!text || !text[0]) {
    return true;
  }
  if (fpv_tg_count_substr(text, "<b>") != fpv_tg_count_substr(text, "</b>")) {
    return false;
  }
  if (fpv_tg_count_substr(text, "<i>") != fpv_tg_count_substr(text, "</i>")) {
    return false;
  }
  if (fpv_tg_count_substr(text, "<u>") != fpv_tg_count_substr(text, "</u>")) {
    return false;
  }
  if (fpv_tg_count_substr(text, "<code>") !=
      fpv_tg_count_substr(text, "</code>")) {
    return false;
  }
  if (fpv_tg_count_anchor_tags(text) !=
      fpv_tg_count_substr(text, "</a>")) {
    return false;
  }
  return true;
}

int fpv_tg_notification_index(const char* type) {
  if (!type) {
    return -1;
  }
  for (size_t i = 0; i < fpv_tg_notification_count; i++) {
    if (strcmp(fpv_tg_notification_ids[i], type) == 0) {
      return (int)i;
    }
  }
  return -1;
}

bool fpv_tg_default_notification_enabled(const char* type) {
  return type && (strcmp(type, "11") == 0 ||
                  strcmp(type, "12") == 0 ||
                  strcmp(type, "14") == 0);
}

const char* fpv_tg_icon_toggle(bool enabled) {
  return enabled ? "\xF0\x9F\x9F\xA2" : "\xF0\x9F\x94\xB4";
}

const char* fpv_tg_icon_bell(bool enabled) {
  return enabled ? "\xF0\x9F\x94\x94" : "\xF0\x9F\x94\x95";
}

const char* fpv_tg_icon_lot_state(bool global_enabled, bool disabled) {
  if (!global_enabled) {
    return "\xE2\x9A\xAA";
  }
  return disabled ? "\xF0\x9F\x94\xB4" : "\xF0\x9F\x9F\xA2";
}

bool fpv_tg_has_suffix(const char* value, const char* suffix) {
  if (!value || !suffix) {
    return false;
  }
  size_t value_len = strlen(value);
  size_t suffix_len = strlen(suffix);
  if (suffix_len == 0 || suffix_len > value_len) {
    return false;
  }
  return strcmp(value + value_len - suffix_len, suffix) == 0;
}

bool fpv_tg_is_supported_upload(const char* filename) {
  return fpv_tg_has_suffix(filename, ".cfg") ||
      fpv_tg_has_suffix(filename, ".txt") ||
      fpv_tg_has_suffix(filename, ".py");
}

bool fpv_tg_is_safe_upload_name(const char* name) {
  if (!name || !name[0]) {
    return false;
  }
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
    return false;
  }
  for (const unsigned char* ptr = (const unsigned char*)name; *ptr; ptr++) {
    if (*ptr < 0x20) {
      return false;
    }
    if (*ptr == '/' || *ptr == '\\' || *ptr == ':') {
      return false;
    }
  }
  return true;
}

const char* fpv_tg_result_label(fpv_result_t code) {
  switch (code) {
    case FPV_OK:
      return "ok";
    case FPV_ERR_INVALID_ARGUMENT:
      return "invalid_argument";
    case FPV_ERR_OUT_OF_MEMORY:
      return "out_of_memory";
    case FPV_ERR_INVALID_STATE:
      return "invalid_state";
    case FPV_ERR_INTERNAL:
      return "internal";
    case FPV_ERR_NOT_FOUND:
      return "not_found";
    case FPV_ERR_IO:
      return "io";
    case FPV_ERR_PARSE:
      return "parse";
    case FPV_ERR_UNSUPPORTED:
      return "unsupported";
    default:
      return "unknown";
  }
}

void fpv_tg_log(
    fpv_telegram_service_t* service,
    fpv_log_level_t level,
    const char* message) {
  if (!service || !message) {
    return;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (service->logger) {
    fpv_logger_log(service->logger, level, "telegram", message, now_ms);
  }
  if (service->bus) {
    fpv_event_t* event =
        fpv_event_create_log(level, "telegram", message, now_ms);
    if (event) {
      if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
        fpv_event_destroy(event);
      }
    }
  }
}

void fpv_tg_log_poll_error(
    fpv_telegram_service_t* service,
    fpv_result_t code) {
  if (!service || code == FPV_OK) {
    return;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (service->last_poll_error != code ||
      service->last_poll_error_ms == 0 ||
      now_ms - service->last_poll_error_ms > 60000) {
    char message[160];
    snprintf(
        message,
        sizeof(message),
        "Telegram getUpdates failed (%s). Check token and network.",
        fpv_tg_result_label(code));
    fpv_tg_log(service, FPV_LOG_WARNING, message);
    service->last_poll_error = code;
    service->last_poll_error_ms = now_ms;
  }
}

const char* fpv_tg_loc(const fpv_telegram_service_t* service, const char* key) {
  if (!service || !service->localizer || !key) {
    return key;
  }
  const char* value = fpv_localizer_get(service->localizer, key);
  return value ? value : key;
}

char* fpv_tg_loc_format(
    const fpv_telegram_service_t* service,
    const char* key,
    const char* const* args,
    size_t arg_count) {
  if (!service || !service->localizer) {
    return fpv_strdup(key ? key : "");
  }
  char* formatted = fpv_localizer_format(service->localizer, key, args, arg_count);
  if (!formatted) {
    return fpv_strdup(key ? key : "");
  }
  return formatted;
}

void fpv_tg_log_format(
    fpv_telegram_service_t* service,
    fpv_log_level_t level,
    const char* key,
    const char* const* args,
    size_t arg_count) {
  char* message = fpv_tg_loc_format(service, key, args, arg_count);
  fpv_tg_log(service, level, message ? message : "");
  fpv_free(message);
}

char* fpv_tg_build_variable_prompt(
    fpv_telegram_service_t* service,
    const char* header_key,
    const char* const* variables,
    size_t variable_count) {
  const char* header = fpv_tg_loc(service, header_key);
  const char* list_label = fpv_tg_loc(service, "v_list");
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append_str(&buffer, &length, &capacity, header);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\n\n", 2);
  fpv_tg_buffer_append_str(&buffer, &length, &capacity, list_label);
  fpv_tg_buffer_append(&buffer, &length, &capacity, ":\n", 2);
  for (size_t i = 0; i < variable_count; i++) {
    const char* line = fpv_tg_loc(service, variables[i]);
    fpv_tg_buffer_append_str(&buffer, &length, &capacity, line);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\n", 1);
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

void fpv_tg_flush_message_buffer(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    char** buffer,
    size_t* length,
    size_t* capacity) {
  if (!buffer || !length || !capacity) {
    return;
  }
  if (!*buffer || *length == 0) {
    fpv_free(*buffer);
    *buffer = NULL;
    *length = 0;
    *capacity = 0;
    return;
  }
  fpv_tg_send_message(service, chat_id, *buffer, NULL, NULL);
  fpv_free(*buffer);
  *buffer = NULL;
  *length = 0;
  *capacity = 0;
}

void fpv_tg_append_code_value(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value) {
  const char* safe = (value && value[0]) ? value : "-";
  char* escaped = fpv_tg_escape_html(safe);
  fpv_tg_buffer_append_str(buffer, length, capacity, "<code>");
  fpv_tg_buffer_append_str(buffer, length, capacity, escaped ? escaped : safe);
  fpv_tg_buffer_append_str(buffer, length, capacity, "</code>\n");
  fpv_free(escaped);
}
void fpv_tg_sleep_ms(uint32_t delay_ms) {
#if defined(_WIN32)
  Sleep(delay_ms);
#else
  struct timespec ts;
  ts.tv_sec = delay_ms / 1000U;
  ts.tv_nsec = (long)(delay_ms % 1000U) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}
