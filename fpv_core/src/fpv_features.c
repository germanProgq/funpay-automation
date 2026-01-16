/* FunPay Vertex phase 4 feature engine implementation. */

#include "fpv_features.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "fpv_cache.h"
#include "fpv_fs.h"
#include "fpv_json.h"
#include "fpv_localization.h"
#include "fpv_platform.h"
#include "fpv_products.h"
#include "fpv_telegram.h"
#include "fpv_string.h"
#include "fpv_time.h"

typedef struct fpv_raise_entry {
  uint64_t category_id;
  uint64_t next_raise_ms;
} fpv_raise_entry_t;

typedef struct fpv_feature_flags {
  bool auto_raise;
  bool auto_response;
  bool auto_delivery;
  bool multi_delivery;
  bool auto_restore;
  bool auto_disable;
  bool old_msg_mode;
  bool block_delivery;
  bool block_response;
  bool block_new_message_notification;
  bool block_new_order_notification;
  bool block_command_notification;
  bool include_my_messages;
  bool include_fp_messages;
  bool include_bot_messages;
  bool notify_only_my_messages;
  bool notify_only_fp_messages;
  bool notify_only_bot_messages;
  bool greetings_cache_init_chats;
  bool greetings_ignore_system_messages;
  bool greetings_send;
  char* greetings_text;
  bool order_confirm_send_reply;
  char* order_confirm_text;
  bool review_reply_enabled[5];
  char* review_reply_texts[5];
  uint32_t requests_delay_ms;
  char* watermark;
  char* language;
} fpv_feature_flags_t;

struct fpv_feature_state {
  fpv_feature_flags_t flags;
  fpv_auto_response_config_t auto_response;
  fpv_auto_delivery_config_t auto_delivery;
  fpv_mutex_t config_mutex;
  fpv_localizer_t* localizer;
  fpv_string_list_t blacklist;
  uint64_t* old_users;
  size_t old_user_count;
  char* blacklist_path;
  char* old_users_path;
  char* adv_profile_path;
  char* products_dir;
  fpv_funpay_account_t* account;
  fpv_funpay_runner_t* runner;
  fpv_scheduler_t* scheduler;
  fpv_logger_t* logger;
  fpv_event_bus_t* bus;
  fpv_telegram_service_t* telegram;
  bool background_scheduled;
  fpv_raise_entry_t* raise_entries;
  size_t raise_count;
  fpv_mutex_t lot_update_mutex;
  bool lot_update_running;
  bool lot_update_pending;
  char* last_lot_update_tag;
  char* pending_lot_update_tag;
};

typedef enum fpv_message_entity_type {
  FPV_ENTITY_TEXT = 0,
  FPV_ENTITY_IMAGE = 1,
  FPV_ENTITY_SLEEP = 2
} fpv_message_entity_type_t;

typedef struct fpv_message_entity {
  fpv_message_entity_type_t type;
  char* text;
  uint64_t image_id;
  double sleep_seconds;
} fpv_message_entity_t;

typedef struct fpv_delivery_notice {
  char* order_id;
  char* buyer_username;
  char* delivery_text;
  int goods_left;
} fpv_delivery_notice_t;

typedef struct fpv_send_task {
  fpv_feature_state_t* state;
  uint64_t chat_id;
  char* chat_name;
  char* message_text;
  bool add_watermark;
  char** products;
  size_t products_count;
  char* products_path;
  bool restore_on_failure;
  bool update_lot_states;
  char* lot_update_tag;
  fpv_delivery_notice_t* delivery_notice;
} fpv_send_task_t;

typedef struct fpv_lot_update_task {
  fpv_feature_state_t* state;
  char* runner_tag;
} fpv_lot_update_task_t;

static void fpv_features_queue_lot_update(
    fpv_feature_state_t* state,
    const char* runner_tag);

static char* fpv_format_order_detail_text(
    const fpv_funpay_order_detail_t* detail,
    const char* text);

static void fpv_features_handle_order(
    fpv_feature_state_t* state,
    const fpv_order_t* order,
    const char* runner_tag);

static void fpv_sleep_ms(uint32_t delay_ms) {
#if defined(_WIN32)
  Sleep(delay_ms);
#else
  struct timespec ts;
  ts.tv_sec = delay_ms / 1000U;
  ts.tv_nsec = (long)(delay_ms % 1000U) * 1000000L;
  nanosleep(&ts, NULL);
#endif
}

static void fpv_features_log(
    fpv_feature_state_t* state,
    fpv_log_level_t level,
    const char* message) {
  if (!state || !message || !state->logger) {
    return;
  }
  fpv_logger_log(state->logger, level, "features", message, fpv_time_now_ms());
}

static char* fpv_features_format_delivery_error(
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

static void fpv_features_notify_delivery_error(
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

static fpv_result_t fpv_features_apply_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const char* locales_dir) {
  if (!state || !settings) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* watermark = settings->watermark ? fpv_strdup(settings->watermark) : NULL;
  char* language = settings->language ? fpv_strdup(settings->language) : NULL;
  char* greetings_text =
      settings->greetings_text ? fpv_strdup(settings->greetings_text) : NULL;
  char* order_confirm_text =
      settings->order_confirm_text ? fpv_strdup(settings->order_confirm_text)
                                   : NULL;
  char* review_texts[5] = {0};
  for (size_t i = 0; i < 5; i++) {
    if (settings->review_reply_texts[i]) {
      review_texts[i] = fpv_strdup(settings->review_reply_texts[i]);
      if (!review_texts[i]) {
        for (size_t j = 0; j < i; j++) {
          fpv_free(review_texts[j]);
        }
        fpv_free(watermark);
        fpv_free(language);
        fpv_free(greetings_text);
        fpv_free(order_confirm_text);
        return FPV_ERR_OUT_OF_MEMORY;
      }
    }
  }

  if ((settings->watermark && !watermark) ||
      (settings->language && !language) ||
      (settings->greetings_text && !greetings_text) ||
      (settings->order_confirm_text && !order_confirm_text)) {
    fpv_free(watermark);
    fpv_free(language);
    fpv_free(greetings_text);
    fpv_free(order_confirm_text);
    for (size_t i = 0; i < 5; i++) {
      fpv_free(review_texts[i]);
    }
    return FPV_ERR_OUT_OF_MEMORY;
  }

  state->flags.auto_raise = settings->auto_raise;
  state->flags.auto_response = settings->auto_response;
  state->flags.auto_delivery = settings->auto_delivery;
  state->flags.multi_delivery = settings->multi_delivery;
  state->flags.auto_restore = settings->auto_restore;
  state->flags.auto_disable = settings->auto_disable;
  state->flags.old_msg_mode = settings->old_msg_mode;
  state->flags.block_delivery = settings->block_delivery;
  state->flags.block_response = settings->block_response;
  state->flags.block_new_message_notification =
      settings->block_new_message_notification;
  state->flags.block_new_order_notification =
      settings->block_new_order_notification;
  state->flags.block_command_notification =
      settings->block_command_notification;
  state->flags.include_my_messages = settings->include_my_messages;
  state->flags.include_fp_messages = settings->include_fp_messages;
  state->flags.include_bot_messages = settings->include_bot_messages;
  state->flags.notify_only_my_messages = settings->notify_only_my_messages;
  state->flags.notify_only_fp_messages = settings->notify_only_fp_messages;
  state->flags.notify_only_bot_messages = settings->notify_only_bot_messages;
  state->flags.greetings_cache_init_chats =
      settings->greetings_cache_init_chats;
  state->flags.greetings_ignore_system_messages =
      settings->greetings_ignore_system_messages;
  state->flags.greetings_send = settings->greetings_send;
  state->flags.order_confirm_send_reply =
      settings->order_confirm_send_reply;
  for (size_t i = 0; i < 5; i++) {
    state->flags.review_reply_enabled[i] = settings->review_reply_enabled[i];
  }
  state->flags.requests_delay_ms = settings->requests_delay_ms;

  fpv_free(state->flags.watermark);
  fpv_free(state->flags.language);
  fpv_free(state->flags.greetings_text);
  fpv_free(state->flags.order_confirm_text);
  for (size_t i = 0; i < 5; i++) {
    fpv_free(state->flags.review_reply_texts[i]);
  }

  state->flags.watermark = watermark;
  state->flags.language = language;
  state->flags.greetings_text = greetings_text;
  state->flags.order_confirm_text = order_confirm_text;
  for (size_t i = 0; i < 5; i++) {
    state->flags.review_reply_texts[i] = review_texts[i];
  }

  const char* lang =
      state->flags.language ? state->flags.language : "ru";
  fpv_localizer_destroy(state->localizer);
  state->localizer = fpv_localizer_create(locales_dir, lang, "ru");
  if (!state->localizer) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

static bool fpv_buffer_append(
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

static bool fpv_buffer_append_char(
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

static char* fpv_trim_copy(const char* value) {
  if (!value) {
    return NULL;
  }
  const char* start = value;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  return fpv_strdup_n(start, (size_t)(end - start));
}

static char* fpv_lower_ascii_copy(const char* value) {
  if (!value) {
    return NULL;
  }
  size_t length = strlen(value);
  char* copy = (char*)malloc(length + 1);
  if (!copy) {
    return NULL;
  }
  for (size_t i = 0; i < length; i++) {
    char ch = value[i];
    if (ch >= 'A' && ch <= 'Z') {
      ch = (char)(ch + ('a' - 'A'));
    }
    copy[i] = ch;
  }
  copy[length] = '\0';
  return copy;
}

static bool fpv_list_contains_string(
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

static bool fpv_list_contains_u64(
    const uint64_t* values,
    size_t count,
    uint64_t value) {
  for (size_t i = 0; i < count; i++) {
    if (values[i] == value) {
      return true;
    }
  }
  return false;
}

typedef enum fpv_features_system_type {
  FPV_SYSTEM_NONE = 0,
  FPV_SYSTEM_ORDER_CONFIRMED = 1,
  FPV_SYSTEM_ORDER_CONFIRMED_ADMIN = 2,
  FPV_SYSTEM_NEW_FEEDBACK = 3,
  FPV_SYSTEM_FEEDBACK_CHANGED = 4,
  FPV_SYSTEM_ORDER_REOPENED = 5,
  FPV_SYSTEM_REFUND = 6,
  FPV_SYSTEM_REFUND_ADMIN = 7,
  FPV_SYSTEM_PARTIAL_REFUND = 8
} fpv_features_system_type_t;

static fpv_features_system_type_t fpv_features_system_type(
    const char* text) {
  if (!text || !text[0]) {
    return FPV_SYSTEM_NONE;
  }
  if (strstr(text, "подтвердил успешное выполнение заказа #")) {
    if (strstr(text, "Администратор")) {
      return FPV_SYSTEM_ORDER_CONFIRMED_ADMIN;
    }
    return FPV_SYSTEM_ORDER_CONFIRMED;
  }
  if (strstr(text, "написал отзыв к заказу #")) {
    return FPV_SYSTEM_NEW_FEEDBACK;
  }
  if (strstr(text, "изменил отзыв к заказу #")) {
    return FPV_SYSTEM_FEEDBACK_CHANGED;
  }
  if (strstr(text, "открыт повторно")) {
    return FPV_SYSTEM_ORDER_REOPENED;
  }
  if (strstr(text, "вернул деньги покупателю") &&
      strstr(text, "по заказу #")) {
    if (strstr(text, "Администратор")) {
      return FPV_SYSTEM_REFUND_ADMIN;
    }
    return FPV_SYSTEM_REFUND;
  }
  if (strstr(text, "Часть средств по заказу #")) {
    return FPV_SYSTEM_PARTIAL_REFUND;
  }
  return FPV_SYSTEM_NONE;
}

static const char* fpv_features_delivery_test_prefix =
    "!\xD0\xB0\xD0\xB2\xD1\x82\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0";
static const char* fpv_features_delivery_test_subcategory =
    "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0, "
    "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82";

static char* fpv_features_extract_order_id(const char* text) {
  if (!text) {
    return NULL;
  }
  const char* ptr = text;
  while ((ptr = strchr(ptr, '#')) != NULL) {
    bool valid = true;
    for (size_t i = 1; i <= 8; i++) {
      if (!ptr[i] || !isalnum((unsigned char)ptr[i])) {
        valid = false;
        break;
      }
    }
    if (valid) {
      return fpv_strdup_n(ptr + 1, 8);
    }
    ptr++;
  }
  return NULL;
}

typedef struct fpv_adv_profile_entry {
  char* order_id;
  uint64_t time_sec;
  double price;
} fpv_adv_profile_entry_t;

static void fpv_adv_profile_entries_destroy(
    fpv_adv_profile_entry_t* entries,
    size_t count) {
  if (!entries) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(entries[i].order_id);
  }
  fpv_free(entries);
}

static char* fpv_features_read_file(const char* path, size_t* out_size) {
  if (out_size) {
    *out_size = 0;
  }
  if (!path) {
    return NULL;
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
  size_t read_size = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read_size] = '\0';
  if (out_size) {
    *out_size = read_size;
  }
  return buffer;
}

static bool fpv_features_write_file(const char* path, const char* data) {
  if (!path || !data) {
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  size_t len = strlen(data);
  bool ok = fwrite(data, 1, len, file) == len;
  fclose(file);
  return ok;
}

static fpv_adv_profile_entry_t* fpv_adv_profile_load(
    const char* path,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!path || !fpv_fs_exists(path)) {
    return NULL;
  }
  size_t size = 0;
  char* content = fpv_features_read_file(path, &size);
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
  fpv_adv_profile_entry_t* entries =
      (fpv_adv_profile_entry_t*)calloc(count, sizeof(*entries));
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
    const fpv_json_value_t* time_val =
        fpv_json_object_get(value, "time");
    const fpv_json_value_t* price_val =
        fpv_json_object_get(value, "price");
    uint64_t time_sec = 0;
    double price = 0.0;
    if (!fpv_json_number_to_uint64(time_val, &time_sec) ||
        !fpv_json_number_to_double(price_val, &price)) {
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

static bool fpv_adv_profile_save(
    const char* path,
    const fpv_adv_profile_entry_t* entries,
    size_t count) {
  if (!path) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_buffer_append(&buffer, &length, &capacity, "{", 1);
  for (size_t i = 0; i < count; i++) {
    if (!entries[i].order_id) {
      continue;
    }
    if (length > 1) {
      fpv_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    fpv_buffer_append(&buffer, &length, &capacity, "\"", 1);
    fpv_buffer_append(&buffer, &length, &capacity,
                      entries[i].order_id, strlen(entries[i].order_id));
    fpv_buffer_append(&buffer, &length, &capacity, "\":{", 3);
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%" PRIu64, entries[i].time_sec);
    fpv_buffer_append(&buffer, &length, &capacity, "\"time\":", 7);
    fpv_buffer_append(&buffer, &length, &capacity, time_buf, strlen(time_buf));
    fpv_buffer_append(&buffer, &length, &capacity, ",\"price\":", 9);
    char price_buf[64];
    snprintf(price_buf, sizeof(price_buf), "%.6f", entries[i].price);
    fpv_buffer_append(&buffer, &length, &capacity, price_buf, strlen(price_buf));
    fpv_buffer_append(&buffer, &length, &capacity, "}", 1);
  }
  fpv_buffer_append(&buffer, &length, &capacity, "}", 1);
  if (!buffer) {
    buffer = fpv_strdup("{}");
  }
  bool ok = fpv_features_write_file(path, buffer);
  fpv_free(buffer);
  return ok;
}

static void fpv_features_update_adv_profile(
    fpv_feature_state_t* state,
    const char* order_id,
    double price,
    bool add) {
  if (!state || !state->adv_profile_path || !order_id || !order_id[0]) {
    return;
  }
  size_t count = 0;
  fpv_adv_profile_entry_t* entries =
      fpv_adv_profile_load(state->adv_profile_path, &count);
  size_t index = SIZE_MAX;
  for (size_t i = 0; i < count; i++) {
    if (entries[i].order_id &&
        strcmp(entries[i].order_id, order_id) == 0) {
      index = i;
      break;
    }
  }

  if (add) {
    if (index == SIZE_MAX) {
      fpv_adv_profile_entry_t* grown =
          (fpv_adv_profile_entry_t*)realloc(
              entries, (count + 1) * sizeof(*grown));
      if (!grown) {
        fpv_adv_profile_entries_destroy(entries, count);
        return;
      }
      entries = grown;
      entries[count].order_id = fpv_strdup(order_id);
      entries[count].time_sec = (uint64_t)time(NULL);
      entries[count].price = price;
      if (entries[count].order_id) {
        count++;
      }
    } else {
      entries[index].time_sec = (uint64_t)time(NULL);
      entries[index].price = price;
    }
  } else {
    if (index != SIZE_MAX) {
      fpv_free(entries[index].order_id);
      entries[index] = entries[count - 1];
      count--;
    }
  }

  fpv_adv_profile_save(state->adv_profile_path, entries, count);
  fpv_adv_profile_entries_destroy(entries, count);
}

static bool fpv_starts_with(const char* text, const char* prefix) {
  if (!text || !prefix) {
    return false;
  }
  size_t len = strlen(prefix);
  return strncmp(text, prefix, len) == 0;
}

static void fpv_features_handle_review_message(
    fpv_feature_state_t* state,
    const char* order_id,
    uint64_t chat_id) {
  if (!state || !state->account || !order_id || !order_id[0]) {
    return;
  }
  fpv_funpay_order_detail_t detail;
  memset(&detail, 0, sizeof(detail));
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_get_order_detail(
          state->account,
          order_id,
          &detail,
          &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || !detail.review.has_review ||
      detail.review.stars <= 0) {
    fpv_funpay_order_detail_clear(&detail);
    return;
  }

  int stars = detail.review.stars;
  if (stars < 1) {
    stars = 1;
  } else if (stars > 5) {
    stars = 5;
  }

  char* reply_text = NULL;
  if (state->flags.review_reply_enabled[stars - 1] &&
      state->flags.review_reply_texts[stars - 1] &&
      state->flags.review_reply_texts[stars - 1][0]) {
    reply_text = fpv_format_order_detail_text(
        &detail,
        state->flags.review_reply_texts[stars - 1]);
    if (reply_text) {
      fpv_funpay_error_t reply_error;
      memset(&reply_error, 0, sizeof(reply_error));
      fpv_funpay_account_send_review(
          state->account,
          order_id,
          reply_text,
          stars,
          &reply_error);
      fpv_funpay_error_clear(&reply_error);
    }
  }

  if (state->telegram && detail.review.text) {
    fpv_telegram_service_notify_review(
        state->telegram,
        detail.id ? detail.id : order_id,
        stars,
        detail.review.text,
        reply_text,
        chat_id,
        detail.buyer_username);
  }

  fpv_free(reply_text);
  fpv_funpay_order_detail_clear(&detail);
}

static void fpv_features_handle_system_message(
    fpv_feature_state_t* state,
    const fpv_message_t* message,
    uint64_t chat_id) {
  if (!state || !message || !message->text) {
    return;
  }
  fpv_features_system_type_t type =
      fpv_features_system_type(message->text);
  if (type == FPV_SYSTEM_NONE) {
    return;
  }
  char* order_id = fpv_features_extract_order_id(message->text);
  if (!order_id) {
    return;
  }

  if (type == FPV_SYSTEM_ORDER_CONFIRMED ||
      type == FPV_SYSTEM_ORDER_CONFIRMED_ADMIN) {
    if (state->account) {
      fpv_funpay_order_detail_t detail;
      memset(&detail, 0, sizeof(detail));
      fpv_funpay_error_t error;
      memset(&error, 0, sizeof(error));
      fpv_result_t result =
          fpv_funpay_account_get_order_detail(
              state->account,
              order_id,
              &detail,
              &error);
      fpv_funpay_error_clear(&error);
      if (result == FPV_OK && detail.amount > 0.0) {
        fpv_features_update_adv_profile(
            state,
            order_id,
            detail.amount,
            true);
      }
      fpv_funpay_order_detail_clear(&detail);
    }
  } else if (type == FPV_SYSTEM_ORDER_REOPENED ||
             type == FPV_SYSTEM_REFUND ||
             type == FPV_SYSTEM_REFUND_ADMIN ||
             type == FPV_SYSTEM_PARTIAL_REFUND) {
    fpv_features_update_adv_profile(state, order_id, 0.0, false);
  } else if (type == FPV_SYSTEM_NEW_FEEDBACK ||
             type == FPV_SYSTEM_FEEDBACK_CHANGED) {
    fpv_features_handle_review_message(state, order_id, chat_id);
  }

  fpv_free(order_id);
}

static fpv_result_t fpv_old_users_add(
    fpv_feature_state_t* state,
    uint64_t chat_id) {
  if (!state) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (fpv_list_contains_u64(state->old_users, state->old_user_count, chat_id)) {
    return FPV_OK;
  }
  uint64_t* grown = (uint64_t*)realloc(
      state->old_users,
      (state->old_user_count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  state->old_users = grown;
  state->old_users[state->old_user_count++] = chat_id;
  if (state->old_users_path) {
    fpv_cache_save_uint64(
        state->old_users_path,
        state->old_users,
        state->old_user_count);
  }
  return FPV_OK;
}

static size_t fpv_count_products(const char* path) {
  if (!path || !fpv_fs_exists(path)) {
    return 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  bool has_content = false;
  int ch = 0;
  while ((ch = fgetc(file)) != EOF) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      if (has_content) {
        count++;
      }
      has_content = false;
      continue;
    }
    has_content = true;
  }
  if (has_content) {
    count++;
  }
  fclose(file);
  return count;
}

static char* fpv_replace_all(
    const char* text,
    const char* key,
    const char* value) {
  if (!text || !key || !key[0]) {
    return text ? fpv_strdup(text) : NULL;
  }
  const char* pos = strstr(text, key);
  if (!pos) {
    return fpv_strdup(text);
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const char* cursor = text;
  size_t key_len = strlen(key);
  size_t value_len = value ? strlen(value) : 0;
  while ((pos = strstr(cursor, key)) != NULL) {
    size_t prefix_len = (size_t)(pos - cursor);
    fpv_buffer_append(&buffer, &length, &capacity, cursor, prefix_len);
    if (value) {
      fpv_buffer_append(&buffer, &length, &capacity, value, value_len);
    }
    cursor = pos + key_len;
  }
  if (*cursor) {
    fpv_buffer_append(&buffer, &length, &capacity, cursor, strlen(cursor));
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

static void fpv_format_date_parts(
    struct tm* tm_value,
    char* date,
    size_t date_len,
    char* time_short,
    size_t time_short_len,
    char* time_full,
    size_t time_full_len,
    char* date_text,
    size_t date_text_len,
    char* full_date_text,
    size_t full_date_text_len) {
  static const char* months[] = {
      "\xD1\x8F\xD0\xBD\xD0\xB2\xD0\xB0\xD1\x80\xD1\x8F",
      "\xD1\x84\xD0\xB5\xD0\xB2\xD1\x80\xD0\xB0\xD0\xBB\xD1\x8F",
      "\xD0\xBC\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0",
      "\xD0\xB0\xD0\xBF\xD1\x80\xD0\xB5\xD0\xBB\xD1\x8F",
      "\xD0\xBC\xD0\xB0\xD1\x8F",
      "\xD0\xB8\xD1\x8E\xD0\xBD\xD1\x8F",
      "\xD0\xB8\xD1\x8E\xD0\xBB\xD1\x8F",
      "\xD0\xB0\xD0\xB2\xD0\xB3\xD1\x83\xD1\x81\xD1\x82\xD0\xB0",
      "\xD1\x81\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xBD\xD0\xBE\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xB4\xD0\xB5\xD0\xBA\xD0\xB0\xD0\xB1\xD1\x80\xD1\x8F"};

  const char* month = "";
  if (tm_value->tm_mon >= 0 && tm_value->tm_mon < 12) {
    month = months[tm_value->tm_mon];
  }

  snprintf(
      date,
      date_len,
      "%02d.%02d.%04d",
      tm_value->tm_mday,
      tm_value->tm_mon + 1,
      tm_value->tm_year + 1900);
  snprintf(
      time_short,
      time_short_len,
      "%02d:%02d",
      tm_value->tm_hour,
      tm_value->tm_min);
  snprintf(
      time_full,
      time_full_len,
      "%02d:%02d:%02d",
      tm_value->tm_hour,
      tm_value->tm_min,
      tm_value->tm_sec);
  snprintf(
      date_text,
      date_text_len,
      "%d %s",
      tm_value->tm_mday,
      month);
  snprintf(
      full_date_text,
      full_date_text_len,
      "%d %s %d \xD0\xB3\xD0\xBE\xD0\xB4\xD0\xB0",
      tm_value->tm_mday,
      month,
      tm_value->tm_year + 1900);
}

static char* fpv_format_message_text(
    const fpv_message_t* message,
    const fpv_chat_t* chat,
    const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username = NULL;
  const char* chat_id = NULL;
  const char* message_text = NULL;

  if (message) {
    username = message->sender_name ? message->sender_name : message->chat_name;
    chat_id = message->chat_id;
    if (message->text) {
      message_text = message->text;
    } else if (message->image_url) {
      message_text = message->image_url;
    }
  } else if (chat) {
    username = chat->title;
    chat_id = chat->id;
    message_text = chat->last_message_text;
  }

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username ? username : "");
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$message_text", message_text ? message_text : "");
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$chat_id", chat_id ? chat_id : "");
  fpv_free(output);
  output = temp;
  return output;
}

static char* fpv_format_order_text(const fpv_order_t* order, const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username = order && order->buyer_username ? order->buyer_username : "";
  const char* order_title = order && order->title ? order->title : "";
  const char* order_id = order && order->id ? order->id : "";

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_desc", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_title", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_id", order_id);
  fpv_free(output);
  output = temp;
  return output;
}

static char* fpv_format_order_detail_text(
    const fpv_funpay_order_detail_t* detail,
    const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username =
      detail && detail->buyer_username ? detail->buyer_username : "";
  const char* order_title =
      detail && detail->title ? detail->title : "";
  const char* order_id = detail && detail->id ? detail->id : "";

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_desc", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_title", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_id", order_id);
  fpv_free(output);
  output = temp;
  return output;
}

static char* fpv_normalize_text_lines(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const char* line_start = text;
  for (const char* ptr = text;; ptr++) {
    if (*ptr == '\n' || *ptr == '\0') {
      const char* end = ptr;
      while (end > line_start && isspace((unsigned char)end[-1]) &&
             end[-1] != '\n') {
        end--;
      }
      const char* start = line_start;
      while (start < end && isspace((unsigned char)*start)) {
        start++;
      }
      if (length > 0) {
        fpv_buffer_append_char(&buffer, &length, &capacity, '\n');
      }
      if (end > start) {
        fpv_buffer_append(
            &buffer,
            &length,
            &capacity,
            start,
            (size_t)(end - start));
      }
      line_start = ptr + 1;
      if (*ptr == '\0') {
        break;
      }
    }
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}

static char* fpv_replace_blank_lines(const char* text) {
  char* result = fpv_strdup(text ? text : "");
  if (!result) {
    return NULL;
  }
  while (strstr(result, "\n\n") != NULL) {
    char* replaced = fpv_replace_all(result, "\n\n", "\n[a][/a]\n");
    fpv_free(result);
    result = replaced;
    if (!result) {
      return NULL;
    }
  }
  return result;
}

static bool fpv_split_text_lines(
    const char* text,
    fpv_message_entity_t** out_entities,
    size_t* out_count) {
  char* copy = fpv_strdup(text ? text : "");
  if (!copy) {
    return false;
  }
  size_t line_count = 0;
  for (char* ptr = copy; *ptr; ptr++) {
    if (*ptr == '\n') {
      line_count++;
    }
  }
  line_count++;

  char** lines = (char**)calloc(line_count, sizeof(*lines));
  if (!lines) {
    fpv_free(copy);
    return false;
  }
  size_t count = 0;
  char* token = strtok(copy, "\n");
  while (token) {
    lines[count++] = token;
    token = strtok(NULL, "\n");
  }

  size_t index = 0;
  while (index < count) {
    size_t chunk_end = index + 20;
    if (chunk_end > count) {
      chunk_end = count;
    }
    char* buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (size_t i = index; i < chunk_end; i++) {
      if (i > index) {
        fpv_buffer_append_char(&buffer, &length, &capacity, '\n');
      }
      fpv_buffer_append(
          &buffer,
          &length,
          &capacity,
          lines[i],
          strlen(lines[i]));
    }
    if (buffer) {
      char* trimmed = fpv_trim_copy(buffer);
      bool keep = trimmed && trimmed[0] != '\0' &&
          strcmp(trimmed, "[a][/a]") != 0;
      fpv_free(trimmed);
      if (keep) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(buffer);
          fpv_free(lines);
          fpv_free(copy);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_TEXT;
        entity->text = buffer;
      } else {
        fpv_free(buffer);
      }
    }
    index = chunk_end;
  }

  fpv_free(lines);
  fpv_free(copy);
  return true;
}

static bool fpv_parse_message_entities(
    const char* text,
    fpv_message_entity_t** out_entities,
    size_t* out_count) {
  if (!out_entities || !out_count) {
    return false;
  }
  *out_entities = NULL;
  *out_count = 0;

  char* normalized = fpv_normalize_text_lines(text ? text : "");
  if (!normalized) {
    return false;
  }
  char* expanded = fpv_replace_blank_lines(normalized);
  fpv_free(normalized);
  if (!expanded) {
    return false;
  }

  const char* ptr = expanded;
  while (*ptr) {
    const char* next = strstr(ptr, "$photo=");
    const char* next_sleep = strstr(ptr, "$sleep=");
    const char* next_new = strstr(ptr, "$new");
    const char* marker = NULL;
    if (next && (!marker || next < marker)) {
      marker = next;
    }
    if (next_sleep && (!marker || next_sleep < marker)) {
      marker = next_sleep;
    }
    if (next_new && (!marker || next_new < marker)) {
      marker = next_new;
    }

    if (!marker) {
      fpv_split_text_lines(ptr, out_entities, out_count);
      break;
    }

    if (marker > ptr) {
      char* segment = fpv_strdup_n(ptr, (size_t)(marker - ptr));
      if (!segment) {
        fpv_free(expanded);
        return false;
      }
      fpv_split_text_lines(segment, out_entities, out_count);
      fpv_free(segment);
    }

    if (marker == next) {
      const char* id_start = marker + strlen("$photo=");
      uint64_t id = 0;
      while (*id_start && isdigit((unsigned char)*id_start)) {
        id = id * 10 + (uint64_t)(*id_start - '0');
        id_start++;
      }
      if (id > 0) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(expanded);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_IMAGE;
        entity->image_id = id;
      }
      ptr = id_start;
      continue;
    }

    if (marker == next_sleep) {
      const char* value_start = marker + strlen("$sleep=");
      char* end = NULL;
      double value = strtod(value_start, &end);
      if (end && end > value_start) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(expanded);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_SLEEP;
        entity->sleep_seconds = value;
        ptr = end;
        continue;
      }
      ptr = marker + 1;
      continue;
    }

    if (marker == next_new) {
      ptr = marker + strlen("$new");
      continue;
    }
  }

  fpv_free(expanded);
  return true;
}

static void fpv_free_message_entities(
    fpv_message_entity_t* entities,
    size_t count) {
  if (!entities) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(entities[i].text);
  }
  fpv_free(entities);
}

static void fpv_features_config_lock(fpv_feature_state_t* state) {
  if (state && state->config_mutex.initialized) {
    fpv_mutex_lock(&state->config_mutex);
  }
}

static void fpv_features_config_unlock(fpv_feature_state_t* state) {
  if (state && state->config_mutex.initialized) {
    fpv_mutex_unlock(&state->config_mutex);
  }
}

static void fpv_delivery_notice_destroy(fpv_delivery_notice_t* notice) {
  if (!notice) {
    return;
  }
  fpv_free(notice->order_id);
  fpv_free(notice->buyer_username);
  fpv_free(notice->delivery_text);
  fpv_free(notice);
}

static fpv_result_t fpv_send_message_entity(
    fpv_feature_state_t* state,
    const fpv_message_entity_t* entity,
    uint64_t chat_id,
    const char* chat_name) {
  if (!state || !entity || !state->account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_message_t* sent = NULL;
  fpv_result_t result = FPV_ERR_INTERNAL;

  if (entity->type == FPV_ENTITY_TEXT) {
    result = fpv_funpay_account_send_message(
        state->account,
        chat_id,
        chat_name,
        entity->text,
        &sent,
        &error);
  } else if (entity->type == FPV_ENTITY_IMAGE) {
    result = fpv_funpay_account_send_image(
        state->account,
        chat_id,
        chat_name,
        entity->image_id,
        &sent,
        &error);
  } else if (entity->type == FPV_ENTITY_SLEEP) {
    uint32_t delay = (uint32_t)lrint(entity->sleep_seconds * 1000.0);
    fpv_sleep_ms(delay);
    return FPV_OK;
  }

  if (result == FPV_OK && sent) {
    if (state->runner && sent->id) {
      uint64_t msg_id = strtoull(sent->id, NULL, 10);
      if (msg_id > 0) {
        fpv_funpay_runner_mark_by_bot(state->runner, chat_id, msg_id);
      }
    }
    if (state->runner && sent->text) {
      fpv_funpay_runner_update_last_message(
          state->runner,
          chat_id,
          sent->text,
          NULL);
    }
  }
  fpv_message_destroy(sent);
  fpv_funpay_error_clear(&error);
  return result;
}

static void fpv_send_task_run(void* context) {
  fpv_send_task_t* task = (fpv_send_task_t*)context;
  if (!task || !task->state) {
    fpv_free(task);
    return;
  }

  fpv_feature_state_t* state = task->state;
  char* message_text = fpv_strdup(task->message_text ? task->message_text : "");
  if (!message_text) {
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task);
    return;
  }

  if (state->flags.watermark && state->flags.watermark[0] &&
      task->add_watermark && strncmp(message_text, "$photo=", 7) != 0) {
    char* joined = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        state->flags.watermark,
        strlen(state->flags.watermark));
    fpv_buffer_append_char(&joined, &length, &capacity, '\n');
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        message_text,
        strlen(message_text));
    fpv_free(message_text);
    message_text = joined;
  }

  fpv_message_entity_t* entities = NULL;
  size_t entity_count = 0;
  if (!fpv_parse_message_entities(message_text, &entities, &entity_count)) {
    fpv_free(message_text);
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task);
    return;
  }

  fpv_free(message_text);

  bool send_failed = false;
  for (size_t i = 0; i < entity_count; i++) {
    int attempts = 3;
    while (attempts-- > 0) {
      fpv_result_t result = fpv_send_message_entity(
          state,
          &entities[i],
          task->chat_id,
          task->chat_name);
      if (result == FPV_OK) {
        break;
      }
      fpv_sleep_ms(1000);
      if (attempts == 0) {
        fpv_features_log(state, FPV_LOG_ERROR, "Message send failed.");
        send_failed = true;
      }
    }
    if (send_failed) {
      break;
    }
  }

  if (send_failed && task->restore_on_failure &&
      task->products_path && task->products && task->products_count > 0) {
    fpv_products_restore(
        task->products_path,
        task->products,
        task->products_count);
  }

  if (task->update_lot_states) {
    fpv_features_queue_lot_update(state, task->lot_update_tag);
  }

  if (task->delivery_notice && state->telegram) {
    if (send_failed) {
      char* error_text =
          fpv_features_format_delivery_error(
              state, task->delivery_notice->order_id);
      const char* text = error_text
          ? error_text
          : (task->delivery_notice->delivery_text
              ? task->delivery_notice->delivery_text
              : "");
      fpv_telegram_service_notify_delivery(
          state->telegram,
          task->delivery_notice->order_id,
          task->delivery_notice->buyer_username,
          text,
          task->delivery_notice->goods_left,
          false);
      fpv_free(error_text);
    } else {
      fpv_telegram_service_notify_delivery(
          state->telegram,
          task->delivery_notice->order_id,
          task->delivery_notice->buyer_username,
          task->delivery_notice->delivery_text,
          task->delivery_notice->goods_left,
          true);
    }
  }

  fpv_free_message_entities(entities, entity_count);
  fpv_free(task->products_path);
  if (task->products) {
    fpv_products_free(task->products, task->products_count);
  }
  fpv_delivery_notice_destroy(task->delivery_notice);
  fpv_free(task->chat_name);
  fpv_free(task->message_text);
  fpv_free(task->lot_update_tag);
  fpv_free(task);
}

fpv_result_t fpv_features_send_message(
    fpv_feature_state_t* state,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark) {
  if (!state || !state->account || chat_id == 0 || !message_text) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* trim = message_text;
  while (*trim && isspace((unsigned char)*trim)) {
    trim++;
  }
  bool starts_photo = strncmp(trim, "$photo=", 7) == 0;
  char* text = fpv_strdup(message_text);
  if (!text) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (state->flags.watermark && state->flags.watermark[0] &&
      add_watermark && !starts_photo) {
    char* joined = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        state->flags.watermark,
        strlen(state->flags.watermark));
    fpv_buffer_append_char(&joined, &length, &capacity, '\n');
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        text,
        strlen(text));
    fpv_free(text);
    text = joined;
  }
  if (!text) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_message_entity_t* entities = NULL;
  size_t entity_count = 0;
  if (!fpv_parse_message_entities(text, &entities, &entity_count)) {
    fpv_free(text);
    return FPV_ERR_PARSE;
  }
  fpv_free(text);
  if (entity_count == 0) {
    fpv_free_message_entities(entities, entity_count);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_result_t result = FPV_OK;
  for (size_t i = 0; i < entity_count; i++) {
    int attempts = 3;
    while (attempts-- > 0) {
      result = fpv_send_message_entity(
          state,
          &entities[i],
          chat_id,
          chat_name);
      if (result == FPV_OK) {
        break;
      }
      fpv_sleep_ms(1000);
    }
    if (result != FPV_OK) {
      fpv_free_message_entities(entities, entity_count);
      return result;
    }
  }
  fpv_free_message_entities(entities, entity_count);
  return FPV_OK;
}

static bool fpv_features_queue_message(
    fpv_feature_state_t* state,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark,
    char** products,
    size_t products_count,
    const char* products_path,
    bool update_lot_states,
    const char* lot_update_tag,
    fpv_delivery_notice_t* delivery_notice) {
  if (!state || !state->scheduler || !message_text) {
    if (products_path && products && products_count > 0) {
      fpv_products_restore(products_path, products, products_count);
    }
    fpv_delivery_notice_destroy(delivery_notice);
    return false;
  }
  fpv_send_task_t* task = (fpv_send_task_t*)calloc(1, sizeof(*task));
  if (!task) {
    if (products_path && products && products_count > 0) {
      fpv_products_restore(products_path, products, products_count);
    }
    fpv_delivery_notice_destroy(delivery_notice);
    return false;
  }
  task->state = state;
  task->chat_id = chat_id;
  task->chat_name = chat_name ? fpv_strdup(chat_name) : NULL;
  task->message_text = fpv_strdup(message_text);
  task->add_watermark = add_watermark;
  task->products = products;
  task->products_count = products_count;
  task->products_path = products_path ? fpv_strdup(products_path) : NULL;
  task->restore_on_failure =
      products_path != NULL && products != NULL && products_count > 0;
  task->update_lot_states = update_lot_states;
  task->lot_update_tag = lot_update_tag ? fpv_strdup(lot_update_tag) : NULL;
  task->delivery_notice = delivery_notice;
  if (products_path && !task->products_path) {
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task->lot_update_tag);
    fpv_delivery_notice_destroy(task->delivery_notice);
    if (products) {
      if (products_path && products_count > 0) {
        fpv_products_restore(products_path, products, products_count);
      }
    }
    fpv_free(task);
    return false;
  }
  if (!task->message_text) {
    fpv_free(task->chat_name);
    fpv_free(task->products_path);
    fpv_free(task->lot_update_tag);
    fpv_delivery_notice_destroy(task->delivery_notice);
    if (products) {
      if (products_path && products_count > 0) {
        fpv_products_restore(products_path, products, products_count);
      }
    }
    fpv_free(task);
    return false;
  }
  fpv_scheduler_enqueue(state->scheduler, fpv_send_task_run, task);
  return true;
}

static void fpv_features_update_lot_states(fpv_feature_state_t* state) {
  if (!state || !state->account) {
    return;
  }
  if (!state->flags.auto_restore && !state->flags.auto_disable) {
    return;
  }

  char* activated_list = NULL;
  size_t activated_len = 0;
  size_t activated_cap = 0;
  size_t activated_count = 0;
  char* deactivated_list = NULL;
  size_t deactivated_len = 0;
  size_t deactivated_cap = 0;
  size_t deactivated_count = 0;

  fpv_funpay_lot_section_t* sections = NULL;
  size_t section_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_sections(
      state->account,
      &sections,
      &section_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || section_count == 0) {
    fpv_free(sections);
    return;
  }

  for (size_t i = 0; i < section_count; i++) {
    fpv_lot_t** lots = NULL;
    size_t lot_count = 0;
    memset(&error, 0, sizeof(error));
    result = fpv_funpay_account_get_trade_lots(
        state->account,
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

    for (size_t j = 0; j < lot_count; j++) {
      fpv_lot_t* lot = lots[j];
      if (!lot || !lot->id) {
        continue;
      }
      uint64_t lot_id = strtoull(lot->id, NULL, 10);
      if (lot_id == 0) {
        continue;
      }
      const char* title = lot->title ? lot->title : "";
      const char* lot_name =
          (title && title[0]) ? title : (lot->id ? lot->id : "");
      bool config_found = false;
      bool disable_restore = false;
      bool disable_disable = false;
      char* products_file = NULL;
      fpv_features_config_lock(state);
      const fpv_auto_delivery_lot_t* config =
          fpv_auto_delivery_find(&state->auto_delivery, title);
      if (config) {
        config_found = true;
        disable_restore = config->disable_auto_restore;
        disable_disable = config->disable_auto_disable;
        if (config->products_file && config->products_file[0]) {
          products_file = fpv_strdup(config->products_file);
        }
      }
      fpv_features_config_unlock(state);
      bool has_products =
          products_file && products_file[0] && state->products_dir;
      size_t products_count = 1;
      if (has_products) {
        char* path = fpv_path_join(state->products_dir, products_file);
        if (path) {
          products_count = fpv_count_products(path);
          fpv_free(path);
        } else {
          products_count = 0;
        }
      }
      fpv_free(products_file);

      bool should_activate = false;
      if (!lot->active && state->flags.auto_restore) {
        if (!config_found) {
          should_activate = true;
        } else if (!disable_restore) {
          if (!state->flags.auto_disable) {
            should_activate = true;
          } else if (!has_products || products_count > 0) {
            should_activate = true;
          }
        }
      }

      if (should_activate) {
        fpv_funpay_error_t lot_error;
        memset(&lot_error, 0, sizeof(lot_error));
        fpv_result_t change_result = fpv_funpay_account_set_lot_active(
            state->account,
            lot_id,
            true,
            &lot_error);
        if (change_result == FPV_OK) {
          char message[256];
          snprintf(message, sizeof(message), "Lot activated: %s", title);
          fpv_features_log(state, FPV_LOG_INFO, message);
          if (lot_name && lot_name[0]) {
            bool ok = true;
            if (activated_count > 0) {
              ok = fpv_buffer_append_char(
                  &activated_list, &activated_len, &activated_cap, '\n');
            }
            if (ok) {
              ok = fpv_buffer_append(
                  &activated_list, &activated_len, &activated_cap,
                  lot_name, strlen(lot_name));
            }
            if (!ok) {
              fpv_free(activated_list);
              activated_list = NULL;
              activated_len = 0;
              activated_cap = 0;
            } else {
              activated_count++;
            }
          }
        } else {
          fpv_features_log(
              state,
              FPV_LOG_WARNING,
              lot_error.message ? lot_error.message : "Lot activation failed.");
        }
        fpv_funpay_error_clear(&lot_error);
      }

      bool should_deactivate = false;
      if (lot->active && state->flags.auto_disable && config_found &&
          !disable_disable && has_products &&
          products_count == 0) {
        should_deactivate = true;
      }

      if (should_deactivate) {
        fpv_funpay_error_t lot_error;
        memset(&lot_error, 0, sizeof(lot_error));
        fpv_result_t change_result = fpv_funpay_account_set_lot_active(
            state->account,
            lot_id,
            false,
            &lot_error);
        if (change_result == FPV_OK) {
          char message[256];
          snprintf(message, sizeof(message), "Lot deactivated: %s", title);
          fpv_features_log(state, FPV_LOG_INFO, message);
          if (lot_name && lot_name[0]) {
            bool ok = true;
            if (deactivated_count > 0) {
              ok = fpv_buffer_append_char(
                  &deactivated_list, &deactivated_len, &deactivated_cap, '\n');
            }
            if (ok) {
              ok = fpv_buffer_append(
                  &deactivated_list, &deactivated_len, &deactivated_cap,
                  lot_name, strlen(lot_name));
            }
            if (!ok) {
              fpv_free(deactivated_list);
              deactivated_list = NULL;
              deactivated_len = 0;
              deactivated_cap = 0;
            } else {
              deactivated_count++;
            }
          }
        } else {
          fpv_features_log(
              state,
              FPV_LOG_WARNING,
              lot_error.message ? lot_error.message : "Lot deactivation failed.");
        }
        fpv_funpay_error_clear(&lot_error);
      }
    }

    for (size_t j = 0; j < lot_count; j++) {
      fpv_lot_destroy(lots[j]);
    }
    fpv_free(lots);
  }

  if (state->telegram) {
    if (activated_list && activated_list[0]) {
      fpv_telegram_service_notify_lots_activated(
          state->telegram, activated_list);
    }
    if (deactivated_list && deactivated_list[0]) {
      fpv_telegram_service_notify_lots_deactivated(
          state->telegram, deactivated_list);
    }
  }
  fpv_free(activated_list);
  fpv_free(deactivated_list);
  fpv_free(sections);
}

static void fpv_features_lot_update_task(void* context) {
  fpv_lot_update_task_t* task = (fpv_lot_update_task_t*)context;
  if (!task) {
    return;
  }
  fpv_feature_state_t* state = task->state;
  if (state) {
    fpv_features_update_lot_states(state);
  }

  if (!state || !state->lot_update_mutex.initialized) {
    fpv_free(task->runner_tag);
    fpv_free(task);
    return;
  }

  fpv_mutex_lock(&state->lot_update_mutex);
  if (task->runner_tag) {
    fpv_free(state->last_lot_update_tag);
    state->last_lot_update_tag = task->runner_tag;
    task->runner_tag = NULL;
  }
  bool pending = state->lot_update_pending;
  char* pending_tag = state->pending_lot_update_tag;
  state->pending_lot_update_tag = NULL;
  state->lot_update_pending = false;
  state->lot_update_running = false;
  fpv_mutex_unlock(&state->lot_update_mutex);

  if (pending) {
    fpv_features_queue_lot_update(state, pending_tag);
  }
  fpv_free(pending_tag);
  fpv_free(task->runner_tag);
  fpv_free(task);
}

static void fpv_features_queue_lot_update(
    fpv_feature_state_t* state,
    const char* runner_tag) {
  if (!state || !state->scheduler) {
    return;
  }
  if (!state->flags.auto_restore && !state->flags.auto_disable) {
    return;
  }
  if (!state->lot_update_mutex.initialized) {
    return;
  }

  fpv_mutex_lock(&state->lot_update_mutex);
  if (runner_tag && state->last_lot_update_tag &&
      strcmp(runner_tag, state->last_lot_update_tag) == 0) {
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  if (state->lot_update_running) {
    state->lot_update_pending = true;
    if (runner_tag && runner_tag[0]) {
      fpv_free(state->pending_lot_update_tag);
      state->pending_lot_update_tag = fpv_strdup(runner_tag);
    }
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  state->lot_update_running = true;
  fpv_mutex_unlock(&state->lot_update_mutex);

  fpv_lot_update_task_t* task =
      (fpv_lot_update_task_t*)calloc(1, sizeof(*task));
  if (!task) {
    fpv_mutex_lock(&state->lot_update_mutex);
    state->lot_update_running = false;
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  task->state = state;
  if (runner_tag && runner_tag[0]) {
    task->runner_tag = fpv_strdup(runner_tag);
  }
  fpv_scheduler_enqueue(state->scheduler, fpv_features_lot_update_task, task);
}

static void fpv_features_handle_message(
    fpv_feature_state_t* state,
    const fpv_message_t* message) {
  if (!state || !message) {
    return;
  }

  uint64_t chat_id = message->chat_id ? strtoull(message->chat_id, NULL, 10) : 0;
  uint64_t account_id = state->account ? fpv_funpay_account_id(state->account) : 0;
  uint64_t author_id = message->sender_id
      ? strtoull(message->sender_id, NULL, 10)
      : 0;
  bool its_me = account_id > 0 && author_id == account_id;
  bool is_system = message->sender_id && author_id == 0;
  bool was_old = chat_id > 0 &&
      fpv_list_contains_u64(state->old_users, state->old_user_count, chat_id);

  if (state->flags.greetings_send && chat_id > 0 && !was_old && !its_me) {
    if (!state->flags.greetings_ignore_system_messages || !is_system) {
      char* greeting = fpv_format_message_text(
          message,
          NULL,
          state->flags.greetings_text);
      if (greeting) {
        fpv_features_queue_message(
            state,
            chat_id,
            message->chat_name,
            greeting,
            true,
            NULL,
            0,
            NULL,
            false,
            NULL,
            NULL);
      }
      fpv_free(greeting);
    }
  }

  if (chat_id > 0) {
    fpv_old_users_add(state, chat_id);
  }

  if (is_system && message->text) {
    fpv_features_handle_system_message(state, message, chat_id);
  }

  const char* msg_text = message->text;
  if ((!msg_text || !msg_text[0]) &&
      (!message->image_url || !message->image_url[0])) {
    char warn_buf[256];
    snprintf(
        warn_buf,
        sizeof(warn_buf),
        "Empty message text (id=%s, chat=%s, sender=%s).",
        message->id ? message->id : "?",
        message->chat_id ? message->chat_id : "?",
        message->sender_name ? message->sender_name : "?");
    fpv_features_log(state, FPV_LOG_WARNING, warn_buf);
  }
  char* lowered = NULL;
  char* cmd_response = NULL;
  char* cmd_notification = NULL;
  char* cmd_command = NULL;
  bool cmd_telegram = false;
  bool cmd_found = false;
  if (msg_text && msg_text[0]) {
    char* trimmed = fpv_trim_copy(msg_text);
    if (trimmed && trimmed[0]) {
      lowered = fpv_lower_ascii_copy(trimmed);
    }
    fpv_free(trimmed);
    if (lowered) {
      fpv_features_config_lock(state);
      const fpv_auto_response_command_t* cmd =
          fpv_auto_response_find(&state->auto_response, lowered);
      if (cmd) {
        cmd_found = true;
        cmd_telegram = cmd->telegram_notification;
        if (cmd->response) {
          cmd_response = fpv_strdup(cmd->response);
        }
        if (cmd->notification_text) {
          cmd_notification = fpv_strdup(cmd->notification_text);
        }
        if (cmd->command) {
          cmd_command = fpv_strdup(cmd->command);
        }
      }
      fpv_features_config_unlock(state);
    }
  }

  if (state->flags.auto_response && cmd_response && cmd_response[0]) {
    const char* username = message->sender_name;
    if (!state->flags.block_response || !username ||
        !fpv_list_contains_string(&state->blacklist, username)) {
      char* formatted = fpv_format_message_text(message, NULL, cmd_response);
      if (formatted && chat_id > 0) {
        fpv_features_queue_message(
            state,
            chat_id,
            message->chat_name,
            formatted,
            true,
            NULL,
            0,
            NULL,
            false,
            NULL,
            NULL);
      }
      fpv_free(formatted);
    }
  }

  if (cmd_found && cmd_telegram && cmd_command &&
      state->telegram && chat_id > 0) {
    const char* username = message->sender_name ? message->sender_name
        : message->chat_name;
    if (!state->flags.block_command_notification || !username ||
        !fpv_list_contains_string(&state->blacklist, username)) {
      char* notification = NULL;
      if (cmd_notification && cmd_notification[0]) {
        notification = fpv_format_message_text(
            message,
            NULL,
            cmd_notification);
      }
      fpv_telegram_service_notify_command(
          state->telegram,
          chat_id,
          message->chat_name,
          username,
          cmd_command,
          notification);
      fpv_free(notification);
    }
  }

  if (state->telegram && chat_id > 0) {
    const char* chat_name = message->chat_name;
    if (!state->flags.block_new_message_notification || !chat_name ||
        !fpv_list_contains_string(&state->blacklist, chat_name)) {
      bool is_my = account_id > 0 && author_id == account_id;
      bool is_fp = message->sender_id && author_id == 0;
      bool is_bot = message->by_bot;
      bool allowed = true;
      if (is_my && !state->flags.include_my_messages) {
        allowed = false;
      }
      if (is_fp && !state->flags.include_fp_messages) {
        allowed = false;
      }
      if (is_bot && !state->flags.include_bot_messages) {
        allowed = false;
      }
      if (allowed) {
        if (is_my && !state->flags.notify_only_my_messages) {
          allowed = false;
        }
        if (is_fp && !state->flags.notify_only_fp_messages) {
          allowed = false;
        }
        if (is_bot && !state->flags.notify_only_bot_messages) {
          allowed = false;
        }
      }
      if (allowed) {
        bool skip_notification = false;
        if (msg_text && msg_text[0]) {
          if (cmd_found) {
            skip_notification = true;
          }
          if (!skip_notification &&
              fpv_starts_with(msg_text, fpv_features_delivery_test_prefix)) {
            skip_notification = true;
          }
        }
        if (!skip_notification) {
          const fpv_message_t* list[1] = { message };
          fpv_telegram_service_notify_new_message(
              state->telegram,
              list,
              1,
              chat_id,
              message->chat_name,
              account_id);
        }
      }
    }
  }

  if (state->telegram && msg_text &&
      fpv_starts_with(msg_text, fpv_features_delivery_test_prefix)) {
    const char* key = msg_text + strlen(fpv_features_delivery_test_prefix);
    while (*key && isspace((unsigned char)*key)) {
      key++;
    }
    if (*key) {
      char* lot_name = fpv_telegram_service_take_delivery_test(
          state->telegram,
          key);
      if (lot_name) {
        uint64_t now_ms = fpv_time_now_ms();
        fpv_order_t* order = fpv_order_create(
            "ADTEST",
            NULL,
            NULL,
            NULL,
            message->chat_name ? message->chat_name : "",
            FPV_ORDER_PAID,
            0.0,
            NULL,
            now_ms,
            now_ms,
            1U,
            lot_name,
            fpv_features_delivery_test_subcategory);
        if (order) {
          fpv_features_handle_order(state, order, NULL);
          fpv_order_destroy(order);
        }
        fpv_free(lot_name);
      }
    }
  }

  fpv_free(cmd_response);
  fpv_free(cmd_notification);
  fpv_free(cmd_command);
  fpv_free(lowered);
}

static void fpv_features_handle_chat(
    fpv_feature_state_t* state,
    const fpv_chat_t* chat) {
  if (!state || !chat) {
    return;
  }
  uint64_t chat_id = chat->id ? strtoull(chat->id, NULL, 10) : 0;
  uint64_t account_id = state->account ? fpv_funpay_account_id(state->account) : 0;
  bool unread = chat->unread_count > 0;
  bool was_old = chat_id > 0 &&
      fpv_list_contains_u64(state->old_users, state->old_user_count, chat_id);
  bool is_system = chat->last_message_text &&
      fpv_features_system_type(chat->last_message_text) != FPV_SYSTEM_NONE;

  if (state->flags.greetings_send && chat_id > 0 && unread && !was_old) {
    if (!state->flags.greetings_ignore_system_messages || !is_system) {
      char* greeting = fpv_format_message_text(
          NULL,
          chat,
          state->flags.greetings_text);
      if (greeting) {
        fpv_features_queue_message(
            state,
            chat_id,
            chat->title,
            greeting,
            true,
            NULL,
            0,
            NULL,
            false,
            NULL,
            NULL);
      }
      fpv_free(greeting);
    }
  }

  if (chat_id > 0) {
    fpv_old_users_add(state, chat_id);
  }

  if (chat->last_message_text && is_system) {
    fpv_message_t sys_message;
    memset(&sys_message, 0, sizeof(sys_message));
    sys_message.text = (char*)chat->last_message_text;
    sys_message.chat_id = (char*)chat->id;
    fpv_features_handle_system_message(state, &sys_message, chat_id);
  }

  const char* msg_text = chat->last_message_text;
  char* lowered = NULL;
  char* cmd_response = NULL;
  char* cmd_notification = NULL;
  char* cmd_command = NULL;
  bool cmd_telegram = false;
  bool cmd_found = false;
  if (msg_text && msg_text[0]) {
    char* trimmed = fpv_trim_copy(msg_text);
    if (trimmed && trimmed[0]) {
      lowered = fpv_lower_ascii_copy(trimmed);
    }
    fpv_free(trimmed);
    if (lowered) {
      fpv_features_config_lock(state);
      const fpv_auto_response_command_t* cmd =
          fpv_auto_response_find(&state->auto_response, lowered);
      if (cmd) {
        cmd_found = true;
        cmd_telegram = cmd->telegram_notification;
        if (cmd->response) {
          cmd_response = fpv_strdup(cmd->response);
        }
        if (cmd->notification_text) {
          cmd_notification = fpv_strdup(cmd->notification_text);
        }
        if (cmd->command) {
          cmd_command = fpv_strdup(cmd->command);
        }
      }
      fpv_features_config_unlock(state);
    }
  }

  if (state->flags.auto_response && cmd_response && cmd_response[0]) {
    const char* username = chat->title;
    if (!state->flags.block_response || !username ||
        !fpv_list_contains_string(&state->blacklist, username)) {
      char* formatted = fpv_format_message_text(NULL, chat, cmd_response);
      if (formatted && chat_id > 0) {
        fpv_features_queue_message(
            state,
            chat_id,
            chat->title,
            formatted,
            true,
            NULL,
            0,
            NULL,
            false,
            NULL,
            NULL);
      }
      fpv_free(formatted);
    }
  }

  if (cmd_found && cmd_telegram && cmd_command &&
      state->telegram && chat_id > 0) {
    const char* username = unread && chat->title
        ? chat->title
        : (state->account ? fpv_funpay_account_username(state->account) : chat->title);
    if (!state->flags.block_command_notification || !username ||
        !fpv_list_contains_string(&state->blacklist, username)) {
      char* notification = NULL;
      if (cmd_notification && cmd_notification[0]) {
        notification = fpv_format_message_text(
            NULL,
            chat,
            cmd_notification);
      }
      fpv_telegram_service_notify_command(
          state->telegram,
          chat_id,
          chat->title,
          username,
          cmd_command,
          notification);
      fpv_free(notification);
    }
  }

  if (state->telegram && chat_id > 0 && unread && msg_text && msg_text[0]) {
    if (!state->flags.block_new_message_notification || !chat->title ||
        !fpv_list_contains_string(&state->blacklist, chat->title)) {
      if (!is_system) {
        bool skip_notification = false;
        if (cmd_found) {
          skip_notification = true;
        }
        if (!skip_notification &&
            fpv_starts_with(msg_text, fpv_features_delivery_test_prefix)) {
          skip_notification = true;
        }
        if (!skip_notification) {
          fpv_message_t msg;
          memset(&msg, 0, sizeof(msg));
          msg.chat_id = (char*)chat->id;
          msg.chat_name = (char*)chat->title;
          msg.text = (char*)msg_text;
          msg.sender_id = (char*)chat->id;
          msg.sender_name = (char*)chat->title;
          const fpv_message_t* list[1] = { &msg };
          fpv_telegram_service_notify_new_message(
              state->telegram,
              list,
              1,
              chat_id,
              chat->title,
              account_id);
        }
      }
    }
  }

  if (state->telegram && msg_text &&
      fpv_starts_with(msg_text, fpv_features_delivery_test_prefix)) {
    const char* key = msg_text + strlen(fpv_features_delivery_test_prefix);
    while (*key && isspace((unsigned char)*key)) {
      key++;
    }
    if (*key) {
      char* lot_name = fpv_telegram_service_take_delivery_test(
          state->telegram,
          key);
      if (lot_name) {
        uint64_t now_ms = fpv_time_now_ms();
        fpv_order_t* order = fpv_order_create(
            "ADTEST",
            NULL,
            NULL,
            NULL,
            chat->title ? chat->title : "",
            FPV_ORDER_PAID,
            0.0,
            NULL,
            now_ms,
            now_ms,
            1U,
            lot_name,
            fpv_features_delivery_test_subcategory);
        if (order) {
          fpv_features_handle_order(state, order, NULL);
          fpv_order_destroy(order);
        }
        fpv_free(lot_name);
      }
    }
  }

  fpv_free(cmd_response);
  fpv_free(cmd_notification);
  fpv_free(cmd_command);
  fpv_free(lowered);
}

static uint32_t fpv_parse_order_amount(const char* text) {
  if (!text) {
    return 1;
  }
  const char* marker = strstr(text, "\xD1\x88\xD1\x82.");
  if (!marker) {
    marker = strstr(text, "pcs.");
  }
  if (!marker) {
    return 1;
  }
  const char* ptr = marker;
  while (ptr > text && isspace((unsigned char)ptr[-1])) {
    ptr--;
  }
  const char* end = ptr;
  while (ptr > text && isdigit((unsigned char)ptr[-1])) {
    ptr--;
  }
  if (ptr == end) {
    return 1;
  }
  char* number = fpv_strdup_n(ptr, (size_t)(end - ptr));
  if (!number) {
    return 1;
  }
  uint32_t value = (uint32_t)strtoul(number, NULL, 10);
  fpv_free(number);
  return value > 0 ? value : 1;
}

static void fpv_features_handle_order(
    fpv_feature_state_t* state,
    const fpv_order_t* order,
    const char* runner_tag) {
  if (!state || !order) {
    return;
  }

  bool wants_lot_update =
      state->flags.auto_restore || state->flags.auto_disable;
  bool update_queued = false;
  bool queue_attempted = false;
  fpv_chat_t* chat = NULL;
  char* delivery_text = NULL;
  char** products = NULL;
  size_t products_count = 0;
  char* products_path = NULL;
  int goods_left = -1;

  const char* buyer = order->buyer_username ? order->buyer_username : "";
  const char* order_title = order->title ? order->title : "";
  const char* order_id = order->id ? order->id : "";

  bool buyer_blocked = buyer[0] &&
      fpv_list_contains_string(&state->blacklist, buyer);
  bool lot_found = false;
  bool lot_disable = false;
  bool lot_disable_auto_delivery = false;
  bool lot_disable_multi_delivery = false;
  char* lot_response = NULL;
  char* lot_products_file = NULL;
  fpv_features_config_lock(state);
  const fpv_auto_delivery_lot_t* lot =
      fpv_auto_delivery_find(&state->auto_delivery, order_title);
  if (lot) {
    lot_found = true;
    lot_disable = lot->disable;
    lot_disable_auto_delivery = lot->disable_auto_delivery;
    lot_disable_multi_delivery = lot->disable_multi_delivery;
    if (lot->response) {
      lot_response = fpv_strdup(lot->response);
    }
    if (lot->products_file && lot->products_file[0]) {
      lot_products_file = fpv_strdup(lot->products_file);
    }
  }
  fpv_features_config_unlock(state);
  bool lot_configured = lot_found && lot_response && lot_response[0];
  bool lot_disabled =
      lot_found && (lot_disable || lot_disable_auto_delivery);
  bool delivery_allowed =
      state->flags.auto_delivery && lot_configured && !lot_disabled;
  if (delivery_allowed && state->flags.block_delivery && buyer_blocked) {
    delivery_allowed = false;
  }

  const char* info_key = NULL;
  if (!lot_configured) {
    info_key = "ntfc_new_order_not_in_cfg";
  } else if (!state->flags.auto_delivery) {
    info_key = "ntfc_new_order_ad_disabled";
  } else if (lot_disabled) {
    info_key = "ntfc_new_order_ad_disabled_for_lot";
  } else if (state->flags.block_delivery && buyer_blocked) {
    info_key = "ntfc_new_order_user_blocked";
  } else {
    info_key = "ntfc_new_order_will_be_delivered";
  }

  bool notify_new_order = state->telegram &&
      (!state->flags.block_new_order_notification || !buyer_blocked);
  uint64_t chat_id = 0;

  if ((notify_new_order || delivery_allowed) && state->account && buyer[0]) {
    fpv_funpay_error_t error;
    memset(&error, 0, sizeof(error));
    chat = fpv_funpay_account_find_chat_by_name(
        state->account,
        buyer,
        true,
        &error);
    fpv_funpay_error_clear(&error);
    if (chat && chat->id) {
      chat_id = strtoull(chat->id, NULL, 10);
    }
  }

  if (notify_new_order && state->telegram) {
    const char* info_text = info_key;
    if (state->localizer && info_key) {
      const char* localized = fpv_localizer_get(state->localizer, info_key);
      if (localized) {
        info_text = localized;
      }
    }
    fpv_telegram_service_notify_new_order(
        state->telegram,
        order_title,
        order_id,
        buyer,
        order->amount,
        NULL,
        chat_id,
        info_text);
  }

  if (!delivery_allowed || !state->account) {
    goto cleanup;
  }

  if (!chat || !chat->id) {
    fpv_features_log(state, FPV_LOG_WARNING,
                     "Order delivery skipped: chat not found.");
    if (state->telegram && order_id[0]) {
      fpv_features_notify_delivery_error(state, order_id, buyer);
    }
    goto cleanup;
  }

  chat_id = strtoull(chat->id, NULL, 10);
  if (chat_id == 0) {
    if (state->telegram && order_id[0]) {
      fpv_features_notify_delivery_error(state, order_id, buyer);
    }
    goto cleanup;
  }

  delivery_text = fpv_format_order_text(order, lot_response);
  if (!delivery_text) {
    if (state->telegram && order_id[0]) {
      fpv_features_notify_delivery_error(state, order_id, buyer);
    }
    goto cleanup;
  }

  if (lot_products_file && lot_products_file[0] && state->products_dir) {
    uint32_t amount = 1;
    if (state->flags.multi_delivery && !lot_disable_multi_delivery) {
      amount = fpv_parse_order_amount(order_title);
    }
    products_path = fpv_path_join(state->products_dir, lot_products_file);
    if (!products_path) {
      if (state->telegram && order_id[0]) {
        fpv_features_notify_delivery_error(state, order_id, buyer);
      }
      goto cleanup;
    }
    size_t remaining = 0;
    fpv_result_t take_result = fpv_products_take(
        products_path,
        amount,
        &products,
        &products_count,
        &remaining);
    if (take_result != FPV_OK) {
      if (state->telegram && order_id[0]) {
        fpv_features_notify_delivery_error(state, order_id, buyer);
      }
      goto cleanup;
    }
    goods_left = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;

    char* joined = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (size_t i = 0; i < products_count; i++) {
      if (i > 0) {
        fpv_buffer_append_char(&joined, &length, &capacity, '\n');
      }
      const char* product = products[i];
      if (!product) {
        continue;
      }
      const char* escaped = strstr(product, "\\n");
      if (!escaped) {
        fpv_buffer_append(
            &joined,
            &length,
            &capacity,
            product,
            strlen(product));
        continue;
      }
      const char* cursor = product;
      while ((escaped = strstr(cursor, "\\n")) != NULL) {
        fpv_buffer_append(
            &joined,
            &length,
            &capacity,
            cursor,
            (size_t)(escaped - cursor));
        fpv_buffer_append_char(&joined, &length, &capacity, '\n');
        cursor = escaped + 2;
      }
      if (*cursor) {
        fpv_buffer_append(
            &joined,
            &length,
            &capacity,
            cursor,
            strlen(cursor));
      }
    }
    char* with_products =
        fpv_replace_all(delivery_text, "$product", joined ? joined : "");
    fpv_free(joined);
    fpv_free(delivery_text);
    delivery_text = with_products;

    if (!delivery_text) {
      if (state->telegram && order_id[0]) {
        fpv_features_notify_delivery_error(state, order_id, buyer);
      }
      goto cleanup;
    }
  }

  fpv_delivery_notice_t* delivery_notice = NULL;
  if (state->telegram) {
    delivery_notice =
        (fpv_delivery_notice_t*)calloc(1, sizeof(*delivery_notice));
    if (delivery_notice) {
      delivery_notice->order_id = fpv_strdup(order_id);
      delivery_notice->buyer_username = fpv_strdup(buyer);
      delivery_notice->delivery_text = fpv_strdup(delivery_text);
      delivery_notice->goods_left = goods_left;
      if (!delivery_notice->order_id ||
          !delivery_notice->delivery_text) {
        fpv_delivery_notice_destroy(delivery_notice);
        delivery_notice = NULL;
      }
    }
  }

  queue_attempted = true;
  bool queued = fpv_features_queue_message(
      state,
      chat_id,
      chat->title,
      delivery_text,
      true,
      products,
      products_count,
      products_path,
      wants_lot_update,
      runner_tag,
      delivery_notice);
  if (queued && wants_lot_update) {
    update_queued = true;
  }
  if (!queued && products) {
    fpv_products_free(products, products_count);
    products = NULL;
  }

cleanup:
  if (!queue_attempted && products_path && products && products_count > 0) {
    fpv_products_restore(products_path, products, products_count);
  }
  if (!queue_attempted && products) {
    fpv_products_free(products, products_count);
  }
  fpv_free(delivery_text);
  fpv_chat_destroy(chat);
  fpv_free(products_path);
  fpv_free(lot_response);
  fpv_free(lot_products_file);
finish:
  if (wants_lot_update && !update_queued) {
    fpv_features_queue_lot_update(state, runner_tag);
  }
}

static fpv_raise_entry_t* fpv_raise_entry_get(
    fpv_feature_state_t* state,
    uint64_t category_id) {
  if (!state) {
    return NULL;
  }
  for (size_t i = 0; i < state->raise_count; i++) {
    if (state->raise_entries[i].category_id == category_id) {
      return &state->raise_entries[i];
    }
  }
  fpv_raise_entry_t* grown = (fpv_raise_entry_t*)realloc(
      state->raise_entries,
      (state->raise_count + 1) * sizeof(*grown));
  if (!grown) {
    return NULL;
  }
  state->raise_entries = grown;
  fpv_raise_entry_t* entry = &state->raise_entries[state->raise_count++];
  entry->category_id = category_id;
  entry->next_raise_ms = 0;
  return entry;
}

static void fpv_raise_task(void* context) {
  fpv_feature_state_t* state = (fpv_feature_state_t*)context;
  if (!state || !state->scheduler || !state->account) {
    return;
  }

  uint64_t now_ms = fpv_time_now_ms();
  uint32_t next_delay = 10000;
  if (!state->flags.auto_raise) {
    fpv_scheduler_schedule_delay(
        state->scheduler,
        next_delay,
        fpv_raise_task,
        state);
    return;
  }

  uint64_t* subcats = NULL;
  size_t subcat_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_subcategories(
      state->account,
      &subcats,
      &subcat_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || subcat_count == 0) {
    fpv_free(subcats);
    fpv_scheduler_schedule_delay(
        state->scheduler,
        next_delay,
        fpv_raise_task,
        state);
    return;
  }

  size_t target_count = 0;
  uint64_t* categories = NULL;
  uint64_t* counts = NULL;
  uint64_t** grouped = NULL;
  char** category_names = NULL;

  for (size_t i = 0; i < subcat_count; i++) {
    uint64_t category_id = 0;
    const char* category_name = NULL;
    if (!fpv_funpay_account_get_subcategory_category(
            state->account,
            subcats[i],
            &category_id,
            &category_name)) {
      continue;
    }
    size_t index = 0;
    for (; index < target_count; index++) {
      if (categories[index] == category_id) {
        break;
      }
    }
    if (index == target_count) {
      uint64_t* cat_grown = (uint64_t*)realloc(
          categories,
          (target_count + 1) * sizeof(*cat_grown));
      uint64_t* count_grown = (uint64_t*)realloc(
          counts,
          (target_count + 1) * sizeof(*count_grown));
      uint64_t** grouped_grown = (uint64_t**)realloc(
          grouped,
          (target_count + 1) * sizeof(*grouped_grown));
      char** names_grown = (char**)realloc(
          category_names,
          (target_count + 1) * sizeof(*names_grown));
      if (!cat_grown || !count_grown || !grouped_grown || !names_grown) {
        fpv_free(cat_grown);
        fpv_free(count_grown);
        fpv_free(grouped_grown);
        fpv_free(names_grown);
        break;
      }
      categories = cat_grown;
      counts = count_grown;
      grouped = grouped_grown;
      category_names = names_grown;
      categories[target_count] = category_id;
      counts[target_count] = 0;
      grouped[target_count] = NULL;
      category_names[target_count] =
          category_name ? fpv_strdup(category_name) : NULL;
      index = target_count++;
    }

    uint64_t* list = (uint64_t*)realloc(
        grouped[index],
        (counts[index] + 1) * sizeof(*list));
    if (!list) {
      continue;
    }
    grouped[index] = list;
    grouped[index][counts[index]++] = subcats[i];
  }

  fpv_free(subcats);

  uint64_t min_next_ms = 0;
  for (size_t i = 0; i < target_count; i++) {
    fpv_raise_entry_t* entry = fpv_raise_entry_get(state, categories[i]);
    if (!entry) {
      continue;
    }
    if (entry->next_raise_ms > now_ms) {
      if (min_next_ms == 0 || entry->next_raise_ms < min_next_ms) {
        min_next_ms = entry->next_raise_ms;
      }
      continue;
    }

    uint32_t wait_seconds = 0;
    fpv_funpay_error_t raise_error;
    memset(&raise_error, 0, sizeof(raise_error));
    fpv_result_t raise_result = fpv_funpay_account_raise_lots(
        state->account,
        categories[i],
        grouped[i],
        (size_t)counts[i],
        &wait_seconds,
        &raise_error);
    fpv_funpay_error_clear(&raise_error);

    if (raise_result == FPV_OK) {
      entry->next_raise_ms = now_ms + 3600ULL * 1000ULL;
      if (state->telegram && category_names &&
          category_names[i] && category_names[i][0]) {
        fpv_telegram_service_notify_lots_raised(
            state->telegram, category_names[i]);
      }
    } else if (wait_seconds > 0) {
      entry->next_raise_ms = now_ms + (uint64_t)wait_seconds * 1000ULL;
    } else {
      entry->next_raise_ms = now_ms + 10000ULL;
    }

    if (min_next_ms == 0 || entry->next_raise_ms < min_next_ms) {
      min_next_ms = entry->next_raise_ms;
    }
  }

  for (size_t i = 0; i < target_count; i++) {
    fpv_free(grouped[i]);
  }
  fpv_free(grouped);
  fpv_free(categories);
  fpv_free(counts);
  if (category_names) {
    for (size_t i = 0; i < target_count; i++) {
      fpv_free(category_names[i]);
    }
    fpv_free(category_names);
  }

  if (min_next_ms > now_ms) {
    uint64_t diff = min_next_ms - now_ms;
    next_delay = (uint32_t)(diff > UINT32_MAX ? UINT32_MAX : diff);
  }

  fpv_scheduler_schedule_delay(
      state->scheduler,
      next_delay,
      fpv_raise_task,
      state);
}

static void fpv_session_refresh_task(void* context) {
  fpv_feature_state_t* state = (fpv_feature_state_t*)context;
  if (!state || !state->scheduler || !state->account) {
    return;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_refresh(state->account, &error);
  fpv_funpay_error_clear(&error);
  uint32_t next_delay = result == FPV_OK ? 3600000U : 60000U;
  fpv_scheduler_schedule_delay(
      state->scheduler,
      next_delay,
      fpv_session_refresh_task,
      state);
}

fpv_result_t fpv_features_init(
    fpv_feature_state_t** out_state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    fpv_logger_t* logger) {
  if (!out_state || !settings || !storage) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_feature_state_t* state =
      (fpv_feature_state_t*)calloc(1, sizeof(*state));
  if (!state) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_mutex_init(&state->config_mutex)) {
    fpv_features_destroy(state);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_mutex_init(&state->lot_update_mutex)) {
    fpv_features_destroy(state);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  state->logger = logger;
  fpv_result_t apply_result =
      fpv_features_apply_settings(state, settings, locales_dir);
  if (apply_result != FPV_OK) {
    fpv_features_destroy(state);
    return apply_result;
  }

  state->products_dir =
      storage->products_dir ? fpv_strdup(storage->products_dir) : NULL;
  if (storage->cache_dir) {
    state->blacklist_path =
        fpv_path_join(storage->cache_dir, "blacklist.json");
    state->old_users_path =
        fpv_path_join(storage->cache_dir, "old_users.json");
    state->adv_profile_path =
        fpv_path_join(storage->cache_dir, "advProfileStat.json");
  }

  if (storage->config_dir) {
    char* auto_response_path =
        fpv_path_join(storage->config_dir, "auto_response.cfg");
    if (auto_response_path) {
      fpv_result_t ar_result =
          fpv_auto_response_config_load(
              auto_response_path,
              &state->auto_response);
      if (ar_result != FPV_OK) {
        fpv_features_log(state, FPV_LOG_WARNING,
                         "Auto-response config load failed.");
      }
      fpv_free(auto_response_path);
    }

    char* auto_delivery_path =
        fpv_path_join(storage->config_dir, "auto_delivery.cfg");
    if (auto_delivery_path && state->products_dir) {
      fpv_result_t ad_result =
          fpv_auto_delivery_config_load(
              auto_delivery_path,
              state->products_dir,
              &state->auto_delivery);
      if (ad_result != FPV_OK) {
        fpv_features_log(state, FPV_LOG_WARNING,
                         "Auto-delivery config load failed.");
      }
      fpv_free(auto_delivery_path);
    } else {
      fpv_free(auto_delivery_path);
    }
  }

  if (state->blacklist_path) {
    fpv_cache_load_strings(
        state->blacklist_path,
        &state->blacklist);
  }
  if (state->old_users_path) {
    fpv_cache_load_uint64(
        state->old_users_path,
        &state->old_users,
        &state->old_user_count);
  }

  *out_state = state;
  return FPV_OK;
}

void fpv_features_destroy(fpv_feature_state_t* state) {
  if (!state) {
    return;
  }
  fpv_free(state->flags.watermark);
  fpv_free(state->flags.language);
  fpv_free(state->flags.greetings_text);
  fpv_free(state->flags.order_confirm_text);
  for (size_t i = 0; i < 5; i++) {
    fpv_free(state->flags.review_reply_texts[i]);
  }
  fpv_auto_response_config_destroy(&state->auto_response);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  fpv_localizer_destroy(state->localizer);
  fpv_string_list_destroy(&state->blacklist);
  fpv_free(state->old_users);
  fpv_free(state->blacklist_path);
  fpv_free(state->old_users_path);
  fpv_free(state->adv_profile_path);
  fpv_free(state->products_dir);
  fpv_free(state->raise_entries);
  fpv_free(state->last_lot_update_tag);
  fpv_free(state->pending_lot_update_tag);
  fpv_mutex_destroy(&state->config_mutex);
  fpv_mutex_destroy(&state->lot_update_mutex);
  free(state);
}

fpv_result_t fpv_features_update_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir) {
  if (!state || !settings || !storage) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_result_t apply_result =
      fpv_features_apply_settings(state, settings, locales_dir);
  if (apply_result != FPV_OK) {
    return apply_result;
  }

  fpv_free(state->products_dir);
  state->products_dir =
      storage->products_dir ? fpv_strdup(storage->products_dir) : NULL;

  fpv_free(state->blacklist_path);
  fpv_free(state->old_users_path);
  fpv_free(state->adv_profile_path);
  state->blacklist_path = NULL;
  state->old_users_path = NULL;
  state->adv_profile_path = NULL;
  if (storage->cache_dir) {
    state->blacklist_path =
        fpv_path_join(storage->cache_dir, "blacklist.json");
    state->old_users_path =
        fpv_path_join(storage->cache_dir, "old_users.json");
    state->adv_profile_path =
        fpv_path_join(storage->cache_dir, "advProfileStat.json");
  }

  fpv_string_list_destroy(&state->blacklist);
  state->blacklist.items = NULL;
  state->blacklist.count = 0;
  fpv_free(state->old_users);
  state->old_users = NULL;
  state->old_user_count = 0;
  if (state->blacklist_path) {
    fpv_cache_load_strings(state->blacklist_path, &state->blacklist);
  }
  if (state->old_users_path) {
    fpv_cache_load_uint64(
        state->old_users_path,
        &state->old_users,
        &state->old_user_count);
  }

  fpv_auto_response_config_t next_response;
  fpv_auto_delivery_config_t next_delivery;
  memset(&next_response, 0, sizeof(next_response));
  memset(&next_delivery, 0, sizeof(next_delivery));

  if (storage->config_dir) {
    char* auto_response_path =
        fpv_path_join(storage->config_dir, "auto_response.cfg");
    if (auto_response_path) {
      fpv_auto_response_config_load(
          auto_response_path,
          &next_response);
      fpv_free(auto_response_path);
    }

    char* auto_delivery_path =
        fpv_path_join(storage->config_dir, "auto_delivery.cfg");
    if (auto_delivery_path && state->products_dir) {
      fpv_auto_delivery_config_load(
          auto_delivery_path,
          state->products_dir,
          &next_delivery);
      fpv_free(auto_delivery_path);
    } else {
      fpv_free(auto_delivery_path);
    }
  }

  fpv_features_config_lock(state);
  fpv_auto_response_config_destroy(&state->auto_response);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  state->auto_response = next_response;
  state->auto_delivery = next_delivery;
  fpv_features_config_unlock(state);

  if (state->runner) {
    fpv_funpay_runner_set_message_requests(
        state->runner,
        !settings->old_msg_mode);
  }

  return FPV_OK;
}

fpv_result_t fpv_features_reload_auto_response(
    fpv_feature_state_t* state,
    const char* path) {
  if (!state || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_auto_response_config_t next;
  memset(&next, 0, sizeof(next));
  fpv_result_t result = fpv_auto_response_config_load(path, &next);
  if (result != FPV_OK) {
    fpv_auto_response_config_destroy(&next);
    return result;
  }
  fpv_features_config_lock(state);
  fpv_auto_response_config_destroy(&state->auto_response);
  state->auto_response = next;
  fpv_features_config_unlock(state);
  return FPV_OK;
}

fpv_result_t fpv_features_reload_auto_delivery(
    fpv_feature_state_t* state,
    const char* path,
    const char* products_dir) {
  if (!state || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* products = products_dir ? products_dir : state->products_dir;
  fpv_auto_delivery_config_t next;
  memset(&next, 0, sizeof(next));
  fpv_result_t result =
      fpv_auto_delivery_config_load(path, products, &next);
  if (result != FPV_OK) {
    fpv_auto_delivery_config_destroy(&next);
    return result;
  }
  fpv_features_config_lock(state);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  state->auto_delivery = next;
  fpv_features_config_unlock(state);
  return FPV_OK;
}

fpv_result_t fpv_features_reload_blacklist(fpv_feature_state_t* state) {
  if (!state || !state->blacklist_path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_string_list_destroy(&state->blacklist);
  state->blacklist.items = NULL;
  state->blacklist.count = 0;
  return fpv_cache_load_strings(state->blacklist_path, &state->blacklist);
}

void fpv_features_set_telegram(
    fpv_feature_state_t* state,
    fpv_telegram_service_t* telegram) {
  if (!state) {
    return;
  }
  state->telegram = telegram;
}

void fpv_features_attach(
    fpv_feature_state_t* state,
    fpv_funpay_account_t* account,
    fpv_funpay_runner_t* runner,
    fpv_scheduler_t* scheduler,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus) {
  if (!state) {
    return;
  }
  state->account = account;
  state->runner = runner;
  state->scheduler = scheduler;
  state->logger = logger;
  state->bus = bus;
  if (state->telegram) {
    fpv_telegram_service_attach_account(state->telegram, account);
    fpv_telegram_service_attach_runner(state->telegram, runner);
    fpv_telegram_service_update_init_messages(state->telegram);
  }
}

void fpv_features_handle_event(
    fpv_feature_state_t* state,
    const fpv_funpay_event_t* event) {
  if (!state || !event) {
    return;
  }
  switch (event->type) {
    case FPV_FUNPAY_EVENT_INITIAL_CHAT:
      if (state->flags.greetings_cache_init_chats && event->chat &&
          event->chat->id) {
        uint64_t chat_id = strtoull(event->chat->id, NULL, 10);
        if (chat_id > 0) {
          fpv_old_users_add(state, chat_id);
        }
      }
      break;
    case FPV_FUNPAY_EVENT_NEW_MESSAGE:
      if (!state->flags.old_msg_mode) {
        fpv_features_handle_message(state, event->message);
      }
      break;
    case FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED:
      if (state->flags.old_msg_mode) {
        fpv_features_handle_chat(state, event->chat);
      }
      break;
    case FPV_FUNPAY_EVENT_NEW_ORDER:
      fpv_features_handle_order(state, event->order, event->runner_tag);
      break;
    case FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED:
      if (event->order && event->order->status == FPV_ORDER_DELIVERED) {
        const fpv_order_t* order = event->order;
        uint64_t chat_id = 0;
        fpv_chat_t* chat = NULL;
        if (order->chat_id) {
          chat_id = strtoull(order->chat_id, NULL, 10);
        }
        if (state->account && order->buyer_username &&
            order->buyer_username[0] && chat_id == 0) {
          fpv_funpay_error_t error;
          memset(&error, 0, sizeof(error));
          chat = fpv_funpay_account_find_chat_by_name(
              state->account,
              order->buyer_username,
              true,
              &error);
          fpv_funpay_error_clear(&error);
          if (chat && chat->id) {
            chat_id = strtoull(chat->id, NULL, 10);
          }
        }

        if (state->flags.order_confirm_send_reply &&
            state->flags.order_confirm_text &&
            state->flags.order_confirm_text[0] &&
            chat_id > 0) {
          char* reply_text =
              fpv_format_order_text(order, state->flags.order_confirm_text);
          if (reply_text) {
            fpv_features_queue_message(
                state,
                chat_id,
                order->buyer_username,
                reply_text,
                true,
                NULL,
                0,
                NULL,
                false,
                NULL,
                NULL);
            fpv_free(reply_text);
          }
        }

        if (state->telegram && chat_id > 0 &&
            order->id && order->buyer_username) {
          fpv_telegram_service_notify_order_confirmed(
              state->telegram,
              order->id,
              order->buyer_username,
              chat_id);
        }
        fpv_chat_destroy(chat);
      }
      break;
    default:
      break;
  }
}

void fpv_features_schedule_background(fpv_feature_state_t* state) {
  if (!state || !state->scheduler || state->background_scheduled) {
    return;
  }
  state->background_scheduled = true;
  fpv_scheduler_schedule_delay(
      state->scheduler,
      10000,
      fpv_raise_task,
      state);
  fpv_scheduler_schedule_delay(
      state->scheduler,
      3600000,
      fpv_session_refresh_task,
      state);
}
