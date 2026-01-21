#include "features/runtime/fpv_features_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/io/fpv_fs.h"

#include "core/data/fpv_json.h"


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

static void fpv_features_handle_order(
    fpv_feature_state_t* state,
    const fpv_order_t* order,
    const char* runner_tag);

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


static bool fpv_starts_with(const char* text, const char* prefix) {
  if (!text || !prefix) {
    return false;
  }
  size_t prefix_len = strlen(prefix);
  return strncmp(text, prefix, prefix_len) == 0;
}

static uint32_t fpv_parse_order_amount(const char* title) {
  if (!title) {
    return 1;
  }
  const char* cursor = title + strlen(title);
  while (cursor > title) {
    while (cursor > title && !isdigit((unsigned char)cursor[-1])) {
      cursor--;
    }
    if (cursor == title) {
      break;
    }
    const char* digit_end = cursor;
    while (cursor > title && isdigit((unsigned char)cursor[-1])) {
      cursor--;
    }
    const char* digit_start = cursor;
    char before = digit_start > title ? digit_start[-1] : '\0';
    if (before == '.' || before == ',') {
      continue;
    }
    size_t len = (size_t)(digit_end - digit_start);
    if (len == 0 || len > 10) {
      continue;
    }
    char temp[16];
    if (len >= sizeof(temp)) {
      continue;
    }
    memcpy(temp, digit_start, len);
    temp[len] = '\0';
    char* end = NULL;
    unsigned long long parsed = strtoull(temp, &end, 10);
    if (!end || *end != '\0' || parsed == 0) {
      continue;
    }
    if (parsed > UINT32_MAX) {
      return UINT32_MAX;
    }
    return (uint32_t)parsed;
  }
  return 1;
}

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

  bool refund_attempted = false;
  bool refund_success = false;
  if (state->flags.auto_refund &&
      state->flags.auto_refund_max_stars > 0 &&
      stars <= (int)state->flags.auto_refund_max_stars &&
      detail.status != FPV_ORDER_REFUNDED &&
      detail.status != FPV_ORDER_CANCELLED) {
    refund_attempted = true;
    int attempts = 3;
    while (attempts > 0) {
      fpv_funpay_error_t refund_error;
      memset(&refund_error, 0, sizeof(refund_error));
      fpv_result_t refund_result =
          fpv_funpay_account_refund(state->account, order_id, &refund_error);
      fpv_funpay_error_clear(&refund_error);
      if (refund_result == FPV_OK) {
        refund_success = true;
        break;
      }
      attempts--;
      if (attempts > 0) {
        fpv_sleep_ms(1000);
      }
    }
    char log_buf[256];
    snprintf(log_buf, sizeof(log_buf),
             "Auto refund %s for order %s (review %d star%s).",
             refund_success ? "succeeded" : "failed",
             order_id,
             stars,
             stars == 1 ? "" : "s");
    fpv_features_log(
        state,
        refund_success ? FPV_LOG_INFO : FPV_LOG_WARNING,
        log_buf);
  }

  char* reply_text = NULL;
  if (state->flags.review_reply_enabled_all &&
      state->flags.review_reply_enabled[stars - 1] &&
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

  if (refund_attempted && state->telegram) {
    fpv_telegram_service_notify_refund(
        state->telegram,
        order_id,
        refund_success);
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
