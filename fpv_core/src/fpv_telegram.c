/* FunPay Vertex Telegram service implementation. */

#include "fpv_telegram.h"

#include <curl/curl.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#else
#include <sys/resource.h>
#endif
#endif

#include "fpv_cache.h"
#include "fpv_feature_config.h"
#include "fpv_features.h"
#include "fpv_fs.h"
#include "fpv_core/fpv_ini.h"
#include "fpv_json.h"
#include "fpv_localization.h"
#include "fpv_platform.h"
#include "fpv_products.h"
#include "fpv_settings.h"
#include "fpv_string.h"
#include "fpv_time.h"

#define FPV_TG_API_BASE "https://api.telegram.org/bot"
#ifndef FPV_VERSION
#define FPV_VERSION "0.1.0"
#endif

typedef enum fpv_tg_state_type {
  FPV_TG_STATE_NONE = 0,
  FPV_TG_STATE_ADD_CMD,
  FPV_TG_STATE_EDIT_CMD_RESPONSE,
  FPV_TG_STATE_EDIT_CMD_NOTIFICATION,
  FPV_TG_STATE_ADD_AD_TO_LOT_MANUAL,
  FPV_TG_STATE_EDIT_LOT_DELIVERY_TEXT,
  FPV_TG_STATE_BIND_PRODUCTS_FILE,
  FPV_TG_STATE_CREATE_PRODUCTS_FILE,
  FPV_TG_STATE_ADD_PRODUCTS_TO_FILE,
  FPV_TG_STATE_SEND_FP_MESSAGE,
  FPV_TG_STATE_EDIT_WATERMARK,
  FPV_TG_STATE_MANUAL_AD_TEST,
  FPV_TG_STATE_BAN,
  FPV_TG_STATE_UNBAN,
  FPV_TG_STATE_EDIT_GREETINGS_TEXT,
  FPV_TG_STATE_EDIT_ORDER_CONFIRM_TEXT,
  FPV_TG_STATE_EDIT_REVIEW_REPLY_TEXT,
  FPV_TG_STATE_ADD_TEMPLATE,
  FPV_TG_STATE_UPLOAD_PRODUCTS_FILE,
  FPV_TG_STATE_UPLOAD_MAIN_CONFIG,
  FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG,
  FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG,
  FPV_TG_STATE_UPLOAD_IMAGE
} fpv_tg_state_type_t;

typedef struct fpv_tg_state_data {
  int command_index;
  int offset;
  int lot_index;
  int file_index;
  int element_index;
  int previous_page;
  int stars;
  uint64_t chat_id;
  char* username;
} fpv_tg_state_data_t;

typedef struct fpv_tg_user_state {
  int64_t chat_id;
  int64_t user_id;
  int message_id;
  fpv_tg_state_type_t type;
  fpv_tg_state_data_t data;
} fpv_tg_user_state_t;

typedef struct fpv_tg_attempt {
  int64_t user_id;
  uint32_t count;
} fpv_tg_attempt_t;

typedef struct fpv_tg_init_message {
  int64_t chat_id;
  int message_id;
} fpv_tg_init_message_t;

typedef struct fpv_tg_delivery_test {
  char* key;
  char* lot_name;
} fpv_tg_delivery_test_t;

typedef struct fpv_tg_chat_settings {
  int64_t chat_id;
  bool enabled[16];
} fpv_tg_chat_settings_t;

typedef struct fpv_tg_http_response {
  char* body;
  size_t body_size;
  long status;
} fpv_tg_http_response_t;

typedef struct fpv_tg_keyboard {
  char* buffer;
  size_t length;
  size_t capacity;
  bool inline_keyboard;
  bool row_open;
  bool first_in_row;
  bool has_rows;
  bool resize;
} fpv_tg_keyboard_t;

static bool fpv_tg_send_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* text,
    const char* reply_markup,
    int* out_message_id);

typedef struct fpv_tg_message {
  int64_t chat_id;
  int message_id;
  int64_t from_id;
  char* chat_type;
  char* chat_username;
  char* from_username;
  char* text;
  char* document_file_id;
  char* document_file_name;
  int64_t document_file_size;
  char* photo_file_id;
  int64_t photo_file_size;
  bool reply_topic_created;
  bool has_document;
  bool has_photo;
} fpv_tg_message_t;

typedef struct fpv_tg_callback {
  char* id;
  char* data;
  int64_t from_id;
  char* from_username;
  int64_t chat_id;
  int message_id;
  char* chat_username;
} fpv_tg_callback_t;

struct fpv_telegram_service {
  char* token;
  char* secret;
  char* locales_dir;
  char* language;
  fpv_localizer_t* localizer;
  fpv_logger_t* logger;
  fpv_event_bus_t* bus;
  fpv_funpay_account_t* account;
  fpv_funpay_runner_t* runner;
  fpv_feature_state_t* features;
  fpv_storage_layout_t storage;
  fpv_thread_t thread;
  fpv_mutex_t mutex;
  bool running;
  uint64_t update_offset;
  uint64_t last_poll_error_ms;
  fpv_result_t last_poll_error;
  fpv_tg_chat_settings_t* notifications;
  size_t notification_count;
  uint64_t* authorized_users;
  size_t authorized_count;
  fpv_string_list_t answer_templates;
  fpv_tg_user_state_t* user_states;
  size_t user_state_count;
  fpv_tg_attempt_t* attempts;
  size_t attempt_count;
  fpv_tg_init_message_t* init_messages;
  size_t init_message_count;
  fpv_tg_delivery_test_t* delivery_tests;
  size_t delivery_test_count;
  fpv_lot_t** profile_lots;
  size_t profile_lot_count;
  uint64_t profile_update_ms;
  uint64_t start_ms;
  uint64_t instance_id;
  void* control_context;
  fpv_telegram_control_fn restart_fn;
  fpv_telegram_control_fn shutdown_fn;
};

static const char* fpv_tg_notification_ids[] = {
    "1",
    "2",
    "3",
    "4",
    "5",
    "5r",
    "6",
    "7",
    "8",
    "9",
    "10",
    "11",
    "12",
    "13"};

static const size_t fpv_tg_notification_count =
    sizeof(fpv_tg_notification_ids) / sizeof(fpv_tg_notification_ids[0]);

static const size_t fpv_tg_cmd_page = 15;
static const size_t fpv_tg_ad_page = 15;
static const size_t fpv_tg_fp_lot_page = 15;
static const size_t fpv_tg_products_page = 15;
static const size_t fpv_tg_template_page = 15;
static const int64_t fpv_tg_max_upload_size = 20 * 1024 * 1024;

static const char* fpv_tg_cbt_main = "1";
static const char* fpv_tg_cbt_category = "2";
static const char* fpv_tg_cbt_switch = "3";
static const char* fpv_tg_cbt_add_cmd = "4";
static const char* fpv_tg_cbt_cmd_list = "5";
static const char* fpv_tg_cbt_edit_cmd = "6";
static const char* fpv_tg_cbt_edit_cmd_response = "7";
static const char* fpv_tg_cbt_edit_cmd_notification = "8";
static const char* fpv_tg_cbt_switch_cmd_notification = "9";
static const char* fpv_tg_cbt_del_cmd = "10";
static const char* fpv_tg_cbt_fp_lots = "11";
static const char* fpv_tg_cbt_add_ad_lot = "12";
static const char* fpv_tg_cbt_add_ad_lot_manual = "13";
static const char* fpv_tg_cbt_ad_lots = "14";
static const char* fpv_tg_cbt_edit_ad_lot = "15";
static const char* fpv_tg_cbt_edit_lot_text = "16";
static const char* fpv_tg_cbt_bind_products = "17";
static const char* fpv_tg_cbt_del_ad_lot = "18";
static const char* fpv_tg_cbt_products_list = "19";
static const char* fpv_tg_cbt_edit_products_file = "20";
static const char* fpv_tg_cbt_upload_products_file = "21";
static const char* fpv_tg_cbt_create_products_file = "22";
static const char* fpv_tg_cbt_add_products = "23";
static const char* fpv_tg_cbt_download_cfg = "24";
static const char* fpv_tg_cbt_template_list = "25";
static const char* fpv_tg_cbt_template_list_ans = "26";
static const char* fpv_tg_cbt_edit_template = "27";
static const char* fpv_tg_cbt_del_template = "28";
static const char* fpv_tg_cbt_add_template = "29";
static const char* fpv_tg_cbt_send_template = "30";
static const char* fpv_tg_cbt_switch_tg = "31";
static const char* fpv_tg_cbt_request_refund = "32";
static const char* fpv_tg_cbt_refund_confirmed = "33";
static const char* fpv_tg_cbt_refund_cancelled = "34";
static const char* fpv_tg_cbt_ban = "35";
static const char* fpv_tg_cbt_unban = "36";
static const char* fpv_tg_cbt_shutdown = "37";
static const char* fpv_tg_cbt_cancel_shutdown = "38";
static const char* fpv_tg_cbt_send_fp_message = "to_node";
static const char* fpv_tg_cbt_upload_image = "upload_image";
static const char* fpv_tg_cbt_update_profile = "39";
static const char* fpv_tg_cbt_manual_ad_test = "40";
static const char* fpv_tg_cbt_clear_state = "41";
static const char* fpv_tg_cbt_back_to_reply = "42";
static const char* fpv_tg_cbt_back_to_order = "43";
static const char* fpv_tg_cbt_param_disabled = "53";
static const char* fpv_tg_cbt_main2 = "54";
static const char* fpv_tg_cbt_edit_greetings = "55";
static const char* fpv_tg_cbt_edit_order_confirm = "56";
static const char* fpv_tg_cbt_send_review_reply = "57";
static const char* fpv_tg_cbt_edit_review_reply = "58";
static const char* fpv_tg_cbt_edit_watermark = "59";
static const char* fpv_tg_cbt_extend_chat = "60";
static const char* fpv_tg_cbt_old_help = "61";
static const char* fpv_tg_cbt_empty = "62";
static const char* fpv_tg_cbt_lang = "63";
static const char* fpv_tg_menu_core = "core";
static const char* fpv_tg_menu_notify = "notify";
static const char* fpv_tg_menu_reply = "reply";
static const char* fpv_tg_menu_delivery = "delivery";
static const char* fpv_tg_cb_update_profile = "update_profile";
static const char* fpv_tg_cb_update_adv_profile = "update_adv_profile";
static const char* fpv_tg_cb_config_loader = "config_loader";
static const char* fpv_tg_cb_upload_main_config = "upload_main_config";
static const char* fpv_tg_cb_upload_auto_response_config =
    "upload_auto_response_config";
static const char* fpv_tg_cb_upload_auto_delivery_config =
    "upload_auto_delivery_config";
static const char* fpv_tg_cb_switch_lot = "switch_lot";
static const char* fpv_tg_cb_test_auto_delivery = "test_auto_delivery";
static const char* fpv_tg_cb_update_funpay_lots = "update_funpay_lots";
static const char* fpv_tg_cb_download_products_file = "download_products_file";
static const char* fpv_tg_cb_delete_products_file = "del_products_file";
static const char* fpv_tg_cb_confirm_delete_products_file =
    "confirm_del_products_file";

static void fpv_tg_http_response_clear(fpv_tg_http_response_t* response) {
  if (!response) {
    return;
  }
  fpv_free(response->body);
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;
}

static bool fpv_tg_buffer_append(
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

static bool fpv_tg_buffer_append_str(
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

static char* fpv_tg_read_file(const char* path, size_t* out_size) {
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

static bool fpv_tg_write_file(const char* path, const char* data) {
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

static bool fpv_tg_write_file_data(
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

static bool fpv_tg_json_escape_append(
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

static char* fpv_tg_make_valid_utf8(const char* text) {
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

static char* fpv_tg_strip_html_tags(const char* text) {
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

static char* fpv_tg_escape_html(const char* text) {
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

static size_t fpv_tg_count_substr(const char* text, const char* needle) {
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

static size_t fpv_tg_count_anchor_tags(const char* text) {
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

static bool fpv_tg_html_is_balanced(const char* text) {
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

static int fpv_tg_notification_index(const char* type) {
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

static bool fpv_tg_default_notification_enabled(const char* type) {
  return type && (strcmp(type, "11") == 0 || strcmp(type, "12") == 0);
}

static const char* fpv_tg_icon_toggle(bool enabled) {
  return enabled ? "\xF0\x9F\x9F\xA2" : "\xF0\x9F\x94\xB4";
}

static const char* fpv_tg_icon_bell(bool enabled) {
  return enabled ? "\xF0\x9F\x94\x94" : "\xF0\x9F\x94\x95";
}

static const char* fpv_tg_icon_lot_state(bool global_enabled, bool disabled) {
  if (!global_enabled) {
    return "\xE2\x9A\xAA";
  }
  return disabled ? "\xF0\x9F\x94\xB4" : "\xF0\x9F\x9F\xA2";
}

static bool fpv_tg_has_suffix(const char* value, const char* suffix) {
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

static bool fpv_tg_is_supported_upload(const char* filename) {
  return fpv_tg_has_suffix(filename, ".cfg") ||
      fpv_tg_has_suffix(filename, ".txt") ||
      fpv_tg_has_suffix(filename, ".py");
}

static bool fpv_tg_is_safe_upload_name(const char* name) {
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

static const char* fpv_tg_result_label(fpv_result_t code) {
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

static void fpv_tg_log(
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

static void fpv_tg_log_poll_error(
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

static const char* fpv_tg_loc(const fpv_telegram_service_t* service, const char* key) {
  if (!service || !service->localizer || !key) {
    return key;
  }
  const char* value = fpv_localizer_get(service->localizer, key);
  return value ? value : key;
}

static char* fpv_tg_loc_format(
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

static void fpv_tg_log_format(
    fpv_telegram_service_t* service,
    fpv_log_level_t level,
    const char* key,
    const char* const* args,
    size_t arg_count) {
  char* message = fpv_tg_loc_format(service, key, args, arg_count);
  fpv_tg_log(service, level, message ? message : "");
  fpv_free(message);
}

static char* fpv_tg_build_variable_prompt(
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

static void fpv_tg_flush_message_buffer(
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

static void fpv_tg_append_code_value(
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

static void fpv_tg_keyboard_destroy(fpv_tg_keyboard_t* kb) {
  if (!kb) {
    return;
  }
  fpv_free(kb->buffer);
  fpv_free(kb);
}

static fpv_tg_keyboard_t* fpv_tg_keyboard_create(bool inline_keyboard, bool resize) {
  fpv_tg_keyboard_t* kb = (fpv_tg_keyboard_t*)calloc(1, sizeof(*kb));
  if (!kb) {
    return NULL;
  }
  kb->inline_keyboard = inline_keyboard;
  kb->resize = resize;
  if (inline_keyboard) {
    const char* prefix = "{\"inline_keyboard\":[";
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                              prefix, strlen(prefix))) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  } else {
    const char* prefix = "{\"keyboard\":[";
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                              prefix, strlen(prefix))) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  }
  kb->row_open = false;
  kb->first_in_row = true;
  kb->has_rows = false;
  return kb;
}

static bool fpv_tg_keyboard_row_begin(fpv_tg_keyboard_t* kb) {
  if (!kb) {
    return false;
  }
  if (kb->row_open) {
    return true;
  }
  if (kb->has_rows) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, ",", 1)) {
      return false;
    }
  }
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "[", 1)) {
    return false;
  }
  kb->row_open = true;
  kb->first_in_row = true;
  return true;
}

static bool fpv_tg_keyboard_row_end(fpv_tg_keyboard_t* kb) {
  if (!kb || !kb->row_open) {
    return false;
  }
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]", 1)) {
    return false;
  }
  kb->row_open = false;
  kb->has_rows = true;
  return true;
}

static bool fpv_tg_keyboard_add_button(
    fpv_tg_keyboard_t* kb,
    const char* text,
    const char* callback_data,
    const char* url) {
  if (!kb || !text) {
    return false;
  }
  if (!fpv_tg_keyboard_row_begin(kb)) {
    return false;
  }
  if (!kb->first_in_row) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, ",", 1)) {
      return false;
    }
  }
  kb->first_in_row = false;
  if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "{\"text\":\"",
                            strlen("{\"text\":\""))) {
    return false;
  }
  if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, text)) {
    return false;
  }
  if (kb->inline_keyboard) {
    if (url && url[0]) {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\",\"url\":\"",
                                strlen("\",\"url\":\""))) {
        return false;
      }
      if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, url)) {
        return false;
      }
    } else {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                "\",\"callback_data\":\"",
                                strlen("\",\"callback_data\":\""))) {
        return false;
      }
      if (!fpv_tg_json_escape_append(&kb->buffer, &kb->length, &kb->capacity, callback_data ? callback_data : "")) {
        return false;
      }
    }
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\"}",
                              strlen("\"}"))) {
      return false;
    }
  } else {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "\"}",
                              strlen("\"}"))) {
      return false;
    }
  }
  return true;
}

static char* fpv_tg_keyboard_finalize(fpv_tg_keyboard_t* kb, bool remove_keyboard) {
  if (!kb) {
    return NULL;
  }
  if (kb->row_open) {
    fpv_tg_keyboard_row_end(kb);
  }
  if (kb->inline_keyboard) {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]}", 2)) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
  } else {
    if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "]", 1)) {
      fpv_tg_keyboard_destroy(kb);
      return NULL;
    }
    if (remove_keyboard) {
      const char* suffix = ",\"remove_keyboard\":true}";
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                suffix, strlen(suffix))) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    } else if (kb->resize) {
      const char* suffix = ",\"resize_keyboard\":true}";
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity,
                                suffix, strlen(suffix))) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    } else {
      if (!fpv_tg_buffer_append(&kb->buffer, &kb->length, &kb->capacity, "}", 1)) {
        fpv_tg_keyboard_destroy(kb);
        return NULL;
      }
    }
  }
  char* result = kb->buffer;
  kb->buffer = NULL;
  fpv_tg_keyboard_destroy(kb);
  return result;
}

static char* fpv_tg_build_clear_state_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_cancel"),
                             fpv_tg_cbt_clear_state, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_keyboard_remove(void) {
  return fpv_strdup("{\"remove_keyboard\":true}");
}

static void fpv_tg_sleep_ms(uint32_t delay_ms) {
#if defined(_WIN32)
  Sleep(delay_ms);
#else
  struct timespec ts;
  ts.tv_sec = delay_ms / 1000U;
  ts.tv_nsec = (long)(delay_ms % 1000U) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

static size_t fpv_tg_write_callback(void* data, size_t size, size_t nmemb, void* userdata) {
  fpv_tg_http_response_t* response = (fpv_tg_http_response_t*)userdata;
  size_t total = size * nmemb;
  if (!response || total == 0) {
    return total;
  }
  char* grown = (char*)realloc(response->body, response->body_size + total + 1);
  if (!grown) {
    return 0;
  }
  response->body = grown;
  memcpy(response->body + response->body_size, data, total);
  response->body_size += total;
  response->body[response->body_size] = '\0';
  return total;
}

static fpv_result_t fpv_tg_http_request(
    const char* method,
    const char* url,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_tg_http_response_t* response) {
  if (!url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return FPV_ERR_INTERNAL;
  }
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fpv_tg_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

  struct curl_slist* headers = NULL;
  if (content_type) {
    char header[128];
    snprintf(header, sizeof(header), "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  if (method && strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    if (body && body_size > 0) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
    }
  } else if (method && strcmp(method, "GET") != 0) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (body && body_size > 0) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
    }
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return FPV_ERR_IO;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return FPV_OK;
}

static fpv_result_t fpv_tg_http_request_multipart(
    const char* url,
    curl_mime* mime,
    fpv_tg_http_response_t* response) {
  if (!url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return FPV_ERR_INTERNAL;
  }
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fpv_tg_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_easy_cleanup(curl);
    return FPV_ERR_IO;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_easy_cleanup(curl);
  return FPV_OK;
}

static fpv_result_t fpv_tg_api_request(
    const fpv_telegram_service_t* service,
    const char* method,
    const char* endpoint,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_json_value_t** out_root) {
  if (!service || !service->token || !endpoint) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/%s", FPV_TG_API_BASE, service->token, endpoint);

  fpv_tg_http_response_t response;
  fpv_result_t result =
      fpv_tg_http_request(method, url, content_type, body, body_size, &response);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  if (fpv_json_parse(response.body, response.body_size, &json, &error) != FPV_OK) {
    if (service && endpoint) {
      char log_buf[256];
      snprintf(log_buf, sizeof(log_buf),
               "Telegram API parse failed (%s).", endpoint);
      fpv_tg_log((fpv_telegram_service_t*)service, FPV_LOG_WARNING, log_buf);
    }
    fpv_tg_http_response_clear(&response);
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* ok_val = fpv_json_object_get(json, "ok");
  bool ok = fpv_json_bool(ok_val, false);
  if (!ok) {
    const fpv_json_value_t* desc_val = fpv_json_object_get(json, "description");
    const fpv_json_value_t* code_val = fpv_json_object_get(json, "error_code");
    const char* desc = fpv_json_string(desc_val);
    int64_t code = 0;
    fpv_json_number_to_int64(code_val, &code);
    if (service && endpoint) {
      char log_buf[512];
      snprintf(log_buf, sizeof(log_buf),
               "Telegram API error (%s): %" PRId64 " %s",
               endpoint,
               code,
               desc ? desc : "unknown error");
      fpv_tg_log((fpv_telegram_service_t*)service, FPV_LOG_WARNING, log_buf);
    }
    fpv_json_destroy(json);
    fpv_tg_http_response_clear(&response);
    return FPV_ERR_INVALID_STATE;
  }

  if (out_root) {
    *out_root = json;
  } else {
    fpv_json_destroy(json);
  }
  fpv_tg_http_response_clear(&response);
  return FPV_OK;
}

static char* fpv_tg_build_send_body(
    int64_t chat_id,
    const char* text,
    const char* reply_markup) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, text ? text : "");
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"parse_mode\":\"HTML\"",
                       strlen("\",\"parse_mode\":\"HTML\""));
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  return buffer;
}

static char* fpv_tg_build_send_body_plain(
    int64_t chat_id,
    const char* text,
    const char* reply_markup) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, text ? text : "");
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"",
                       strlen("\""));
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  return buffer;
}

static bool fpv_tg_send_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* text,
    const char* reply_markup,
    int* out_message_id) {
  if (out_message_id) {
    *out_message_id = 0;
  }
  if (!service || chat_id == 0 || !text) {
    return false;
  }
  char* safe_text = fpv_tg_make_valid_utf8(text);
  const char* send_text = safe_text ? safe_text : text;
  bool use_html = fpv_tg_html_is_balanced(send_text);
  char* stripped_text = NULL;
  const char* payload_text = send_text;
  if (!use_html) {
    stripped_text = fpv_tg_strip_html_tags(send_text);
    if (stripped_text) {
      payload_text = stripped_text;
    }
  }
  char* body = use_html
      ? fpv_tg_build_send_body(chat_id, payload_text, reply_markup)
      : fpv_tg_build_send_body_plain(chat_id, payload_text, reply_markup);
  if (!body) {
    fpv_free(stripped_text);
    fpv_free(safe_text);
    return false;
  }
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "sendMessage", "application/json",
                         body, strlen(body), &root);
  fpv_free(body);
  if (req != FPV_OK || !root) {
    fpv_json_destroy(root);
    if (use_html) {
      char* fallback_text = fpv_tg_strip_html_tags(send_text);
      const char* plain_text = fallback_text ? fallback_text : send_text;
      char* plain_body =
          fpv_tg_build_send_body_plain(chat_id, plain_text, reply_markup);
      if (!plain_body) {
        fpv_free(fallback_text);
        fpv_free(stripped_text);
        fpv_free(safe_text);
        return false;
      }
      fpv_json_value_t* plain_root = NULL;
      fpv_result_t plain_req =
          fpv_tg_api_request(service, "POST", "sendMessage",
                             "application/json", plain_body,
                             strlen(plain_body), &plain_root);
      fpv_free(plain_body);
      if (plain_req != FPV_OK || !plain_root) {
        fpv_json_destroy(plain_root);
        fpv_free(fallback_text);
        fpv_free(stripped_text);
        fpv_free(safe_text);
        return false;
      }
      if (out_message_id) {
        const fpv_json_value_t* result = fpv_json_object_get(plain_root, "result");
        const fpv_json_value_t* id_val =
            result ? fpv_json_object_get(result, "message_id") : NULL;
        int64_t msg_id = 0;
        if (fpv_json_number_to_int64(id_val, &msg_id)) {
          *out_message_id = (int)msg_id;
        }
      }
      fpv_json_destroy(plain_root);
      fpv_free(fallback_text);
      fpv_free(stripped_text);
      fpv_free(safe_text);
      return true;
    }
    fpv_free(stripped_text);
    fpv_free(safe_text);
    return false;
  }
  if (out_message_id) {
    const fpv_json_value_t* result = fpv_json_object_get(root, "result");
    const fpv_json_value_t* id_val =
        result ? fpv_json_object_get(result, "message_id") : NULL;
    int64_t msg_id = 0;
    if (fpv_json_number_to_int64(id_val, &msg_id)) {
      *out_message_id = (int)msg_id;
    }
  }
  fpv_json_destroy(root);
  fpv_free(stripped_text);
  fpv_free(safe_text);
  return true;
}

static bool fpv_tg_edit_message_text(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* text,
    const char* reply_markup) {
  if (!service || chat_id == 0 || message_id == 0 || !text) {
    return false;
  }
  char* safe_text = fpv_tg_make_valid_utf8(text);
  const char* send_text = safe_text ? safe_text : text;
  bool use_html = fpv_tg_html_is_balanced(send_text);
  char* stripped_text = NULL;
  const char* payload_text = send_text;
  if (!use_html) {
    stripped_text = fpv_tg_strip_html_tags(send_text);
    if (stripped_text) {
      payload_text = stripped_text;
    }
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":",
                       strlen(",\"message_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, payload_text);
  if (use_html) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"parse_mode\":\"HTML\"",
                         strlen("\",\"parse_mode\":\"HTML\""));
  } else {
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  }
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "editMessageText",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  fpv_free(stripped_text);
  fpv_free(safe_text);
  fpv_json_destroy(root);
  return req == FPV_OK;
}

static bool fpv_tg_edit_message_reply_markup(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* reply_markup) {
  if (!service || chat_id == 0 || message_id == 0 || !reply_markup) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":",
                       strlen(",\"message_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                       strlen(",\"reply_markup\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "editMessageReplyMarkup",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  fpv_json_destroy(root);
  return req == FPV_OK;
}

static void fpv_tg_answer_callback(
    fpv_telegram_service_t* service,
    const char* callback_id,
    const char* text,
    bool show_alert) {
  if (!service || !callback_id) {
    return;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"callback_query_id\":\"",
                       strlen("{\"callback_query_id\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, callback_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  if (text && text[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                         strlen(",\"text\":\""));
    fpv_tg_json_escape_append(&buffer, &length, &capacity, text);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  }
  if (show_alert) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"show_alert\":true",
                         strlen(",\"show_alert\":true"));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_tg_api_request(service, "POST", "answerCallbackQuery",
                     "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
}

static bool fpv_tg_delete_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id) {
  if (!service || chat_id == 0 || message_id == 0) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":", 11);
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":", 14);
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "deleteMessage",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
  return req == FPV_OK;
}

static bool fpv_tg_pin_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id) {
  if (!service || chat_id == 0 || message_id == 0) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":", 11);
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":", 14);
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "pinChatMessage",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
  return req == FPV_OK;
}

static bool fpv_tg_send_document(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* file_path,
    const char* caption,
    const char* reply_markup) {
  if (!service || chat_id == 0 || !file_path) {
    return false;
  }
  char* safe_caption = NULL;
  const char* caption_text = caption;
  char* caption_plain = NULL;
  if (caption && caption[0]) {
    safe_caption = fpv_tg_make_valid_utf8(caption);
    if (safe_caption) {
      caption_text = safe_caption;
    }
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/sendDocument", FPV_TG_API_BASE, service->token);
  CURL* curl = curl_easy_init();
  if (!curl) {
    fpv_free(safe_caption);
    return false;
  }
  curl_mime* mime = curl_mime_init(curl);
  if (!mime) {
    curl_easy_cleanup(curl);
    fpv_free(safe_caption);
    return false;
  }
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "chat_id");
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  curl_mime_data(part, chat_buf, CURL_ZERO_TERMINATED);

  part = curl_mime_addpart(mime);
  curl_mime_name(part, "document");
  curl_mime_filedata(part, file_path);

  if (caption_text && caption_text[0]) {
    bool use_html = fpv_tg_html_is_balanced(caption_text);
    if (!use_html) {
      caption_plain = fpv_tg_strip_html_tags(caption_text);
      if (caption_plain) {
        caption_text = caption_plain;
      }
    }
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption_text, CURL_ZERO_TERMINATED);
    if (use_html) {
      part = curl_mime_addpart(mime);
      curl_mime_name(part, "parse_mode");
      curl_mime_data(part, "HTML", CURL_ZERO_TERMINATED);
    }
  }
  if (reply_markup && reply_markup[0]) {
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "reply_markup");
    curl_mime_data(part, reply_markup, CURL_ZERO_TERMINATED);
  }

  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request_multipart(url, mime, &response);
  curl_mime_free(mime);
  curl_easy_cleanup(curl);
  fpv_tg_http_response_clear(&response);
  fpv_free(caption_plain);
  fpv_free(safe_caption);
  return result == FPV_OK;
}

static bool fpv_tg_send_photo(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const void* data,
    size_t size,
    const char* caption,
    const char* reply_markup,
    int* out_message_id) {
  if (out_message_id) {
    *out_message_id = 0;
  }
  if (!service || chat_id == 0 || !data || size == 0) {
    return false;
  }
  char* safe_caption = NULL;
  const char* caption_text = caption;
  char* caption_plain = NULL;
  if (caption && caption[0]) {
    safe_caption = fpv_tg_make_valid_utf8(caption);
    if (safe_caption) {
      caption_text = safe_caption;
    }
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/sendPhoto", FPV_TG_API_BASE, service->token);
  CURL* curl = curl_easy_init();
  if (!curl) {
    fpv_free(safe_caption);
    return false;
  }
  curl_mime* mime = curl_mime_init(curl);
  if (!mime) {
    curl_easy_cleanup(curl);
    fpv_free(safe_caption);
    return false;
  }
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "chat_id");
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  curl_mime_data(part, chat_buf, CURL_ZERO_TERMINATED);

  part = curl_mime_addpart(mime);
  curl_mime_name(part, "photo");
  curl_mime_data(part, (const char*)data, (curl_off_t)size);
  curl_mime_filename(part, "photo.jpg");

  if (caption_text && caption_text[0]) {
    bool use_html = fpv_tg_html_is_balanced(caption_text);
    if (!use_html) {
      caption_plain = fpv_tg_strip_html_tags(caption_text);
      if (caption_plain) {
        caption_text = caption_plain;
      }
    }
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption_text, CURL_ZERO_TERMINATED);
    if (use_html) {
      part = curl_mime_addpart(mime);
      curl_mime_name(part, "parse_mode");
      curl_mime_data(part, "HTML", CURL_ZERO_TERMINATED);
    }
  }
  if (reply_markup && reply_markup[0]) {
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "reply_markup");
    curl_mime_data(part, reply_markup, CURL_ZERO_TERMINATED);
  }

  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request_multipart(url, mime, &response);
  curl_mime_free(mime);
  curl_easy_cleanup(curl);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return false;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  if (fpv_json_parse(response.body, response.body_size, &json, &error) == FPV_OK) {
    const fpv_json_value_t* ok_val = fpv_json_object_get(json, "ok");
    if (fpv_json_bool(ok_val, false)) {
      const fpv_json_value_t* res_val = fpv_json_object_get(json, "result");
      const fpv_json_value_t* id_val = fpv_json_object_get(res_val, "message_id");
      int64_t msg_id = 0;
      if (fpv_json_number_to_int64(id_val, &msg_id) && out_message_id) {
        *out_message_id = (int)msg_id;
      }
    }
  }
  fpv_json_destroy(json);
  fpv_tg_http_response_clear(&response);
  fpv_free(caption_plain);
  fpv_free(safe_caption);
  return true;
}

static fpv_result_t fpv_tg_get_file_path(
    fpv_telegram_service_t* service,
    const char* file_id,
    char** out_path) {
  if (!service || !file_id || !out_path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_path = NULL;
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"file_id\":\"", 12);
  fpv_tg_json_escape_append(&buffer, &length, &capacity, file_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"}", 2);

  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "getFile",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  if (req != FPV_OK || !root) {
    fpv_json_destroy(root);
    return req;
  }
  const fpv_json_value_t* result = fpv_json_object_get(root, "result");
  const fpv_json_value_t* path_val =
      result ? fpv_json_object_get(result, "file_path") : NULL;
  const char* path = fpv_json_string(path_val);
  if (path && path[0]) {
    *out_path = fpv_strdup(path);
  }
  fpv_json_destroy(root);
  return *out_path ? FPV_OK : FPV_ERR_NOT_FOUND;
}

static fpv_result_t fpv_tg_download_file(
    fpv_telegram_service_t* service,
    const char* file_path,
    void** out_data,
    size_t* out_size) {
  if (!service || !file_path || !out_data || !out_size) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_data = NULL;
  *out_size = 0;
  char url[512];
  snprintf(url, sizeof(url), "https://api.telegram.org/file/bot%s/%s",
           service->token, file_path);
  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request("GET", url, NULL, NULL, 0, &response);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return result;
  }
  *out_data = response.body;
  *out_size = response.body_size;
  response.body = NULL;
  response.body_size = 0;
  fpv_tg_http_response_clear(&response);
  return FPV_OK;
}

static fpv_result_t fpv_tg_download_tg_file(
    fpv_telegram_service_t* service,
    const char* file_id,
    void** out_data,
    size_t* out_size) {
  if (!service || !file_id || !out_data || !out_size) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* file_path = NULL;
  fpv_result_t result = fpv_tg_get_file_path(service, file_id, &file_path);
  if (result != FPV_OK) {
    fpv_free(file_path);
    return result;
  }
  result = fpv_tg_download_file(service, file_path, out_data, out_size);
  fpv_free(file_path);
  return result;
}

static fpv_tg_chat_settings_t* fpv_tg_find_chat_settings(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->notification_count; i++) {
    if (service->notifications[i].chat_id == chat_id) {
      return &service->notifications[i];
    }
  }
  return NULL;
}

static bool fpv_tg_is_notification_enabled(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type) {
  int idx = fpv_tg_notification_index(type);
  if (idx < 0) {
    return false;
  }
  fpv_tg_chat_settings_t* settings = fpv_tg_find_chat_settings(service, chat_id);
  if (!settings) {
    return false;
  }
  return settings->enabled[idx];
}

static bool fpv_tg_set_notification(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type,
    bool enabled) {
  int idx = fpv_tg_notification_index(type);
  if (!service || idx < 0) {
    return false;
  }
  fpv_tg_chat_settings_t* settings = fpv_tg_find_chat_settings(service, chat_id);
  if (!settings) {
    fpv_tg_chat_settings_t* grown =
        (fpv_tg_chat_settings_t*)realloc(
            service->notifications,
            (service->notification_count + 1) * sizeof(*grown));
    if (!grown) {
      return false;
    }
    service->notifications = grown;
    settings = &service->notifications[service->notification_count++];
    memset(settings, 0, sizeof(*settings));
    settings->chat_id = chat_id;
    for (size_t i = 0; i < fpv_tg_notification_count; i++) {
      settings->enabled[i] = fpv_tg_default_notification_enabled(
          fpv_tg_notification_ids[i]);
    }
  }
  settings->enabled[idx] = enabled;
  return true;
}

static fpv_result_t fpv_tg_save_notification_settings(
    fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "notifications.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{", 1);
  for (size_t i = 0; i < service->notification_count; i++) {
    if (i > 0) {
      fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    char chat_buf[32];
    snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, service->notifications[i].chat_id);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
    fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\":{", 3);
    bool first = true;
    for (size_t n = 0; n < fpv_tg_notification_count; n++) {
      if (!first) {
        fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
      }
      first = false;
      fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
      fpv_tg_buffer_append(&buffer, &length, &capacity,
                           fpv_tg_notification_ids[n],
                           strlen(fpv_tg_notification_ids[n]));
      fpv_tg_buffer_append(&buffer, &length, &capacity, "\":", 2);
      fpv_tg_buffer_append(&buffer, &length, &capacity,
                           service->notifications[i].enabled[n] ? "1" : "0",
                           1);
    }
    fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  if (!buffer) {
    fpv_free(path);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_tg_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  fpv_free(buffer);
  fpv_free(path);
  return result;
}

static fpv_result_t fpv_tg_load_notification_settings(
    fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "notifications.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_fs_exists(path)) {
    fpv_free(path);
    return FPV_OK;
  }
  size_t size = 0;
  char* content = fpv_tg_read_file(path, &size);
  fpv_free(path);
  if (!content) {
    return FPV_ERR_IO;
  }
  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  fpv_result_t parse_result = fpv_json_parse(content, size, &json, &error);
  fpv_free(content);
  if (parse_result != FPV_OK || !json) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  if (!fpv_json_is_type(json, FPV_JSON_OBJECT)) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  size_t count = fpv_json_object_size(json);
  if (count == 0) {
    fpv_json_destroy(json);
    return FPV_OK;
  }

  fpv_tg_chat_settings_t* settings =
      (fpv_tg_chat_settings_t*)calloc(count, sizeof(*settings));
  if (!settings) {
    fpv_json_destroy(json);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t stored = 0;
  for (size_t idx = 0; idx < count; idx++) {
    const char* key = fpv_json_object_key(json, idx);
    const fpv_json_value_t* value = fpv_json_object_value(json, idx);
    int64_t chat_id = 0;
    if (!key || !key[0]) {
      continue;
    }
    chat_id = strtoll(key, NULL, 10);
    if (chat_id == 0 || !fpv_json_is_type(value, FPV_JSON_OBJECT)) {
      continue;
    }
    settings[stored].chat_id = chat_id;
    for (size_t i = 0; i < fpv_tg_notification_count; i++) {
      const fpv_json_value_t* entry =
          fpv_json_object_get(value, fpv_tg_notification_ids[i]);
      bool enabled = false;
      if (fpv_json_is_type(entry, FPV_JSON_BOOL)) {
        enabled = fpv_json_bool(entry, false);
      } else if (fpv_json_is_type(entry, FPV_JSON_NUMBER)) {
        int64_t flag = 0;
        if (fpv_json_number_to_int64(entry, &flag)) {
          enabled = flag != 0;
        }
      }
      settings[stored].enabled[i] = enabled;
    }
    stored++;
  }
  fpv_json_destroy(json);
  service->notifications = settings;
  service->notification_count = stored;
  return FPV_OK;
}

static bool fpv_tg_is_authorized(
    const fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return false;
  }
  for (size_t i = 0; i < service->authorized_count; i++) {
    if ((int64_t)service->authorized_users[i] == user_id) {
      return true;
    }
  }
  return false;
}

static fpv_result_t fpv_tg_load_authorized_users(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "tg_authorized_users.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_load_uint64(path, &service->authorized_users,
                                              &service->authorized_count);
  fpv_free(path);
  return result;
}

static fpv_result_t fpv_tg_save_authorized_users(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "tg_authorized_users.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_save_uint64(path, service->authorized_users,
                                              service->authorized_count);
  fpv_free(path);
  return result;
}

static fpv_result_t fpv_tg_load_answer_templates(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "answer_templates.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_load_strings(path, &service->answer_templates);
  fpv_free(path);
  return result;
}

static fpv_result_t fpv_tg_save_answer_templates(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "answer_templates.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_save_strings(path, &service->answer_templates);
  fpv_free(path);
  return result;
}

static fpv_tg_user_state_t* fpv_tg_get_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_tg_user_state_t* state = &service->user_states[i];
    if (state->chat_id == chat_id && state->user_id == user_id) {
      return state;
    }
  }
  return NULL;
}

static void fpv_tg_clear_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    bool delete_message) {
  if (!service) {
    return;
  }
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_tg_user_state_t* state = &service->user_states[i];
    if (state->chat_id == chat_id && state->user_id == user_id) {
      if (delete_message && state->message_id > 0) {
        fpv_tg_delete_message(service, chat_id, state->message_id);
      }
      fpv_free(state->data.username);
      *state = service->user_states[service->user_state_count - 1];
      service->user_state_count--;
      return;
    }
  }
}

static bool fpv_tg_set_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    int message_id,
    fpv_tg_state_type_t type,
    const fpv_tg_state_data_t* data) {
  if (!service) {
    return false;
  }
  fpv_tg_user_state_t* state = fpv_tg_get_state(service, chat_id, user_id);
  if (!state) {
    fpv_tg_user_state_t* grown = (fpv_tg_user_state_t*)realloc(
        service->user_states,
        (service->user_state_count + 1) * sizeof(*grown));
    if (!grown) {
      return false;
    }
    service->user_states = grown;
    state = &service->user_states[service->user_state_count++];
    memset(state, 0, sizeof(*state));
    state->chat_id = chat_id;
    state->user_id = user_id;
  }
  fpv_free(state->data.username);
  memset(&state->data, 0, sizeof(state->data));
  if (data) {
    state->data = *data;
    if (data->username) {
      state->data.username = fpv_strdup(data->username);
    }
  }
  state->message_id = message_id;
  state->type = type;
  return true;
}

static fpv_result_t fpv_tg_update_profile_lots(fpv_telegram_service_t* service) {
  if (!service || !service->account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < service->profile_lot_count; i++) {
    fpv_lot_destroy(service->profile_lots[i]);
  }
  fpv_free(service->profile_lots);
  service->profile_lots = NULL;
  service->profile_lot_count = 0;

  fpv_funpay_lot_section_t* sections = NULL;
  size_t section_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_sections(
      service->account, &sections, &section_count, &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || section_count == 0) {
    fpv_free(sections);
    return result;
  }

  fpv_lot_t** all_lots = NULL;
  size_t all_count = 0;
  for (size_t i = 0; i < section_count; i++) {
    if (sections[i].is_currency) {
      continue;
    }
    fpv_lot_t** lots = NULL;
    size_t lot_count = 0;
    memset(&error, 0, sizeof(error));
    result = fpv_funpay_account_get_trade_lots(
        service->account,
        sections[i].id,
        sections[i].is_currency,
        &lots,
        &lot_count,
        &error);
    fpv_funpay_error_clear(&error);
    if (result != FPV_OK || lot_count == 0) {
      if (lots) {
        for (size_t j = 0; j < lot_count; j++) {
          fpv_lot_destroy(lots[j]);
        }
        fpv_free(lots);
      }
      continue;
    }
    fpv_lot_t** grown = (fpv_lot_t**)realloc(
        all_lots, (all_count + lot_count) * sizeof(*grown));
    if (!grown) {
      for (size_t j = 0; j < lot_count; j++) {
        fpv_lot_destroy(lots[j]);
      }
      fpv_free(lots);
      continue;
    }
    all_lots = grown;
    for (size_t j = 0; j < lot_count; j++) {
      all_lots[all_count++] = lots[j];
    }
    fpv_free(lots);
  }
  fpv_free(sections);
  service->profile_lots = all_lots;
  service->profile_lot_count = all_count;
  service->profile_update_ms = fpv_time_now_ms();
  return FPV_OK;
}

static bool fpv_tg_is_valid_filename(const char* name) {
  if (!name || !name[0]) {
    return false;
  }
  for (const unsigned char* ptr = (const unsigned char*)name; *ptr; ptr++) {
    if (*ptr >= 0x80) {
      continue;
    }
    if (!(isalnum(*ptr) || *ptr == '_' || *ptr == '-' || *ptr == ' ')) {
      return false;
    }
  }
  return true;
}

static size_t fpv_tg_count_products(const char* path) {
  if (!path || !fpv_fs_exists(path)) {
    return 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), file)) {
    char* ptr = buffer;
    while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')) {
      ptr++;
    }
    if (*ptr) {
      count++;
    }
  }
  fclose(file);
  return count;
}

static bool fpv_tg_append_products(const char* path, const char* data, bool at_start) {
  if (!path || !data) {
    return false;
  }
  if (!at_start) {
    FILE* file = fopen(path, "ab");
    if (!file) {
      return false;
    }
    size_t len = strlen(data);
    if (len > 0) {
      fwrite("\n", 1, 1, file);
      fwrite(data, 1, len, file);
    }
    fclose(file);
    return true;
  }

  size_t size = 0;
  char* current = fpv_tg_read_file(path, &size);
  if (!current) {
    current = fpv_strdup("");
  }
  size_t data_len = strlen(data);
  size_t new_len = data_len + 1 + size;
  char* merged = (char*)malloc(new_len + 1);
  if (!merged) {
    fpv_free(current);
    return false;
  }
  memcpy(merged, data, data_len);
  merged[data_len] = '\n';
  memcpy(merged + data_len + 1, current, size);
  merged[new_len] = '\0';
  fpv_free(current);
  bool ok = fpv_tg_write_file(path, merged);
  fpv_free(merged);
  return ok;
}

static bool fpv_tg_trim_lower(char* text) {
  if (!text) {
    return false;
  }
  char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  size_t len = (size_t)(end - start);
  if (start != text) {
    memmove(text, start, len);
  }
  text[len] = '\0';
  for (char* ptr = text; *ptr; ptr++) {
    if (*ptr >= 'A' && *ptr <= 'Z') {
      *ptr = (char)(*ptr - 'A' + 'a');
    }
  }
  return len > 0;
}

static void fpv_tg_message_clear(fpv_tg_message_t* message) {
  if (!message) {
    return;
  }
  fpv_free(message->chat_type);
  fpv_free(message->chat_username);
  fpv_free(message->from_username);
  fpv_free(message->text);
  fpv_free(message->document_file_id);
  fpv_free(message->document_file_name);
  fpv_free(message->photo_file_id);
  memset(message, 0, sizeof(*message));
}

static void fpv_tg_callback_clear(fpv_tg_callback_t* cb) {
  if (!cb) {
    return;
  }
  fpv_free(cb->id);
  fpv_free(cb->data);
  fpv_free(cb->from_username);
  fpv_free(cb->chat_username);
  memset(cb, 0, sizeof(*cb));
}

static bool fpv_tg_parse_message(const fpv_json_value_t* value, fpv_tg_message_t* out) {
  if (!value || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const fpv_json_value_t* msg_id = fpv_json_object_get(value, "message_id");
  int64_t mid = 0;
  if (!fpv_json_number_to_int64(msg_id, &mid)) {
    return false;
  }
  out->message_id = (int)mid;
  const fpv_json_value_t* chat = fpv_json_object_get(value, "chat");
  const fpv_json_value_t* chat_id = fpv_json_object_get(chat, "id");
  int64_t cid = 0;
  if (!fpv_json_number_to_int64(chat_id, &cid)) {
    return false;
  }
  out->chat_id = cid;
  const fpv_json_value_t* chat_type = fpv_json_object_get(chat, "type");
  out->chat_type = fpv_strdup(fpv_json_string(chat_type));
  const fpv_json_value_t* chat_username = fpv_json_object_get(chat, "username");
  const char* chat_user = fpv_json_string(chat_username);
  if (chat_user && chat_user[0]) {
    out->chat_username = fpv_strdup(chat_user);
  }
  const fpv_json_value_t* from = fpv_json_object_get(value, "from");
  const fpv_json_value_t* from_id = fpv_json_object_get(from, "id");
  int64_t uid = 0;
  fpv_json_number_to_int64(from_id, &uid);
  out->from_id = uid;
  const fpv_json_value_t* from_username = fpv_json_object_get(from, "username");
  const char* uname = fpv_json_string(from_username);
  if (uname && uname[0]) {
    out->from_username = fpv_strdup(uname);
  }
  const fpv_json_value_t* text = fpv_json_object_get(value, "text");
  const char* text_val = fpv_json_string(text);
  if (text_val && text_val[0]) {
    out->text = fpv_strdup(text_val);
  }
  const fpv_json_value_t* reply = fpv_json_object_get(value, "reply_to_message");
  if (reply) {
    const fpv_json_value_t* topic = fpv_json_object_get(reply, "forum_topic_created");
    out->reply_topic_created = fpv_json_bool(topic, false);
  }

  const fpv_json_value_t* document = fpv_json_object_get(value, "document");
  if (document) {
    const char* file_id = fpv_json_string(fpv_json_object_get(document, "file_id"));
    const char* file_name = fpv_json_string(fpv_json_object_get(document, "file_name"));
    int64_t file_size = 0;
    fpv_json_number_to_int64(fpv_json_object_get(document, "file_size"), &file_size);
    if (file_id && file_id[0]) {
      out->document_file_id = fpv_strdup(file_id);
      out->has_document = true;
    }
    if (file_name && file_name[0]) {
      out->document_file_name = fpv_strdup(file_name);
    }
    out->document_file_size = file_size;
  }

  const fpv_json_value_t* photo = fpv_json_object_get(value, "photo");
  if (photo && fpv_json_is_type(photo, FPV_JSON_ARRAY)) {
    size_t count = fpv_json_array_size(photo);
    if (count > 0) {
      const fpv_json_value_t* last = fpv_json_array_get(photo, count - 1);
      const char* file_id = fpv_json_string(fpv_json_object_get(last, "file_id"));
      int64_t file_size = 0;
      fpv_json_number_to_int64(fpv_json_object_get(last, "file_size"), &file_size);
      if (file_id && file_id[0]) {
        out->photo_file_id = fpv_strdup(file_id);
        out->photo_file_size = file_size;
        out->has_photo = true;
      }
    }
  }
  return true;
}

static bool fpv_tg_parse_callback(const fpv_json_value_t* value, fpv_tg_callback_t* out) {
  if (!value || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char* id = fpv_json_string(fpv_json_object_get(value, "id"));
  const char* data = fpv_json_string(fpv_json_object_get(value, "data"));
  if (!id || !data) {
    return false;
  }
  out->id = fpv_strdup(id);
  out->data = fpv_strdup(data);
  const fpv_json_value_t* from = fpv_json_object_get(value, "from");
  int64_t uid = 0;
  fpv_json_number_to_int64(fpv_json_object_get(from, "id"), &uid);
  out->from_id = uid;
  const char* uname = fpv_json_string(fpv_json_object_get(from, "username"));
  if (uname && uname[0]) {
    out->from_username = fpv_strdup(uname);
  }
  const fpv_json_value_t* message = fpv_json_object_get(value, "message");
  if (message) {
    int64_t chat_id = 0;
    fpv_json_number_to_int64(
        fpv_json_object_get(fpv_json_object_get(message, "chat"), "id"),
        &chat_id);
    out->chat_id = chat_id;
    int64_t msg_id = 0;
    fpv_json_number_to_int64(fpv_json_object_get(message, "message_id"), &msg_id);
    out->message_id = (int)msg_id;
    const char* chat_username =
        fpv_json_string(fpv_json_object_get(fpv_json_object_get(message, "chat"), "username"));
    if (chat_username && chat_username[0]) {
      out->chat_username = fpv_strdup(chat_username);
    }
  }
  return true;
}

static bool fpv_tg_format_time_hms(uint64_t timestamp_ms, char* buffer, size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  struct tm tm_value;
#if defined(_WIN32)
  if (localtime_s(&tm_value, &seconds) != 0) {
    return false;
  }
#else
  if (!localtime_r(&seconds, &tm_value)) {
    return false;
  }
#endif
  int written = snprintf(
      buffer,
      buffer_len,
      "%02d:%02d:%02d",
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec);
  return written > 0 && (size_t)written < buffer_len;
}

static char* fpv_tg_build_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance,
    uint32_t active_sales) {
  const char* username = service && service->account
      ? fpv_funpay_account_username(service->account)
      : "";
  uint64_t id = service && service->account
      ? fpv_funpay_account_id(service->account)
      : 0;
  uint32_t sales = active_sales;
  if (service && service->account) {
    sales = fpv_funpay_account_active_sales(service->account);
  }
  char time_buf[32];
  time_buf[0] = '\0';
  if (service && service->account) {
    uint64_t updated_ms = fpv_funpay_account_last_update_ms(service->account);
    if (updated_ms > 0) {
      fpv_tg_format_time_hms(updated_ms, time_buf, sizeof(time_buf));
    }
  }
  char buffer[2048];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xD0\xA1\xD1\x82\xD0\xB0\xD1\x82\xD0\xB8\xD1\x81\xD1\x82\xD0\xB8\xD0\xBA\xD0\xB0 \xD0\xB0\xD0\xBA\xD0\xBA\xD0\xB0\xD1\x83\xD0\xBD\xD1\x82\xD0\xB0 <b><i>%s</i></b>\n\n"
      "<b>ID:</b> <code>%" PRIu64 "</code>\n"
      "<b>\xD0\x9D\xD0\xB5\xD0\xB7\xD0\xB0\xD0\xB2\xD0\xB5\xD1\x80\xD1\x88\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD1\x85 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xBE\xD0\xB2:</b> <code>%u</code>\n"
      "<b>\xD0\x91\xD0\xB0\xD0\xBB\xD0\xB0\xD0\xBD\xD1\x81:</b> \n"
      "    <b>\xE2\x82\xBD:</b> <code>%.2f\xE2\x82\xBD</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f\xE2\x82\xBD</code>.\n"
      "    <b>$:</b> <code>%.2f$</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f$</code>.\n"
      "    <b>\xE2\x82\xAC:</b> <code>%.2f\xE2\x82\xAC</code>, \xD0\xB4\xD0\xBE\xD1\x81\xD1\x82\xD1\x83\xD0\xBF\xD0\xBD\xD0\xBE \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xB2\xD1\x8B\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB0 <code>%.2f\xE2\x82\xAC</code>.\n\n"
      "<i>\xD0\x9E\xD0\xB1\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBE:</i>  <code>%s</code>",
      username ? username : "",
      id,
      sales,
      balance ? balance->total_rub : 0.0,
      balance ? balance->available_rub : 0.0,
      balance ? balance->total_usd : 0.0,
      balance ? balance->available_usd : 0.0,
      balance ? balance->total_eur : 0.0,
      balance ? balance->available_eur : 0.0,
      time_buf);
  return fpv_strdup(buffer);
}

static uint64_t fpv_tg_cpu_count(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors > 0 ? info.dwNumberOfProcessors : 1;
#else
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  if (count < 1) {
    return 1;
  }
  return (uint64_t)count;
#endif
}

#if defined(_WIN32)
static uint64_t fpv_tg_filetime_to_uint64(FILETIME value) {
  ULARGE_INTEGER combined;
  combined.LowPart = value.dwLowDateTime;
  combined.HighPart = value.dwHighDateTime;
  return combined.QuadPart;
}

static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  FILETIME idle_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
    return false;
  }
  uint64_t idle_ticks = fpv_tg_filetime_to_uint64(idle_time);
  uint64_t kernel_ticks = fpv_tg_filetime_to_uint64(kernel_time);
  uint64_t user_ticks = fpv_tg_filetime_to_uint64(user_time);
  if (total) {
    *total = kernel_ticks + user_ticks;
  }
  if (idle) {
    *idle = idle_ticks;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  FILETIME creation;
  FILETIME exit_time;
  FILETIME kernel_time;
  FILETIME user_time;
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit_time,
                       &kernel_time, &user_time)) {
    return false;
  }
  uint64_t kernel_ticks = fpv_tg_filetime_to_uint64(kernel_time);
  uint64_t user_ticks = fpv_tg_filetime_to_uint64(user_time);
  if (out_seconds) {
    *out_seconds = (double)(kernel_ticks + user_ticks) / 10000000.0;
  }
  return true;
}
#elif defined(__APPLE__)
static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  host_cpu_load_info_data_t info;
  mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                      (host_info_t)&info, &count) != KERN_SUCCESS) {
    return false;
  }
  uint64_t user = info.cpu_ticks[CPU_STATE_USER];
  uint64_t system = info.cpu_ticks[CPU_STATE_SYSTEM];
  uint64_t nice = info.cpu_ticks[CPU_STATE_NICE];
  uint64_t idle_ticks = info.cpu_ticks[CPU_STATE_IDLE];
  if (total) {
    *total = user + system + nice + idle_ticks;
  }
  if (idle) {
    *idle = idle_ticks;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return false;
  }
  double user = (double)usage.ru_utime.tv_sec +
      (double)usage.ru_utime.tv_usec / 1000000.0;
  double sys = (double)usage.ru_stime.tv_sec +
      (double)usage.ru_stime.tv_usec / 1000000.0;
  if (out_seconds) {
    *out_seconds = user + sys;
  }
  return true;
}
#else
static bool fpv_tg_read_system_cpu(uint64_t* total, uint64_t* idle) {
  FILE* file = fopen("/proc/stat", "r");
  if (!file) {
    return false;
  }
  char line[256];
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return false;
  }
  fclose(file);
  uint64_t user = 0;
  uint64_t nice = 0;
  uint64_t system = 0;
  uint64_t idle_ticks = 0;
  uint64_t iowait = 0;
  uint64_t irq = 0;
  uint64_t softirq = 0;
  uint64_t steal = 0;
  if (sscanf(line, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
             " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
             &user, &nice, &system, &idle_ticks, &iowait, &irq, &softirq,
             &steal) < 4) {
    return false;
  }
  if (total) {
    *total = user + nice + system + idle_ticks + iowait + irq + softirq + steal;
  }
  if (idle) {
    *idle = idle_ticks + iowait;
  }
  return true;
}

static bool fpv_tg_read_process_cpu(double* out_seconds) {
  FILE* file = fopen("/proc/self/stat", "r");
  if (!file) {
    return false;
  }
  char buffer[4096];
  if (!fgets(buffer, sizeof(buffer), file)) {
    fclose(file);
    return false;
  }
  fclose(file);
  char* end = strrchr(buffer, ')');
  if (!end) {
    return false;
  }
  char* ptr = end + 2;
  unsigned long long utime = 0;
  unsigned long long stime = 0;
  for (int field = 3; field <= 15 && *ptr; field++) {
    while (*ptr == ' ') {
      ptr++;
    }
    char* next = NULL;
    unsigned long long value = strtoull(ptr, &next, 10);
    if (!next || next == ptr) {
      return false;
    }
    if (field == 14) {
      utime = value;
    } else if (field == 15) {
      stime = value;
      break;
    }
    ptr = next;
  }
  long ticks = sysconf(_SC_CLK_TCK);
  if (ticks <= 0) {
    return false;
  }
  if (out_seconds) {
    *out_seconds = (double)(utime + stime) / (double)ticks;
  }
  return true;
}
#endif

static bool fpv_tg_get_cpu_usage(double* out_total, double* out_process) {
  uint64_t total1 = 0;
  uint64_t idle1 = 0;
  double proc1 = 0.0;
  if (!fpv_tg_read_system_cpu(&total1, &idle1) ||
      !fpv_tg_read_process_cpu(&proc1)) {
    return false;
  }
  uint64_t start_ms = fpv_time_now_ms();
  fpv_tg_sleep_ms(150);
  uint64_t total2 = 0;
  uint64_t idle2 = 0;
  double proc2 = 0.0;
  if (!fpv_tg_read_system_cpu(&total2, &idle2) ||
      !fpv_tg_read_process_cpu(&proc2)) {
    return false;
  }
  uint64_t end_ms = fpv_time_now_ms();
  uint64_t total_delta = total2 > total1 ? total2 - total1 : 0;
  uint64_t idle_delta = idle2 > idle1 ? idle2 - idle1 : 0;
  if (total_delta == 0) {
    return false;
  }
  double total_usage =
      (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
  double interval_sec = (double)(end_ms - start_ms) / 1000.0;
  if (interval_sec <= 0.0) {
    interval_sec = 0.001;
  }
  uint64_t cpu_count = fpv_tg_cpu_count();
  double proc_delta = proc2 - proc1;
  double process_usage =
      (proc_delta / interval_sec) * 100.0 / (double)cpu_count;
  if (out_total) {
    *out_total = total_usage;
  }
  if (out_process) {
    *out_process = process_usage;
  }
  return true;
}

static bool fpv_tg_get_memory_stats(
    uint64_t* total_mb,
    uint64_t* used_mb,
    uint64_t* free_mb,
    uint64_t* process_mb) {
  uint64_t total = 0;
  uint64_t free_mem = 0;
  uint64_t used = 0;
  uint64_t proc = 0;
#if defined(_WIN32)
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    total = (uint64_t)(status.ullTotalPhys / 1048576ULL);
    free_mem = (uint64_t)(status.ullAvailPhys / 1048576ULL);
    used = total > free_mem ? total - free_mem : 0;
  }
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    proc = (uint64_t)(pmc.WorkingSetSize / 1048576ULL);
  }
#elif defined(__APPLE__)
  vm_statistics64_data_t vm_stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
    uint64_t page_size = 0;
    host_page_size(mach_host_self(), (vm_size_t*)&page_size);
    total = (uint64_t)((vm_stats.active_count + vm_stats.inactive_count +
                        vm_stats.wire_count + vm_stats.free_count) *
                       page_size / 1048576ULL);
    free_mem = (uint64_t)(vm_stats.free_count * page_size / 1048576ULL);
    used = total > free_mem ? total - free_mem : 0;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    proc = (uint64_t)(usage.ru_maxrss / 1048576ULL);
  }
#else
  long pages = sysconf(_SC_PHYS_PAGES);
  long free_pages = sysconf(_SC_AVPHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages > 0 && free_pages >= 0 && page_size > 0) {
    total = (uint64_t)pages * (uint64_t)page_size / 1048576ULL;
    free_mem = (uint64_t)free_pages * (uint64_t)page_size / 1048576ULL;
    used = total > free_mem ? total - free_mem : 0;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    proc = (uint64_t)(usage.ru_maxrss / 1024ULL);
  }
#endif
  if (total_mb) {
    *total_mb = total;
  }
  if (used_mb) {
    *used_mb = used;
  }
  if (free_mb) {
    *free_mb = free_mem;
  }
  if (process_mb) {
    *process_mb = proc;
  }
  return total > 0;
}

static bool fpv_tg_format_uptime(
    uint64_t seconds,
    char* buffer,
    size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  uint64_t days = seconds / 86400ULL;
  uint64_t hours = (seconds % 86400ULL) / 3600ULL;
  uint64_t minutes = (seconds % 3600ULL) / 60ULL;
  uint64_t secs = seconds % 60ULL;
  if (days > 0) {
    int written = snprintf(
        buffer, buffer_len, "%" PRIu64 "d %02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
        days, hours, minutes, secs);
    return written > 0 && (size_t)written < buffer_len;
  }
  int written = snprintf(
      buffer, buffer_len, "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64,
      hours, minutes, secs);
  return written > 0 && (size_t)written < buffer_len;
}

typedef struct fpv_tg_adv_profile_entry {
  char* order_id;
  uint64_t time_sec;
  double price;
} fpv_tg_adv_profile_entry_t;

static void fpv_tg_adv_profile_entries_destroy(
    fpv_tg_adv_profile_entry_t* entries,
    size_t count) {
  if (!entries) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(entries[i].order_id);
  }
  fpv_free(entries);
}

static fpv_tg_adv_profile_entry_t* fpv_tg_load_adv_profile(
    const fpv_telegram_service_t* service,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!service || !service->storage.cache_dir) {
    return NULL;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "advProfileStat.json");
  if (!path) {
    return NULL;
  }
  if (!fpv_fs_exists(path)) {
    fpv_free(path);
    return NULL;
  }
  size_t size = 0;
  char* content = fpv_tg_read_file(path, &size);
  fpv_free(path);
  if (!content) {
    return NULL;
  }
  fpv_json_value_t* root = NULL;
  fpv_json_error_t error;
  fpv_result_t result = fpv_json_parse(content, size, &root, &error);
  fpv_free(content);
  if (result != FPV_OK || !root || !fpv_json_is_type(root, FPV_JSON_OBJECT)) {
    fpv_json_destroy(root);
    return NULL;
  }
  size_t count = fpv_json_object_size(root);
  fpv_tg_adv_profile_entry_t* entries =
      (fpv_tg_adv_profile_entry_t*)calloc(count, sizeof(*entries));
  if (!entries) {
    fpv_json_destroy(root);
    return NULL;
  }
  size_t used = 0;
  for (size_t i = 0; i < count; i++) {
    const char* key = fpv_json_object_key(root, i);
    const fpv_json_value_t* value = fpv_json_object_value(root, i);
    if (!key || !key[0] || !value || !fpv_json_is_type(value, FPV_JSON_OBJECT)) {
      continue;
    }
    uint64_t time_sec = 0;
    double price = 0.0;
    if (!fpv_json_number_to_uint64(fpv_json_object_get(value, "time"), &time_sec) ||
        !fpv_json_number_to_double(fpv_json_object_get(value, "price"), &price)) {
      continue;
    }
    entries[used].order_id = fpv_strdup(key);
    if (!entries[used].order_id) {
      continue;
    }
    entries[used].time_sec = time_sec;
    entries[used].price = price;
    used++;
  }
  fpv_json_destroy(root);
  if (out_count) {
    *out_count = used;
  }
  return entries;
}

typedef struct fpv_tg_order_stats {
  uint32_t sales_day;
  uint32_t sales_week;
  uint32_t sales_month;
  uint32_t sales_all;
  double sales_price_day;
  double sales_price_week;
  double sales_price_month;
  double sales_price_all;
  uint32_t refunds_day;
  uint32_t refunds_week;
  uint32_t refunds_month;
  uint32_t refunds_all;
  double refunds_price_day;
  double refunds_price_week;
  double refunds_price_month;
  double refunds_price_all;
} fpv_tg_order_stats_t;

static bool fpv_tg_collect_order_stats(
    fpv_telegram_service_t* service,
    fpv_tg_order_stats_t* stats) {
  if (!service || !service->account || !stats) {
    return false;
  }
  memset(stats, 0, sizeof(*stats));
  uint64_t now_ms = fpv_time_now_ms();
  const uint64_t day_ms = 86400000ULL;
  const uint64_t week_ms = 7ULL * day_ms;
  const uint64_t month_ms = 30ULL * day_ms;
  char* continue_from = NULL;
  bool ok = true;
  do {
    fpv_order_t** orders = NULL;
    size_t order_count = 0;
    char* next = NULL;
    fpv_funpay_error_t error;
    memset(&error, 0, sizeof(error));
    fpv_result_t result = fpv_funpay_account_get_orders_page(
        service->account,
        NULL,
        continue_from,
        &orders,
        &order_count,
        &next,
        &error);
    fpv_funpay_error_clear(&error);
    fpv_free(continue_from);
    continue_from = NULL;
    if (result != FPV_OK) {
      if (orders) {
        for (size_t i = 0; i < order_count; i++) {
          fpv_order_destroy(orders[i]);
        }
        fpv_free(orders);
      }
      fpv_free(next);
      ok = false;
      break;
    }
    for (size_t i = 0; i < order_count; i++) {
      fpv_order_t* order = orders[i];
      if (!order) {
        continue;
      }
      bool refunded = order->status == FPV_ORDER_REFUNDED;
      double amount = order->amount;
      if (refunded) {
        stats->refunds_all++;
        stats->refunds_price_all += amount;
      } else {
        stats->sales_all++;
        stats->sales_price_all += amount;
      }
      if (order->created_at_ms > 0 && now_ms >= order->created_at_ms) {
        uint64_t age_ms = now_ms - order->created_at_ms;
        if (age_ms <= day_ms) {
          if (refunded) {
            stats->refunds_day++;
            stats->refunds_week++;
            stats->refunds_month++;
            stats->refunds_price_day += amount;
            stats->refunds_price_week += amount;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_day++;
            stats->sales_week++;
            stats->sales_month++;
            stats->sales_price_day += amount;
            stats->sales_price_week += amount;
            stats->sales_price_month += amount;
          }
        } else if (age_ms <= week_ms) {
          if (refunded) {
            stats->refunds_week++;
            stats->refunds_month++;
            stats->refunds_price_week += amount;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_week++;
            stats->sales_month++;
            stats->sales_price_week += amount;
            stats->sales_price_month += amount;
          }
        } else if (age_ms <= month_ms) {
          if (refunded) {
            stats->refunds_month++;
            stats->refunds_price_month += amount;
          } else {
            stats->sales_month++;
            stats->sales_price_month += amount;
          }
        }
      }
    }
    if (orders) {
      for (size_t i = 0; i < order_count; i++) {
        fpv_order_destroy(orders[i]);
      }
      fpv_free(orders);
    }
    continue_from = next;
  } while (continue_from && continue_from[0]);
  fpv_free(continue_from);
  return ok;
}

static char* fpv_tg_build_adv_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance) {
  if (!service || !service->account) {
    return NULL;
  }
  const char* username = fpv_funpay_account_username(service->account);
  uint64_t id = fpv_funpay_account_id(service->account);
  uint32_t active_sales = fpv_funpay_account_active_sales(service->account);
  const char* currency = fpv_funpay_account_currency(service->account);
  const char* symbol = "\xE2\x82\xBD";
  double total_balance = balance ? balance->total_rub : 0.0;
  double available_balance = balance ? balance->available_rub : 0.0;
  if (currency && strcmp(currency, "USD") == 0) {
    symbol = "$";
    total_balance = balance ? balance->total_usd : 0.0;
    available_balance = balance ? balance->available_usd : 0.0;
  } else if (currency && strcmp(currency, "EUR") == 0) {
    symbol = "\xE2\x82\xAC";
    total_balance = balance ? balance->total_eur : 0.0;
    available_balance = balance ? balance->available_eur : 0.0;
  }

  double can_hour = 0.0;
  double can_day = 0.0;
  double can_2day = 0.0;
  size_t entry_count = 0;
  fpv_tg_adv_profile_entry_t* entries =
      fpv_tg_load_adv_profile(service, &entry_count);
  uint64_t now_sec = (uint64_t)time(NULL);
  for (size_t i = 0; i < entry_count; i++) {
    uint64_t age = now_sec > entries[i].time_sec
        ? now_sec - entries[i].time_sec
        : 0;
    if (age > 172800) {
      continue;
    }
    if (age > 169200) {
      can_hour += entries[i].price;
    } else if (age > 86400) {
      can_day += entries[i].price;
    } else {
      can_2day += entries[i].price;
    }
  }
  fpv_tg_adv_profile_entries_destroy(entries, entry_count);

  fpv_tg_order_stats_t stats;
  if (!fpv_tg_collect_order_stats(service, &stats)) {
    return NULL;
  }

  char time_buf[32];
  time_buf[0] = '\0';
  uint64_t updated_ms = fpv_funpay_account_last_update_ms(service->account);
  if (updated_ms > 0) {
    fpv_tg_format_time_hms(updated_ms, time_buf, sizeof(time_buf));
  }

  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char buffer[8192];
  snprintf(
      buffer,
      sizeof(buffer),
      "Account statistics <b><i>%s</i></b>\n\n"
      "<b>ID:</b> <code>%" PRIu64 "</code>\n"
      "<b>Balance:</b> <code>%.2f %s</code>\n"
      "<b>Active orders:</b> <code>%u</code>\n\n"
      "<b>Available to withdraw</b>\n"
      "<b>Now:</b> <code>%.1f %s</code>\n"
      "<b>In an hour:</b> <code>+%.1f %s</code>\n"
      "<b>In a day:</b> <code>+%.1f %s</code>\n"
      "<b>In 2 days:</b> <code>+%.1f %s</code>\n\n"
      "<b>Goods sold</b>\n"
      "<b>Day:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Week:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Month:</b> <code>%u (%.1f %s)</code>\n"
      "<b>All time:</b> <code>%u (%.1f %s)</code>\n\n"
      "<b>Goods refunded</b>\n"
      "<b>Day:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Week:</b> <code>%u (%.1f %s)</code>\n"
      "<b>Month:</b> <code>%u (%.1f %s)</code>\n"
      "<b>All time:</b> <code>%u (%.1f %s)</code>\n\n"
      "<i>Updated:</i>  <code>%s</code>",
      escaped_user ? escaped_user : "",
      id,
      total_balance,
      symbol,
      active_sales,
      available_balance,
      symbol,
      can_hour,
      symbol,
      can_day,
      symbol,
      can_2day,
      symbol,
      stats.sales_day,
      stats.sales_price_day,
      symbol,
      stats.sales_week,
      stats.sales_price_week,
      symbol,
      stats.sales_month,
      stats.sales_price_month,
      symbol,
      stats.sales_all,
      stats.sales_price_all,
      symbol,
      stats.refunds_day,
      stats.refunds_price_day,
      symbol,
      stats.refunds_week,
      stats.refunds_price_week,
      symbol,
      stats.refunds_month,
      stats.refunds_price_month,
      symbol,
      stats.refunds_all,
      stats.refunds_price_all,
      symbol,
      time_buf);
  fpv_free(escaped_user);
  return fpv_strdup(buffer);
}

static char* fpv_tg_build_sysinfo_text(
    fpv_telegram_service_t* service,
    uint64_t chat_id) {
  if (!service) {
    return NULL;
  }
  double total_cpu = 0.0;
  double process_cpu = 0.0;
  fpv_tg_get_cpu_usage(&total_cpu, &process_cpu);
  uint64_t total_mb = 0;
  uint64_t used_mb = 0;
  uint64_t free_mb = 0;
  uint64_t proc_mb = 0;
  fpv_tg_get_memory_stats(&total_mb, &used_mb, &free_mb, &proc_mb);
  char cpu_lines[128];
  snprintf(cpu_lines, sizeof(cpu_lines), "    CPU:  <code>%.1f%%</code>", total_cpu);
  char process_buf[32];
  snprintf(process_buf, sizeof(process_buf), "%.1f", process_cpu);
  char total_buf[32];
  char used_buf[32];
  char free_buf[32];
  char proc_buf[32];
  snprintf(total_buf, sizeof(total_buf), "%" PRIu64, total_mb);
  snprintf(used_buf, sizeof(used_buf), "%" PRIu64, used_mb);
  snprintf(free_buf, sizeof(free_buf), "%" PRIu64, free_mb);
  snprintf(proc_buf, sizeof(proc_buf), "%" PRIu64, proc_mb);
  uint64_t uptime_sec = 0;
  if (service->start_ms > 0) {
    uint64_t now_ms = fpv_time_now_ms();
    if (now_ms > service->start_ms) {
      uptime_sec = (now_ms - service->start_ms) / 1000ULL;
    }
  }
  char uptime_buf[64];
  fpv_tg_format_uptime(uptime_sec, uptime_buf, sizeof(uptime_buf));
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
  char* formatted = fpv_tg_loc_format(
      service,
      "sys_info",
      (const char*[]){
          cpu_lines,
          process_buf,
          total_buf,
          used_buf,
          free_buf,
          proc_buf,
          uptime_buf,
          chat_buf},
      8);
  if (formatted) {
    return formatted;
  }
  return fpv_strdup("");
}

static bool fpv_tg_parse_bool(const char* value, bool* out) {
  if (!value || !out) {
    return false;
  }
  if (value[0] == '1' && value[1] == '\0') {
    *out = true;
    return true;
  }
  if (value[0] == '0' && value[1] == '\0') {
    *out = false;
    return true;
  }
  if (strcasecmp(value, "true") == 0) {
    *out = true;
    return true;
  }
  if (strcasecmp(value, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool fpv_tg_starts_with(const char* text, const char* prefix) {
  if (!text || !prefix) {
    return false;
  }
  size_t len = strlen(prefix);
  return strncmp(text, prefix, len) == 0;
}

static bool fpv_tg_generate_key(char* buffer, size_t length) {
  if (!buffer || length < 2) {
    return false;
  }
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  size_t charset_len = sizeof(charset) - 1;
  for (size_t i = 0; i + 1 < length; i++) {
    buffer[i] = charset[rand() % charset_len];
  }
  buffer[length - 1] = '\0';
  return true;
}

static size_t fpv_tg_get_offset(size_t element_index, size_t max_per_page) {
  size_t elements_amount = element_index + 1;
  size_t elements_on_page = elements_amount % max_per_page;
  if (elements_on_page == 0) {
    elements_on_page = max_per_page;
  }
  if (elements_amount <= elements_on_page) {
    return 0;
  }
  return element_index - elements_on_page + 1;
}

static bool fpv_tg_find_ini_section(
    fpv_ini_t* ini,
    const char* name,
    size_t* out_index) {
  if (out_index) {
    *out_index = 0;
  }
  if (!ini || !name) {
    return false;
  }
  size_t count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (section && strcmp(section, name) == 0) {
      if (out_index) {
        *out_index = i;
      }
      return true;
    }
  }
  return false;
}

static void fpv_tg_add_navigation_buttons(
    fpv_tg_keyboard_t* kb,
    size_t offset,
    size_t max_per_page,
    size_t page_count,
    size_t total_count,
    const char* callback_base,
    const char* extra) {
  if (!kb || !callback_base || total_count == 0) {
    return;
  }
  bool has_prev = offset > 0;
  bool has_next = offset + page_count < total_count;
  if (!has_prev && !has_next) {
    return;
  }

  size_t back_offset = 0;
  if (has_prev) {
    back_offset = offset > max_per_page ? offset - max_per_page : 0;
  }
  size_t last_offset = fpv_tg_get_offset(total_count - 1, max_per_page);
  size_t next_offset = offset + page_count;

  char first_cb[128];
  char back_cb[128];
  char next_cb[128];
  char last_cb[128];
  const char* tail = extra ? extra : "";
  if (has_prev) {
    snprintf(first_cb, sizeof(first_cb), "%s:0%s", callback_base, tail);
    snprintf(back_cb, sizeof(back_cb), "%s:%zu%s", callback_base, back_offset, tail);
  } else {
    snprintf(first_cb, sizeof(first_cb), "%s", fpv_tg_cbt_empty);
    snprintf(back_cb, sizeof(back_cb), "%s", fpv_tg_cbt_empty);
  }
  if (has_next) {
    snprintf(next_cb, sizeof(next_cb), "%s:%zu%s", callback_base, next_offset, tail);
    snprintf(last_cb, sizeof(last_cb), "%s:%zu%s", callback_base, last_offset, tail);
  } else {
    snprintf(next_cb, sizeof(next_cb), "%s", fpv_tg_cbt_empty);
    snprintf(last_cb, sizeof(last_cb), "%s", fpv_tg_cbt_empty);
  }

  fpv_tg_keyboard_add_button(kb, "<<", first_cb, NULL);
  fpv_tg_keyboard_add_button(kb, "<", back_cb, NULL);
  fpv_tg_keyboard_add_button(kb, ">", next_cb, NULL);
  fpv_tg_keyboard_add_button(kb, ">>", last_cb, NULL);
  fpv_tg_keyboard_row_end(kb);
}

static fpv_tg_attempt_t* fpv_tg_find_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->attempt_count; i++) {
    if (service->attempts[i].user_id == user_id) {
      return &service->attempts[i];
    }
  }
  return NULL;
}

static uint32_t fpv_tg_increment_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return 0;
  }
  fpv_tg_attempt_t* attempt = fpv_tg_find_attempt(service, user_id);
  if (!attempt) {
    fpv_tg_attempt_t* grown = (fpv_tg_attempt_t*)realloc(
        service->attempts,
        (service->attempt_count + 1) * sizeof(*grown));
    if (!grown) {
      return 0;
    }
    service->attempts = grown;
    attempt = &service->attempts[service->attempt_count++];
    attempt->user_id = user_id;
    attempt->count = 0;
  }
  attempt->count++;
  return attempt->count;
}

static void fpv_tg_clear_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return;
  }
  for (size_t i = 0; i < service->attempt_count; i++) {
    if (service->attempts[i].user_id == user_id) {
      service->attempts[i] = service->attempts[service->attempt_count - 1];
      service->attempt_count--;
      return;
    }
  }
}

static bool fpv_tg_add_authorized_user(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return false;
  }
  if (fpv_tg_is_authorized(service, user_id)) {
    return true;
  }
  uint64_t* grown = (uint64_t*)realloc(
      service->authorized_users,
      (service->authorized_count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  service->authorized_users = grown;
  service->authorized_users[service->authorized_count++] = (uint64_t)user_id;
  fpv_tg_clear_attempt(service, user_id);
  fpv_tg_save_authorized_users(service);
  return true;
}

static void fpv_tg_setup_default_notifications(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (fpv_tg_find_chat_settings(service, chat_id)) {
    return;
  }
  fpv_tg_set_notification(service, chat_id, "11", true);
  fpv_tg_set_notification(service, chat_id, "12", true);
  fpv_tg_save_notification_settings(service);
}

static char* fpv_tg_config_path(
    const fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !service->storage.config_dir || !name) {
    return NULL;
  }
  return fpv_path_join(service->storage.config_dir, name);
}

static char* fpv_tg_products_path(
    const fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !service->storage.products_dir || !name) {
    return NULL;
  }
  return fpv_path_join(service->storage.products_dir, name);
}

static bool fpv_tg_reload_main_settings(fpv_telegram_service_t* service) {
  if (!service || !service->storage.config_dir) {
    return false;
  }
  char* config_path = fpv_tg_config_path(service, "_main.cfg");
  if (!config_path) {
    return false;
  }
  fpv_settings_t settings;
  memset(&settings, 0, sizeof(settings));
  fpv_result_t result = fpv_settings_load(config_path, &settings);
  fpv_free(config_path);
  if (result != FPV_OK) {
    fpv_settings_destroy(&settings);
    return false;
  }
  if (service->features) {
    fpv_features_update_settings(
        service->features,
        &settings,
        &service->storage,
        service->locales_dir);
  }
  if (settings.language && settings.language[0]) {
    fpv_telegram_service_update_language(
        service,
        service->locales_dir,
        settings.language);
  }
  fpv_settings_destroy(&settings);
  return true;
}

static void fpv_tg_set_my_commands(fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  struct {
    const char* command;
    const char* key;
  } commands[] = {
      {"menu", "cmd_menu"},
      {"all", "cmd_all"},
      {"profile", "cmd_profile"},
      {"test_lot", "cmd_test_lot"},
      {"upload_img", "cmd_upload_img"},
      {"ban", "cmd_ban"},
      {"unban", "cmd_unban"},
      {"black_list", "cmd_black_list"},
      {"watermark", "cmd_watermark"},
      {"logs", "cmd_logs"},
      {"del_logs", "cmd_del_logs"},
      {"about", "cmd_about"},
      {"old_orders", "cmd_old_orders"},
      {"sys", "cmd_sys"},
      {"keyboard", "cmd_keyboard"},
      {"change_cookie", "cmd_change_cookie"},
      {"restart", "cmd_restart"},
      {"power_off", "cmd_power_off"}};
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"commands\":[", 13);
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (i > 0) {
      fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"command\":\"", 12);
    fpv_tg_json_escape_append(&buffer, &length, &capacity, commands[i].command);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"description\":\"", 17);
    fpv_tg_json_escape_append(
        &buffer, &length, &capacity, fpv_tg_loc(service, commands[i].key));
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"}", 2);
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "]}", 2);
  if (!buffer) {
    return;
  }
  fpv_json_value_t* root = NULL;
  fpv_tg_api_request(
      service,
      "POST",
      "setMyCommands",
      "application/json",
      buffer,
      strlen(buffer),
      &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
}

static bool fpv_tg_load_settings(
    fpv_telegram_service_t* service,
    fpv_settings_t* out_settings) {
  if (!service || !out_settings) {
    return false;
  }
  char* path = fpv_tg_config_path(service, "_main.cfg");
  if (!path) {
    return false;
  }
  memset(out_settings, 0, sizeof(*out_settings));
  fpv_result_t result = fpv_settings_load(path, out_settings);
  fpv_free(path);
  if (result != FPV_OK) {
    fpv_settings_destroy(out_settings);
    return false;
  }
  return true;
}

static bool fpv_tg_update_main_config_value(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    const char* value) {
  if (!service || !section || !key) {
    return false;
  }
  char* path = fpv_tg_config_path(service, "_main.cfg");
  if (!path) {
    return false;
  }
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    fpv_free(path);
    return false;
  }
  bool ok = fpv_ini_set(ini, section, key, value) == FPV_OK &&
      fpv_ini_save(ini, path) == FPV_OK;
  fpv_ini_destroy(ini);
  fpv_free(path);
  return ok;
}

static bool fpv_tg_update_main_config_bool(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    bool value) {
  return fpv_tg_update_main_config_value(
      service, section, key, value ? "1" : "0");
}

static fpv_ini_t* fpv_tg_load_ini(
    fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !name) {
    return NULL;
  }
  char* path = fpv_tg_config_path(service, name);
  if (!path) {
    return NULL;
  }
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  fpv_free(path);
  return ini;
}

static bool fpv_tg_save_ini(
    fpv_telegram_service_t* service,
    const char* name,
    const fpv_ini_t* ini) {
  if (!service || !name || !ini) {
    return false;
  }
  char* path = fpv_tg_config_path(service, name);
  if (!path) {
    return false;
  }
  bool ok = fpv_ini_save(ini, path) == FPV_OK;
  fpv_free(path);
  return ok;
}

static void fpv_tg_free_string_array(char** items, size_t count) {
  if (!items) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(items[i]);
  }
  fpv_free(items);
}

static int fpv_tg_string_compare(const void* left, const void* right) {
  const char* a = *(const char* const*)left;
  const char* b = *(const char* const*)right;
  if (!a && !b) {
    return 0;
  }
  if (!a) {
    return -1;
  }
  if (!b) {
    return 1;
  }
  return strcmp(a, b);
}

static char** fpv_tg_list_products_files(
    const fpv_telegram_service_t* service,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!service || !service->storage.products_dir) {
    return NULL;
  }
  char** files = NULL;
  size_t count = 0;
#if defined(_WIN32)
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", service->storage.products_dir);
  WIN32_FIND_DATAA data;
  HANDLE handle = FindFirstFileA(pattern, &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return NULL;
  }
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    if (!fpv_tg_has_suffix(data.cFileName, ".txt")) {
      continue;
    }
    char* name = fpv_strdup(data.cFileName);
    if (!name) {
      continue;
    }
    char** grown = (char**)realloc(files, (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(name);
      continue;
    }
    files = grown;
    files[count++] = name;
  } while (FindNextFileA(handle, &data));
  FindClose(handle);
#else
  DIR* dir = opendir(service->storage.products_dir);
  if (!dir) {
    return NULL;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!fpv_tg_has_suffix(entry->d_name, ".txt")) {
      continue;
    }
    char* name = fpv_strdup(entry->d_name);
    if (!name) {
      continue;
    }
    char** grown = (char**)realloc(files, (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(name);
      continue;
    }
    files = grown;
    files[count++] = name;
  }
  closedir(dir);
#endif
  if (count > 1) {
    qsort(files, count, sizeof(*files), fpv_tg_string_compare);
  }
  if (out_count) {
    *out_count = count;
  }
  return files;
}

static bool fpv_tg_file_size(const char* path, size_t* out_size) {
  if (out_size) {
    *out_size = 0;
  }
  if (!path) {
    return false;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  long size = ftell(file);
  fclose(file);
  if (size < 0) {
    return false;
  }
  if (out_size) {
    *out_size = (size_t)size;
  }
  return true;
}

static bool fpv_tg_format_datetime(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  struct tm tm_value;
#if defined(_WIN32)
  if (localtime_s(&tm_value, &seconds) != 0) {
    return false;
  }
#else
  if (!localtime_r(&seconds, &tm_value)) {
    return false;
  }
#endif
  int written = snprintf(
      buffer,
      buffer_len,
      "%02d.%02d.%04d %02d:%02d:%02d",
      tm_value.tm_mday,
      tm_value.tm_mon + 1,
      tm_value.tm_year + 1900,
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec);
  return written > 0 && (size_t)written < buffer_len;
}

static void* fpv_tg_thread_main(void* context);
static void fpv_tg_handle_message(fpv_telegram_service_t* service, fpv_tg_message_t* message);
static void fpv_tg_handle_callback(fpv_telegram_service_t* service, fpv_tg_callback_t* callback);

fpv_telegram_service_t* fpv_telegram_service_create(
    const char* token,
    const char* secret,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    const char* language,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus,
    void* control_context,
    fpv_telegram_control_fn restart_fn,
    fpv_telegram_control_fn shutdown_fn) {
  if (!token || !token[0] || !storage) {
    return NULL;
  }
  fpv_telegram_service_t* service =
      (fpv_telegram_service_t*)calloc(1, sizeof(*service));
  if (!service) {
    return NULL;
  }
  if (!fpv_mutex_init(&service->mutex)) {
    free(service);
    return NULL;
  }
  service->token = fpv_strdup(token);
  service->secret = secret ? fpv_strdup(secret) : NULL;
  service->logger = logger;
  service->bus = bus;
  service->locales_dir = locales_dir ? fpv_strdup(locales_dir) : NULL;
  service->language = language ? fpv_strdup(language) : fpv_strdup("ru");
  service->localizer =
      fpv_localizer_create(locales_dir, service->language, "ru");
  if (!service->token || !service->language || !service->localizer) {
    fpv_telegram_service_destroy(service);
    return NULL;
  }
  service->storage = *storage;
  if (storage->config_dir) {
    service->storage.config_dir = fpv_strdup(storage->config_dir);
  }
  if (storage->cache_dir) {
    service->storage.cache_dir = fpv_strdup(storage->cache_dir);
  }
  if (storage->products_dir) {
    service->storage.products_dir = fpv_strdup(storage->products_dir);
  }
  if (storage->logs_dir) {
    service->storage.logs_dir = fpv_strdup(storage->logs_dir);
  }
  if (storage->plugins_dir) {
    service->storage.plugins_dir = fpv_strdup(storage->plugins_dir);
  }

  service->control_context = control_context;
  service->restart_fn = restart_fn;
  service->shutdown_fn = shutdown_fn;

  fpv_tg_load_notification_settings(service);
  fpv_tg_load_authorized_users(service);
  fpv_tg_load_answer_templates(service);
  return service;
}

void fpv_telegram_service_destroy(fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  fpv_telegram_service_stop(service);
  fpv_free(service->token);
  fpv_free(service->secret);
  fpv_free(service->locales_dir);
  fpv_free(service->language);
  fpv_localizer_destroy(service->localizer);
  fpv_free(service->notifications);
  fpv_free(service->authorized_users);
  fpv_string_list_destroy(&service->answer_templates);
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_free(service->user_states[i].data.username);
  }
  fpv_free(service->user_states);
  fpv_free(service->attempts);
  fpv_free(service->init_messages);
  for (size_t i = 0; i < service->delivery_test_count; i++) {
    fpv_free(service->delivery_tests[i].key);
    fpv_free(service->delivery_tests[i].lot_name);
  }
  fpv_free(service->delivery_tests);
  for (size_t i = 0; i < service->profile_lot_count; i++) {
    fpv_lot_destroy(service->profile_lots[i]);
  }
  fpv_free(service->profile_lots);
  fpv_free(service->storage.config_dir);
  fpv_free(service->storage.cache_dir);
  fpv_free(service->storage.products_dir);
  fpv_free(service->storage.logs_dir);
  fpv_free(service->storage.plugins_dir);
  fpv_mutex_destroy(&service->mutex);
  free(service);
}

bool fpv_telegram_service_start(fpv_telegram_service_t* service) {
  if (!service || service->running) {
    return false;
  }
  service->start_ms = fpv_time_now_ms();
  srand((unsigned int)service->start_ms);
  service->instance_id =
      ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ service->start_ms;
  fpv_tg_set_my_commands(service);
  service->running = true;
  if (!fpv_thread_create(&service->thread, fpv_tg_thread_main, service)) {
    service->running = false;
    return false;
  }
  return true;
}

void fpv_telegram_service_stop(fpv_telegram_service_t* service) {
  if (!service || !service->running) {
    return;
  }
  service->running = false;
  fpv_thread_join(&service->thread);
}

void fpv_telegram_service_attach_account(
    fpv_telegram_service_t* service,
    fpv_funpay_account_t* account) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->account = account;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_attach_runner(
    fpv_telegram_service_t* service,
    fpv_funpay_runner_t* runner) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->runner = runner;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_attach_features(
    fpv_telegram_service_t* service,
    fpv_feature_state_t* features) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->features = features;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_update_language(
    fpv_telegram_service_t* service,
    const char* locales_dir,
    const char* language) {
  if (!service || !language || !language[0]) {
    return;
  }
  char* language_copy = fpv_strdup(language);
  char* locales_copy = locales_dir ? fpv_strdup(locales_dir) : NULL;
  fpv_mutex_lock(&service->mutex);
  fpv_free(service->language);
  service->language = language_copy;
  fpv_free(service->locales_dir);
  service->locales_dir = locales_copy;
  fpv_localizer_destroy(service->localizer);
  service->localizer =
      fpv_localizer_create(service->locales_dir, service->language, "ru");
  fpv_mutex_unlock(&service->mutex);
  fpv_tg_set_my_commands(service);
}

void fpv_telegram_service_update_init_messages(
    fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  size_t msg_count = service->init_message_count;
  fpv_tg_init_message_t* messages = NULL;
  if (msg_count > 0) {
    messages = (fpv_tg_init_message_t*)calloc(msg_count, sizeof(*messages));
    if (messages) {
      memcpy(messages, service->init_messages, msg_count * sizeof(*messages));
    }
  }
  fpv_mutex_unlock(&service->mutex);

  if (!account || !messages) {
    fpv_free(messages);
    return;
  }

  fpv_funpay_balance_t balance;
  memset(&balance, 0, sizeof(balance));
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_funpay_account_get_balance(account, &balance, &error);
  fpv_funpay_error_clear(&error);

  const char* username = fpv_funpay_account_username(account);
  uint64_t account_id = fpv_funpay_account_id(account);
  uint32_t sales = fpv_funpay_account_active_sales(account);

  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char id_buf[32];
  char rub_buf[32];
  char usd_buf[32];
  char eur_buf[32];
  char sales_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRIu64, account_id);
  snprintf(rub_buf, sizeof(rub_buf), "%.2f", balance.total_rub);
  snprintf(usd_buf, sizeof(usd_buf), "%.2f", balance.total_usd);
  snprintf(eur_buf, sizeof(eur_buf), "%.2f", balance.total_eur);
  snprintf(sales_buf, sizeof(sales_buf), "%u", sales);
  char* text = fpv_tg_loc_format(
      service,
      "fpv_init",
      (const char*[]){
          FPV_VERSION,
          escaped_user ? escaped_user : "",
          id_buf,
          rub_buf,
          usd_buf,
          eur_buf,
          sales_buf},
      7);
  fpv_free(escaped_user);
  if (!text) {
    fpv_free(messages);
    return;
  }

  for (size_t i = 0; i < msg_count; i++) {
    if (messages[i].chat_id == 0 || messages[i].message_id == 0) {
      continue;
    }
    fpv_tg_edit_message_text(
        service, messages[i].chat_id, messages[i].message_id, text, NULL);
  }
  fpv_free(text);
  fpv_free(messages);
}

static bool fpv_tg_send_notification(
    fpv_telegram_service_t* service,
    const char* text,
    const char* reply_markup,
    const char* notification_type,
    const void* photo_data,
    size_t photo_size,
    bool pin,
    bool capture_init) {
  if (!service || !text || !notification_type) {
    return false;
  }
  int type_index = fpv_tg_notification_index(notification_type);
  if (type_index < 0) {
    return false;
  }
  fpv_mutex_lock(&service->mutex);
  size_t count = service->notification_count;
  fpv_tg_chat_settings_t* settings =
      (fpv_tg_chat_settings_t*)calloc(count, sizeof(*settings));
  if (settings) {
    memcpy(settings, service->notifications, count * sizeof(*settings));
  }
  fpv_mutex_unlock(&service->mutex);
  if (!settings) {
    return false;
  }
  bool any_sent = false;
  for (size_t i = 0; i < count; i++) {
    if (!settings[i].enabled[type_index]) {
      continue;
    }
    int message_id = 0;
    bool needs_id = capture_init || pin;
    bool sent = false;
    if (photo_data && photo_size > 0) {
      sent = fpv_tg_send_photo(
          service, settings[i].chat_id, photo_data, photo_size, text,
          reply_markup, needs_id ? &message_id : NULL);
    } else {
      sent = fpv_tg_send_message(
          service, settings[i].chat_id, text, reply_markup,
          needs_id ? &message_id : NULL);
    }
    if (!sent) {
      char chat_buf[32];
      snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, settings[i].chat_id);
      char* msg = fpv_tg_loc_format(
          service,
          "log_tg_notification_error",
          (const char*[]){chat_buf},
          1);
      fpv_tg_log(service, FPV_LOG_WARNING,
                 msg ? msg : "Telegram send error.");
      fpv_free(msg);
      continue;
    }
    if (capture_init && message_id > 0) {
      fpv_mutex_lock(&service->mutex);
      fpv_tg_init_message_t* grown =
          (fpv_tg_init_message_t*)realloc(
              service->init_messages,
              (service->init_message_count + 1) * sizeof(*grown));
      if (grown) {
        service->init_messages = grown;
        service->init_messages[service->init_message_count].chat_id =
            settings[i].chat_id;
        service->init_messages[service->init_message_count].message_id =
            message_id;
        service->init_message_count++;
      }
      fpv_mutex_unlock(&service->mutex);
    }
    if (pin && message_id > 0) {
      fpv_tg_pin_message(service, settings[i].chat_id, message_id);
    }
    any_sent = true;
  }
  fpv_free(settings);
  return any_sent;
}

static char* fpv_tg_build_reply_keyboard(
    fpv_telegram_service_t* service,
    uint64_t chat_id,
    const char* username,
    bool again,
    bool extend) {
  if (!service || chat_id == 0) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char reply_cb[128];
  snprintf(reply_cb, sizeof(reply_cb), "%s:%" PRIu64 ":%s",
           fpv_tg_cbt_send_fp_message, chat_id, username ? username : "");
  char templates_cb[160];
  snprintf(templates_cb, sizeof(templates_cb), "%s:0:%" PRIu64 ":%s:%d:%d",
           fpv_tg_cbt_template_list_ans, chat_id, username ? username : "",
           again ? 1 : 0, extend ? 1 : 0);
  const char* reply_label = fpv_tg_loc(service, again ? "msg_reply2" : "msg_reply");
  fpv_tg_keyboard_add_button(kb, reply_label, reply_cb, NULL);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "msg_templates"),
                             templates_cb, NULL);
  if (extend) {
    char extend_cb[128];
    snprintf(extend_cb, sizeof(extend_cb), "%s:%" PRIu64 ":%s",
             fpv_tg_cbt_extend_chat, chat_id, username ? username : "");
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "msg_more"), extend_cb, NULL);
  }
  char url_label[256];
  if (username && username[0]) {
    snprintf(url_label, sizeof(url_label), "\xF0\x9F\x8C\x90 %s", username);
  } else {
    snprintf(url_label, sizeof(url_label), "\xF0\x9F\x8C\x90");
  }
  char url[256];
  snprintf(url, sizeof(url), "https://funpay.com/chat/?node=%" PRIu64, chat_id);
  fpv_tg_keyboard_add_button(kb, url_label, NULL, url);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_order_keyboard(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* username,
    uint64_t chat_id,
    bool confirmation,
    bool no_refund) {
  if (!service || !order_id || !order_id[0]) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }

  if (!no_refund) {
    if (confirmation) {
      char yes_cb[128];
      char no_cb[128];
      snprintf(yes_cb, sizeof(yes_cb), "%s:%s:%" PRIu64 ":%s",
               fpv_tg_cbt_refund_confirmed, order_id, chat_id,
               username ? username : "");
      snprintf(no_cb, sizeof(no_cb), "%s:%s:%" PRIu64 ":%s",
               fpv_tg_cbt_refund_cancelled, order_id, chat_id,
               username ? username : "");
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_yes"), yes_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_no"), no_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    } else {
      char refund_cb[128];
      snprintf(refund_cb, sizeof(refund_cb), "%s:%s:%" PRIu64 ":%s",
               fpv_tg_cbt_request_refund, order_id, chat_id,
               username ? username : "");
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ord_refund"),
                                 refund_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
  }

  char order_url[256];
  snprintf(order_url, sizeof(order_url), "https://funpay.com/orders/%s/", order_id);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ord_open"), NULL, order_url);
  fpv_tg_keyboard_row_end(kb);

  if (chat_id > 0) {
    char reply_cb[128];
    snprintf(reply_cb, sizeof(reply_cb), "%s:%" PRIu64 ":%s",
             fpv_tg_cbt_send_fp_message, chat_id, username ? username : "");
    char templates_cb[192];
    snprintf(templates_cb, sizeof(templates_cb), "%s:0:%" PRIu64 ":%s:2:%s:%d",
             fpv_tg_cbt_template_list_ans, chat_id, username ? username : "",
             order_id, no_refund ? 1 : 0);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ord_answer"), reply_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ord_templates"),
                               templates_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_messages_text(
    fpv_telegram_service_t* service,
    const fpv_message_t* const* messages,
    size_t message_count,
    const char* chat_name,
    uint64_t account_id,
    size_t max_messages) {
  if (!service || !messages || message_count == 0) {
    return NULL;
  }
  size_t start = 0;
  if (max_messages > 0 && message_count > max_messages) {
    start = message_count - max_messages;
  }
  char* text = NULL;
  size_t length = 0;
  size_t capacity = 0;
  uint64_t last_author = UINT64_MAX;
  bool last_by_bot = false;
  const char* you = fpv_tg_loc(service, "you");
  const char* support = fpv_tg_loc(service, "support");
  const char* photo_label = fpv_tg_loc(service, "photo");
  for (size_t i = start; i < message_count; i++) {
    const fpv_message_t* msg = messages[i];
    if (!msg) {
      continue;
    }
    if ((!msg->text || !msg->text[0]) &&
        (!msg->image_url || !msg->image_url[0])) {
      char log_buf[256];
      snprintf(
          log_buf,
          sizeof(log_buf),
          "Empty message payload (id=%s, chat=%s, sender=%s).",
          msg->id ? msg->id : "?",
          msg->chat_id ? msg->chat_id : "?",
          msg->sender_name ? msg->sender_name : "?");
      fpv_tg_log(service, FPV_LOG_WARNING, log_buf);
    }
    uint64_t author_id = msg->sender_id ? strtoull(msg->sender_id, NULL, 10) : 0;
    bool same_author = author_id == last_author && msg->by_bot == last_by_bot;
    char* escaped_author = NULL;
    if (msg->sender_name && msg->sender_name[0]) {
      escaped_author = fpv_tg_escape_html(msg->sender_name);
    }

    if (!same_author) {
      const char* author_prefix = "";
      if (author_id == account_id) {
        if (msg->by_bot) {
          author_prefix = "<i><b>\xF0\x9F\xA4\x96 ";
        } else {
          author_prefix = "<i><b>\xF0\x9F\xAB\xB5 ";
        }
        fpv_tg_buffer_append(&text, &length, &capacity, author_prefix, strlen(author_prefix));
        fpv_tg_buffer_append(&text, &length, &capacity, you ? you : "", you ? strlen(you) : 0);
        if (msg->by_bot) {
          fpv_tg_buffer_append(&text, &length, &capacity, " (FPV):</b></i> ", 16);
        } else {
          fpv_tg_buffer_append(&text, &length, &capacity, ":</b></i> ", 11);
        }
      } else if (author_id == 0) {
        fpv_tg_buffer_append_str(&text, &length, &capacity,
                                 "<i><b>\xF0\x9F\x94\xB5 ");
        if (escaped_author && escaped_author[0]) {
          fpv_tg_buffer_append(&text, &length, &capacity,
                               escaped_author, strlen(escaped_author));
        } else {
          fpv_tg_buffer_append(&text, &length, &capacity, "FunPay", 6);
        }
        fpv_tg_buffer_append(&text, &length, &capacity, ": </b></i>", 10);
      } else if (msg->badge && msg->badge[0]) {
        fpv_tg_buffer_append_str(&text, &length, &capacity,
                                 "<i><b>\xF0\x9F\x86\x98 ");
        fpv_tg_buffer_append(&text, &length, &capacity,
                             escaped_author ? escaped_author : "",
                             escaped_author ? strlen(escaped_author) : 0);
        fpv_tg_buffer_append(&text, &length, &capacity, " (", 2);
        fpv_tg_buffer_append(&text, &length, &capacity,
                             support ? support : "",
                             support ? strlen(support) : 0);
        fpv_tg_buffer_append(&text, &length, &capacity, "): </b></i> ", 12);
      } else if (msg->sender_name && chat_name &&
                 strcmp(msg->sender_name, chat_name) == 0) {
        fpv_tg_buffer_append_str(&text, &length, &capacity,
                                 "<i><b>\xF0\x9F\x91\xA4 ");
        fpv_tg_buffer_append(&text, &length, &capacity,
                             escaped_author ? escaped_author : "",
                             escaped_author ? strlen(escaped_author) : 0);
        fpv_tg_buffer_append(&text, &length, &capacity, ": </b></i> ", 10);
      } else {
        fpv_tg_buffer_append_str(&text, &length, &capacity,
                                 "<i><b>\xF0\x9F\x86\x98 ");
        fpv_tg_buffer_append(&text, &length, &capacity,
                             escaped_author ? escaped_author : "",
                             escaped_author ? strlen(escaped_author) : 0);
        fpv_tg_buffer_append(&text, &length, &capacity, " ", 1);
        fpv_tg_buffer_append(&text, &length, &capacity,
                             support ? support : "",
                             support ? strlen(support) : 0);
        fpv_tg_buffer_append(&text, &length, &capacity, ": </b></i> ", 10);
      }
    }
    fpv_free(escaped_author);

    if (msg->text && msg->text[0]) {
      char* escaped_text = fpv_tg_escape_html(msg->text);
      fpv_tg_buffer_append(&text, &length, &capacity, "<code>", 6);
      fpv_tg_buffer_append(&text, &length, &capacity,
                           escaped_text ? escaped_text : "",
                           escaped_text ? strlen(escaped_text) : 0);
      fpv_tg_buffer_append(&text, &length, &capacity, "</code>", 7);
      fpv_free(escaped_text);
    } else if (msg->image_url && msg->image_url[0]) {
      char* escaped_url = fpv_tg_escape_html(msg->image_url);
      fpv_tg_buffer_append(&text, &length, &capacity, "<a href=\"", 9);
      fpv_tg_buffer_append(&text, &length, &capacity,
                           escaped_url ? escaped_url : "",
                           escaped_url ? strlen(escaped_url) : 0);
      fpv_tg_buffer_append(&text, &length, &capacity, "\">", 2);
      fpv_tg_buffer_append(&text, &length, &capacity,
                           photo_label ? photo_label : "",
                           photo_label ? strlen(photo_label) : 0);
      fpv_tg_buffer_append(&text, &length, &capacity, "</a>", 4);
      fpv_free(escaped_url);
    }
    fpv_tg_buffer_append(&text, &length, &capacity, "\n\n", 2);
    last_author = author_id;
    last_by_bot = msg->by_bot;
  }
  return text;
}

void fpv_telegram_service_notify_delivery(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    const char* delivery_text,
    int64_t goods_left,
    bool success) {
  if (!service || !order_id) {
    return;
  }
  char* escaped_delivery = fpv_tg_escape_html(delivery_text ? delivery_text : "");
  char* text = NULL;
  if (!success) {
    const char* fmt = "\xE2\x9D\x8C <code>%s</code>";
    size_t size = strlen(fmt) + (escaped_delivery ? strlen(escaped_delivery) : 0) + 16;
    text = (char*)malloc(size);
    if (text) {
      snprintf(text, size, fmt, escaped_delivery ? escaped_delivery : "");
    }
  } else {
    const char* amount =
        goods_left < 0 ? "<b>\xE2\x88\x9E</b>" : NULL;
    char left_buf[32];
    if (!amount) {
      snprintf(left_buf, sizeof(left_buf), "<code>%" PRId64 "</code>", goods_left);
      amount = left_buf;
    }
    const char* fmt =
        "\xE2\x9C\x85 \xD0\xA3\xD1\x81\xD0\xBF\xD0\xB5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD0\xBB \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80 \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xBE\xD1\x80\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB0 <code>%s</code>.\n\n"
        "\xF0\x9F\x9B\x92 <b><i>\xD0\xA2\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80:</i></b>\n"
        "<code>%s</code>\n\n"
        "\xF0\x9F\x93\x8B <b><i>\xD0\x9E\xD1\x81\xD1\x82\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80\xD0\xBE\xD0\xB2: </i></b>%s";
    size_t size = strlen(fmt) + strlen(order_id) +
        (escaped_delivery ? strlen(escaped_delivery) : 0) + strlen(amount) + 32;
    text = (char*)malloc(size);
    if (text) {
      snprintf(text, size, fmt, order_id, escaped_delivery ? escaped_delivery : "", amount);
    }
  }
  fpv_free(escaped_delivery);
  if (!text) {
    return;
  }
  fpv_tg_send_notification(
      service, text, NULL, "8", NULL, 0, false, false);
  fpv_free(text);
}

void fpv_telegram_service_notify_new_message(
    fpv_telegram_service_t* service,
    const fpv_message_t* const* messages,
    size_t message_count,
    uint64_t chat_id,
    const char* chat_name,
    uint64_t account_id) {
  if (!service || !messages || message_count == 0 || chat_id == 0) {
    return;
  }
  char* text = fpv_tg_build_messages_text(
      service, messages, message_count, chat_name, account_id, 0);
  if (!text) {
    return;
  }

  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, chat_name, false, true);
  fpv_tg_send_notification(service, text, reply_markup, "2", NULL, 0, false, false);
  fpv_free(reply_markup);
  fpv_free(text);
}

void fpv_telegram_service_notify_command(
    fpv_telegram_service_t* service,
    uint64_t chat_id,
    const char* chat_name,
    const char* username,
    const char* command,
    const char* text) {
  if (!service || !command) {
    return;
  }
  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char* message_text = NULL;
  if (text && text[0]) {
    char* escaped_text = fpv_tg_escape_html(text);
    message_text = escaped_text ? escaped_text : fpv_strdup("");
  } else {
    char* escaped_cmd = fpv_tg_escape_html(command);
    char buffer[512];
    snprintf(
        buffer,
        sizeof(buffer),
        "\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x92\xBB \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C <b><i>%s</i></b> \xD0\xB2\xD0\xB2\xD0\xB5\xD0\xBB \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x83 <code>%s</code>.",
        escaped_user ? escaped_user : "",
        escaped_cmd ? escaped_cmd : "");
    fpv_free(escaped_cmd);
    message_text = fpv_strdup(buffer);
  }
  fpv_free(escaped_user);
  if (!message_text) {
    return;
  }
  fpv_tg_send_notification(service, message_text, NULL, "3", NULL, 0, false, false);
  fpv_free(message_text);
}

void fpv_telegram_service_notify_new_order(
    fpv_telegram_service_t* service,
    const char* order_title,
    const char* order_id,
    const char* buyer_username,
    double price,
    const char* price_text,
    uint64_t chat_id,
    const char* delivery_info) {
  if (!service || !order_id || !buyer_username) {
    return;
  }
  char price_buf[32];
  if (price_text && price_text[0]) {
    snprintf(price_buf, sizeof(price_buf), "%s", price_text);
  } else {
    snprintf(price_buf, sizeof(price_buf), "%.2f", price);
  }
  char* escaped_title = fpv_tg_escape_html(order_title ? order_title : "");
  char* escaped_user = fpv_tg_escape_html(buyer_username);
  char* escaped_info = fpv_tg_escape_html(delivery_info ? delivery_info : "");
  char* formatted = fpv_tg_loc_format(
      service,
      "ntfc_new_order",
      (const char*[]){
          escaped_title ? escaped_title : "",
          escaped_user ? escaped_user : "",
          price_buf,
          order_id,
          escaped_info ? escaped_info : ""},
      5);
  char* text = formatted ? fpv_strdup(formatted) : NULL;
  fpv_free(formatted);
  fpv_free(escaped_title);
  fpv_free(escaped_user);
  fpv_free(escaped_info);
  if (!text) {
    return;
  }
  char* reply_markup =
      fpv_tg_build_order_keyboard(service, order_id, buyer_username, chat_id, false, false);
  fpv_tg_send_notification(service, text, reply_markup, "4", NULL, 0, false, false);
  fpv_free(reply_markup);
  fpv_free(text);
}

void fpv_telegram_service_notify_order_confirmed(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    uint64_t chat_id) {
  if (!service || !order_id || !buyer_username) {
    return;
  }
  char* escaped_user = fpv_tg_escape_html(buyer_username);
  char buffer[512];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\xAA\x99 \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C <a href=\"https://funpay.com/chat/?node=%" PRIu64 "\">%s</a> "
      "\xD0\xBF\xD0\xBE\xD0\xB4\xD1\x82\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB4\xD0\xB8\xD0\xBB \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0 <code>%s</code>.",
      chat_id,
      escaped_user ? escaped_user : "",
      order_id);
  fpv_free(escaped_user);
  fpv_tg_send_notification(service, buffer, NULL, "5", NULL, 0, false, false);
}

void fpv_telegram_service_notify_review(
    fpv_telegram_service_t* service,
    const char* order_id,
    int stars,
    const char* review_text,
    const char* reply_text,
    uint64_t chat_id,
    const char* buyer_username) {
  if (!service || !order_id || stars <= 0) {
    return;
  }
  char star_buf[16];
  if (stars > 5) {
    stars = 5;
  }
  memset(star_buf, 0, sizeof(star_buf));
  for (int i = 0; i < stars && i < 5; i++) {
    strcat(star_buf, "\xE2\xAD\x90");
  }
  char* escaped_review = fpv_tg_escape_html(review_text ? review_text : "");
  char* escaped_reply = reply_text ? fpv_tg_escape_html(reply_text) : NULL;
  char buffer[2048];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x94\xAE \xD0\x92\xD1\x8B \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD0\xBB\xD0\xB8 %s \xD0\xB7\xD0\xB0 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7 <code>%s</code>!\n\n"
      "\xF0\x9F\x92\xAC<b>\xD0\x9E\xD1\x82\xD0\xB7\xD1\x8B\xD0\xB2:</b>\n<code>%s</code>",
      star_buf,
      order_id,
      escaped_review ? escaped_review : "");
  fpv_free(escaped_review);
  if (escaped_reply) {
    char* merged = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_tg_buffer_append(&merged, &length, &capacity, buffer, strlen(buffer));
    fpv_tg_buffer_append(&merged, &length, &capacity,
                         "\n\n\xF0\x9F\x97\xA8\xEF\xB8\x8F<b>\xD0\x9E\xD1\x82\xD0\xB2\xD0\xB5\xD1\x82:</b>\n<code>",
                         45);
    fpv_tg_buffer_append(&merged, &length, &capacity,
                         escaped_reply, strlen(escaped_reply));
    fpv_tg_buffer_append(&merged, &length, &capacity, "</code>", 7);
    fpv_tg_send_notification(service, merged, NULL, "5r", NULL, 0, false, false);
    fpv_free(merged);
  } else {
    fpv_tg_send_notification(service, buffer, NULL, "5r", NULL, 0, false, false);
  }
  fpv_free(escaped_reply);
}

void fpv_telegram_service_notify_lots_activated(
    fpv_telegram_service_t* service,
    const char* lot_list) {
  if (!service || !lot_list) {
    return;
  }
  char* escaped = fpv_tg_escape_html(lot_list);
  char buffer[1024];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x9F\xA2 <b>\xD0\x90\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBB \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B:</b>\n\n<code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "6", NULL, 0, false, false);
}

void fpv_telegram_service_notify_lots_deactivated(
    fpv_telegram_service_t* service,
    const char* lot_list) {
  if (!service || !lot_list) {
    return;
  }
  char* escaped = fpv_tg_escape_html(lot_list);
  char buffer[1024];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x94\xB4 <b>\xD0\x94\xD0\xB5\xD0\xB0\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBB \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B:</b>\n\n<code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "7", NULL, 0, false, false);
}

void fpv_telegram_service_notify_lots_raised(
    fpv_telegram_service_t* service,
    const char* category_name) {
  if (!service || !category_name) {
    return;
  }
  char* escaped = fpv_tg_escape_html(category_name);
  char buffer[512];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xE2\xA4\xB4\xEF\xB8\x8F<b><i>\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F\xD0\xBB \xD0\xB2\xD1\x81\xD0\xB5 \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B \xD0\xBA\xD0\xB0\xD1\x82\xD0\xB5\xD0\xB3\xD0\xBE\xD1\x80\xD0\xB8\xD0\xB8</i></b> <code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "9", NULL, 0, false, false);
}

char* fpv_telegram_service_take_delivery_test(
    fpv_telegram_service_t* service,
    const char* key) {
  if (!service || !key || !key[0]) {
    return NULL;
  }
  fpv_mutex_lock(&service->mutex);
  for (size_t i = 0; i < service->delivery_test_count; i++) {
    fpv_tg_delivery_test_t* entry = &service->delivery_tests[i];
    if (entry->key && strcmp(entry->key, key) == 0) {
      char* lot_name =
          entry->lot_name ? fpv_strdup(entry->lot_name) : NULL;
      fpv_free(entry->key);
      fpv_free(entry->lot_name);
      service->delivery_tests[i] =
          service->delivery_tests[service->delivery_test_count - 1];
      service->delivery_test_count--;
      fpv_mutex_unlock(&service->mutex);
      return lot_name;
    }
  }
  fpv_mutex_unlock(&service->mutex);
  return NULL;
}

static bool fpv_tg_is_private_chat(const fpv_tg_message_t* message) {
  return message && message->chat_type &&
      strcmp(message->chat_type, "private") == 0;
}

static char* fpv_tg_trim_copy(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  const char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  size_t len = (size_t)(end - start);
  char* copy = (char*)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, start, len);
  copy[len] = '\0';
  return copy;
}

static char* fpv_tg_trim_inplace(char* text) {
  if (!text) {
    return NULL;
  }
  char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  size_t len = (size_t)(end - start);
  if (start != text) {
    memmove(text, start, len);
  }
  text[len] = '\0';
  return text;
}

static bool fpv_tg_is_simple_tag(const char* text) {
  if (!text) {
    return false;
  }
  size_t len = strlen(text);
  if (len < 3 || text[0] != '[' || text[len - 1] != ']') {
    return false;
  }
  for (size_t i = 1; i + 1 < len; i++) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
      return false;
    }
  }
  return true;
}

static bool fpv_tg_ar_command_exists(fpv_ini_t* ini, const char* command) {
  if (!ini || !command) {
    return false;
  }
  char* needle = fpv_strdup(command);
  if (!needle) {
    return false;
  }
  if (!fpv_tg_trim_lower(needle)) {
    fpv_free(needle);
    return false;
  }
  bool found = false;
  size_t sections = fpv_ini_section_count(ini);
  for (size_t i = 0; i < sections && !found; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (!section) {
      continue;
    }
    char* section_copy = fpv_strdup(section);
    if (!section_copy) {
      continue;
    }
    char* token = strtok(section_copy, "|");
    while (token) {
      char* trimmed = fpv_tg_trim_copy(token);
      if (trimmed) {
        fpv_tg_trim_lower(trimmed);
        if (trimmed[0] && strcmp(trimmed, needle) == 0) {
          found = true;
          fpv_free(trimmed);
          break;
        }
        fpv_free(trimmed);
      }
      token = strtok(NULL, "|");
    }
    fpv_free(section_copy);
  }
  fpv_free(needle);
  return found;
}

static size_t fpv_tg_split_tokens(char* value, char** tokens, size_t max_tokens) {
  if (!value || !tokens || max_tokens == 0) {
    return 0;
  }
  size_t count = 0;
  char* ptr = value;
  while (count < max_tokens) {
    tokens[count++] = ptr;
    char* sep = strchr(ptr, ':');
    if (!sep) {
      break;
    }
    *sep = '\0';
    ptr = sep + 1;
  }
  return count;
}

static bool fpv_tg_parse_size_value(const char* text, size_t* out) {
  if (out) {
    *out = 0;
  }
  if (!text || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  if (value > SIZE_MAX) {
    return false;
  }
  if (out) {
    *out = (size_t)value;
  }
  return true;
}

static bool fpv_tg_parse_uint64_value(const char* text, uint64_t* out) {
  if (out) {
    *out = 0;
  }
  if (!text || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  if (out) {
    *out = (uint64_t)value;
  }
  return true;
}

static bool fpv_tg_parse_command(
    const char* text,
    char* command,
    size_t command_size,
    const char** out_args) {
  if (out_args) {
    *out_args = NULL;
  }
  if (!text || text[0] != '/' || !command || command_size == 0) {
    return false;
  }
  const char* ptr = text + 1;
  size_t idx = 0;
  while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '@') {
    if (idx + 1 < command_size) {
      command[idx++] = *ptr;
    }
    ptr++;
  }
  command[idx] = '\0';
  while (*ptr && *ptr != ' ') {
    ptr++;
  }
  if (*ptr == ' ') {
    ptr++;
    while (*ptr && isspace((unsigned char)*ptr)) {
      ptr++;
    }
    if (out_args) {
      *out_args = ptr;
    }
  }
  return idx > 0;
}

static char* fpv_tg_replace_username(const char* text, const char* username) {
  const char* needle = "$username";
  const char* repl = username ? username : "";
  size_t needle_len = strlen(needle);
  size_t repl_len = strlen(repl);
  size_t count = 0;
  const char* ptr = text ? text : "";
  while ((ptr = strstr(ptr, needle)) != NULL) {
    count++;
    ptr += needle_len;
  }
  if (count == 0) {
    return fpv_strdup(text ? text : "");
  }
  size_t base_len = strlen(text ? text : "");
  size_t total_len = base_len + count * (repl_len - needle_len);
  char* out = (char*)malloc(total_len + 1);
  if (!out) {
    return NULL;
  }
  const char* src = text ? text : "";
  char* dst = out;
  while ((ptr = strstr(src, needle)) != NULL) {
    size_t chunk = (size_t)(ptr - src);
    memcpy(dst, src, chunk);
    dst += chunk;
    memcpy(dst, repl, repl_len);
    dst += repl_len;
    src = ptr + needle_len;
  }
  size_t tail = strlen(src);
  memcpy(dst, src, tail);
  dst += tail;
  *dst = '\0';
  return out;
}

static bool fpv_tg_string_list_contains(
    const fpv_string_list_t* list,
    const char* value) {
  if (!list || !value) {
    return false;
  }
  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i] && strcmp(list->items[i], value) == 0) {
      return true;
    }
  }
  return false;
}

static bool fpv_tg_string_list_add(
    fpv_string_list_t* list,
    const char* value) {
  if (!list || !value) {
    return false;
  }
  char** grown = (char**)realloc(list->items, (list->count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  list->items = grown;
  list->items[list->count] = fpv_strdup(value);
  if (!list->items[list->count]) {
    return false;
  }
  list->count++;
  return true;
}

static bool fpv_tg_string_list_remove(
    fpv_string_list_t* list,
    size_t index) {
  if (!list || index >= list->count) {
    return false;
  }
  fpv_free(list->items[index]);
  for (size_t i = index + 1; i < list->count; i++) {
    list->items[i - 1] = list->items[i];
  }
  list->count--;
  if (list->count == 0) {
    fpv_free(list->items);
    list->items = NULL;
    return true;
  }
  char** resized = (char**)realloc(list->items, list->count * sizeof(*resized));
  if (resized) {
    list->items = resized;
  }
  return true;
}

static char* fpv_tg_blacklist_path(const fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return NULL;
  }
  return fpv_path_join(service->storage.cache_dir, "blacklist.json");
}

static fpv_result_t fpv_tg_load_blacklist(
    const fpv_telegram_service_t* service,
    fpv_string_list_t* out_list) {
  if (!service || !out_list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_tg_blacklist_path(service);
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_load_strings(path, out_list);
  fpv_free(path);
  return result;
}

static fpv_result_t fpv_tg_save_blacklist(
    const fpv_telegram_service_t* service,
    const fpv_string_list_t* list) {
  if (!service || !list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_tg_blacklist_path(service);
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_save_strings(path, list);
  fpv_free(path);
  return result;
}

static bool fpv_tg_is_log_file(const char* name) {
  return name && fpv_tg_has_suffix(name, ".log");
}

static bool fpv_tg_get_file_mtime(const char* path, uint64_t* out_time) {
  if (out_time) {
    *out_time = 0;
  }
  if (!path) {
    return false;
  }
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA info;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) {
    return false;
  }
  ULARGE_INTEGER value;
  value.LowPart = info.ftLastWriteTime.dwLowDateTime;
  value.HighPart = info.ftLastWriteTime.dwHighDateTime;
  if (out_time) {
    *out_time = value.QuadPart;
  }
  return true;
#else
  struct stat info;
  if (stat(path, &info) != 0) {
    return false;
  }
  if (out_time) {
    *out_time = (uint64_t)info.st_mtime;
  }
  return true;
#endif
}

static char* fpv_tg_find_latest_log(const fpv_telegram_service_t* service) {
  if (!service || !service->storage.logs_dir) {
    return NULL;
  }
  char* best_path = NULL;
  uint64_t best_time = 0;
#if defined(_WIN32)
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", service->storage.logs_dir);
  WIN32_FIND_DATAA data;
  HANDLE handle = FindFirstFileA(pattern, &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return NULL;
  }
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    if (!fpv_tg_is_log_file(data.cFileName)) {
      continue;
    }
    char* full = fpv_path_join(service->storage.logs_dir, data.cFileName);
    if (!full) {
      continue;
    }
    uint64_t modified = 0;
    fpv_tg_get_file_mtime(full, &modified);
    if (!best_path || modified >= best_time) {
      fpv_free(best_path);
      best_path = full;
      best_time = modified;
    } else {
      fpv_free(full);
    }
  } while (FindNextFileA(handle, &data));
  FindClose(handle);
#else
  DIR* dir = opendir(service->storage.logs_dir);
  if (!dir) {
    return NULL;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!fpv_tg_is_log_file(entry->d_name)) {
      continue;
    }
    char* full = fpv_path_join(service->storage.logs_dir, entry->d_name);
    if (!full) {
      continue;
    }
    uint64_t modified = 0;
    fpv_tg_get_file_mtime(full, &modified);
    if (!best_path || modified >= best_time) {
      fpv_free(best_path);
      best_path = full;
      best_time = modified;
    } else {
      fpv_free(full);
    }
  }
  closedir(dir);
#endif
  return best_path;
}

static char* fpv_tg_format_toggle_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled) {
  const char* icon = fpv_tg_icon_toggle(enabled);
  return fpv_tg_loc_format(service, key, (const char*[]){icon}, 1);
}

static char* fpv_tg_format_bell_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled) {
  const char* icon = fpv_tg_icon_bell(enabled);
  return fpv_tg_loc_format(service, key, (const char*[]){icon}, 1);
}

static char* fpv_tg_build_old_keyboard(void) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(false, true);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(
      kb, "\xF0\x9F\x93\x8B \xD0\x9B\xD0\xBE\xD0\xB3\xD0\xB8 \xF0\x9F\x93\x8B",
      NULL, NULL);
  fpv_tg_keyboard_add_button(
      kb, "\xE2\x9A\x99\xEF\xB8\x8F \xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8 "
          "\xE2\x9A\x99\xEF\xB8\x8F",
      NULL, NULL);
  fpv_tg_keyboard_add_button(
      kb, "\xF0\x9F\x93\x88 \xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xB0 \xF0\x9F\x93\x88",
      NULL, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(
      kb, "\xF0\x9F\x94\x84 \xD0\x9F\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA "
          "\xF0\x9F\x94\x84",
      NULL, NULL);
  fpv_tg_keyboard_add_button(
      kb, "\xE2\x9D\x8C \xD0\x97\xD0\xB0\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C \xE2\x9D\x8C",
      NULL, NULL);
  fpv_tg_keyboard_add_button(
      kb, "\xF0\x9F\x94\x8C \xD0\x9E\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 "
          "\xF0\x9F\x94\x8C",
      NULL, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_settings_sections(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_core);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_global"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_notify);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_notifications"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_reply);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autoresponse"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_delivery);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autodelivery"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_settings_group(
    fpv_telegram_service_t* service,
    const char* group) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !group) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[64];
  if (strcmp(group, fpv_tg_menu_core) == 0) {
    const char* lang = service && service->language ? service->language : "ru";
    const char* ru_cb = strcmp(lang, "ru") == 0 ? fpv_tg_cbt_empty : NULL;
    const char* eng_cb = strcmp(lang, "eng") == 0 ? fpv_tg_cbt_empty : NULL;
    char ru_buf[16];
    char eng_buf[16];
    if (!ru_cb) {
      snprintf(ru_buf, sizeof(ru_buf), "%s:ru", fpv_tg_cbt_lang);
      ru_cb = ru_buf;
    }
    if (!eng_cb) {
      snprintf(eng_buf, sizeof(eng_buf), "%s:eng", fpv_tg_cbt_lang);
      eng_cb = eng_buf;
    }
    fpv_tg_keyboard_add_button(
        kb, "\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA", ru_cb, NULL);
    fpv_tg_keyboard_add_button(
        kb, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", eng_cb, NULL);
    fpv_tg_keyboard_row_end(kb);

    snprintf(cb_buf, sizeof(cb_buf), "%s:main", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(
        kb, fpv_tg_loc(service, "mm_global_settings"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:configs", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_configs"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:bl", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_blacklist"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else if (strcmp(group, fpv_tg_menu_notify) == 0) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:tg", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(
        kb, fpv_tg_loc(service, "mm_notifications_settings"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:mv", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_new_msg_view"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else if (strcmp(group, fpv_tg_menu_reply) == 0) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:ar", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autoresponse"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_template_list);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_templates"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:gr", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_greetings"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:rr", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_review_reply"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else if (strcmp(group, fpv_tg_menu_delivery) == 0) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:ad", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(
        kb, fpv_tg_loc(service, "mm_autodelivery_settings"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:oc", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_order_confirm"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }

  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_main_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char* label = fpv_tg_format_toggle_label(service, "gs_autoraise", settings->auto_raise);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:autoRaise", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autoresponse", settings->auto_response);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:autoResponse", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_toggle_label(service, "gs_autodelivery", settings->auto_delivery);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:autoDelivery", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_nultidelivery", settings->multi_delivery);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:multiDelivery", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_toggle_label(service, "gs_autorestore", settings->auto_restore);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:autoRestore", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autodisable", settings->auto_disable);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:autoDisable", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_toggle_label(service, "gs_old_msg_mode", settings->old_msg_mode);
  snprintf(cb_buf, sizeof(cb_buf), "%s:FunPay:oldMsgGetMode", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_add_button(kb, "?", fpv_tg_cbt_old_help, NULL);
  fpv_tg_keyboard_row_end(kb);

  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_new_message_view_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char* label = fpv_tg_format_toggle_label(service, "mv_incl_my_msg", settings->include_my_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:includeMyMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "mv_incl_fp_msg", settings->include_fp_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:includeFPMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "mv_incl_bot_msg", settings->include_bot_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:includeBotMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "mv_only_my_msg", settings->notify_only_my_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:notifyOnlyMyMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "mv_only_fp_msg", settings->notify_only_fp_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:notifyOnlyFPMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "mv_only_bot_msg", settings->notify_only_bot_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:NewMessageView:notifyOnlyBotMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_greeting_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char* label = fpv_tg_format_toggle_label(service, "gr_greetings", settings->greetings_send);
  snprintf(cb_buf, sizeof(cb_buf), "%s:Greetings:sendGreetings", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "gr_cache_init_chats", settings->greetings_cache_init_chats);
  snprintf(cb_buf, sizeof(cb_buf), "%s:Greetings:cacheInitChats", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "gr_ignore_sys_msgs", settings->greetings_ignore_system_messages);
  snprintf(cb_buf, sizeof(cb_buf), "%s:Greetings:ignoreSystemMessages", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gr_edit_message"),
                             fpv_tg_cbt_edit_greetings, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_order_confirm_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char* label = fpv_tg_format_toggle_label(service, "oc_send_reply",
                                           settings->order_confirm_send_reply);
  snprintf(cb_buf, sizeof(cb_buf), "%s:OrderConfirm:sendReply", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "oc_edit_message"),
                             fpv_tg_cbt_edit_order_confirm, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_review_reply_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char star_buf[16];
  for (int i = 1; i <= 5; i++) {
    memset(star_buf, 0, sizeof(star_buf));
    for (int j = 0; j < i && j < 5; j++) {
      strcat(star_buf, "\xE2\xAD\x90");
    }
    snprintf(cb_buf, sizeof(cb_buf), "%s:%d", fpv_tg_cbt_send_review_reply, i);
    fpv_tg_keyboard_add_button(kb, star_buf, cb_buf, NULL);
    const char* icon = fpv_tg_icon_toggle(settings->review_reply_enabled[i - 1]);
    snprintf(cb_buf, sizeof(cb_buf), "%s:ReviewReply:star%dReply", fpv_tg_cbt_switch, i);
    fpv_tg_keyboard_add_button(kb, icon, cb_buf, NULL);
    snprintf(cb_buf, sizeof(cb_buf), "%s:%d", fpv_tg_cbt_edit_review_reply, i);
    fpv_tg_keyboard_add_button(kb, "\xE2\x9C\x8F\xEF\xB8\x8F", cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_notifications_settings(
    fpv_telegram_service_t* service,
    uint64_t chat_id) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[128];
  char* label = NULL;
  label = fpv_tg_format_bell_label(
      service, "ns_new_msg",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "2"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":2", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_bell_label(
      service, "ns_cmd",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "3"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":3", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_bell_label(
      service, "ns_new_order",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "4"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":4", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_bell_label(
      service, "ns_order_confirmed",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "5"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":5", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_bell_label(
      service, "ns_lot_activate",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "6"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":6", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_bell_label(
      service, "ns_lot_deactivate",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "7"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":7", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_bell_label(
      service, "ns_delivery",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "8"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":8", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  label = fpv_tg_format_bell_label(
      service, "ns_raise",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "9"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":9", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_bell_label(
      service, "ns_new_review",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "5r"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":5r", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  label = fpv_tg_format_bell_label(
      service, "ns_bot_start",
      fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "1"));
  snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":1", fpv_tg_cbt_switch_tg, chat_id);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_blacklist_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  char cb_buf[128];
  char* label = fpv_tg_format_toggle_label(service, "bl_autodelivery", settings->block_delivery);
  snprintf(cb_buf, sizeof(cb_buf), "%s:BlockList:blockDelivery", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "bl_autoresponse", settings->block_response);
  snprintf(cb_buf, sizeof(cb_buf), "%s:BlockList:blockResponse", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "bl_new_msg_notifications",
                                     settings->block_new_message_notification);
  snprintf(cb_buf, sizeof(cb_buf), "%s:BlockList:blockNewMessageNotification", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "bl_new_order_notifications",
                                     settings->block_new_order_notification);
  snprintf(cb_buf, sizeof(cb_buf), "%s:BlockList:blockNewOrderNotification", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  label = fpv_tg_format_toggle_label(service, "bl_command_notifications",
                                     settings->block_command_notification);
  snprintf(cb_buf, sizeof(cb_buf), "%s:BlockList:blockCommandNotification", fpv_tg_cbt_switch);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_auto_response_settings(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_cmd_list);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_edit_commands"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_add_command"), fpv_tg_cbt_add_cmd, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_auto_delivery_settings(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_ad_lots);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_edit_autodelivery"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_fp_lots);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_add_autodelivery"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_products_list);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_edit_goods_file"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_upload_goods_file"),
                             fpv_tg_cbt_upload_products_file, NULL);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_create_goods_file"),
                             fpv_tg_cbt_create_products_file, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_configs_uploader(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:main", fpv_tg_cbt_download_cfg);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_download_main"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:autoResponse", fpv_tg_cbt_download_cfg);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_download_ar"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:autoDelivery", fpv_tg_cbt_download_cfg);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_download_ad"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_upload_main"),
                             fpv_tg_cb_upload_main_config, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_upload_ar"),
                             fpv_tg_cb_upload_auto_response_config, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "cfg_upload_ad"),
                             fpv_tg_cb_upload_auto_delivery_config, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_profile_keyboard(
    fpv_telegram_service_t* service,
    bool advanced) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  if (advanced) {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"),
                               fpv_tg_cb_update_adv_profile, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"),
                               fpv_tg_cb_update_profile, NULL);
  } else {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"),
                               fpv_tg_cb_update_profile, NULL);
    fpv_tg_keyboard_add_button(kb, "\xE2\x96\xB6\xEF\xB8\x8F \xD0\x95\xD1\x89\xD0\xB5",
                               fpv_tg_cb_update_adv_profile, NULL);
  }
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_command_info_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index) {
  if (!service || !ini) {
    return NULL;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    return NULL;
  }
  const char* response = fpv_ini_get(ini, command, "response");
  const char* notification_text = fpv_ini_get(ini, command, "notificationText");
  if (!notification_text || !notification_text[0]) {
    notification_text = "\xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C $username \xD0\xB2\xD0\xB2\xD0\xB5\xD0\xBB \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x83 $message_text.";
  }
  char* escaped_command = fpv_tg_escape_html(command);
  char* escaped_response = fpv_tg_escape_html(response ? response : "");
  char* escaped_notification = fpv_tg_escape_html(notification_text);
  char time_buf[32];
  fpv_tg_format_time_hms(fpv_time_now_ms(), time_buf, sizeof(time_buf));
  const char* response_label = fpv_tg_loc(service, "ar_response_text");
  const char* notification_label = fpv_tg_loc(service, "ar_notification_text");
  const char* last_update = fpv_tg_loc(service, "gl_last_update");
  char buffer[4096];
  snprintf(
      buffer,
      sizeof(buffer),
      "<b>[%s]</b>\n\n"
      "<b><i>%s:</i></b> <code>%s</code>\n"
      "<b><i>%s:</i></b> <code>%s</code>\n"
      "<i>%s:</i>  <code>%s</code>",
      escaped_command ? escaped_command : "",
      response_label ? response_label : "",
      escaped_response ? escaped_response : "",
      notification_label ? notification_label : "",
      escaped_notification ? escaped_notification : "",
      last_update ? last_update : "",
      time_buf);
  fpv_free(escaped_command);
  fpv_free(escaped_response);
  fpv_free(escaped_notification);
  return fpv_strdup(buffer);
}

static char* fpv_tg_build_commands_list_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t offset) {
  if (!service || !ini) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = fpv_ini_section_count(ini);
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_cmd_page; i++) {
    const char* name = fpv_ini_section_name(ini, i);
    if (!name) {
      continue;
    }
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_cmd, i, offset);
    fpv_tg_keyboard_add_button(kb, name, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb,
      offset,
      fpv_tg_cmd_page,
      page_count,
      total,
      fpv_tg_cbt_cmd_list,
      NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:ar", fpv_tg_cbt_category);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_to_ar"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_to_mm"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_edit_command_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index,
    size_t offset) {
  if (!service || !ini) {
    return NULL;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    return NULL;
  }
  const char* notification =
      fpv_ini_get(ini, command, "telegramNotification");
  bool enabled = false;
  if (notification && notification[0]) {
    fpv_tg_parse_bool(notification, &enabled);
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_cmd_response, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_edit_response"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_cmd_notification, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ar_edit_notification"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  char* label = fpv_tg_format_bell_label(service, "ar_notification", enabled);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_switch_cmd_notification, command_index, offset);
  fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_del_cmd, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_delete"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_cmd_list, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_cmd, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_cmd_refresh_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_cmd_list);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_refresh_keyboard(
    fpv_telegram_service_t* service,
    const char* callback) {
  if (!callback) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), callback, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_ar_add_error_keyboard(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:ar", fpv_tg_cbt_category);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  fpv_tg_keyboard_add_button(
      kb, fpv_tg_loc(service, "ar_add_another"), fpv_tg_cbt_add_cmd, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_ar_add_success_keyboard(
    fpv_telegram_service_t* service,
    size_t command_index,
    size_t offset) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char back_cb[32];
  char edit_cb[64];
  snprintf(back_cb, sizeof(back_cb), "%s:ar", fpv_tg_cbt_category);
  snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
           fpv_tg_cbt_edit_cmd, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
  fpv_tg_keyboard_add_button(
      kb, fpv_tg_loc(service, "ar_add_more"), fpv_tg_cbt_add_cmd, NULL);
  fpv_tg_keyboard_add_button(
      kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_ar_edit_done_keyboard(
    fpv_telegram_service_t* service,
    const char* edit_callback,
    size_t command_index,
    size_t offset) {
  if (!edit_callback) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char back_cb[64];
  char edit_cb[64];
  snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
           fpv_tg_cbt_edit_cmd, command_index, offset);
  snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
           edit_callback, command_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_edit"), edit_cb, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static void fpv_tg_reload_auto_response(fpv_telegram_service_t* service) {
  if (!service || !service->features) {
    return;
  }
  char* path = fpv_tg_config_path(service, "auto_response.cfg");
  if (!path) {
    return;
  }
  fpv_features_reload_auto_response(service->features, path);
  fpv_free(path);
}

static void fpv_tg_reload_auto_delivery(fpv_telegram_service_t* service) {
  if (!service || !service->features) {
    return;
  }
  char* path = fpv_tg_config_path(service, "auto_delivery.cfg");
  if (!path) {
    return;
  }
  fpv_features_reload_auto_delivery(
      service->features,
      path,
      service->storage.products_dir);
  fpv_free(path);
}

static char* fpv_tg_build_products_files_list(
    fpv_telegram_service_t* service,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = 0;
  char** files = fpv_tg_list_products_files(service, &total);
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_products_page; i++) {
    char* file_name = files[i];
    if (!file_name) {
      continue;
    }
    char* path = fpv_tg_products_path(service, file_name);
    size_t count = fpv_tg_count_products(path);
    fpv_free(path);
    char label[512];
    snprintf(label, sizeof(label), "%zu %s, %s",
             count, fpv_tg_loc(service, "gl_pcs"), file_name);
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_products_file, i, offset);
    fpv_tg_keyboard_add_button(kb, label, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_products_page, page_count, total, fpv_tg_cbt_products_list, NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:ad", fpv_tg_cbt_category);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_ad"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_mm"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_free_string_array(files, total);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_products_file_edit_keyboard(
    fpv_telegram_service_t* service,
    size_t file_index,
    size_t offset,
    bool confirm_delete) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[96];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu:%zu:0",
           fpv_tg_cbt_add_products, file_index, file_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_add_goods"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu",
           fpv_tg_cb_download_products_file, file_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_download"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  if (!confirm_delete) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu",
             fpv_tg_cb_delete_products_file, file_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_delete"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu",
             fpv_tg_cb_confirm_delete_products_file, file_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_yes"), cb_buf, NULL);
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu",
             fpv_tg_cbt_edit_products_file, file_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_no"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_products_list, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu",
           fpv_tg_cbt_edit_products_file, file_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_products_file_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    const char* file_name) {
  if (!service || !file_name) {
    return NULL;
  }
  size_t count = 0;
  char* path = fpv_tg_products_path(service, file_name);
  count = fpv_tg_count_products(path);
  fpv_free(path);

  char* uses = NULL;
  size_t uses_len = 0;
  size_t uses_cap = 0;
  if (ini) {
    size_t sections = fpv_ini_section_count(ini);
    for (size_t i = 0; i < sections; i++) {
      const char* section = fpv_ini_section_name(ini, i);
      const char* file_ref = fpv_ini_get(ini, section, "productsFileName");
      if (file_ref && strcmp(file_ref, file_name) == 0) {
        char* escaped = fpv_tg_escape_html(section);
        fpv_tg_buffer_append(&uses, &uses_len, &uses_cap, "<code>", 6);
        fpv_tg_buffer_append(&uses, &uses_len, &uses_cap,
                             escaped ? escaped : "",
                             escaped ? strlen(escaped) : 0);
        fpv_tg_buffer_append(&uses, &uses_len, &uses_cap, "</code>\n", 8);
        fpv_free(escaped);
      }
    }
  }
  if (!uses) {
    uses = fpv_strdup("");
  }
  char time_buf[32];
  fpv_tg_format_time_hms(fpv_time_now_ms(), time_buf, sizeof(time_buf));
  char buffer[4096];
  snprintf(
      buffer,
      sizeof(buffer),
      "<b><u>%s</u></b>\n\n"
      "<b><i>%s:</i></b>  <code>%zu</code>\n"
      "<b><i>%s:</i></b>\n%s"
      "<i>%s:</i>  <code>%s</code>",
      file_name,
      fpv_tg_loc(service, "gf_amount"),
      count,
      fpv_tg_loc(service, "gf_uses"),
      uses,
      fpv_tg_loc(service, "gl_last_update"),
      time_buf);
  fpv_free(uses);
  return fpv_strdup(buffer);
}

static char* fpv_tg_build_lots_list(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t offset) {
  if (!service || !ini) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = fpv_ini_section_count(ini);
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_ad_page; i++) {
    const char* name = fpv_ini_section_name(ini, i);
    if (!name) {
      continue;
    }
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_ad_lot, i, offset);
    fpv_tg_keyboard_add_button(kb, name, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_ad_page, page_count, total, fpv_tg_cbt_ad_lots, NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:ad", fpv_tg_cbt_category);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_ad"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_mm"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_funpay_lots_list(
    fpv_telegram_service_t* service,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = service->profile_lot_count;
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_fp_lot_page; i++) {
    fpv_lot_t* lot = service->profile_lots[i];
    if (!lot || !lot->title) {
      continue;
    }
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_add_ad_lot, i, offset);
    fpv_tg_keyboard_add_button(kb, lot->title, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_fp_lot_page, page_count, total, fpv_tg_cbt_fp_lots, NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_add_ad_lot_manual, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "fl_manual"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cb_update_funpay_lots, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:ad", fpv_tg_cbt_category);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_ad"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_to_mm"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_lot_info_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t lot_index) {
  if (!service || !ini) {
    return NULL;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  if (!lot_name) {
    return NULL;
  }
  const char* response = fpv_ini_get(ini, lot_name, "response");
  const char* file_name = fpv_ini_get(ini, lot_name, "productsFileName");
  const char* file_path = NULL;
  char file_path_buf[256];
  char goods_buf[64];
  if (!file_name || !file_name[0]) {
    file_path = "<b><u>\xD0\xBD\xD0\xB5 \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD1\x8F\xD0\xB7\xD0\xB0\xD0\xBD.</u></b>";
    snprintf(goods_buf, sizeof(goods_buf), "<code>\xE2\x88\x9E</code>");
  } else {
    char* full = fpv_tg_products_path(service, file_name);
    if (full && !fpv_fs_exists(full)) {
      fpv_tg_write_file(full, "");
    }
    size_t count = fpv_tg_count_products(full);
    fpv_free(full);
    snprintf(file_path_buf, sizeof(file_path_buf),
             "<code>storage/products/%s</code>", file_name);
    file_path = file_path_buf;
    snprintf(goods_buf, sizeof(goods_buf), "<code>%zu</code>", count);
  }
  char* escaped_lot = fpv_tg_escape_html(lot_name);
  char* escaped_response = fpv_tg_escape_html(response ? response : "");
  char time_buf[32];
  fpv_tg_format_time_hms(fpv_time_now_ms(), time_buf, sizeof(time_buf));
  char buffer[4096];
  snprintf(
      buffer,
      sizeof(buffer),
      "<b>%s</b>\n\n"
      "<b><i>\xD0\xA2\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82 \xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB8:</i></b> <code>%s</code>\n"
      "<b><i>\xD0\x9A\xD0\xBE\xD0\xBB-\xD0\xB2\xD0\xBE \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80\xD0\xBE\xD0\xB2: </i></b> %s\n"
      "<b><i>\xD0\xA4\xD0\xB0\xD0\xB9\xD0\xBB \xD1\x81 \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB8: </i></b>%s\n"
      "<i>\xD0\x9E\xD0\xB1\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBE:</i>  <code>%s</code>",
      escaped_lot ? escaped_lot : "",
      escaped_response ? escaped_response : "",
      goods_buf,
      file_path ? file_path : "",
      time_buf);
  fpv_free(escaped_lot);
  fpv_free(escaped_response);
  return fpv_strdup(buffer);
}

static char* fpv_tg_build_edit_lot_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    const fpv_settings_t* settings,
    size_t lot_index,
    size_t offset) {
  if (!service || !ini || !settings) {
    return NULL;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  if (!lot_name) {
    return NULL;
  }
  const char* file_name = fpv_ini_get(ini, lot_name, "productsFileName");
  const char* disable = fpv_ini_get(ini, lot_name, "disable");
  const char* disable_multi = fpv_ini_get(ini, lot_name, "disableMultiDelivery");
  const char* disable_restore = fpv_ini_get(ini, lot_name, "disableAutoRestore");
  const char* disable_disable = fpv_ini_get(ini, lot_name, "disableAutoDisable");
  bool disabled = false;
  bool disabled_multi = false;
  bool disabled_restore = false;
  bool disabled_disable = false;
  fpv_tg_parse_bool(disable, &disabled);
  fpv_tg_parse_bool(disable_multi, &disabled_multi);
  fpv_tg_parse_bool(disable_restore, &disabled_restore);
  fpv_tg_parse_bool(disable_disable, &disabled_disable);

  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[128];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_lot_text, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_edit_delivery_text"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);

  if (!file_name || !file_name[0]) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_bind_products, lot_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_link_goods_file"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_bind_products, lot_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_link_goods_file"), cb_buf, NULL);
    size_t file_count = 0;
    char** files = fpv_tg_list_products_files(service, &file_count);
    size_t file_index = 0;
    bool found = false;
    for (size_t i = 0; i < file_count; i++) {
      if (files[i] && strcmp(files[i], file_name) == 0) {
        file_index = i;
        found = true;
        break;
      }
    }
    if (!found && file_name && file_name[0]) {
      char* full = fpv_tg_products_path(service, file_name);
      if (full && !fpv_fs_exists(full)) {
        fpv_tg_write_file(full, "");
      }
      fpv_free(full);
      fpv_tg_free_string_array(files, file_count);
      files = fpv_tg_list_products_files(service, &file_count);
      for (size_t i = 0; i < file_count; i++) {
        if (files[i] && strcmp(files[i], file_name) == 0) {
          file_index = i;
          found = true;
          break;
        }
      }
    }
    if (found) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu:%zu:1",
               fpv_tg_cbt_add_products, file_index, lot_index, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_add_goods"), cb_buf, NULL);
    }
    fpv_tg_keyboard_row_end(kb);
    fpv_tg_free_string_array(files, file_count);
  }

  const char* icon_delivery = fpv_tg_icon_lot_state(settings->auto_delivery, disabled);
  char* label = fpv_tg_loc_format(
      service, "ea_delivery", (const char*[]){icon_delivery}, 1);
  snprintf(cb_buf, sizeof(cb_buf), "%s:disable:%zu:%zu", fpv_tg_cb_switch_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, label,
                             settings->auto_delivery ? cb_buf : fpv_tg_cbt_param_disabled,
                             NULL);
  fpv_free(label);
  const char* icon_multi = fpv_tg_icon_lot_state(settings->multi_delivery, disabled_multi);
  label = fpv_tg_loc_format(
      service, "ea_multidelivery", (const char*[]){icon_multi}, 1);
  snprintf(cb_buf, sizeof(cb_buf), "%s:disableMultiDelivery:%zu:%zu", fpv_tg_cb_switch_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, label,
                             settings->multi_delivery ? cb_buf : fpv_tg_cbt_param_disabled,
                             NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  const char* icon_restore = fpv_tg_icon_lot_state(settings->auto_restore, disabled_restore);
  label = fpv_tg_loc_format(
      service, "ea_restore", (const char*[]){icon_restore}, 1);
  snprintf(cb_buf, sizeof(cb_buf), "%s:disableAutoRestore:%zu:%zu", fpv_tg_cb_switch_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, label,
                             settings->auto_restore ? cb_buf : fpv_tg_cbt_param_disabled,
                             NULL);
  fpv_free(label);
  const char* icon_disable = fpv_tg_icon_lot_state(settings->auto_disable, disabled_disable);
  label = fpv_tg_loc_format(
      service, "ea_deactivate", (const char*[]){icon_disable}, 1);
  snprintf(cb_buf, sizeof(cb_buf), "%s:disableAutoDisable:%zu:%zu", fpv_tg_cb_switch_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, label,
                             settings->auto_disable ? cb_buf : fpv_tg_cbt_param_disabled,
                             NULL);
  fpv_free(label);
  fpv_tg_keyboard_row_end(kb);

  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cb_test_auto_delivery, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_test"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_del_ad_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_delete"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);

  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_ad_lots, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_ad_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_templates_list(
    fpv_telegram_service_t* service,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = service->answer_templates.count;
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_template_page; i++) {
    const char* tmpl = service->answer_templates.items[i];
    if (!tmpl) {
      continue;
    }
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_template, i, offset);
    fpv_tg_keyboard_add_button(kb, tmpl, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_template_page, page_count, total, fpv_tg_cbt_template_list, NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_add_template, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_template_edit_keyboard(
    fpv_telegram_service_t* service,
    size_t template_index,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_del_template, template_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_delete"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_template_list, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static char* fpv_tg_build_templates_list_ans(
    fpv_telegram_service_t* service,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = service->answer_templates.count;
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  const char* extra_tail = extra ? extra : "";
  for (size_t i = offset; i < total && page_count < fpv_tg_template_page; i++) {
    const char* tmpl = service->answer_templates.items[i];
    if (!tmpl) {
      continue;
    }
    char* label = fpv_tg_replace_username(tmpl, username);
    char cb_buf[256];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%" PRIu64 ":%s:%d%s",
             fpv_tg_cbt_send_template, i, chat_id,
             username ? username : "", prev_page, extra_tail);
    fpv_tg_keyboard_add_button(kb, label ? label : "", cb_buf, NULL);
    fpv_free(label);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  char extra_buf[256];
  if (extra_tail && extra_tail[0]) {
    snprintf(extra_buf, sizeof(extra_buf), ":%" PRIu64 ":%s:%d%s",
             chat_id, username ? username : "", prev_page, extra_tail);
  } else {
    snprintf(extra_buf, sizeof(extra_buf), ":%" PRIu64 ":%s:%d",
             chat_id, username ? username : "", prev_page);
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_template_page, page_count, total,
      fpv_tg_cbt_template_list_ans, extra_buf);

  char cb_buf[256];
  if (prev_page == 0 || prev_page == 1) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":%s:%d%s",
             fpv_tg_cbt_back_to_reply, chat_id, username ? username : "",
             prev_page, extra_tail);
  } else {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":%s%s",
             fpv_tg_cbt_back_to_order, chat_id, username ? username : "",
             extra_tail);
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static bool fpv_tg_check_template_index(
    fpv_telegram_service_t* service,
    size_t template_index,
    int64_t chat_id,
    int message_id,
    bool edit_message) {
  if (!service) {
    return false;
  }
  if (template_index < service->answer_templates.count) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", template_index);
  char* text = fpv_tg_loc_format(
      service,
      "tmplt_not_found_err",
      (const char*[]){index_buf},
      1);
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_template_list);
  char* reply_markup = fpv_tg_build_refresh_keyboard(service, cb_buf);
  if (edit_message && message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        chat_id,
        message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(
        service,
        chat_id,
        text ? text : "",
        reply_markup,
        NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  return false;
}

static void fpv_tg_open_templates_list_ans(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_templates_list_ans(
      service, offset, chat_id, username, prev_page, extra);
  fpv_tg_edit_message_reply_markup(
      service,
      callback->chat_id,
      callback->message_id,
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_template_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  if (!fpv_tg_check_template_index(
          service,
          template_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* tmpl = service->answer_templates.items[template_index];
  char* escaped = fpv_tg_escape_html(tmpl ? tmpl : "");
  char* text = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append_str(&text, &length, &capacity, "<code>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, escaped ? escaped : "");
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</code>");
  if (!text) {
    text = fpv_strdup("");
  }
  char* reply_markup =
      fpv_tg_build_template_edit_keyboard(service, template_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_delete_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  if (!fpv_tg_check_template_index(
          service,
          template_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* tmpl = service->answer_templates.items[template_index];
  char* tmpl_copy = tmpl ? fpv_strdup(tmpl) : fpv_strdup("");
  fpv_tg_string_list_remove(&service->answer_templates, template_index);
  fpv_tg_save_answer_templates(service);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_tmplt_deleted",
      (const char*[]){uname, id_buf, tmpl_copy ? tmpl_copy : ""},
      3);
  fpv_free(tmpl_copy);
  char* reply_markup = fpv_tg_build_templates_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_tmplt"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_send_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra1,
    const char* extra2) {
  if (!service || !callback) {
    return;
  }
  if (template_index >= service->answer_templates.count) {
    char index_buf[32];
    snprintf(index_buf, sizeof(index_buf), "%zu", template_index);
    char* text = fpv_tg_loc_format(
        service,
        "tmplt_not_found_err",
        (const char*[]){index_buf},
        1);
    fpv_tg_send_message(service, callback->chat_id, text ? text : "", NULL, NULL);
    fpv_free(text);
    char* reply_markup = NULL;
    if (prev_page == 0 || prev_page == 1) {
      bool extend = false;
      fpv_tg_parse_bool(extra1, &extend);
      reply_markup = fpv_tg_build_reply_keyboard(
          service,
          chat_id,
          username,
          prev_page == 1,
          extend);
    } else if (prev_page == 2 && extra1 && extra1[0]) {
      bool no_refund = false;
      fpv_tg_parse_bool(extra2, &no_refund);
      reply_markup = fpv_tg_build_order_keyboard(
          service,
          extra1,
          username,
          chat_id,
          false,
          no_refund);
    }
    if (reply_markup) {
      fpv_tg_edit_message_reply_markup(
          service,
          callback->chat_id,
          callback->message_id,
          reply_markup);
      fpv_free(reply_markup);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  const char* tmpl = service->answer_templates.items[template_index];
  char* text = fpv_tg_replace_username(tmpl ? tmpl : "", username);
  fpv_result_t result = FPV_ERR_INVALID_ARGUMENT;
  if (service->features) {
    result = fpv_features_send_message(
        service->features,
        chat_id,
        username,
        text ? text : "",
        true);
  }
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
  char* reply_markup = fpv_tg_build_reply_keyboard(
      service,
      chat_id,
      username,
      true,
      true);
  if (result == FPV_OK) {
    char* escaped = fpv_tg_escape_html(text ? text : "");
    char* response = fpv_tg_loc_format(
        service,
        "tmplt_msg_sent",
        (const char*[]){chat_buf, username ? username : "", escaped ? escaped : ""},
        3);
    fpv_tg_send_message(
        service,
        callback->chat_id,
        response ? response : "",
        reply_markup,
        NULL);
    fpv_free(response);
    fpv_free(escaped);
  } else {
    char* response = fpv_tg_loc_format(
        service,
        "msg_sending_error",
        (const char*[]){chat_buf, username ? username : ""},
        2);
    fpv_tg_send_message(
        service,
        callback->chat_id,
        response ? response : "",
        reply_markup,
        NULL);
    fpv_free(response);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static fpv_funpay_account_t* fpv_tg_get_account(fpv_telegram_service_t* service);

static void fpv_tg_update_reply_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username,
    bool again,
    bool extend) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, username, again, extend);
  if (reply_markup) {
    fpv_tg_edit_message_reply_markup(
        service,
        callback->chat_id,
        callback->message_id,
        reply_markup);
    fpv_free(reply_markup);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_update_order_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username,
    bool confirmation,
    bool no_refund) {
  if (!service || !callback || !order_id || !order_id[0]) {
    return;
  }
  char* reply_markup = fpv_tg_build_order_keyboard(
      service, order_id, username, chat_id, confirmation, no_refund);
  if (reply_markup) {
    fpv_tg_edit_message_reply_markup(
        service,
        callback->chat_id,
        callback->message_id,
        reply_markup);
    fpv_free(reply_markup);
  }
}

static void fpv_tg_extend_chat_history(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username) {
  if (!service || !callback || chat_id == 0) {
    return;
  }
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_message_t** messages = NULL;
  size_t message_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_chat_history(
      account,
      chat_id,
      username,
      &messages,
      &message_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || message_count == 0) {
    if (messages) {
      for (size_t i = 0; i < message_count; i++) {
        fpv_message_destroy(messages[i]);
      }
      fpv_free(messages);
    }
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  uint64_t account_id = fpv_funpay_account_id(account);
  char* text = fpv_tg_build_messages_text(
      service,
      (const fpv_message_t* const*)messages,
      message_count,
      username,
      account_id,
      10);
  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, username, false, false);
  if (text) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text,
        reply_markup);
  } else {
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  for (size_t i = 0; i < message_count; i++) {
    fpv_message_destroy(messages[i]);
  }
  fpv_free(messages);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_confirm_refund(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username) {
  if (!service || !callback || !order_id || !order_id[0]) {
    if (service && callback) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
    }
    return;
  }
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  int status_id = 0;
  int attempts = 3;
  bool success = false;
  if (account) {
    while (attempts > 0) {
      fpv_funpay_error_t error;
      memset(&error, 0, sizeof(error));
      fpv_result_t result =
          fpv_funpay_account_refund(account, order_id, &error);
      fpv_funpay_error_clear(&error);
      if (result == FPV_OK) {
        success = true;
        break;
      }
      char attempts_buf[32];
      snprintf(attempts_buf, sizeof(attempts_buf), "%d", attempts);
      char* text = fpv_tg_loc_format(
          service,
          "refund_attempt",
          (const char*[]){order_id, attempts_buf},
          2);
      if (status_id == 0) {
        fpv_tg_send_message(
            service,
            callback->chat_id,
            text ? text : "",
            NULL,
            &status_id);
      } else {
        fpv_tg_edit_message_text(
            service,
            callback->chat_id,
            status_id,
            text ? text : "",
            NULL);
      }
      fpv_free(text);
      attempts--;
      if (attempts > 0) {
        fpv_tg_sleep_ms(1000);
      }
    }
  }

  if (!success) {
    char* text = fpv_tg_loc_format(
        service,
        "refund_error",
        (const char*[]){order_id},
        1);
    if (status_id > 0) {
      fpv_tg_edit_message_text(
          service,
          callback->chat_id,
          status_id,
          text ? text : "",
          NULL);
    } else {
      fpv_tg_send_message(
          service,
          callback->chat_id,
          text ? text : "",
          NULL,
          NULL);
    }
    fpv_free(text);
    fpv_tg_update_order_keyboard(
        service, callback, order_id, chat_id, username, false, false);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  char* text = fpv_tg_loc_format(
      service,
      "refund_complete",
      (const char*[]){order_id},
      1);
  if (status_id > 0) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        status_id,
        text ? text : "",
        NULL);
  } else {
    fpv_tg_send_message(
        service,
        callback->chat_id,
        text ? text : "",
        NULL,
        NULL);
  }
  fpv_free(text);
  fpv_tg_update_order_keyboard(
      service, callback, order_id, chat_id, username, false, true);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static char* fpv_tg_build_power_off_keyboard(
    fpv_telegram_service_t* service,
    uint64_t instance_id,
    int state) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char yes_cb[64];
  char no_cb[64];
  snprintf(no_cb, sizeof(no_cb), "%s", fpv_tg_cbt_cancel_shutdown);
  snprintf(yes_cb, sizeof(yes_cb), "%s:%d:%" PRIu64, fpv_tg_cbt_shutdown, state + 1, instance_id);
  if (state == 0) {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_yes"), yes_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_no"), no_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
    return fpv_tg_keyboard_finalize(kb, false);
  }
  if (state == 1) {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_no"), no_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_yes"), yes_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
    return fpv_tg_keyboard_finalize(kb, false);
  }
  if (state == 5) {
    snprintf(yes_cb, sizeof(yes_cb), "%s:%d:%" PRIu64, fpv_tg_cbt_shutdown, 6, instance_id);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_yep"), yes_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
    return fpv_tg_keyboard_finalize(kb, false);
  }

  int total = state == 2 ? 10 : state == 3 ? 30 : 40;
  int row_width = state == 2 ? 2 : state == 3 ? 5 : 7;
  int yes_index = rand() % total;
  const char* yes_label = fpv_tg_loc(service, "gl_yes");
  const char* no_label = fpv_tg_loc(service, "gl_no");
  if (state == 4) {
    yes_label = fpv_tg_loc(service, "gl_no");
    no_label = fpv_tg_loc(service, "gl_yes");
    snprintf(yes_cb, sizeof(yes_cb), "%s:%d:%" PRIu64, fpv_tg_cbt_shutdown, 5, instance_id);
  }
  for (int i = 0; i < total; i++) {
    const char* label = (i == yes_index) ? yes_label : no_label;
    const char* cb = (i == yes_index) ? yes_cb : no_cb;
    fpv_tg_keyboard_add_button(kb, label, cb, NULL);
    if ((i + 1) % row_width == 0) {
      fpv_tg_keyboard_row_end(kb);
    }
  }
  if (total % row_width != 0) {
    fpv_tg_keyboard_row_end(kb);
  }
  return fpv_tg_keyboard_finalize(kb, false);
}

static fpv_funpay_account_t* fpv_tg_get_account(fpv_telegram_service_t* service) {
  if (!service) {
    return NULL;
  }
  fpv_funpay_account_t* account = NULL;
  fpv_mutex_lock(&service->mutex);
  account = service->account;
  fpv_mutex_unlock(&service->mutex);
  return account;
}

static void fpv_tg_send_menu(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* reply_markup = fpv_tg_build_settings_sections(service);
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "desc_main"),
                      reply_markup, NULL);
  fpv_free(reply_markup);
}

static void fpv_tg_send_cfg_not_found(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* name);

static char* fpv_tg_build_desc_with_text(
    fpv_telegram_service_t* service,
    const char* key,
    const char* value) {
  char* escaped = fpv_tg_escape_html(value ? value : "");
  char* text = fpv_tg_loc_format(
      service,
      key,
      (const char*[]){escaped ? escaped : ""},
      1);
  fpv_free(escaped);
  if (!text) {
    text = fpv_strdup("");
  }
  return text;
}

static char* fpv_tg_build_desc_with_chat_id(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  char* text = fpv_tg_loc_format(
      service,
      "desc_ns",
      (const char*[]){chat_buf},
      1);
  if (!text) {
    text = fpv_strdup("");
  }
  return text;
}

static void fpv_tg_open_main_sections(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    bool second_page) {
  if (!service || !callback) {
    return;
  }
  (void)second_page;
  char* reply_markup = fpv_tg_build_settings_sections(service);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_main"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_templates_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_templates_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_tmplt"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_settings_category(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* category) {
  if (!service || !callback || !category) {
    return;
  }
  fpv_settings_t settings;
  memset(&settings, 0, sizeof(settings));
  bool need_settings = false;
  bool has_settings = false;
  if (strcmp(category, "main") == 0 ||
      strcmp(category, "bl") == 0 ||
      strcmp(category, "mv") == 0 ||
      strcmp(category, "gr") == 0 ||
      strcmp(category, "oc") == 0 ||
      strcmp(category, "rr") == 0) {
    need_settings = true;
  }
  if (need_settings) {
    if (!fpv_tg_load_settings(service, &settings)) {
      fpv_tg_send_cfg_not_found(service, callback->chat_id, "_main.cfg");
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return;
    }
    has_settings = true;
  }

  const char* text = NULL;
  char* text_owned = NULL;
  char* reply_markup = NULL;
  if (strcmp(category, fpv_tg_menu_core) == 0) {
    reply_markup = fpv_tg_build_settings_group(service, fpv_tg_menu_core);
    text = fpv_tg_loc(service, "desc_main");
  } else if (strcmp(category, fpv_tg_menu_notify) == 0) {
    reply_markup = fpv_tg_build_settings_group(service, fpv_tg_menu_notify);
    text = fpv_tg_loc(service, "desc_main");
  } else if (strcmp(category, fpv_tg_menu_reply) == 0) {
    reply_markup = fpv_tg_build_settings_group(service, fpv_tg_menu_reply);
    text = fpv_tg_loc(service, "desc_main");
  } else if (strcmp(category, fpv_tg_menu_delivery) == 0) {
    reply_markup = fpv_tg_build_settings_group(service, fpv_tg_menu_delivery);
    text = fpv_tg_loc(service, "desc_main");
  } else if (strcmp(category, "main") == 0) {
    reply_markup = fpv_tg_build_main_settings(service, &settings);
    text = fpv_tg_loc(service, "desc_gs");
  } else if (strcmp(category, "tg") == 0) {
    fpv_tg_setup_default_notifications(service, callback->chat_id);
    reply_markup = fpv_tg_build_notifications_settings(service, (uint64_t)callback->chat_id);
    text_owned = fpv_tg_build_desc_with_chat_id(service, callback->chat_id);
    text = text_owned;
  } else if (strcmp(category, "ar") == 0) {
    reply_markup = fpv_tg_build_auto_response_settings(service);
    text = fpv_tg_loc(service, "desc_ar");
  } else if (strcmp(category, "ad") == 0) {
    reply_markup = fpv_tg_build_auto_delivery_settings(service);
    text = fpv_tg_loc(service, "desc_ad");
  } else if (strcmp(category, "bl") == 0) {
    reply_markup = fpv_tg_build_blacklist_settings(service, &settings);
    text = fpv_tg_loc(service, "desc_bl");
  } else if (strcmp(category, "gr") == 0) {
    reply_markup = fpv_tg_build_greeting_settings(service, &settings);
    text_owned = fpv_tg_build_desc_with_text(
        service,
        "desc_gr",
        settings.greetings_text);
    text = text_owned;
  } else if (strcmp(category, "oc") == 0) {
    reply_markup = fpv_tg_build_order_confirm_settings(service, &settings);
    text_owned = fpv_tg_build_desc_with_text(
        service,
        "desc_oc",
        settings.order_confirm_text);
    text = text_owned;
  } else if (strcmp(category, "rr") == 0) {
    reply_markup = fpv_tg_build_review_reply_settings(service, &settings);
    text = fpv_tg_loc(service, "desc_or");
  } else if (strcmp(category, "mv") == 0) {
    reply_markup = fpv_tg_build_new_message_view_settings(service, &settings);
    text = fpv_tg_loc(service, "desc_mv");
  } else if (strcmp(category, "configs") == 0) {
    reply_markup = fpv_tg_build_configs_uploader(service);
    text = fpv_tg_loc(service, "desc_cfg");
  } else if (strcmp(category, "templates") == 0) {
    reply_markup = fpv_tg_build_templates_list(service, 0);
    text = fpv_tg_loc(service, "desc_tmplt");
  } else {
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    fpv_free(text_owned);
    fpv_free(reply_markup);
    if (has_settings) {
      fpv_settings_destroy(&settings);
    }
    return;
  }

  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text_owned);
  if (has_settings) {
    fpv_settings_destroy(&settings);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static const char* fpv_tg_category_for_section(const char* section) {
  if (!section) {
    return NULL;
  }
  if (strcmp(section, "FunPay") == 0) {
    return "main";
  }
  if (strcmp(section, "BlockList") == 0) {
    return "bl";
  }
  if (strcmp(section, "NewMessageView") == 0) {
    return "mv";
  }
  if (strcmp(section, "Greetings") == 0) {
    return "gr";
  }
  if (strcmp(section, "OrderConfirm") == 0) {
    return "oc";
  }
  if (strcmp(section, "ReviewReply") == 0) {
    return "rr";
  }
  return NULL;
}

static void fpv_tg_toggle_setting(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* section,
    const char* key) {
  if (!service || !callback || !section || !key) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "_main.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "_main.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* current = fpv_ini_get(ini, section, key);
  bool enabled = false;
  if (current && current[0]) {
    fpv_tg_parse_bool(current, &enabled);
  }
  const char* next = enabled ? "0" : "1";
  bool ok = fpv_ini_set(ini, section, key, next) == FPV_OK &&
      fpv_tg_save_ini(service, "_main.cfg", ini);
  fpv_ini_destroy(ini);
  if (ok) {
    fpv_tg_reload_main_settings(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_param_changed",
        (const char*[]){uname, id_buf, key, section, next},
        5);
  }
  const char* category = fpv_tg_category_for_section(section);
  if (category) {
    fpv_tg_open_settings_category(service, callback, category);
    return;
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_toggle_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* type) {
  if (!service || !callback || !type || !type[0]) {
    return;
  }
  bool enabled = fpv_tg_is_notification_enabled(service, (int64_t)chat_id, type);
  bool next = !enabled;
  if (fpv_tg_set_notification(service, (int64_t)chat_id, type, next)) {
    fpv_tg_save_notification_settings(service);
    char id_buf[32];
    char chat_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_notification_switched",
        (const char*[]){uname, id_buf, type, chat_buf, next ? "1" : "0"},
        5);
  }
  fpv_tg_open_settings_category(service, callback, "tg");
}

static void fpv_tg_switch_language(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* lang) {
  if (!service || !callback || !lang || !lang[0]) {
    return;
  }
  if (fpv_tg_update_main_config_value(service, "Other", "language", lang)) {
    fpv_tg_reload_main_settings(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_param_changed",
        (const char*[]){uname, id_buf, "language", "Other", lang},
        5);
  }
  fpv_tg_open_main_sections(service, callback, false);
}

static void fpv_tg_handle_old_help(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback) {
  if (!service || !callback) {
    return;
  }
  fpv_tg_send_message(
      service,
      callback->chat_id,
      fpv_tg_loc(service, "old_mode_help"),
      NULL,
      NULL);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_prompt_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    fpv_tg_state_type_t type,
    const char* prompt_key) {
  if (!service || !message || !prompt_key) {
    return;
  }
  char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
  int prompt_id = 0;
  fpv_tg_send_message(service, message->chat_id,
                      fpv_tg_loc(service, prompt_key),
                      reply_markup, &prompt_id);
  fpv_free(reply_markup);
  fpv_tg_set_state(service, message->chat_id, message->from_id,
                   prompt_id, type, NULL);
}

static void fpv_tg_prompt_state_from_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t type,
    const char* prompt_text) {
  if (!service || !callback || !prompt_text) {
    return;
  }
  char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
  int prompt_id = 0;
  fpv_tg_send_message(service, callback->chat_id,
                      prompt_text,
                      reply_markup, &prompt_id);
  fpv_free(reply_markup);
  fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                   prompt_id, type, NULL);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_prompt_state_with_data(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t type,
    const char* prompt_text,
    const fpv_tg_state_data_t* data) {
  if (!service || !callback || !prompt_text) {
    return;
  }
  char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
  int prompt_id = 0;
  fpv_tg_send_message(service, callback->chat_id,
                      prompt_text,
                      reply_markup, &prompt_id);
  fpv_free(reply_markup);
  fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                   prompt_id, type, data);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_prompt_add_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  static const char* vars[] = {"v_username", "v_photo"};
  char* text = fpv_tg_build_variable_prompt(
      service,
      "V_new_template",
      vars,
      sizeof(vars) / sizeof(vars[0]));
  fpv_tg_state_data_t data;
  memset(&data, 0, sizeof(data));
  data.offset = (int)offset;
  fpv_tg_prompt_state_with_data(
      service,
      callback,
      FPV_TG_STATE_ADD_TEMPLATE,
      text ? text : "",
      &data);
  fpv_free(text);
}

static bool fpv_tg_handle_document_upload(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    fpv_tg_state_type_t type) {
  if (!service || !message || !message->has_document) {
    return false;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  if (!message->document_file_id || !message->document_file_name) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\xA4\xD0\xB0\xD0\xB9\xD0\xBB \xD0\xBD\xD0\xB5 \xD0\xBE\xD0\xb1\xD0\xBD\xD0\xb0\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD.",
                        NULL, NULL);
    return true;
  }
  if (!fpv_tg_is_safe_upload_name(message->document_file_name)) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9D\xD0\xb5\xD0\xb4\xD0\xBE\xD0\xBF\xD1\x83\xD1\x81\xD1\x82\xD0\xb8\xD0\xBC\xD0\xBE\xD0\xb5 \xD0\xb8\xD0\xBC\xD1\x8F \xD1\x84\xD0\xb0\xD0\xb9\xD0\xBB\xD0\xb0.",
                        NULL, NULL);
    return true;
  }
  if (message->document_file_size >= fpv_tg_max_upload_size) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\xa0\xD0\xb0\xD0\xb7\xD0\xBC\xD0\xb5\xD1\x80 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xBB\xD0\xb0 \xD0\xBD\xD0\xb5 \xD0\xb4\xD0\xBE\xD0\xbb\xD0\xb6\xD0\xb5\xD0\xBD \xD0\xBF\xD1\x80\xD0\xb5\xD0\xb2\xD1\x8b\xD1\x88\xD0\xb0\xD1\x82\xD1\x8c 20\xD0\x9C\xD0\x91.",
                        NULL, NULL);
    return true;
  }
  if (!fpv_tg_is_supported_upload(message->document_file_name)) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\xa4\xD0\xb0\xD0\xb9\xD0\xBB \xD0\xb4\xD0\xbe\xD0\xbb\xD0\xb6\xD0\xb5\xD0\xBD \xD0\xb1\xD1\x8b\xD1\x82\xD1\x8c \xD1\x82\xD0\xb5\xD0\xba\xD1\x81\xD1\x82\xD0\xbe\xD0\xb2\xD1\x8b\xD0\xBC.",
                        NULL, NULL);
    return true;
  }
  if (type == FPV_TG_STATE_UPLOAD_PRODUCTS_FILE &&
      !fpv_tg_has_suffix(message->document_file_name, ".txt")) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\xa4\xD0\xb0\xD0\xb9\xD0\xBB \xD1\x81 \xD1\x82\xD0\xbe\xD0\xb2\xD0\xb0\xD1\x80\xD0\xb0\xD0\xBC\xD0\xb8 \xD0\xb4\xD0\xbe\xD0\xbb\xD0\xb6\xD0\xb5\xD0\xBD \xD0\xb8\xD0\xBC\xD0\xb5\xD1\x82\xD1\x8c \xD1\x80\xD0\xb0\xD1\x81\xD1\x88\xD0\xb8\xD1\x80\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5 .txt.",
                        NULL, NULL);
    return true;
  }

  void* data = NULL;
  size_t size = 0;
  if (fpv_tg_download_tg_file(service, message->document_file_id, &data, &size) != FPV_OK) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9F\xD1\x80\xD0\xBE\xD0\xb8\xD0\xb7\xD0\xBE\xD1\x88\xD0\xBB\xD0\xb0 \xD0\xBE\xD1\x88\xD0\xb8\xD0\xb1\xD0\xBA\xD0\xb0 \xD0\xBF\xD1\x80\xD0\xb8 \xD0\xb7\xD0\xb0\xD0\xb3\xD1\x80\xD1\x83\xD0\xb7\xD0\xba\xD0\xb5 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xBB\xD0\xb0.",
                        NULL, NULL);
    fpv_free(data);
    return true;
  }

  char* path = NULL;
  if (type == FPV_TG_STATE_UPLOAD_PRODUCTS_FILE) {
    path = fpv_tg_products_path(service, message->document_file_name);
  } else if (type == FPV_TG_STATE_UPLOAD_MAIN_CONFIG) {
    path = fpv_tg_config_path(service, "_main.cfg");
  } else if (type == FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG) {
    path = fpv_tg_config_path(service, "auto_response.cfg");
  } else if (type == FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG) {
    path = fpv_tg_config_path(service, "auto_delivery.cfg");
  }

  if (!path || !fpv_tg_write_file_data(path, data, size)) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9F\xD1\x80\xD0\xBE\xD0\xb8\xD0\xb7\xD0\xBE\xD1\x88\xD0\xbb\xD0\xb0 \xD0\xBE\xD1\x88\xD0\xb8\xD0\xb1\xD0\xBA\xD0\xb0 \xD0\xBF\xD1\x80\xD0\xb8 \xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xb0\xD0\xBD\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb8 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb0.",
                        NULL, NULL);
    fpv_free(path);
    fpv_free(data);
    return true;
  }
  fpv_free(data);

  if (type == FPV_TG_STATE_UPLOAD_PRODUCTS_FILE) {
    size_t products_count = fpv_tg_count_products(path);
    char* escaped_name = fpv_tg_escape_html(message->document_file_name);
    char* text = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "%zu", products_count);
    fpv_tg_buffer_append_str(
        &text, &length, &capacity,
        "\xE2\x9C\x85 \xD0\xa4\xD0\xb0\xD0\xb9\xD0\xBB \xD1\x81 \xD1\x82\xD0\xBE\xD0\xb2\xD0\xb0\xD1\x80\xD0\xb0\xD0\xBC\xD0\xb8 <code>storage/products/");
    fpv_tg_buffer_append_str(&text, &length, &capacity, escaped_name);
    fpv_tg_buffer_append_str(
        &text, &length, &capacity,
        "</code> \xD1\x83\xD1\x81\xD0\xBF\xD0\xb5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xb7\xD0\xb0\xD0\xb3\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD. "
        "\xD0\xa2\xD0\xBE\xD0\xb2\xD0\xb0\xD1\x80\xD0\xBE\xD0\xb2 \xD0\xb2 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb5: <code>");
    fpv_tg_buffer_append_str(&text, &length, &capacity, count_buf);
    fpv_tg_buffer_append_str(&text, &length, &capacity, "</code>.");
    if (!text) {
      text = fpv_strdup("");
    }

    size_t total = 0;
    size_t file_index = 0;
    bool found = false;
    char** files = fpv_tg_list_products_files(service, &total);
    for (size_t i = 0; i < total; i++) {
      if (files[i] && strcmp(files[i], message->document_file_name) == 0) {
        file_index = i;
        found = true;
        break;
      }
    }
    fpv_tg_free_string_array(files, total);
    char* reply_markup = NULL;
    if (found) {
      reply_markup = fpv_tg_build_products_file_edit_keyboard(service, file_index, 0, false);
    }
    fpv_tg_send_message(service, message->chat_id, text, reply_markup, NULL);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_free(escaped_name);
    fpv_free(path);
    return true;
  }

  fpv_free(path);

  if (type == FPV_TG_STATE_UPLOAD_MAIN_CONFIG) {
    fpv_tg_send_message(
        service,
        message->chat_id,
        "\xE2\x9C\x85 \xD0\x9E\xD1\x81\xD0\xBD\xD0\xBE\xD0\xb2\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD1\x83\xD1\x81\xD0\xBF\xD0\xb5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xb7\xD0\xb0\xD0\xb3\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD. \xD0\x9F\xD0\xb5\xD1\x80\xD0\xb5\xD0\xb7\xD0\xb0\xD0\xBF\xD1\x83\xD1\x81\xD1\x82\xD0\xb8\xD1\x82\xD0\xb5 FPV \xD0\xb4\xD0\xBB\xD1\x8F \xD0\xBF\xD1\x80\xD0\xb8\xD0\xbc\xD0\xb5\xD0\xBD\xD0\xb5\xD0\xbd\xD0\xb8\xD1\x8f \xD0\xb8\xD0\xb7\xD0\xbc\xD0\xb5\xD0\xbd\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb9.",
        NULL,
        NULL);
  } else if (type == FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG) {
    fpv_tg_send_message(
        service,
        message->chat_id,
        "\xE2\x9C\x85 \xD0\x9A\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE\xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD1\x87\xD0\xb8\xD0\xBA\xD0\xb0 \xD1\x83\xD1\x81\xD0\xBF\xD0\xb5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xb7\xD0\xb0\xD0\xb3\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD.",
        NULL,
        NULL);
  } else if (type == FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG) {
    fpv_tg_send_message(
        service,
        message->chat_id,
        "\xE2\x9C\x85 \xD0\x9A\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE-\xD0\xb2\xD1\x8B\xD0\xb4\xD0\xb0\xD1\x87\xD0\xb8 \xD1\x83\xD1\x81\xD0\xBF\xD0\xb5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xb7\xD0\xb0\xD0\xb3\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD.",
        NULL,
        NULL);
  }
  return true;
}

static bool fpv_tg_handle_photo_upload(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message || !message->has_photo) {
    return false;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  if (!message->photo_file_id) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9F\xD0\xBE\xD0\xb4\xD0\xb4\xD0\xb5\xD1\x80\xD0\xb6\xD0\xb8\xD0\xb2\xD0\xb0\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F \xD1\x82\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xBA\xD0\xBE \xD1\x84\xD0\xBE\xD1\x80\xD0\xBC\xD0\xb0\xD1\x82\xD1\x8b <code>.png</code>, <code>.jpg</code>, <code>.gif</code>.",
                        NULL, NULL);
    return true;
  }
  if (message->photo_file_size >= fpv_tg_max_upload_size) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\xa0\xD0\xb0\xD0\xb7\xD0\xBC\xD0\xb5\xD1\x80 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb0 \xD0\xBD\xD0\xb5 \xD0\xb4\xD0\xBE\xD0\xbb\xD0\xb6\xD0\xb5\xD0\xBD \xD0\xBF\xD1\x80\xD0\xb5\xD0\xb2\xD1\x8b\xD1\x88\xD0\xb0\xD1\x82\xD1\x8c 20\xD0\x9C\xD0\x91.",
                        NULL, NULL);
    return true;
  }

  void* data = NULL;
  size_t size = 0;
  if (fpv_tg_download_tg_file(service, message->photo_file_id, &data, &size) != FPV_OK) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9D\xD0\xb5 \xD1\x83\xD0\xb4\xD0\xb0\xD0\xbb\xD0\xbe\xD1\x81\xD1\x8c \xD0\xb2\xD1\x8b\xD0\xb3\xD1\x80\xD1\x83\xD0\xb7\xD0\xb8\xD1\x82\xD1\x8C \xD0\xb8\xD0\xb7\xD0\xBE\xD0\xb1\xD1\x80\xD0\xb0\xD0\xb6\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5. \xD0\x9F\xD0\xBE\xD0\xb4\xD1\x80\xD0\xBE\xD0\xb1\xD0\xBD\xD0\xb5\xD0\xb5 \xD0\xb2 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb5 <code>logs/log.log</code>.",
                        NULL, NULL);
    fpv_free(data);
    return true;
  }

  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9D\xD0\xb5 \xD1\x83\xD0\xb4\xD0\xb0\xD0\xbb\xD0\xbe\xD1\x81\xD1\x8c \xD0\xb2\xD1\x8b\xD0\xb3\xD1\x80\xD1\x83\xD0\xb7\xD0\xb8\xD1\x82\xD1\x8C \xD0\xb8\xD0\xb7\xD0\xBE\xD0\xb1\xD1\x80\xD0\xb0\xD0\xb6\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5. \xD0\x9F\xD0\xBE\xD0\xb4\xD1\x80\xD0\xBE\xD0\xb1\xD0\xBD\xD0\xb5\xD0\xb5 \xD0\xb2 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb5 <code>logs/log.log</code>.",
                        NULL, NULL);
    fpv_free(data);
    return true;
  }

  uint64_t image_id = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_upload_image(
      account, data, size, "fpv_upload.jpg", &image_id, &error);
  fpv_funpay_error_clear(&error);
  fpv_free(data);
  if (result != FPV_OK || image_id == 0) {
    fpv_tg_send_message(service, message->chat_id,
                        "\xE2\x9D\x8C \xD0\x9D\xD0\xb5 \xD1\x83\xD0\xb4\xD0\xb0\xD0\xbb\xD0\xbe\xD1\x81\xD1\x8c \xD0\xb2\xD1\x8b\xD0\xb3\xD1\x80\xD1\x83\xD0\xb7\xD0\xb8\xD1\x82\xD1\x8C \xD0\xb8\xD0\xb7\xD0\xBE\xD0\xb1\xD1\x80\xD0\xb0\xD0\xb6\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5. \xD0\x9F\xD0\xBE\xD0\xb4\xD1\x80\xD0\xBE\xD0\xb1\xD0\xBD\xD0\xb5\xD0\xb5 \xD0\xb2 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xbb\xD0\xb5 <code>logs/log.log</code>.",
                        NULL, NULL);
    return true;
  }

  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRIu64, image_id);
  char* text = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append_str(
      &text, &length, &capacity,
      "\xE2\x9C\x85 \xD0\x98\xD0\xb7\xD0\xBE\xD0\xb1\xD1\x80\xD0\xb0\xD0\xb6\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5 \xD0\xb2\xD1\x8b\xD0\xb3\xD1\x80\xD1\x83\xD0\xb6\xD0\xb5\xD0\xBD\xD0\xBE \xD0\xBD\xD0\xb0 \xD1\x81\xD0\xb5\xD1\x80\xD0\xb2\xD0\xb5\xD1\x80 FunPay.\n\n"
      "<b>ID:</b> <code>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, id_buf);
  fpv_tg_buffer_append_str(
      &text, &length, &capacity,
      "</code>\n\n"
      "\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xb7\xD1\x83\xD0\xb9\xD1\x82\xD0\xb5 \xD1\x8D\xD1\x82\xD0\xBE\xD1\x82 ID \xD0\xb2 \xD1\x82\xD0\xb5\xD0\xBA\xD1\x81\xD1\x82\xD0\xb0\xD1\x85 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE\xD0\xb2\xD1\x8B\xD0\xb4\xD0\xb0\xD1\x87\xD0\xb8/\xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE\xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD0\xb0 \xD1\x81 \xD0\xBF\xD0\xb5\xD1\x80\xD0\xb5\xD0\xBC\xD0\xb5\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xb9 <code>$photo</code>\n\n"
      "\xD0\x9D\xD0\xb0\xD0\xBF\xD1\x80\xD0\xb8\xD0\xBC\xD0\xb5\xD1\x80: <code>$photo=");
  fpv_tg_buffer_append_str(&text, &length, &capacity, id_buf);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</code>");
  if (!text) {
    text = fpv_strdup("");
  }
  fpv_tg_send_message(service, message->chat_id, text, NULL, NULL);
  fpv_free(text);
  return true;
}

static bool fpv_tg_handle_upload_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_UPLOAD_PRODUCTS_FILE:
    case FPV_TG_STATE_UPLOAD_MAIN_CONFIG:
    case FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG:
    case FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG:
      return fpv_tg_handle_document_upload(service, message, state->type);
    case FPV_TG_STATE_UPLOAD_IMAGE:
      return fpv_tg_handle_photo_upload(service, message);
    default:
      break;
  }
  return false;
}

static bool fpv_tg_handle_send_fp_message(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  char* response_text = fpv_tg_trim_copy(message->text);
  if (!response_text) {
    return true;
  }

  fpv_result_t result = FPV_ERR_INVALID_ARGUMENT;
  if (service->features) {
    result = fpv_features_send_message(
        service->features,
        data->chat_id,
        data->username,
        response_text,
        true);
  }
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, data->chat_id);
  char* reply_markup = fpv_tg_build_reply_keyboard(
      service, data->chat_id, data->username, true, true);
  if (result == FPV_OK) {
    char* text = fpv_tg_loc_format(
        service,
        "msg_sent",
        (const char*[]){chat_buf, data->username ? data->username : ""},
        2);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
  } else {
    char* text = fpv_tg_loc_format(
        service,
        "msg_sending_error",
        (const char*[]){chat_buf, data->username ? data->username : ""},
        2);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
  }
  fpv_free(reply_markup);
  fpv_free(response_text);
  return true;
}

static bool fpv_tg_handle_misc_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_SEND_FP_MESSAGE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_send_fp_message(service, message, &data);
    }
    default:
      break;
  }
  return false;
}

static void fpv_tg_send_cfg_not_found(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* name) {
  if (!service || chat_id == 0 || !name) {
    return;
  }
  char* text = fpv_tg_loc_format(
      service,
      "cfg_not_found_err",
      (const char*[]){name},
      1);
  fpv_tg_send_message(service, chat_id, text ? text : "", NULL, NULL);
  fpv_free(text);
}

static bool fpv_tg_check_ar_command_index(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index,
    int64_t chat_id,
    int message_id,
    bool edit_message) {
  if (!ini) {
    return false;
  }
  size_t total = fpv_ini_section_count(ini);
  if (command_index < total) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", command_index);
  char* text = fpv_tg_loc_format(
      service,
      "ar_cmd_not_found_err",
      (const char*[]){index_buf},
      1);
  char* reply_markup = fpv_tg_build_cmd_refresh_keyboard(service);
  if (edit_message && message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        chat_id,
        message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(
        service,
        chat_id,
        text ? text : "",
        reply_markup,
        NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  return false;
}

static bool fpv_tg_check_ad_lot_index(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t lot_index,
    int64_t chat_id,
    int message_id,
    bool edit_message) {
  if (!ini) {
    return false;
  }
  size_t total = fpv_ini_section_count(ini);
  if (lot_index < total) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", lot_index);
  char* text = fpv_tg_loc_format(
      service,
      "ad_lot_not_found_err",
      (const char*[]){index_buf},
      1);
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_ad_lots);
  char* reply_markup = fpv_tg_build_refresh_keyboard(service, cb_buf);
  if (edit_message && message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        chat_id,
        message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(
        service,
        chat_id,
        text ? text : "",
        reply_markup,
        NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  return false;
}

static bool fpv_tg_check_products_file_index(
    fpv_telegram_service_t* service,
    size_t file_index,
    size_t file_count,
    int64_t chat_id,
    int message_id,
    bool edit_message,
    const char* back_callback) {
  if (file_index < file_count) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", file_index);
  char* text = fpv_tg_loc_format(
      service,
      "gf_not_found_err",
      (const char*[]){index_buf},
      1);
  char* reply_markup = NULL;
  if (back_callback && back_callback[0]) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_callback, NULL);
      fpv_tg_keyboard_row_end(kb);
      reply_markup = fpv_tg_keyboard_finalize(kb, false);
    }
  } else {
    char cb_buf[32];
    snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_products_list);
    reply_markup = fpv_tg_build_refresh_keyboard(service, cb_buf);
  }
  if (edit_message && message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        chat_id,
        message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(
        service,
        chat_id,
        text ? text : "",
        reply_markup,
        NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  return false;
}

static void fpv_tg_open_ar_commands_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* reply_markup = fpv_tg_build_commands_list_keyboard(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ar_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_ar_command_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* text = fpv_tg_build_command_info_text(service, ini, command_index);
  char* reply_markup =
      fpv_tg_build_edit_command_keyboard(service, ini, command_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_prompt_ar_edit_text(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t state,
    const char* prompt_key,
    size_t command_index,
    size_t offset) {
  if (!service || !callback || !prompt_key) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_ini_destroy(ini);
  static const char* vars[] = {
      "v_date",
      "v_date_text",
      "v_full_date_text",
      "v_time",
      "v_full_time",
      "v_username",
      "v_message_text",
      "v_chat_id",
      "v_photo"};
  char* text = fpv_tg_build_variable_prompt(
      service,
      prompt_key,
      vars,
      sizeof(vars) / sizeof(vars[0]));
  char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
  int prompt_id = 0;
  fpv_tg_send_message(service, callback->chat_id,
                      text ? text : "",
                      reply_markup,
                      &prompt_id);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_tg_state_data_t data;
  memset(&data, 0, sizeof(data));
  data.command_index = (int)command_index;
  data.offset = (int)offset;
  fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                   prompt_id, state, &data);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_toggle_ar_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* notification =
      fpv_ini_get(ini, command, "telegramNotification");
  bool enabled = false;
  if (notification && notification[0]) {
    fpv_tg_parse_bool(notification, &enabled);
  }
  const char* next = enabled ? "0" : "1";
  bool ok = fpv_ini_set(ini, command, "telegramNotification", next) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_response(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_param_changed",
        (const char*[]){uname, id_buf, "telegramNotification", command, next},
        5);
  }
  char* text = fpv_tg_build_command_info_text(service, ini, command_index);
  char* reply_markup =
      fpv_tg_build_edit_command_keyboard(service, ini, command_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_delete_ar_command(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  bool ok = command &&
      fpv_ini_remove_section(ini, command) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_response(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_ar_cmd_deleted",
        (const char*[]){uname, id_buf, command},
        3);
  }
  char* reply_markup = fpv_tg_build_commands_list_keyboard(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ar_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static bool fpv_tg_handle_ar_add_command(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  char* raw_command = fpv_strdup(message->text);
  if (!raw_command || !fpv_tg_trim_lower(raw_command)) {
    char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "ar_enter_new_cmd"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  char* raw_copy = fpv_strdup(raw_command);
  if (!raw_copy) {
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_string_list_t list;
  memset(&list, 0, sizeof(list));
  bool error = false;
  char* token = strtok(raw_copy, "|");
  while (token) {
    if (!fpv_tg_trim_lower(token)) {
      token = strtok(NULL, "|");
      continue;
    }
    if (fpv_tg_string_list_contains(&list, token)) {
      char* escaped = fpv_tg_escape_html(token);
      char* text = fpv_tg_loc_format(
          service,
          "ar_subcmd_duplicate_err",
          (const char*[]){escaped ? escaped : ""},
          1);
      char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
      fpv_tg_send_message(
          service,
          message->chat_id,
          text ? text : "",
          reply_markup,
          NULL);
      fpv_free(reply_markup);
      fpv_free(text);
      fpv_free(escaped);
      error = true;
      break;
    }
    if (fpv_tg_ar_command_exists(ini, token)) {
      char* escaped = fpv_tg_escape_html(token);
      char* text = fpv_tg_loc_format(
          service,
          "ar_cmd_already_exists_err",
          (const char*[]){escaped ? escaped : ""},
          1);
      char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
      fpv_tg_send_message(
          service,
          message->chat_id,
          text ? text : "",
          reply_markup,
          NULL);
      fpv_free(reply_markup);
      fpv_free(text);
      fpv_free(escaped);
      error = true;
      break;
    }
    if (!fpv_tg_string_list_add(&list, token)) {
      error = true;
      break;
    }
    token = strtok(NULL, "|");
  }
  if (!error && list.count == 0) {
    char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "ar_enter_new_cmd"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    error = true;
  }
  fpv_string_list_destroy(&list);
  fpv_free(raw_copy);
  if (error) {
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  bool ok = fpv_ini_set(
      ini,
      raw_command,
      "response",
      "\xD0\x94\xD0\xb0\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xb0\xD0\xbd\xD0\xb4\xD0\xb5 \xD0\xBD\xD0\xb5\xD0\xBE\xD0\xb1\xD1\x85\xD0\xBE\xD0\xb4\xD0\xb8\xD0\xbc\xD0\xBE \xD0\xBD\xD0\xb0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xb8\xD1\x82\xD1\x8C \xD1\x82\xD0\xb5\xD0\xba\xD1\x81\xD1\x82 \xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD0\xb0 :(") == FPV_OK &&
      fpv_ini_set(ini, raw_command, "telegramNotification", "0") == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  size_t command_index = fpv_ini_section_count(ini);
  if (command_index > 0) {
    command_index--;
  }
  size_t offset = fpv_tg_get_offset(command_index, fpv_tg_cmd_page);
  char* reply_markup =
      fpv_tg_build_ar_add_success_keyboard(service, command_index, offset);
  char* escaped = fpv_tg_escape_html(raw_command);
  char* text = fpv_tg_loc_format(
      service,
      "ar_cmd_added",
      (const char*[]){escaped ? escaped : ""},
      1);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ar_added",
      (const char*[]){uname, id_buf, raw_command},
      3);
  fpv_free(raw_command);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_ar_edit_response(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  size_t command_index = data->command_index < 0 ? 0 : (size_t)data->command_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* response_text = fpv_tg_trim_copy(message->text);
  if (!response_text) {
    fpv_ini_destroy(ini);
    return true;
  }
  if (!response_text[0]) {
    fpv_free(response_text);
    response_text = fpv_strdup(
        "\xD0\x94\xD0\xb0\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xb0\xD0\xbd\xD0\xb4\xD0\xb5 \xD0\xBD\xD0\xb5\xD0\xBE\xD0\xb1\xD1\x85\xD0\xBE\xD0\xb4\xD0\xb8\xD0\xbc\xD0\xBE \xD0\xBD\xD0\xb0\xD1\x81\xD1\x82\xD1\x80\xD0\xbe\xD0\xb8\xD1\x82\xD1\x8c \xD1\x82\xD0\xb5\xD0\xba\xD1\x81\xD1\x82 \xD0\xbe\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD0\xb0 :(");
    if (!response_text) {
      fpv_ini_destroy(ini);
      return true;
    }
  }
  bool ok = fpv_ini_set(ini, command, "response", response_text) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(response_text);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  char* reply_markup = fpv_tg_build_ar_edit_done_keyboard(
      service, fpv_tg_cbt_edit_cmd_response, command_index, offset);
  char* escaped_cmd = fpv_tg_escape_html(command);
  char* escaped_resp = fpv_tg_escape_html(response_text);
  char* text = fpv_tg_loc_format(
      service,
      "ar_response_text_changed",
      (const char*[]){escaped_cmd ? escaped_cmd : "",
                      escaped_resp ? escaped_resp : ""},
      2);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped_cmd);
  fpv_free(escaped_resp);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ar_response_text_changed",
      (const char*[]){uname, id_buf, command, response_text},
      4);
  fpv_free(response_text);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_ar_edit_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  size_t command_index = data->command_index < 0 ? 0 : (size_t)data->command_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* notification_text = fpv_tg_trim_copy(message->text);
  if (!notification_text) {
    fpv_ini_destroy(ini);
    return true;
  }
  bool ok = true;
  if (notification_text[0]) {
    ok = fpv_ini_set(ini, command, "notificationText", notification_text) == FPV_OK;
  } else {
    ok = fpv_ini_remove_entry(ini, command, "notificationText") == FPV_OK;
  }
  ok = ok && fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(notification_text);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  char* reply_markup = fpv_tg_build_ar_edit_done_keyboard(
      service, fpv_tg_cbt_edit_cmd_notification, command_index, offset);
  char* escaped_cmd = fpv_tg_escape_html(command);
  char* escaped_note = fpv_tg_escape_html(notification_text);
  char* text = fpv_tg_loc_format(
      service,
      "ar_notification_text_changed",
      (const char*[]){escaped_cmd ? escaped_cmd : "",
                      escaped_note ? escaped_note : ""},
      2);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped_cmd);
  fpv_free(escaped_note);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ar_notification_text_changed",
      (const char*[]){uname, id_buf, command, notification_text},
      4);
  fpv_free(notification_text);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_ar_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_ADD_CMD:
      return fpv_tg_handle_ar_add_command(service, message);
    case FPV_TG_STATE_EDIT_CMD_RESPONSE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ar_edit_response(service, message, &data);
    }
    case FPV_TG_STATE_EDIT_CMD_NOTIFICATION: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ar_edit_notification(service, message, &data);
    }
    default:
      break;
  }
  return false;
}

static char* fpv_tg_build_ad_edit_done_keyboard(
    fpv_telegram_service_t* service,
    const char* edit_callback,
    size_t lot_index,
    size_t offset) {
  if (!edit_callback) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char back_cb[64];
  char edit_cb[64];
  snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
           fpv_tg_cbt_edit_ad_lot, lot_index, offset);
  snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
           edit_callback, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_edit"), edit_cb, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

static void fpv_tg_open_ad_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* reply_markup = fpv_tg_build_lots_list(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ad_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_funpay_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  if (service->profile_lot_count == 0 && service->profile_update_ms == 0) {
    fpv_result_t result = fpv_tg_update_profile_lots(service);
    if (result != FPV_OK) {
      char* reply_markup = fpv_tg_build_funpay_lots_list(service, offset);
      const char* text = fpv_tg_loc(service, "ad_lots_list_updating_err");
      fpv_tg_edit_message_text(
          service,
          callback->chat_id,
          callback->message_id,
          text ? text : "",
          reply_markup);
      fpv_free(reply_markup);
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      return;
    }
  }
  char time_buf[64];
  if (service->profile_update_ms == 0 ||
      !fpv_tg_format_datetime(service->profile_update_ms, time_buf, sizeof(time_buf))) {
    snprintf(time_buf, sizeof(time_buf), "-");
  }
  char* text = fpv_tg_loc_format(
      service,
      "desc_ad_fp_lot_list",
      (const char*[]){time_buf},
      1);
  char* reply_markup = fpv_tg_build_funpay_lots_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_products_files_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_products_files_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_gf"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_products_file_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  size_t file_count = 0;
  char** files = fpv_tg_list_products_files(service, &file_count);
  if (!fpv_tg_check_products_file_index(
          service,
          file_index,
          file_count,
          callback->chat_id,
          callback->message_id,
          true,
          NULL)) {
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  char* text = fpv_tg_build_products_file_text(service, ini, files[file_index]);
  char* reply_markup =
      fpv_tg_build_products_file_edit_keyboard(service, file_index, offset, false);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_tg_free_string_array(files, file_count);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_open_edit_ad_lot(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_settings_t settings;
  if (!fpv_tg_load_settings(service, &settings)) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "_main.cfg");
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* text = fpv_tg_build_lot_info_text(service, ini, lot_index);
  char* reply_markup =
      fpv_tg_build_edit_lot_keyboard(service, ini, &settings, lot_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_settings_destroy(&settings);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_prompt_ad_edit_text(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          callback->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_ini_destroy(ini);
  static const char* vars[] = {
      "v_date",
      "v_date_text",
      "v_full_date_text",
      "v_time",
      "v_full_time",
      "v_username",
      "v_product",
      "v_order_id",
      "v_order_title",
      "v_photo"};
  char* text = fpv_tg_build_variable_prompt(
      service,
      "v_edit_delivery_text",
      vars,
      sizeof(vars) / sizeof(vars[0]));
  char* reply_markup = fpv_tg_build_clear_state_keyboard(service);
  int prompt_id = 0;
  fpv_tg_send_message(service, callback->chat_id,
                      text ? text : "",
                      reply_markup,
                      &prompt_id);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_tg_state_data_t data;
  memset(&data, 0, sizeof(data));
  data.lot_index = (int)lot_index;
  data.offset = (int)offset;
  fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                   prompt_id, FPV_TG_STATE_EDIT_LOT_DELIVERY_TEXT, &data);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static bool fpv_tg_add_delivery_test(
    fpv_telegram_service_t* service,
    const char* key,
    const char* lot_name) {
  if (!service || !key || !key[0] || !lot_name) {
    return false;
  }
  char* key_copy = fpv_strdup(key);
  char* lot_copy = fpv_strdup(lot_name);
  if (!key_copy || !lot_copy) {
    fpv_free(key_copy);
    fpv_free(lot_copy);
    return false;
  }
  fpv_mutex_lock(&service->mutex);
  fpv_tg_delivery_test_t* grown = (fpv_tg_delivery_test_t*)realloc(
      service->delivery_tests,
      (service->delivery_test_count + 1) * sizeof(*grown));
  if (!grown) {
    fpv_mutex_unlock(&service->mutex);
    fpv_free(key_copy);
    fpv_free(lot_copy);
    return false;
  }
  service->delivery_tests = grown;
  service->delivery_tests[service->delivery_test_count].key = key_copy;
  service->delivery_tests[service->delivery_test_count].lot_name = lot_copy;
  service->delivery_test_count++;
  fpv_mutex_unlock(&service->mutex);
  return true;
}

static void fpv_tg_add_ad_lot_from_funpay(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t fp_lot_index,
    size_t fp_lots_offset) {
  if (!service || !callback) {
    return;
  }
  if (fp_lot_index >= service->profile_lot_count ||
      !service->profile_lots ||
      !service->profile_lots[fp_lot_index] ||
      !service->profile_lots[fp_lot_index]->title) {
    char index_buf[32];
    snprintf(index_buf, sizeof(index_buf), "%zu", fp_lot_index);
    char* text = fpv_tg_loc_format(
        service,
        "ad_lot_not_found_err",
        (const char*[]){index_buf},
        1);
    char cb_buf[32];
    snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_fp_lots);
    char* reply_markup = fpv_tg_build_refresh_keyboard(service, cb_buf);
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text ? text : "",
        reply_markup);
    fpv_free(reply_markup);
    fpv_free(text);
    char msg[256];
    snprintf(msg, sizeof(msg),
             "Add AD lot failed: invalid FunPay lot index %s (total=%zu).",
             index_buf, service->profile_lot_count);
    fpv_tg_log(service, FPV_LOG_WARNING, msg);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_log(service, FPV_LOG_WARNING,
               "Add AD lot failed: auto_delivery.cfg not found.");
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* lot_name = service->profile_lots[fp_lot_index]->title;
  size_t lot_index = 0;
  if (fpv_tg_find_ini_section(ini, lot_name, &lot_index)) {
    size_t ad_offset = fpv_tg_get_offset(lot_index, fpv_tg_ad_page);
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      char edit_cb[64];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_fp_lots, fp_lots_offset);
      snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
               fpv_tg_cbt_edit_ad_lot, lot_index, ad_offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* escaped = fpv_tg_escape_html(lot_name);
    char* text = fpv_tg_loc_format(
        service,
        "ad_already_ad_err",
        (const char*[]){escaped ? escaped : ""},
        1);
    fpv_tg_send_message(
        service,
        callback->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_free(escaped);
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  static const char* default_response =
      "\xD0\xA1\xD0\xBF\xD0\xB0\xD1\x81\xD0\xB8\xD0\xB1\xD0\xBE \xD0\xB7\xD0\xB0 \xD0\xBF\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBF\xD0\xBA\xD1\x83, $username!\n\n"
      "\xD0\x92\xD0\xBE\xD1\x82 \xD1\x82\xD0\xB2\xD0\xBE\xD0\xb9 \xD1\x82\xD0\xBE\xD0\xb2\xD0\xb0\xD1\x80:\n$product";
  bool ok = fpv_ini_set(ini, lot_name, "response", default_response) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_tg_reload_auto_delivery(service);
  size_t new_index = fpv_ini_section_count(ini);
  if (new_index > 0) {
    new_index--;
  }
  size_t ad_offset = fpv_tg_get_offset(new_index, fpv_tg_ad_page);
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[32];
    char edit_cb[64];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_fp_lots, fp_lots_offset);
    snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_ad_lot, new_index, ad_offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  char* escaped = fpv_tg_escape_html(lot_name);
  char* text = fpv_tg_loc_format(
      service,
      "ad_lot_linked",
      (const char*[]){escaped ? escaped : ""},
      1);
  fpv_tg_send_message(
      service,
      callback->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ad_linked",
      (const char*[]){uname, id_buf, lot_name},
      3);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_toggle_lot_setting(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* param,
    size_t lot_index,
    size_t offset) {
  if (!service || !callback || !param) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  const char* current = lot_name ? fpv_ini_get(ini, lot_name, param) : NULL;
  bool enabled = false;
  fpv_tg_parse_bool(current, &enabled);
  const char* next = enabled ? "0" : "1";
  bool ok = lot_name &&
      fpv_ini_set(ini, lot_name, param, next) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_delivery(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_param_changed",
        (const char*[]){uname, id_buf, param, lot_name ? lot_name : "", next},
        5);
  }
  fpv_settings_t settings;
  if (!fpv_tg_load_settings(service, &settings)) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "_main.cfg");
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* text = fpv_tg_build_lot_info_text(service, ini, lot_index);
  char* reply_markup =
      fpv_tg_build_edit_lot_keyboard(service, ini, &settings, lot_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_settings_destroy(&settings);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_create_delivery_test(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  char key[64];
  if (!fpv_tg_generate_key(key, 51)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_tg_add_delivery_test(service, key, lot_name ? lot_name : "");

  char* escaped_lot = fpv_tg_escape_html(lot_name ? lot_name : "");
  char* escaped_key = fpv_tg_escape_html(key);
  char* text = fpv_tg_loc_format(
      service,
      "test_ad_key_created",
      (const char*[]){escaped_lot ? escaped_lot : "",
                      escaped_key ? escaped_key : ""},
      2);
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[64];
    char more_cb[64];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_ad_lot, lot_index, offset);
    snprintf(more_cb, sizeof(more_cb), "%s:%zu:%zu",
             fpv_tg_cb_test_auto_delivery, lot_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_more_test"), more_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  fpv_tg_send_message(
      service,
      callback->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped_lot);
  fpv_free(escaped_key);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_new_ad_key",
      (const char*[]){uname, id_buf, lot_name ? lot_name : "", key},
      4);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_delete_ad_lot(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_delivery.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  bool ok = lot_name &&
      fpv_ini_remove_section(ini, lot_name) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_delivery(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_ad_deleted",
        (const char*[]){uname, id_buf, lot_name ? lot_name : ""},
        3);
  }
  char* reply_markup = fpv_tg_build_lots_list(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ad_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_update_funpay_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  int status_id = 0;
  fpv_tg_send_message(
      service,
      callback->chat_id,
      fpv_tg_loc(service, "ad_updating_lots_list"),
      NULL,
      &status_id);
  fpv_result_t result = fpv_tg_update_profile_lots(service);
  if (result != FPV_OK) {
    if (status_id > 0) {
      fpv_tg_edit_message_text(
          service,
          callback->chat_id,
          status_id,
          fpv_tg_loc(service, "ad_lots_list_updating_err"),
          NULL);
    } else {
      fpv_tg_send_message(
          service,
          callback->chat_id,
          fpv_tg_loc(service, "ad_lots_list_updating_err"),
          NULL,
          NULL);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (status_id > 0) {
    fpv_tg_delete_message(service, callback->chat_id, status_id);
  }
  fpv_tg_open_funpay_lots_list(service, callback, offset);
}

static void fpv_tg_send_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index) {
  if (!service || !callback) {
    return;
  }
  size_t file_count = 0;
  char** files = fpv_tg_list_products_files(service, &file_count);
  if (!fpv_tg_check_products_file_index(
          service,
          file_index,
          file_count,
          callback->chat_id,
          callback->message_id,
          true,
          NULL)) {
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* path = fpv_tg_products_path(service, files[file_index]);
  size_t count = fpv_tg_count_products(path);
  if (count == 0) {
    char* text = fpv_tg_loc_format(
        service,
        "gf_empty_error",
        (const char*[]){files[file_index]},
        1);
    fpv_tg_answer_callback(service, callback->id, text ? text : "", true);
    fpv_free(text);
    fpv_free(path);
    fpv_tg_free_string_array(files, file_count);
    return;
  }
  fpv_tg_send_document(service, callback->chat_id, path, NULL, NULL);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_gf_downloaded",
      (const char*[]){uname, id_buf, files[file_index]},
      3);
  fpv_free(path);
  fpv_tg_free_string_array(files, file_count);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_confirm_delete_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index,
    size_t offset,
    bool confirm) {
  if (!service || !callback) {
    return;
  }
  size_t file_count = 0;
  char** files = fpv_tg_list_products_files(service, &file_count);
  if (!fpv_tg_check_products_file_index(
          service,
          file_index,
          file_count,
          callback->chat_id,
          callback->message_id,
          true,
          NULL)) {
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  if (!confirm) {
    char* reply_markup =
        fpv_tg_build_products_file_edit_keyboard(service, file_index, offset, true);
    fpv_tg_edit_message_reply_markup(
        service,
        callback->chat_id,
        callback->message_id,
        reply_markup);
    fpv_free(reply_markup);
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  const char* file_name = files[file_index];
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  bool linked = false;
  if (ini) {
    size_t sections = fpv_ini_section_count(ini);
    for (size_t i = 0; i < sections; i++) {
      const char* section = fpv_ini_section_name(ini, i);
      const char* ref = fpv_ini_get(ini, section, "productsFileName");
      if (ref && file_name && strcmp(ref, file_name) == 0) {
        linked = true;
        break;
      }
    }
  }
  if (linked) {
    char* text = fpv_tg_loc_format(
        service,
        "gf_linked_err",
        (const char*[]){file_name ? file_name : ""},
        1);
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
               fpv_tg_cbt_edit_products_file, file_index, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text ? text : "",
        reply_markup);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_ini_destroy(ini);
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_ini_destroy(ini);

  char* path = fpv_tg_products_path(service, file_name);
  bool ok = path && remove(path) == 0;
  fpv_free(path);
  if (!ok) {
    char* text = fpv_tg_loc_format(
        service,
        "gf_deleting_err",
        (const char*[]){file_name ? file_name : ""},
        1);
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
               fpv_tg_cbt_edit_products_file, file_index, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text ? text : "",
        reply_markup);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_tg_free_string_array(files, file_count);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* reply_markup = fpv_tg_build_products_files_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_gf"),
      reply_markup);
  fpv_free(reply_markup);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_gf_deleted",
      (const char*[]){uname, id_buf, file_name ? file_name : ""},
      3);
  fpv_tg_free_string_array(files, file_count);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static bool fpv_tg_handle_ad_add_lot_manual(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    return true;
  }
  char* lot_name = fpv_tg_trim_copy(message->text);
  if (!lot_name || !lot_name[0]) {
    fpv_free(lot_name);
    fpv_ini_destroy(ini);
    return true;
  }
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (fpv_tg_find_ini_section(ini, lot_name, NULL)) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      char add_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_fp_lots, offset);
      snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_ad_lot_manual, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_add_another_ad"), add_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* escaped = fpv_tg_escape_html(lot_name);
    char* text = fpv_tg_loc_format(
        service,
        "ad_lot_already_exists",
        (const char*[]){escaped ? escaped : ""},
        1);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_free(escaped);
    fpv_free(lot_name);
    fpv_ini_destroy(ini);
    return true;
  }

  static const char* default_response =
      "\xD0\xA1\xD0\xBF\xD0\xB0\xD1\x81\xD0\xB8\xD0\xB1\xD0\xBE \xD0\xB7\xD0\xB0 \xD0\xBF\xD0\xBE\xD0\xBA\xD1\x83\xD0\xBF\xD0\xBA\xD1\x83, $username!\n\n"
      "\xD0\x92\xD0\xBE\xD1\x82 \xD1\x82\xD0\xB2\xD0\xBE\xD0\xB9 \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80:\n\n$product";
  bool ok = fpv_ini_set(ini, lot_name, "response", default_response) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    fpv_free(lot_name);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_delivery(service);
  size_t lot_index = fpv_ini_section_count(ini);
  if (lot_index > 0) {
    lot_index--;
  }
  size_t ad_offset = fpv_tg_get_offset(lot_index, fpv_tg_ad_page);
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[32];
    char add_cb[32];
    char edit_cb[64];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_fp_lots, offset);
    snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_ad_lot_manual, offset);
    snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_ad_lot, lot_index, ad_offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ad_add_more_ad"), add_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  char* escaped = fpv_tg_escape_html(lot_name);
  char* text = fpv_tg_loc_format(
      service,
      "ad_lot_linked",
      (const char*[]){escaped ? escaped : ""},
      1);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ad_linked",
      (const char*[]){uname, id_buf, lot_name},
      3);
  fpv_free(lot_name);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_ad_edit_delivery_text(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    return true;
  }
  size_t lot_index = data->lot_index < 0 ? 0 : (size_t)data->lot_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  if (!lot_name) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* new_response = fpv_tg_trim_copy(message->text);
  if (!new_response) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* file_name = fpv_ini_get(ini, lot_name, "productsFileName");
  if (file_name && file_name[0] && !strstr(new_response, "$product")) {
    char* reply_markup = fpv_tg_build_ad_edit_done_keyboard(
        service, fpv_tg_cbt_edit_lot_text, lot_index, offset);
    char* escaped = fpv_tg_escape_html(lot_name);
    char* text = fpv_tg_loc_format(
        service,
        "ad_product_var_err",
        (const char*[]){escaped ? escaped : ""},
        1);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(text);
    fpv_free(escaped);
    fpv_free(new_response);
    fpv_ini_destroy(ini);
    return true;
  }
  bool ok = fpv_ini_set(ini, lot_name, "response", new_response) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    fpv_free(new_response);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_delivery(service);
  char* reply_markup = fpv_tg_build_ad_edit_done_keyboard(
      service, fpv_tg_cbt_edit_lot_text, lot_index, offset);
  char* escaped_lot = fpv_tg_escape_html(lot_name);
  char* escaped_resp = fpv_tg_escape_html(new_response);
  char* text = fpv_tg_loc_format(
      service,
      "ad_text_changed",
      (const char*[]){escaped_lot ? escaped_lot : "",
                      escaped_resp ? escaped_resp : ""},
      2);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped_lot);
  fpv_free(escaped_resp);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ad_text_changed",
      (const char*[]){uname, id_buf, lot_name, new_response},
      4);
  fpv_free(new_response);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_ad_bind_products(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_delivery.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    return true;
  }
  size_t lot_index = data->lot_index < 0 ? 0 : (size_t)data->lot_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ad_lot_index(
          service,
          ini,
          lot_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* lot_name = fpv_ini_section_name(ini, lot_index);
  if (!lot_name) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* file_raw = fpv_tg_trim_copy(message->text);
  if (!file_raw || !file_raw[0]) {
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }

  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[64];
    char link_cb[64];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_ad_lot, lot_index, offset);
    snprintf(link_cb, sizeof(link_cb), "%s:%zu:%zu",
             fpv_tg_cbt_bind_products, lot_index, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "ea_link_another_gf"), link_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;

  if (strcmp(file_raw, "-") == 0) {
    bool ok = fpv_ini_remove_entry(ini, lot_name, "productsFileName") == FPV_OK &&
        fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
    if (!ok) {
      fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
      fpv_free(reply_markup);
      fpv_free(file_raw);
      fpv_ini_destroy(ini);
      return true;
    }
    fpv_tg_reload_auto_delivery(service);
    char* escaped_lot = fpv_tg_escape_html(lot_name);
    char* text = fpv_tg_loc_format(
        service,
        "ad_gf_unlinked",
        (const char*[]){escaped_lot ? escaped_lot : ""},
        1);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(escaped_lot);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
    const char* uname = message->from_username ? message->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_gf_unlinked",
        (const char*[]){uname, id_buf, lot_name},
        3);
    fpv_free(reply_markup);
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }

  const char* response = fpv_ini_get(ini, lot_name, "response");
  if (!response || !strstr(response, "$product")) {
    fpv_tg_keyboard_t* err_kb = fpv_tg_keyboard_create(true, false);
    if (err_kb) {
      char back_cb[64];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
               fpv_tg_cbt_edit_ad_lot, lot_index, offset);
      fpv_tg_keyboard_add_button(err_kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_row_end(err_kb);
    }
    char* err_markup = err_kb ? fpv_tg_keyboard_finalize(err_kb, false) : NULL;
    char* text = fpv_tg_loc_format(service, "ad_product_var_err2", NULL, 0);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        err_markup,
        NULL);
    fpv_free(err_markup);
    fpv_free(text);
    fpv_free(reply_markup);
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }

  if (!fpv_tg_is_valid_filename(file_raw)) {
    char* text = fpv_tg_loc_format(service, "gf_name_invalid", NULL, 0);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(reply_markup);
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }

  size_t name_len = strlen(file_raw);
  char* file_name = (char*)malloc(name_len + 5);
  if (!file_name) {
    fpv_free(reply_markup);
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }
  snprintf(file_name, name_len + 5, "%s.txt", file_raw);
  char* path = fpv_tg_products_path(service, file_name);
  bool existed = path && fpv_fs_exists(path);
  if (!existed) {
    char* escaped = fpv_tg_escape_html(file_name);
    char* text = fpv_tg_loc_format(
        service,
        "ad_creating_gf",
        (const char*[]){escaped ? escaped : ""},
        1);
    fpv_tg_send_message(service, message->chat_id, text ? text : "", NULL, NULL);
    fpv_free(text);
    fpv_free(escaped);
    if (!fpv_tg_write_file(path, "")) {
      char* err = fpv_tg_loc_format(
          service,
          "gf_creation_err",
          (const char*[]){file_name},
          1);
      fpv_tg_send_message(service, message->chat_id, err ? err : "", reply_markup, NULL);
      fpv_free(err);
      fpv_free(path);
      fpv_free(file_name);
      fpv_free(reply_markup);
      fpv_free(file_raw);
      fpv_ini_destroy(ini);
      return true;
    }
  }
  fpv_free(path);

  bool ok = fpv_ini_set(ini, lot_name, "productsFileName", file_name) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_delivery.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_delivery.cfg");
    fpv_free(file_name);
    fpv_free(reply_markup);
    fpv_free(file_raw);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_delivery(service);
  char* escaped_file = fpv_tg_escape_html(file_name);
  char* escaped_lot = fpv_tg_escape_html(lot_name);
  char* text = NULL;
  if (existed) {
    text = fpv_tg_loc_format(
        service,
        "ad_gf_linked",
        (const char*[]){escaped_file ? escaped_file : "",
                        escaped_lot ? escaped_lot : ""},
        2);
  } else {
    text = fpv_tg_loc_format(
        service,
        "ad_gf_created_and_linked",
        (const char*[]){escaped_file ? escaped_file : "",
                        escaped_lot ? escaped_lot : ""},
        2);
  }
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(text);
  fpv_free(escaped_file);
  fpv_free(escaped_lot);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      existed ? "log_gf_linked" : "log_gf_created_and_linked",
      (const char*[]){uname, id_buf, file_name, lot_name},
      4);
  fpv_free(file_name);
  fpv_free(reply_markup);
  fpv_free(file_raw);
  fpv_ini_destroy(ini);
  return true;
}

static bool fpv_tg_handle_create_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  char* name = fpv_tg_trim_copy(message->text);
  if (!name || !name[0] || !fpv_tg_is_valid_filename(name)) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:ad", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_create_another"),
                                 fpv_tg_cbt_create_products_file, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* text = fpv_tg_loc_format(service, "gf_name_invalid", NULL, 0);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(reply_markup);
    fpv_free(name);
    return true;
  }

  size_t name_len = strlen(name);
  char* file_name = (char*)malloc(name_len + 5);
  if (!file_name) {
    fpv_free(name);
    return true;
  }
  snprintf(file_name, name_len + 5, "%s.txt", name);
  fpv_free(name);

  char* path = fpv_tg_products_path(service, file_name);
  bool exists = path && fpv_fs_exists(path);
  if (exists) {
    size_t file_count = 0;
    char** files = fpv_tg_list_products_files(service, &file_count);
    size_t file_index = 0;
    bool found = false;
    for (size_t i = 0; i < file_count; i++) {
      if (files[i] && strcmp(files[i], file_name) == 0) {
        file_index = i;
        found = true;
        break;
      }
    }
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:ad", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_create_another"),
                                 fpv_tg_cbt_create_products_file, NULL);
      if (found) {
        size_t offset = fpv_tg_get_offset(file_index, fpv_tg_products_page);
        char edit_cb[64];
        snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
                 fpv_tg_cbt_edit_products_file, file_index, offset);
        fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
      }
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* escaped = fpv_tg_escape_html(file_name);
    char* text = fpv_tg_loc_format(
        service,
        "gf_already_exists_err",
        (const char*[]){escaped ? escaped : ""},
        1);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(escaped);
    fpv_free(reply_markup);
    fpv_tg_free_string_array(files, file_count);
    fpv_free(path);
    fpv_free(file_name);
    return true;
  }

  if (!fpv_tg_write_file(path, "")) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:ad", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_create_another"),
                                 fpv_tg_cbt_create_products_file, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* text = fpv_tg_loc_format(
        service,
        "gf_creation_err",
        (const char*[]){file_name},
        1);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(reply_markup);
    fpv_free(path);
    fpv_free(file_name);
    return true;
  }

  size_t file_count = 0;
  char** files = fpv_tg_list_products_files(service, &file_count);
  size_t file_index = 0;
  bool found = false;
  for (size_t i = 0; i < file_count; i++) {
    if (files[i] && strcmp(files[i], file_name) == 0) {
      file_index = i;
      found = true;
      break;
    }
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[32];
    snprintf(back_cb, sizeof(back_cb), "%s:ad", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_create_more"),
                               fpv_tg_cbt_create_products_file, NULL);
    if (found) {
      size_t offset = fpv_tg_get_offset(file_index, fpv_tg_products_page);
      char edit_cb[64];
      snprintf(edit_cb, sizeof(edit_cb), "%s:%zu:%zu",
               fpv_tg_cbt_edit_products_file, file_index, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_configure"), edit_cb, NULL);
    }
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  char* escaped = fpv_tg_escape_html(file_name);
  char* text = fpv_tg_loc_format(
      service,
      "gf_created",
      (const char*[]){escaped ? escaped : ""},
      1);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(text);
  fpv_free(escaped);
  fpv_free(reply_markup);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_gf_created",
      (const char*[]){uname, id_buf, file_name},
      3);
  fpv_tg_free_string_array(files, file_count);
  fpv_free(path);
  fpv_free(file_name);
  return true;
}

static char* fpv_tg_build_products_payload(
    const char* text,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!text) {
    return fpv_strdup("");
  }
  char* copy = fpv_tg_trim_copy(text);
  if (!copy) {
    return NULL;
  }
  char* line = copy;
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  while (line) {
    char* next = strchr(line, '\n');
    if (next) {
      *next = '\0';
      next++;
    }
    if (line[0]) {
      if (out_count && *out_count > 0) {
        fpv_tg_buffer_append(&buffer, &length, &capacity, "\n", 1);
      }
      fpv_tg_buffer_append(&buffer, &length, &capacity, line, strlen(line));
      if (out_count) {
        (*out_count)++;
      }
    }
    line = next;
  }
  fpv_free(copy);
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

static bool fpv_tg_handle_add_products(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  size_t file_index = data->file_index < 0 ? 0 : (size_t)data->file_index;
  size_t element_index = data->element_index < 0 ? 0 : (size_t)data->element_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  size_t prev_page = data->previous_page < 0 ? 0 : (size_t)data->previous_page;

  size_t file_count = 0;
  char** files = fpv_tg_list_products_files(service, &file_count);
  char back_cb[64];
  if (prev_page == 0) {
    snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_products_file, file_index, offset);
  } else {
    snprintf(back_cb, sizeof(back_cb), "%s:%zu:%zu",
             fpv_tg_cbt_edit_ad_lot, element_index, offset);
  }
  const char* error_cb = prev_page == 0 ? NULL : back_cb;
  if (!fpv_tg_check_products_file_index(
          service,
          file_index,
          file_count,
          message->chat_id,
          0,
          false,
          error_cb)) {
    fpv_tg_free_string_array(files, file_count);
    return true;
  }
  char* path = fpv_tg_products_path(service, files[file_index]);
  size_t product_count = 0;
  char* payload = fpv_tg_build_products_payload(message->text, &product_count);
  if (!payload || !path || !fpv_tg_append_products(path, payload, false)) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char try_cb[96];
      snprintf(try_cb, sizeof(try_cb), "%s:%zu:%zu:%zu:%zu",
               fpv_tg_cbt_add_products, file_index, element_index, offset, prev_page);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_try_add_again"), try_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    char* text = fpv_tg_loc_format(service, "gf_add_goods_err", NULL, 0);
    fpv_tg_send_message(
        service,
        message->chat_id,
        text ? text : "",
        reply_markup,
        NULL);
    fpv_free(text);
    fpv_free(reply_markup);
    fpv_free(payload);
    fpv_free(path);
    fpv_tg_free_string_array(files, file_count);
    return true;
  }
  fpv_free(path);

  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char add_cb[96];
    snprintf(add_cb, sizeof(add_cb), "%s:%zu:%zu:%zu:%zu",
             fpv_tg_cbt_add_products, file_index, element_index, offset, prev_page);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gf_add_more"), add_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  char count_buf[32];
  snprintf(count_buf, sizeof(count_buf), "%zu", product_count);
  char* text = fpv_tg_loc_format(
      service,
      "gf_new_goods",
      (const char*[]){count_buf, files[file_index]},
      2);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(text);
  fpv_free(reply_markup);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_gf_new_goods",
      (const char*[]){uname, id_buf, count_buf, files[file_index]},
      4);
  fpv_free(payload);
  fpv_tg_free_string_array(files, file_count);
  return true;
}

static bool fpv_tg_handle_manual_ad_test(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  char* lot_name = fpv_tg_trim_copy(message->text);
  if (!lot_name) {
    return true;
  }
  char key[64];
  if (!fpv_tg_generate_key(key, 51)) {
    fpv_free(lot_name);
    return true;
  }
  fpv_tg_add_delivery_test(service, key, lot_name);
  char* escaped_lot = fpv_tg_escape_html(lot_name);
  char* escaped_key = fpv_tg_escape_html(key);
  char* text = fpv_tg_loc_format(
      service,
      "test_ad_key_created",
      (const char*[]){escaped_lot ? escaped_lot : "",
                      escaped_key ? escaped_key : ""},
      2);
  fpv_tg_send_message(service, message->chat_id, text ? text : "", NULL, NULL);
  fpv_free(text);
  fpv_free(escaped_lot);
  fpv_free(escaped_key);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_new_ad_key",
      (const char*[]){uname, id_buf, lot_name, key},
      4);
  fpv_free(lot_name);
  return true;
}

static bool fpv_tg_handle_ad_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_ADD_AD_TO_LOT_MANUAL: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ad_add_lot_manual(service, message, &data);
    }
    case FPV_TG_STATE_EDIT_LOT_DELIVERY_TEXT: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ad_edit_delivery_text(service, message, &data);
    }
    case FPV_TG_STATE_BIND_PRODUCTS_FILE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ad_bind_products(service, message, &data);
    }
    case FPV_TG_STATE_CREATE_PRODUCTS_FILE:
      return fpv_tg_handle_create_products_file(service, message);
    case FPV_TG_STATE_ADD_PRODUCTS_TO_FILE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_add_products(service, message, &data);
    }
    case FPV_TG_STATE_MANUAL_AD_TEST:
      return fpv_tg_handle_manual_ad_test(service, message);
    default:
      break;
  }
  return false;
}

static bool fpv_tg_handle_add_template(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data) {
  if (!service || !message || !data) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  char* template_text = fpv_tg_trim_copy(message->text);
  if (!template_text) {
    return true;
  }
  if (fpv_tg_string_list_contains(&service->answer_templates, template_text)) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      char add_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_template_list, offset);
      snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_template, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add_another"), add_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "tmplt_already_exists_err"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(template_text);
    return true;
  }
  if (!fpv_tg_string_list_add(&service->answer_templates, template_text)) {
    fpv_free(template_text);
    return true;
  }
  fpv_tg_save_answer_templates(service);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_tmplt_added",
      (const char*[]){uname, id_buf, template_text},
      3);
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[32];
    char add_cb[32];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_template_list, offset);
    snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_template, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add_more"), add_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  fpv_tg_send_message(
      service,
      message->chat_id,
      fpv_tg_loc(service, "tmplt_added"),
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(template_text);
  return true;
}

static bool fpv_tg_handle_template_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return false;
  }
  fpv_tg_user_state_t* state =
      fpv_tg_get_state(service, message->chat_id, message->from_id);
  if (!state) {
    return false;
  }
  switch (state->type) {
    case FPV_TG_STATE_ADD_TEMPLATE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_add_template(service, message, &data);
    }
    default:
      break;
  }
  return false;
}

static bool fpv_tg_is_profile_missing_stats(const fpv_funpay_error_t* error) {
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

static void fpv_tg_log_funpay_error(
    fpv_telegram_service_t* service,
    const char* context,
    fpv_result_t result,
    const fpv_funpay_error_t* error) {
  if (!service || !context) {
    return;
  }
  char buffer[512];
  const char* result_label = fpv_tg_result_label(result);
  snprintf(
      buffer,
      sizeof(buffer),
      "%s failed (result=%s funpay_code=%d http=%ld method=%s url=%s msg=%s)",
      context,
      result_label ? result_label : "unknown",
      error ? (int)error->code : 0,
      error ? error->http_status : 0L,
      (error && error->method) ? error->method : "-",
      (error && error->url) ? error->url : "-",
      (error && error->message) ? error->message : "-");
  fpv_tg_log(service, FPV_LOG_WARNING, buffer);
}

static void fpv_tg_send_profile(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  int status_id = 0;
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "updating_profile"),
                      NULL, &status_id);
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    fpv_tg_log(service, FPV_LOG_WARNING, "Profile update failed: account not attached.");
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    return;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t refresh_result = fpv_funpay_account_refresh(account, &error);
  if (refresh_result != FPV_OK) {
    fpv_tg_log_funpay_error(service, "Profile refresh", refresh_result, &error);
    fpv_funpay_error_clear(&error);
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    return;
  }
  fpv_funpay_error_clear(&error);
  fpv_funpay_balance_t balance;
  memset(&balance, 0, sizeof(balance));
  fpv_result_t balance_result =
      fpv_funpay_account_get_balance(account, &balance, &error);
  bool missing_stats =
      balance_result != FPV_OK && fpv_tg_is_profile_missing_stats(&error);
  if (balance_result != FPV_OK && !missing_stats) {
    fpv_tg_log_funpay_error(service, "Profile balance", balance_result, &error);
  }
  fpv_funpay_error_clear(&error);
  if (balance_result != FPV_OK) {
    const char* err_key = missing_stats ? "profile_no_stats" : "profile_updating_error";
    const char* err_text = fpv_tg_loc(service, err_key);
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    return;
  }
  char* text = fpv_tg_build_profile_text(service, &balance, 0);
  if (!text) {
    fpv_tg_log(service, FPV_LOG_WARNING, "Profile build failed: no text.");
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    return;
  }
  char* reply_markup = fpv_tg_build_profile_keyboard(service, false);
  fpv_tg_send_message(service, chat_id, text, reply_markup, NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  if (status_id > 0) {
    fpv_tg_delete_message(service, chat_id, status_id);
  }
}

static void fpv_tg_update_profile_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    bool advanced) {
  if (!service || !callback) {
    return;
  }
  int status_id = 0;
  fpv_tg_send_message(service, callback->chat_id,
                      fpv_tg_loc(service, "updating_profile"),
                      NULL, &status_id);
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    fpv_tg_log(service, FPV_LOG_WARNING, "Profile update callback failed: account not attached.");
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, callback->chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, callback->chat_id, err_text, NULL, NULL);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t refresh_result = fpv_funpay_account_refresh(account, &error);
  if (refresh_result != FPV_OK) {
    fpv_tg_log_funpay_error(service, "Profile refresh (callback)", refresh_result, &error);
    fpv_funpay_error_clear(&error);
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, callback->chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, callback->chat_id, err_text, NULL, NULL);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_funpay_error_clear(&error);
  fpv_funpay_balance_t balance;
  memset(&balance, 0, sizeof(balance));
  fpv_result_t balance_result =
      fpv_funpay_account_get_balance(account, &balance, &error);
  bool missing_stats =
      balance_result != FPV_OK && fpv_tg_is_profile_missing_stats(&error);
  if (balance_result != FPV_OK && !missing_stats) {
    fpv_tg_log_funpay_error(service, "Profile balance (callback)", balance_result, &error);
  }
  fpv_funpay_error_clear(&error);
  if (balance_result != FPV_OK) {
    const char* err_key = missing_stats ? "profile_no_stats" : "profile_updating_error";
    const char* err_text = fpv_tg_loc(service, err_key);
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, callback->chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, callback->chat_id, err_text, NULL, NULL);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* text = advanced
      ? fpv_tg_build_adv_profile_text(service, &balance)
      : fpv_tg_build_profile_text(service, &balance, 0);
  if (!text) {
    fpv_tg_log(service, FPV_LOG_WARNING, "Profile build failed: no text.");
    const char* err_text = fpv_tg_loc(service, "profile_updating_error");
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, callback->chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, callback->chat_id, err_text, NULL, NULL);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* reply_markup = fpv_tg_build_profile_keyboard(service, advanced);
  if (callback->message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text,
        reply_markup);
  } else {
    fpv_tg_send_message(service, callback->chat_id, text, reply_markup, NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  if (status_id > 0) {
    fpv_tg_delete_message(service, callback->chat_id, status_id);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_send_sysinfo(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* text = fpv_tg_build_sysinfo_text(service, (uint64_t)chat_id);
  if (!text) {
    return;
  }
  fpv_tg_send_message(service, chat_id, text, NULL, NULL);
  fpv_free(text);
}

static void fpv_tg_send_all_settings(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  fpv_settings_t settings;
  if (!fpv_tg_load_settings(service, &settings)) {
    char* err = fpv_tg_loc_format(
        service,
        "cfg_not_found_err",
        (const char*[]){"_main.cfg"},
        1);
    fpv_tg_send_message(service, chat_id, err ? err : "", NULL, NULL);
    fpv_free(err);
    return;
  }

  char* text = NULL;
  size_t length = 0;
  size_t capacity = 0;

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_global"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  char* label = fpv_tg_format_toggle_label(service, "gs_autoraise", settings.auto_raise);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autoresponse", settings.auto_response);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autodelivery", settings.auto_delivery);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_nultidelivery", settings.multi_delivery);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autorestore", settings.auto_restore);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_autodisable", settings.auto_disable);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gs_old_msg_mode", settings.old_msg_mode);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n\n");
  fpv_free(label);
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_blacklist"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  label = fpv_tg_format_toggle_label(service, "bl_autodelivery", settings.block_delivery);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "bl_autoresponse", settings.block_response);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(
      service,
      "bl_new_msg_notifications",
      settings.block_new_message_notification);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(
      service,
      "bl_new_order_notifications",
      settings.block_new_order_notification);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(
      service,
      "bl_command_notifications",
      settings.block_command_notification);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n\n");
  fpv_free(label);
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_new_msg_view"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  label = fpv_tg_format_toggle_label(service, "mv_incl_my_msg", settings.include_my_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "mv_incl_fp_msg", settings.include_fp_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "mv_incl_bot_msg", settings.include_bot_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "mv_only_my_msg", settings.notify_only_my_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "mv_only_fp_msg", settings.notify_only_fp_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "mv_only_bot_msg", settings.notify_only_bot_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n\n");
  fpv_free(label);
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_greetings"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  label = fpv_tg_format_toggle_label(service, "gr_greetings", settings.greetings_send);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gr_cache_init_chats", settings.greetings_cache_init_chats);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  label = fpv_tg_format_toggle_label(service, "gr_ignore_sys_msgs", settings.greetings_ignore_system_messages);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  fpv_tg_append_code_value(&text, &length, &capacity, settings.greetings_text);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_order_confirm"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  label = fpv_tg_format_toggle_label(service, "oc_send_reply", settings.order_confirm_send_reply);
  fpv_tg_buffer_append_str(&text, &length, &capacity, label);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_free(label);
  fpv_tg_append_code_value(&text, &length, &capacity, settings.order_confirm_text);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "mm_review_reply"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  for (int i = 0; i < 5; i++) {
    const char* icon = fpv_tg_icon_toggle(settings.review_reply_enabled[i]);
    const char* stars =
        i == 0 ? "\xE2\xAD\x90" :
        i == 1 ? "\xE2\xAD\x90\xE2\xAD\x90" :
        i == 2 ? "\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90" :
        i == 3 ? "\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90" :
        "\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90\xE2\xAD\x90";
    char star_label[32];
    snprintf(star_label, sizeof(star_label), "%s %s", icon, stars);
    if (settings.review_reply_texts[i] && settings.review_reply_texts[i][0]) {
      char* escaped = fpv_tg_escape_html(settings.review_reply_texts[i]);
      char* line = fpv_tg_loc_format(
          service,
          "review_reply_text",
          (const char*[]){star_label, escaped ? escaped : ""},
          2);
      fpv_tg_buffer_append_str(&text, &length, &capacity, line);
      fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
      fpv_free(line);
      fpv_free(escaped);
    } else {
      char* line = fpv_tg_loc_format(
          service,
          "review_reply_empty",
          (const char*[]){star_label},
          1);
      fpv_tg_buffer_append_str(&text, &length, &capacity, line);
      fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
      fpv_free(line);
    }
  }
  fpv_tg_buffer_append_str(&text, &length, &capacity, "\n");
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);

  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "cmd_watermark"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  fpv_tg_append_code_value(&text, &length, &capacity, settings.watermark);
  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "cmd_language"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  fpv_tg_append_code_value(&text, &length, &capacity, settings.language);
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);
  fpv_settings_destroy(&settings);
}

static void fpv_tg_send_about(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* text = fpv_tg_loc_format(
      service,
      "about",
      (const char*[]){FPV_VERSION},
      1);
  if (!text) {
    return;
  }
  fpv_tg_send_message(service, chat_id, text, NULL, NULL);
  fpv_free(text);
}

static void fpv_tg_send_logs(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* path = fpv_tg_find_latest_log(service);
  if (!path) {
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "logfile_not_found"),
                        NULL, NULL);
    return;
  }
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "logfile_sending"),
                      NULL, NULL);
  if (!fpv_tg_send_document(service, chat_id, path, NULL, NULL)) {
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "logfile_error"),
                        NULL, NULL);
  }
  fpv_free(path);
}

static void fpv_tg_delete_logs(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  size_t deleted = 0;
  if (service->storage.logs_dir && service->storage.logs_dir[0]) {
#if defined(_WIN32)
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", service->storage.logs_dir);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);
    if (handle != INVALID_HANDLE_VALUE) {
      do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          continue;
        }
        if (fpv_tg_is_log_file(data.cFileName)) {
          continue;
        }
        char* full = fpv_path_join(service->storage.logs_dir, data.cFileName);
        if (full) {
          if (remove(full) == 0) {
            deleted++;
          }
          fpv_free(full);
        }
      } while (FindNextFileA(handle, &data));
      FindClose(handle);
    }
#else
    DIR* dir = opendir(service->storage.logs_dir);
    if (dir) {
      struct dirent* entry = NULL;
      while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
          continue;
        }
        if (fpv_tg_is_log_file(entry->d_name)) {
          continue;
        }
        char* full = fpv_path_join(service->storage.logs_dir, entry->d_name);
        if (full) {
          if (remove(full) == 0) {
            deleted++;
          }
          fpv_free(full);
        }
      }
      closedir(dir);
    }
#endif
  }
  char deleted_buf[32];
  snprintf(deleted_buf, sizeof(deleted_buf), "%zu", deleted);
  char* text = fpv_tg_loc_format(
      service,
      "logfile_deleted",
      (const char*[]){deleted_buf},
      1);
  if (!text) {
    return;
  }
  fpv_tg_send_message(service, chat_id, text, NULL, NULL);
  fpv_free(text);
}

static void fpv_tg_send_blacklist(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  fpv_string_list_t list;
  memset(&list, 0, sizeof(list));
  if (fpv_tg_load_blacklist(service, &list) != FPV_OK || list.count == 0) {
    fpv_string_list_destroy(&list);
    fpv_tg_send_message(service, chat_id,
                        fpv_tg_loc(service, "blacklist_empty"),
                        NULL, NULL);
    return;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  for (size_t i = 0; i < list.count; i++) {
    const char* item = list.items[i];
    if (!item) {
      continue;
    }
    char* escaped = fpv_tg_escape_html(item);
    if (length > 0) {
      fpv_tg_buffer_append(&buffer, &length, &capacity, ", ", 2);
    }
    fpv_tg_buffer_append(&buffer, &length, &capacity, "<code>", 6);
    fpv_tg_buffer_append(&buffer, &length, &capacity,
                         escaped ? escaped : "",
                         escaped ? strlen(escaped) : 0);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "</code>", 7);
    fpv_free(escaped);
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  fpv_tg_send_message(service, chat_id, buffer, NULL, NULL);
  fpv_free(buffer);
  fpv_string_list_destroy(&list);
}

static void fpv_tg_send_old_orders(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  int status_id = 0;
  fpv_tg_send_message(service, chat_id,
                      "\xD0\xA1\xD0\xBA\xD0\xB0\xD0\xBD\xD0\xB8\xD1\x80\xD1\x83\xD1\x8E \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD1\x8B (\xD1\x8D\xD1\x82\xD0\xBE \xD0\xBC\xD0\xBE\xD0\xB6\xD0\xB5\xD1\x82 \xD0\xB7\xD0\xB0\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C \xD0\xBA\xD0\xb0\xD0\xBA\xD0\xBE\xD0\xb5-\xD1\x82\xD0\xBE \xD0\xb2\xD1\x80\xD0\xb5\xD0\xbc\xD1\x8f)...",
                      NULL, &status_id);
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    const char* err_text =
        "\xE2\x9D\x8C \xD0\x9D\xD0\xB5 \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C \xD1\x81\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xBE\xD0%B2.";
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    return;
  }

  uint64_t now_ms = fpv_time_now_ms();
  const uint64_t day_ms = 86400000ULL;
  char* continue_from = NULL;
  bool ok = true;
  char* orders_text = NULL;
  size_t orders_len = 0;
  size_t orders_cap = 0;
  size_t old_count = 0;

  do {
    fpv_order_t** orders = NULL;
    size_t order_count = 0;
    char* next = NULL;
    fpv_funpay_error_t error;
    memset(&error, 0, sizeof(error));
    fpv_result_t result = fpv_funpay_account_get_orders_page(
        account,
        "paid",
        continue_from,
        &orders,
        &order_count,
        &next,
        &error);
    fpv_funpay_error_clear(&error);
    fpv_free(continue_from);
    continue_from = NULL;
    if (result != FPV_OK) {
      if (orders) {
        for (size_t i = 0; i < order_count; i++) {
          fpv_order_destroy(orders[i]);
        }
        fpv_free(orders);
      }
      fpv_free(next);
      ok = false;
      break;
    }
    for (size_t i = 0; i < order_count; i++) {
      fpv_order_t* order = orders[i];
      if (!order || !order->id || order->status != FPV_ORDER_PAID) {
        continue;
      }
      if (order->created_at_ms > 0 &&
          now_ms > order->created_at_ms &&
          now_ms - order->created_at_ms <= day_ms) {
        continue;
      }
      if (old_count > 0) {
        fpv_tg_buffer_append(&orders_text, &orders_len, &orders_cap, ", ", 2);
      }
      fpv_tg_buffer_append(&orders_text, &orders_len, &orders_cap,
                           order->id, strlen(order->id));
      old_count++;
    }
    if (orders) {
      for (size_t i = 0; i < order_count; i++) {
        fpv_order_destroy(orders[i]);
      }
      fpv_free(orders);
    }
    continue_from = next;
    if (continue_from && continue_from[0]) {
      fpv_tg_sleep_ms(1000);
    }
  } while (continue_from && continue_from[0]);
  fpv_free(continue_from);

  if (!ok) {
    const char* err_text =
        "\xE2\x9D\x8C \xD0\x9D\xD0\xB5 \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C \xD1\x81\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0%B7\xD0\xBE\xD0%B2.";
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, err_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, err_text, NULL, NULL);
    }
    fpv_free(orders_text);
    return;
  }

  if (old_count == 0) {
    const char* none_text =
        "\xE2\x9D\x8C \xD0\x9F\xD1\x80\xD0\xBE\xD1\x81\xD1\x80\xD0\xBE\xD1\x87\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8B\xD1\x85 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xb7\xD0\xBE\xD0\xb2 \xD0\xBD\xD0\xb5\xD1\x82.";
    if (status_id > 0) {
      fpv_tg_edit_message_text(service, chat_id, status_id, none_text, NULL);
    } else {
      fpv_tg_send_message(service, chat_id, none_text, NULL, NULL);
    }
    fpv_free(orders_text);
    return;
  }

  char* message_text = NULL;
  size_t msg_len = 0;
  size_t msg_cap = 0;
  const char* prefix =
      "\xD0\x97\xD0\xB4\xD1\x80\xD0\xB0\xD0\xB2\xD1\x81\xD1\x82\xD0\xB2\xD1\x83\xD0\xB9\xD1\x82\xD0\xB5!\n\n"
      "\xD0\x9F\xD1\x80\xD0\xBE\xD1\x88\xD1\x83 \xD0\xBF\xD0\xBE\xD0\xB4\xD1\x82\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB4\xD0\xB8\xD1\x82\xD1\x8C \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD1\x81\xD0\xBB\xD0\xb5\xD0\xb4\xD1\x83\xD1\x8e\xD1\x89\xD0\xb8\xD1\x85 \xD0\xb7\xD0\xb0\xD0\xba\xD0\xb0\xD0\xb7\xD0\xbe\xD0\xb2:\n";
  const char* suffix =
      "\n\n\xD0\x97\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xb5 \xD0\xB1\xD0\xBB\xD0\xb0\xD0\xb3\xD0\xBE\xD0\xb4\xD0\xb0\xD1\x80\xD1\x8E,\n"
      "\xD0\xA1 \xD1\x83\xD0\xb2\xD0\xb0\xD0\xb6\xD0\xb5\xD0\xbd\xD0\xb8\xD0\xb5\xD0\xBC.";
  fpv_tg_buffer_append(&message_text, &msg_len, &msg_cap, prefix, strlen(prefix));
  fpv_tg_buffer_append(&message_text, &msg_len, &msg_cap,
                       orders_text ? orders_text : "",
                       orders_text ? strlen(orders_text) : 0);
  fpv_tg_buffer_append(&message_text, &msg_len, &msg_cap, suffix, strlen(suffix));
  if (!message_text) {
    message_text = fpv_strdup("");
  }
  char* escaped = fpv_tg_escape_html(message_text);
  fpv_free(message_text);
  fpv_free(orders_text);
  if (!escaped) {
    return;
  }
  char* final = NULL;
  size_t final_len = 0;
  size_t final_cap = 0;
  fpv_tg_buffer_append(&final, &final_len, &final_cap, "<code>", 6);
  fpv_tg_buffer_append(&final, &final_len, &final_cap, escaped, strlen(escaped));
  fpv_tg_buffer_append(&final, &final_len, &final_cap, "</code>", 7);
  fpv_free(escaped);
  if (!final) {
    final = fpv_strdup("");
  }
  if (status_id > 0) {
    fpv_tg_edit_message_text(service, chat_id, status_id, final, NULL);
  } else {
    fpv_tg_send_message(service, chat_id, final, NULL, NULL);
  }
  fpv_free(final);
}

static void fpv_tg_change_cookie(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* args) {
  if (!service || chat_id == 0) {
    return;
  }
  if (!args || !args[0]) {
    fpv_tg_send_message(
        service,
        chat_id,
        "\xD0\x9A\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0 \xD0\xB2\xD0\xb2\xD0\xb5\xD0\xb4\xD0\xb5\xD0\xBD\xD0\xb0 \xD0\xBD\xD0\xb5 \xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD0\xb8\xD0\xbb\xD1\x8C\xD0\xBD\xD0\xBE! /change_cookie [golden_key]",
        NULL,
        NULL);
    return;
  }
  char* token = fpv_tg_trim_copy(args);
  if (!token) {
    return;
  }
  char* end = token;
  while (*end && !isspace((unsigned char)*end)) {
    end++;
  }
  *end = '\0';
  if (strlen(token) != 32) {
    fpv_tg_send_message(
        service,
        chat_id,
        "\xD0\x9D\xD0\xB5\xD0\xb2\xD0\xb5\xD1\x80\xD0\xbd\xD1\x8b\xD0\xb9 \xD1\x84\xD0\xbe\xD1\x80\xD0\xBC\xD0\xb0\xD1\x82 \xD1\x82\xD0\xBE\xD0\xBA\xD0\xb5\xD0\xbd\xD0\xb0. \xD0\x9F\xD0\xBE\xD0\xBF\xD1\x80\xD0\xBE\xD0\xb1\xD1\x83\xD0\xb9 \xD0\xb5\xD1\x89\xD0\xb5 \xD1\x80\xD0\xb0\xD0\xb7!",
        NULL,
        NULL);
    fpv_free(token);
    return;
  }
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (account) {
    fpv_funpay_account_set_golden_key(account, token);
    fpv_funpay_error_t error;
    memset(&error, 0, sizeof(error));
    fpv_funpay_account_refresh(account, &error);
    fpv_funpay_error_clear(&error);
  }
  fpv_tg_update_main_config_value(service, "FunPay", "golden_key", token);
  fpv_tg_send_message(
      service,
      chat_id,
      "\xE2\x9C\x85 \xD0\xA3\xD1\x81\xD0\xBF\xD0\xB5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xB8\xD0\xb7\xD0\xBC\xD0\xb5\xD0\xbd\xD0\xb5\xD0\xBD\xD0\xBE \xD0\xBF\xD0\xb5\xD1\x80\xD0\xb5\xD0\xb7\xD0\xb0\xD0\xBF\xD1\x83\xD1\x81\xD1\x82\xD0\xb8\xD1\x82\xD0\xb5 \xD0\xb1\xD0\xbe\xD1\x82\xD0\xb0.",
      NULL,
      NULL);
  fpv_free(token);
}

static void fpv_tg_open_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* reply_markup = fpv_tg_build_old_keyboard();
  fpv_tg_send_message(
      service,
      chat_id,
      "\xD0\x9A\xD0\xBB\xD0\xb0\xD0\xb2\xD0\xb8\xD0\xb0\xD1\x82\xD1\x83\xD1\x80\xD0\xb0 \xD0\xBF\xD0\xBE\xD1\x8f\xD0\xb2\xD0\xb8\xD0\xbb\xD0\xb0\xD1\x81\xD1\x8C!",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
}

static void fpv_tg_close_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* reply_markup = fpv_tg_keyboard_remove();
  fpv_tg_send_message(
      service,
      chat_id,
      "\xD0\x9A\xD0\xBB\xD0\xb0\xD0\xb2\xD0\xb8\xD0\xb0\xD1\x82\xD1\x83\xD1\x80\xD0\xb0 \xD1\x81\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD0\xb0!",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
}

static void fpv_tg_restart(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "restarting"),
                      NULL, NULL);
  if (service->restart_fn) {
    service->restart_fn(service->control_context);
  }
}

static void fpv_tg_power_off(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* reply_markup =
      fpv_tg_build_power_off_keyboard(service, service->instance_id, 0);
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "power_off_0"),
                      reply_markup,
                      NULL);
  fpv_free(reply_markup);
}

static char* fpv_tg_build_empty_inline_keyboard(void) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  return fpv_tg_keyboard_finalize(kb, false);
}

static const char* fpv_tg_power_off_state_key(int state) {
  switch (state) {
    case 1:
      return "power_off_1";
    case 2:
      return "power_off_2";
    case 3:
      return "power_off_3";
    case 4:
      return "power_off_4";
    case 5:
      return "power_off_5";
    case 6:
      return "power_off_6";
    default:
      return NULL;
  }
}

static void fpv_tg_cancel_shutdown_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_empty_inline_keyboard();
  const char* text = fpv_tg_loc(service, "power_off_cancelled");
  if (callback->message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(service, callback->chat_id, text ? text : "",
                        reply_markup, NULL);
  }
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

static void fpv_tg_handle_shutdown_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    int state,
    uint64_t instance_id) {
  if (!service || !callback) {
    return;
  }
  if (instance_id != service->instance_id) {
    fpv_tg_answer_callback(service, callback->id,
                           fpv_tg_loc(service, "power_off_error"), true);
    return;
  }
  const char* key = fpv_tg_power_off_state_key(state);
  if (!key) {
    fpv_tg_answer_callback(service, callback->id,
                           fpv_tg_loc(service, "power_off_error"), true);
    return;
  }
  char* reply_markup = NULL;
  if (state < 6) {
    reply_markup = fpv_tg_build_power_off_keyboard(service, instance_id, state);
  } else {
    reply_markup = fpv_tg_build_empty_inline_keyboard();
  }
  const char* text = fpv_tg_loc(service, key);
  if (callback->message_id > 0) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text ? text : "",
        reply_markup);
  } else {
    fpv_tg_send_message(service, callback->chat_id, text ? text : "",
                        reply_markup, NULL);
  }
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
  if (state == 6 && service->shutdown_fn) {
    service->shutdown_fn(service->control_context);
  }
}

static void fpv_tg_handle_message(fpv_telegram_service_t* service, fpv_tg_message_t* message) {
  if (!service || !message) {
    return;
  }
  bool is_private = message->chat_type && strcmp(message->chat_type, "private") == 0;
  bool is_authorized = fpv_tg_is_authorized(service, message->from_id);

  if (!message->reply_topic_created && (!is_private || is_authorized)) {
    fpv_tg_setup_default_notifications(service, message->chat_id);
  }

  if (is_private && !is_authorized) {
    if (!message->text || !message->text[0]) {
      return;
    }
    fpv_tg_attempt_t* attempt = fpv_tg_find_attempt(service, message->from_id);
    if (attempt && attempt->count >= 5) {
      return;
    }
    if (service->secret && strcmp(message->text, service->secret) == 0) {
      if (fpv_tg_add_authorized_user(service, message->from_id)) {
        fpv_tg_setup_default_notifications(service, message->chat_id);
      }
      fpv_tg_send_message(service, message->chat_id,
                          fpv_tg_loc(service, "access_granted"),
                          NULL, NULL);
      char id_buf[32];
      snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
      const char* uname = message->from_username ? message->from_username : "";
      fpv_tg_log_format(service, FPV_LOG_WARNING, "log_access_granted",
                        (const char*[]){uname, id_buf}, 2);
      return;
    }
    fpv_tg_increment_attempt(service, message->from_id);
    const char* uname = message->from_username ? message->from_username : "";
    char* text = fpv_tg_loc_format(service, "access_denied",
                                   (const char*[]){uname}, 1);
    fpv_tg_send_message(service, message->chat_id, text ? text : "", NULL, NULL);
    fpv_free(text);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
    fpv_tg_log_format(service, FPV_LOG_WARNING, "log_access_attempt",
                      (const char*[]){uname, id_buf}, 2);
    return;
  }
  if (!is_authorized) {
    return;
  }

  if (fpv_tg_handle_upload_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_misc_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_ar_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_ad_state(service, message)) {
    return;
  }
  if (fpv_tg_handle_template_state(service, message)) {
    return;
  }

  if (!message->text || !message->text[0]) {
    return;
  }

  const char* command = NULL;
  const char* args = NULL;
  char command_buf[64];
  if (fpv_tg_parse_command(message->text, command_buf, sizeof(command_buf), &args)) {
    fpv_tg_trim_lower(command_buf);
    command = command_buf;
  } else {
    if (strcmp(message->text,
               "\xF0\x9F\x93\x8B \xD0\x9B\xD0\xBE\xD0\xB3\xD0\xB8 \xF0\x9F\x93\x8B") == 0) {
      command = "logs";
    } else if (strcmp(message->text,
                      "\xE2\x9A\x99\xEF\xB8\x8F \xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xb9\xD0\xBA\xD0\xb8 \xE2\x9A\x99\xEF\xB8\x8F") == 0) {
      command = "menu";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x93\x88 \xD0\xA1\xD0\xb8\xD1\x81\xD1\x82\xD0\xb5\xD0\xBC\xD0\xb0 \xF0\x9F\x93\x88") == 0) {
      command = "sys";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x94\x84 \xD0\x9F\xD0\xb5\xD1\x80\xD0\xb5\xD0\xb7\xD0\xb0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA \xF0\x9F\x94\x84") == 0) {
      command = "restart";
    } else if (strcmp(message->text,
                      "\xF0\x9F\x94\x8C \xD0\x9E\xD1\x82\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xb5\xD0\xBD\xD0\xb8\xD0\xb5 \xF0\x9F\x94\x8C") == 0) {
      command = "power_off";
    } else if (strcmp(message->text,
                      "\xE2\x9D\x8C \xD0\x97\xD0\xb0\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C \xE2\x9D\x8C") == 0) {
      fpv_tg_close_old_keyboard(service, message->chat_id);
      return;
    }
  }

  if (!command) {
    return;
  }

  if (strcmp(command, "menu") == 0) {
    fpv_tg_send_menu(service, message->chat_id);
    return;
  }
  if (strcmp(command, "all") == 0) {
    fpv_tg_send_all_settings(service, message->chat_id);
    return;
  }
  if (strcmp(command, "profile") == 0) {
    fpv_tg_send_profile(service, message->chat_id);
    return;
  }
  if (strcmp(command, "test_lot") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_MANUAL_AD_TEST,
                        "create_test_ad_key");
    return;
  }
  if (strcmp(command, "upload_img") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_UPLOAD_IMAGE,
                        "send_img");
    return;
  }
  if (strcmp(command, "ban") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_BAN,
                        "act_blacklist");
    return;
  }
  if (strcmp(command, "unban") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_UNBAN,
                        "act_unban");
    return;
  }
  if (strcmp(command, "black_list") == 0) {
    fpv_tg_send_blacklist(service, message->chat_id);
    return;
  }
  if (strcmp(command, "watermark") == 0) {
    fpv_tg_prompt_state(service, message,
                        FPV_TG_STATE_EDIT_WATERMARK,
                        "act_edit_watermark");
    return;
  }
  if (strcmp(command, "logs") == 0) {
    fpv_tg_send_logs(service, message->chat_id);
    return;
  }
  if (strcmp(command, "del_logs") == 0) {
    fpv_tg_delete_logs(service, message->chat_id);
    return;
  }
  if (strcmp(command, "about") == 0) {
    fpv_tg_send_about(service, message->chat_id);
    return;
  }
  if (strcmp(command, "sys") == 0) {
    fpv_tg_send_sysinfo(service, message->chat_id);
    return;
  }
  if (strcmp(command, "old_orders") == 0) {
    fpv_tg_send_old_orders(service, message->chat_id);
    return;
  }
  if (strcmp(command, "keyboard") == 0) {
    fpv_tg_open_old_keyboard(service, message->chat_id);
    return;
  }
  if (strcmp(command, "change_cookie") == 0) {
    fpv_tg_change_cookie(service, message->chat_id, args);
    return;
  }
  if (strcmp(command, "restart") == 0) {
    fpv_tg_restart(service, message->chat_id);
    return;
  }
  if (strcmp(command, "power_off") == 0) {
    fpv_tg_power_off(service, message->chat_id);
    return;
  }
}

static void fpv_tg_handle_callback(fpv_telegram_service_t* service, fpv_tg_callback_t* callback) {
  if (!service || !callback) {
    return;
  }
  if (!fpv_tg_is_authorized(service, callback->from_id)) {
    char user_buf[32];
    char chat_buf[32];
    snprintf(user_buf, sizeof(user_buf), "%" PRId64, callback->from_id);
    snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, callback->chat_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    const char* chat_name = callback->chat_username ? callback->chat_username : "";
    fpv_tg_log_format(service, FPV_LOG_WARNING, "log_click_attempt",
                      (const char*[]){uname, user_buf, chat_name, chat_buf}, 4);
    return;
  }

  if (!callback->data) {
    return;
  }
  if (strcmp(callback->data, fpv_tg_cbt_upload_products_file) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_PRODUCTS_FILE,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD1\x84\xD0\xb0\xD0\xb9\xD0\xBB \xD1\x81 \xD1\x82\xD0\xBE\xD0\xb2\xD0\xb0\xD1\x80\xD0\xb0\xD0\xBC\xD0\xb8.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_main_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_MAIN_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBE\xD1\x81\xD0\xBD\xD0\xBE\xD0\xb2\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_auto_response_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE\xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD1\x87\xD0\xb8\xD0\xBA\xD0\xb0.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cb_upload_auto_delivery_config) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG,
        "\xD0\x9E\xD1\x82\xD0\xBF\xD1\x80\xD0\xb0\xD0\xb2\xD1\x8C\xD1\x82\xD0\xb5 \xD0\xBC\xD0\xBD\xD0\xb5 \xD0\xBA\xD0\xBE\xD0\xBD\xD1\x84\xD0\xb8\xD0\xb3 \xD0\xb0\xD0\xb2\xD1\x82\xD0\xBE-\xD0\xb2\xD1\x8B\xD0\xb4\xD0\xb0\xD1\x87\xD0\xb8.");
    return;
  }
  if (strcmp(callback->data, fpv_tg_cbt_upload_image) == 0) {
    fpv_tg_prompt_state_from_callback(
        service,
        callback,
        FPV_TG_STATE_UPLOAD_IMAGE,
        fpv_tg_loc(service, "send_img"));
    return;
  }

  char* data_copy = fpv_strdup(callback->data);
  if (!data_copy) {
    return;
  }
  char* tokens[10] = {0};
  size_t token_count = fpv_tg_split_tokens(data_copy, tokens, 10);
  if (token_count > 0) {
    if (strcmp(tokens[0], fpv_tg_cbt_main) == 0) {
      fpv_tg_open_main_sections(service, callback, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_main2) == 0) {
      fpv_tg_open_main_sections(service, callback, true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_empty) == 0) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_param_disabled) == 0) {
      fpv_tg_answer_callback(service, callback->id,
                             fpv_tg_loc(service, "param_disabled"), true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_clear_state) == 0) {
      fpv_tg_user_state_t* state =
          fpv_tg_get_state(service, callback->chat_id, callback->from_id);
      int prompt_id = state ? state->message_id : 0;
      fpv_tg_clear_state(service, callback->chat_id, callback->from_id, false);
      if (prompt_id > 0) {
        fpv_tg_delete_message(service, callback->chat_id, prompt_id);
      } else if (callback->message_id > 0) {
        fpv_tg_delete_message(service, callback->chat_id, callback->message_id);
      }
      fpv_tg_answer_callback(service, callback->id, NULL, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_old_help) == 0) {
      fpv_tg_handle_old_help(service, callback);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_cancel_shutdown) == 0) {
      fpv_tg_cancel_shutdown_callback(service, callback);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_shutdown) == 0) {
      size_t state = 0;
      uint64_t instance_id = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &state) &&
          state <= (size_t)INT_MAX &&
          fpv_tg_parse_uint64_value(tokens[2], &instance_id)) {
        fpv_tg_handle_shutdown_callback(service, callback, (int)state, instance_id);
      } else {
        fpv_tg_answer_callback(service, callback->id,
                               fpv_tg_loc(service, "power_off_error"), true);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_profile) == 0 ||
        strcmp(tokens[0], fpv_tg_cbt_update_profile) == 0) {
      fpv_tg_update_profile_callback(service, callback, false);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_adv_profile) == 0) {
      fpv_tg_update_profile_callback(service, callback, true);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_config_loader) == 0) {
      fpv_tg_open_settings_category(service, callback, "configs");
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_category) == 0) {
      if (token_count > 1) {
        fpv_tg_open_settings_category(service, callback, tokens[1]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_template_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_templates_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_template_list_ans) == 0) {
      size_t offset = 0;
      size_t prev_page = 0;
      uint64_t chat_id = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &offset) &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        char extra_buf[256];
        extra_buf[0] = '\0';
        if (token_count > 5) {
          size_t used = 0;
          for (size_t i = 5; i < token_count; i++) {
            int written = snprintf(
                extra_buf + used,
                sizeof(extra_buf) - used,
                ":%s",
                tokens[i] ? tokens[i] : "");
            if (written < 0) {
              break;
            }
            used += (size_t)written;
            if (used >= sizeof(extra_buf)) {
              extra_buf[sizeof(extra_buf) - 1] = '\0';
              break;
            }
          }
        }
        const char* extra = extra_buf[0] ? extra_buf : NULL;
        fpv_tg_open_templates_list_ans(
            service,
            callback,
            offset,
            chat_id,
            tokens[3],
            (int)prev_page,
            extra);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_template) == 0) {
      size_t template_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_template_editor(service, callback, template_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_template) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_prompt_add_template(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_template) == 0) {
      size_t template_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_template(service, callback, template_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_send_template) == 0) {
      size_t template_index = 0;
      uint64_t chat_id = 0;
      size_t prev_page = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &template_index) &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        const char* extra1 = token_count > 5 ? tokens[5] : NULL;
        const char* extra2 = token_count > 6 ? tokens[6] : NULL;
        fpv_tg_send_template(
            service,
            callback,
            template_index,
            chat_id,
            tokens[3],
            (int)prev_page,
            extra1,
            extra2);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_send_fp_message) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 1 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.chat_id = chat_id;
        data.username = token_count > 2 ? tokens[2] : NULL;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_SEND_FP_MESSAGE,
            fpv_tg_loc(service, "enter_msg_text"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_back_to_reply) == 0) {
      uint64_t chat_id = 0;
      bool again = false;
      bool extend = false;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id) &&
          fpv_tg_parse_bool(tokens[3], &again)) {
        if (token_count > 4) {
          fpv_tg_parse_bool(tokens[4], &extend);
        }
        fpv_tg_update_reply_keyboard(
            service,
            callback,
            chat_id,
            token_count > 2 ? tokens[2] : NULL,
            again,
            extend);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_back_to_order) == 0) {
      uint64_t chat_id = 0;
      bool no_refund = false;
      if (token_count > 4 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_parse_bool(tokens[4], &no_refund);
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[3],
            chat_id,
            tokens[2],
            false,
            no_refund);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_extend_chat) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 1 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_extend_chat_history(
            service,
            callback,
            chat_id,
            token_count > 2 ? tokens[2] : NULL);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_request_refund) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3],
            true,
            false);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_refund_cancelled) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_update_order_keyboard(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3],
            false,
            false);
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_refund_confirmed) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 3 &&
          fpv_tg_parse_uint64_value(tokens[2], &chat_id)) {
        fpv_tg_confirm_refund(
            service,
            callback,
            tokens[1],
            chat_id,
            tokens[3]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch) == 0) {
      if (token_count > 2) {
        fpv_tg_toggle_setting(service, callback, tokens[1], tokens[2]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch_tg) == 0) {
      uint64_t chat_id = 0;
      if (token_count > 2 &&
          fpv_tg_parse_uint64_value(tokens[1], &chat_id)) {
        fpv_tg_toggle_notification(service, callback, chat_id, tokens[2]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_lang) == 0) {
      if (token_count > 1) {
        fpv_tg_switch_language(service, callback, tokens[1]);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_cmd_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_ar_commands_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_cmd) == 0) {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_ADD_CMD,
          fpv_tg_loc(service, "ar_enter_new_cmd"));
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_ar_command_editor(
            service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd_response) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ar_edit_text(
            service,
            callback,
            FPV_TG_STATE_EDIT_CMD_RESPONSE,
            "v_edit_response_text",
            command_index,
            offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_cmd_notification) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ar_edit_text(
            service,
            callback,
            FPV_TG_STATE_EDIT_CMD_NOTIFICATION,
            "v_edit_notification_text",
            command_index,
            offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_switch_cmd_notification) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_toggle_ar_notification(
            service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_cmd) == 0) {
      size_t command_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &command_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_ar_command(service, callback, command_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_ad_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_ad_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_fp_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_funpay_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_ad_lot_manual) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_state_data_t data;
      memset(&data, 0, sizeof(data));
      data.offset = (int)offset;
      fpv_tg_prompt_state_with_data(
          service,
          callback,
          FPV_TG_STATE_ADD_AD_TO_LOT_MANUAL,
          fpv_tg_loc(service, "copy_lot_name"),
          &data);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_ad_lot) == 0) {
      size_t fp_lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &fp_lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_add_ad_lot_from_funpay(service, callback, fp_lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_ad_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_edit_ad_lot(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_lot_text) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_prompt_ad_edit_text(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_bind_products) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.lot_index = (int)lot_index;
        data.offset = (int)offset;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_BIND_PRODUCTS_FILE,
            fpv_tg_loc(service, "ad_link_gf"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_switch_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 3 &&
          fpv_tg_parse_size_value(tokens[2], &lot_index) &&
          fpv_tg_parse_size_value(tokens[3], &offset)) {
        fpv_tg_toggle_lot_setting(service, callback, tokens[1], lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_test_auto_delivery) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_create_delivery_test(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_del_ad_lot) == 0) {
      size_t lot_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &lot_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_delete_ad_lot(service, callback, lot_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_update_funpay_lots) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_update_funpay_lots_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_products_list) == 0) {
      size_t offset = 0;
      if (token_count > 1) {
        fpv_tg_parse_size_value(tokens[1], &offset);
      }
      fpv_tg_open_products_files_list(service, callback, offset);
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_edit_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_open_products_file_editor(service, callback, file_index, offset);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_create_products_file) == 0) {
      fpv_tg_prompt_state_from_callback(
          service,
          callback,
          FPV_TG_STATE_CREATE_PRODUCTS_FILE,
          fpv_tg_loc(service, "act_create_gf"));
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cbt_add_products) == 0) {
      size_t file_index = 0;
      size_t element_index = 0;
      size_t offset = 0;
      size_t prev_page = 0;
      if (token_count > 4 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &element_index) &&
          fpv_tg_parse_size_value(tokens[3], &offset) &&
          fpv_tg_parse_size_value(tokens[4], &prev_page)) {
        fpv_tg_state_data_t data;
        memset(&data, 0, sizeof(data));
        data.file_index = (int)file_index;
        data.element_index = (int)element_index;
        data.offset = (int)offset;
        data.previous_page = (int)prev_page;
        fpv_tg_prompt_state_with_data(
            service,
            callback,
            FPV_TG_STATE_ADD_PRODUCTS_TO_FILE,
            fpv_tg_loc(service, "gf_send_new_goods"),
            &data);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_download_products_file) == 0) {
      size_t file_index = 0;
      if (token_count > 1 && fpv_tg_parse_size_value(tokens[1], &file_index)) {
        fpv_tg_send_products_file(service, callback, file_index);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_delete_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_confirm_delete_products_file(service, callback, file_index, offset, false);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
    if (strcmp(tokens[0], fpv_tg_cb_confirm_delete_products_file) == 0) {
      size_t file_index = 0;
      size_t offset = 0;
      if (token_count > 2 &&
          fpv_tg_parse_size_value(tokens[1], &file_index) &&
          fpv_tg_parse_size_value(tokens[2], &offset)) {
        fpv_tg_confirm_delete_products_file(service, callback, file_index, offset, true);
      } else {
        fpv_tg_answer_callback(service, callback->id, NULL, false);
      }
      fpv_free(data_copy);
      return;
    }
  }
  if (callback->data && callback->data[0]) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Unhandled callback: %s", callback->data);
    fpv_tg_log(service, FPV_LOG_WARNING, msg);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
  fpv_free(data_copy);
}

static void* fpv_tg_thread_main(void* context) {
  fpv_telegram_service_t* service = (fpv_telegram_service_t*)context;
  if (!service) {
    return NULL;
  }

  fpv_tg_log(service, FPV_LOG_INFO, "Telegram polling started.");
  fpv_tg_send_notification(service, fpv_tg_loc(service, "bot_started"),
                           NULL, "1", NULL, 0, false, true);

  while (service->running) {
    char* body = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_tg_buffer_append(&body, &length, &capacity, "{\"timeout\":25", 13);
    if (service->update_offset > 0) {
      char offset_buf[32];
      snprintf(offset_buf, sizeof(offset_buf), ",\"offset\":%" PRIu64, service->update_offset);
      fpv_tg_buffer_append(&body, &length, &capacity, offset_buf, strlen(offset_buf));
    }
    fpv_tg_buffer_append(&body, &length, &capacity,
                         ",\"allowed_updates\":[\"message\",\"callback_query\"]}",
                         55);

    fpv_json_value_t* root = NULL;
    fpv_result_t req = fpv_tg_api_request(
        service, "POST", "getUpdates", "application/json",
        body, strlen(body), &root);
    fpv_free(body);
    if (req != FPV_OK || !root) {
      fpv_tg_log_poll_error(service, req != FPV_OK ? req : FPV_ERR_PARSE);
      fpv_json_destroy(root);
      fpv_tg_sleep_ms(1500);
      continue;
    }
    service->last_poll_error = FPV_OK;
    service->last_poll_error_ms = 0;
    const fpv_json_value_t* result = fpv_json_object_get(root, "result");
    if (!result || !fpv_json_is_type(result, FPV_JSON_ARRAY)) {
      fpv_json_destroy(root);
      continue;
    }
    size_t count = fpv_json_array_size(result);
    for (size_t i = 0; i < count; i++) {
      const fpv_json_value_t* update = fpv_json_array_get(result, i);
      if (!update) {
        continue;
      }
      int64_t update_id = 0;
      fpv_json_number_to_int64(fpv_json_object_get(update, "update_id"), &update_id);
      if (update_id >= 0) {
        service->update_offset = (uint64_t)update_id + 1;
      }
      const fpv_json_value_t* message_val = fpv_json_object_get(update, "message");
      if (message_val) {
        fpv_tg_message_t message;
        if (fpv_tg_parse_message(message_val, &message)) {
          fpv_tg_handle_message(service, &message);
          fpv_tg_message_clear(&message);
        }
        continue;
      }
      const fpv_json_value_t* cb_val = fpv_json_object_get(update, "callback_query");
      if (cb_val) {
        fpv_tg_callback_t callback;
        if (fpv_tg_parse_callback(cb_val, &callback)) {
          fpv_tg_handle_callback(service, &callback);
          fpv_tg_callback_clear(&callback);
        }
      }
    }
    fpv_json_destroy(root);
  }
  return NULL;
}
