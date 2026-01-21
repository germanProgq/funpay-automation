/* FunPay Vertex feature engine internal definitions. */

#ifndef FPV_FEATURES_INTERNAL_H
#define FPV_FEATURES_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "features/runtime/fpv_features.h"


#include "core/io/fpv_cache.h"

#include "core/app/fpv_localization.h"

#include "core/base/fpv_platform.h"

#include "core/data/fpv_products.h"

#include "core/base/fpv_string.h"

#include "telegram/core/fpv_telegram.h"

#include "core/base/fpv_time.h"


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
  bool auto_refund;
  uint32_t auto_refund_max_stars;
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
  bool review_reply_enabled_all;
  bool review_reply_enabled[5];
  char* review_reply_texts[5];
  uint32_t requests_delay_ms;
  char* watermark;
  char* language;
} fpv_feature_flags_t;

struct fpv_feature_state {
  fpv_feature_flags_t flags;
  fpv_feature_mask_t entitlements;
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
  bool sras_active;
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

void fpv_features_log(
    fpv_feature_state_t* state,
    fpv_log_level_t level,
    const char* message);
void fpv_sleep_ms(uint32_t delay_ms);
bool fpv_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len);
bool fpv_buffer_append_char(
    char** buffer,
    size_t* length,
    size_t* capacity,
    char ch);
char* fpv_trim_copy(const char* value);
char* fpv_replace_all(
    const char* text,
    const char* key,
    const char* value);
void fpv_features_config_lock(fpv_feature_state_t* state);
void fpv_features_config_unlock(fpv_feature_state_t* state);
fpv_result_t fpv_features_apply_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const char* locales_dir);
bool fpv_features_is_sras_error(const fpv_funpay_error_t* error);
char* fpv_features_format_delivery_error(
    fpv_feature_state_t* state,
    const char* order_id);
void fpv_features_notify_delivery_error(
    fpv_feature_state_t* state,
    const char* order_id,
    const char* buyer_username);
char* fpv_format_message_text(
    const fpv_message_t* message,
    const fpv_chat_t* chat,
    const char* text);
char* fpv_format_order_text(const fpv_order_t* order, const char* text);
char* fpv_format_order_detail_text(
    const fpv_funpay_order_detail_t* detail,
    const char* text);
bool fpv_parse_message_entities(
    const char* text,
    fpv_message_entity_t** out_entities,
    size_t* out_count);
void fpv_free_message_entities(
    fpv_message_entity_t* entities,
    size_t count);
void fpv_delivery_notice_destroy(fpv_delivery_notice_t* notice);
bool fpv_features_queue_message(
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
    fpv_delivery_notice_t* delivery_notice);
void fpv_features_queue_lot_update(
    fpv_feature_state_t* state,
    const char* runner_tag);

#endif
