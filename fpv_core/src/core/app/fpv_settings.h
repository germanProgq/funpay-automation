/* FunPay Vertex core settings loader. */

#ifndef FPV_SETTINGS_H
#define FPV_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "fpv_core/fpv_entitlements.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_result.h"

typedef struct fpv_settings {
  fpv_funpay_account_config_t account;
  fpv_funpay_runner_config_t runner;
  fpv_product_tier_t tier;
  bool auto_raise;
  bool auto_response;
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
  bool telegram_enabled;
  char* telegram_token;
  char* telegram_secret;
  uint32_t requests_delay_ms;
  char* watermark;
  char* language;
} fpv_settings_t;

fpv_result_t fpv_settings_load(const char* path, fpv_settings_t* settings);
void fpv_settings_destroy(fpv_settings_t* settings);

#endif
