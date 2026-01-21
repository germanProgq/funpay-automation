/* FunPay Vertex core settings loader implementation. */

#include "core/app/fpv_settings.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_core/fpv_entitlements.h"
#include "fpv_core/fpv_ini.h"
#include "core/base/fpv_string.h"


static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static int fpv_ascii_strcasecmp(const char* left, const char* right) {
  size_t index = 0;
  while (left[index] != '\0' || right[index] != '\0') {
    int a = fpv_ascii_tolower((unsigned char)left[index]);
    int b = fpv_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
    if (left[index] == '\0' || right[index] == '\0') {
      break;
    }
    index++;
  }
  return 0;
}

static bool fpv_parse_bool(const char* value, bool* out) {
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
  if (fpv_ascii_strcasecmp(value, "true") == 0) {
    *out = true;
    return true;
  }
  if (fpv_ascii_strcasecmp(value, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static bool fpv_parse_uint32(const char* value, uint32_t* out) {
  if (!value || !out || !value[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long parsed = strtoul(value, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (uint32_t)parsed;
  return true;
}

static void fpv_settings_apply_entitlements(fpv_settings_t* settings) {
  if (!settings) {
    return;
  }
  fpv_feature_mask_t mask = fpv_feature_mask_for_tier(settings->tier);

  if (!fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_RAISE)) {
    settings->auto_raise = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_RESPONSE)) {
    settings->auto_response = false;
    settings->greetings_send = false;
    settings->order_confirm_send_reply = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_DELIVERY)) {
    settings->auto_delivery = false;
    settings->multi_delivery = false;
    settings->auto_restore = false;
    settings->auto_disable = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_MULTI_DELIVERY)) {
    settings->multi_delivery = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_STATUS_MANAGER)) {
    settings->auto_restore = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_STALE_LOT_DETECTOR)) {
    settings->auto_disable = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_REFUND)) {
    settings->auto_refund = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_BUYER_BLACKLIST)) {
    settings->block_delivery = false;
    settings->block_response = false;
    settings->block_new_message_notification = false;
    settings->block_new_order_notification = false;
    settings->block_command_notification = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_TELEGRAM_NOTIFICATIONS)) {
    settings->telegram_enabled = false;
    settings->include_my_messages = false;
    settings->include_fp_messages = false;
    settings->include_bot_messages = false;
    settings->notify_only_my_messages = false;
    settings->notify_only_fp_messages = false;
    settings->notify_only_bot_messages = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_PROXY_IPV4)) {
    settings->account.proxy.enabled = false;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_WATERMARK)) {
    fpv_free(settings->watermark);
    settings->watermark = NULL;
  }
  if (!fpv_feature_mask_has(mask, FPV_FEATURE_AI_REVIEWS)) {
    settings->review_reply_enabled_all = false;
    for (size_t i = 0; i < 5; i++) {
      settings->review_reply_enabled[i] = false;
    }
  }
}

static fpv_result_t fpv_settings_require(
    const fpv_ini_t* ini,
    const char* section,
    const char* key,
    const char** out_value) {
  const char* value = fpv_ini_get(ini, section, key);
  if (!value) {
    return FPV_ERR_PARSE;
  }
  if (out_value) {
    *out_value = value;
  }
  return FPV_OK;
}

static void fpv_settings_reset(fpv_settings_t* settings) {
  if (!settings) {
    return;
  }
  fpv_free((char*)settings->account.golden_key);
  fpv_free((char*)settings->account.user_agent);
  fpv_free((char*)settings->account.proxy.host);
  fpv_free((char*)settings->account.proxy.username);
  fpv_free((char*)settings->account.proxy.password);
  fpv_free(settings->watermark);
  fpv_free(settings->language);
  fpv_free(settings->greetings_text);
  fpv_free(settings->order_confirm_text);
  fpv_free(settings->telegram_token);
  fpv_free(settings->telegram_secret);
  for (size_t i = 0; i < 5; i++) {
    fpv_free(settings->review_reply_texts[i]);
  }
  memset(settings, 0, sizeof(*settings));
  settings->tier = FPV_TIER_BASIC;
}

fpv_result_t fpv_settings_load(const char* path, fpv_settings_t* settings) {
  if (!path || !settings) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_settings_reset(settings);

  fpv_ini_error_t ini_error;
  fpv_ini_t* ini = fpv_ini_load(path, &ini_error);
  if (!ini) {
    return ini_error.code == FPV_OK ? FPV_ERR_PARSE : ini_error.code;
  }

  const char* golden_key = NULL;
  const char* user_agent = NULL;
  const char* proxy_enable = NULL;
  const char* proxy_ip = NULL;
  const char* proxy_port = NULL;
  const char* proxy_login = NULL;
  const char* proxy_password = NULL;
  const char* telegram_enabled = NULL;
  const char* telegram_token = NULL;
  const char* telegram_secret = NULL;
  const char* requests_delay = NULL;
  const char* language = NULL;
  const char* watermark = NULL;
  const char* auto_raise = NULL;
  const char* auto_response = NULL;
  const char* auto_delivery = NULL;
  const char* multi_delivery = NULL;
  const char* auto_restore = NULL;
  const char* auto_disable = NULL;
  const char* auto_refund = NULL;
  const char* auto_refund_max_stars = NULL;
  const char* old_msg_mode = NULL;
  const char* block_delivery = NULL;
  const char* block_response = NULL;
  const char* block_new_message_notification = NULL;
  const char* block_new_order_notification = NULL;
  const char* block_command_notification = NULL;
  const char* include_my_messages = NULL;
  const char* include_fp_messages = NULL;
  const char* include_bot_messages = NULL;
  const char* notify_only_my_messages = NULL;
  const char* notify_only_fp_messages = NULL;
  const char* notify_only_bot_messages = NULL;
  const char* greetings_cache_init = NULL;
  const char* greetings_ignore_system = NULL;
  const char* greetings_send = NULL;
  const char* greetings_text = NULL;
  const char* order_confirm_send = NULL;
  const char* order_confirm_text = NULL;
  const char* review_reply_master = NULL;
  const char* review_reply_enabled[5] = {0};
  const char* review_reply_text[5] = {0};
  const char* tier = NULL;

  fpv_result_t result = fpv_settings_require(
      ini, "FunPay", "golden_key", &golden_key);
  if (result != FPV_OK || !golden_key[0]) {
    fpv_ini_destroy(ini);
    return FPV_ERR_PARSE;
  }
  user_agent = fpv_ini_get(ini, "FunPay", "user_agent");
  auto_raise = fpv_ini_get(ini, "FunPay", "autoRaise");
  auto_response = fpv_ini_get(ini, "FunPay", "autoResponse");
  auto_delivery = fpv_ini_get(ini, "FunPay", "autoDelivery");
  multi_delivery = fpv_ini_get(ini, "FunPay", "multiDelivery");
  auto_restore = fpv_ini_get(ini, "FunPay", "autoRestore");
  auto_disable = fpv_ini_get(ini, "FunPay", "autoDisable");
  auto_refund = fpv_ini_get(ini, "FunPay", "autoRefund");
  auto_refund_max_stars = fpv_ini_get(ini, "FunPay", "autoRefundMaxStars");
  old_msg_mode = fpv_ini_get(ini, "FunPay", "oldMsgGetMode");

  proxy_enable = fpv_ini_get(ini, "Proxy", "enable");
  proxy_ip = fpv_ini_get(ini, "Proxy", "ip");
  proxy_port = fpv_ini_get(ini, "Proxy", "port");
  proxy_login = fpv_ini_get(ini, "Proxy", "login");
  proxy_password = fpv_ini_get(ini, "Proxy", "password");

  telegram_enabled = fpv_ini_get(ini, "Telegram", "enabled");
  telegram_token = fpv_ini_get(ini, "Telegram", "token");
  telegram_secret = fpv_ini_get(ini, "Telegram", "secretKey");

  tier = fpv_ini_get(ini, "Product", "tier");
  settings->tier = fpv_tier_from_string(tier);

  requests_delay = fpv_ini_get(ini, "Other", "requestsDelay");
  language = fpv_ini_get(ini, "Other", "language");
  watermark = fpv_ini_get(ini, "Other", "watermark");

  block_delivery = fpv_ini_get(ini, "BlockList", "blockDelivery");
  block_response = fpv_ini_get(ini, "BlockList", "blockResponse");
  block_new_message_notification =
      fpv_ini_get(ini, "BlockList", "blockNewMessageNotification");
  block_new_order_notification =
      fpv_ini_get(ini, "BlockList", "blockNewOrderNotification");
  block_command_notification =
      fpv_ini_get(ini, "BlockList", "blockCommandNotification");

  include_my_messages = fpv_ini_get(ini, "NewMessageView", "includeMyMessages");
  include_fp_messages = fpv_ini_get(ini, "NewMessageView", "includeFPMessages");
  include_bot_messages = fpv_ini_get(ini, "NewMessageView", "includeBotMessages");
  notify_only_my_messages =
      fpv_ini_get(ini, "NewMessageView", "notifyOnlyMyMessages");
  notify_only_fp_messages =
      fpv_ini_get(ini, "NewMessageView", "notifyOnlyFPMessages");
  notify_only_bot_messages =
      fpv_ini_get(ini, "NewMessageView", "notifyOnlyBotMessages");

  greetings_cache_init = fpv_ini_get(ini, "Greetings", "cacheInitChats");
  greetings_ignore_system = fpv_ini_get(ini, "Greetings", "ignoreSystemMessages");
  greetings_send = fpv_ini_get(ini, "Greetings", "sendGreetings");
  greetings_text = fpv_ini_get(ini, "Greetings", "greetingsText");

  order_confirm_send = fpv_ini_get(ini, "OrderConfirm", "sendReply");
  order_confirm_text = fpv_ini_get(ini, "OrderConfirm", "replyText");

  review_reply_master = fpv_ini_get(ini, "ReviewReply", "enabled");
  for (size_t i = 0; i < 5; i++) {
    char key_enabled[32];
    char key_text[32];
    snprintf(key_enabled, sizeof(key_enabled), "star%zuReply", i + 1);
    snprintf(key_text, sizeof(key_text), "star%zuReplyText", i + 1);
    review_reply_enabled[i] = fpv_ini_get(ini, "ReviewReply", key_enabled);
    review_reply_text[i] = fpv_ini_get(ini, "ReviewReply", key_text);
  }

  settings->account.golden_key = fpv_strdup(golden_key);
  if (!settings->account.golden_key) {
    fpv_ini_destroy(ini);
    fpv_settings_reset(settings);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (user_agent && user_agent[0]) {
    settings->account.user_agent = fpv_strdup(user_agent);
    if (!settings->account.user_agent) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  settings->account.timeout_ms = 10000;
  bool proxy_enabled = false;
  if (proxy_enable && proxy_enable[0]) {
    fpv_parse_bool(proxy_enable, &proxy_enabled);
  }
  settings->account.proxy.enabled = proxy_enabled;
  if (proxy_ip && proxy_ip[0]) {
    settings->account.proxy.host = fpv_strdup(proxy_ip);
    if (!settings->account.proxy.host) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (proxy_port && proxy_port[0]) {
    uint32_t port = 0;
    if (fpv_parse_uint32(proxy_port, &port)) {
      settings->account.proxy.port = (uint16_t)port;
    }
  }
  if (proxy_login && proxy_login[0]) {
    settings->account.proxy.username = fpv_strdup(proxy_login);
    if (!settings->account.proxy.username) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (proxy_password && proxy_password[0]) {
    settings->account.proxy.password = fpv_strdup(proxy_password);
    if (!settings->account.proxy.password) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  if (auto_raise && auto_raise[0]) {
    fpv_parse_bool(auto_raise, &settings->auto_raise);
  }
  if (auto_response && auto_response[0]) {
    fpv_parse_bool(auto_response, &settings->auto_response);
  }
  if (auto_delivery && auto_delivery[0]) {
    fpv_parse_bool(auto_delivery, &settings->auto_delivery);
  }
  if (multi_delivery && multi_delivery[0]) {
    fpv_parse_bool(multi_delivery, &settings->multi_delivery);
  }
  if (auto_restore && auto_restore[0]) {
    fpv_parse_bool(auto_restore, &settings->auto_restore);
  }
  if (auto_disable && auto_disable[0]) {
    fpv_parse_bool(auto_disable, &settings->auto_disable);
  }
  if (auto_refund && auto_refund[0]) {
    fpv_parse_bool(auto_refund, &settings->auto_refund);
  }
  settings->auto_refund_max_stars = 2;
  if (auto_refund_max_stars && auto_refund_max_stars[0]) {
    uint32_t max_stars = 0;
    if (fpv_parse_uint32(auto_refund_max_stars, &max_stars)) {
      if (max_stars < 1) {
        max_stars = 1;
      }
      if (max_stars > 5) {
        max_stars = 5;
      }
      settings->auto_refund_max_stars = max_stars;
    }
  }
  if (old_msg_mode && old_msg_mode[0]) {
    fpv_parse_bool(old_msg_mode, &settings->old_msg_mode);
  }
  if (block_delivery && block_delivery[0]) {
    fpv_parse_bool(block_delivery, &settings->block_delivery);
  }
  if (block_response && block_response[0]) {
    fpv_parse_bool(block_response, &settings->block_response);
  }
  if (block_new_message_notification && block_new_message_notification[0]) {
    fpv_parse_bool(
        block_new_message_notification,
        &settings->block_new_message_notification);
  }
  if (block_new_order_notification && block_new_order_notification[0]) {
    fpv_parse_bool(
        block_new_order_notification,
        &settings->block_new_order_notification);
  }
  if (block_command_notification && block_command_notification[0]) {
    fpv_parse_bool(
        block_command_notification,
        &settings->block_command_notification);
  }

  if (include_my_messages && include_my_messages[0]) {
    fpv_parse_bool(include_my_messages, &settings->include_my_messages);
  }
  if (include_fp_messages && include_fp_messages[0]) {
    fpv_parse_bool(include_fp_messages, &settings->include_fp_messages);
  }
  if (include_bot_messages && include_bot_messages[0]) {
    fpv_parse_bool(include_bot_messages, &settings->include_bot_messages);
  }
  if (notify_only_my_messages && notify_only_my_messages[0]) {
    fpv_parse_bool(
        notify_only_my_messages,
        &settings->notify_only_my_messages);
  }
  if (notify_only_fp_messages && notify_only_fp_messages[0]) {
    fpv_parse_bool(
        notify_only_fp_messages,
        &settings->notify_only_fp_messages);
  }
  if (notify_only_bot_messages && notify_only_bot_messages[0]) {
    fpv_parse_bool(
        notify_only_bot_messages,
        &settings->notify_only_bot_messages);
  }

  if (greetings_cache_init && greetings_cache_init[0]) {
    fpv_parse_bool(
        greetings_cache_init,
        &settings->greetings_cache_init_chats);
  }
  if (greetings_ignore_system && greetings_ignore_system[0]) {
    fpv_parse_bool(
        greetings_ignore_system,
        &settings->greetings_ignore_system_messages);
  }
  if (greetings_send && greetings_send[0]) {
    fpv_parse_bool(greetings_send, &settings->greetings_send);
  }
  if (greetings_text && greetings_text[0]) {
    settings->greetings_text = fpv_strdup(greetings_text);
    if (!settings->greetings_text) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  if (order_confirm_send && order_confirm_send[0]) {
    fpv_parse_bool(order_confirm_send, &settings->order_confirm_send_reply);
  }
  if (order_confirm_text && order_confirm_text[0]) {
    settings->order_confirm_text = fpv_strdup(order_confirm_text);
    if (!settings->order_confirm_text) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  bool review_reply_master_present =
      review_reply_master && review_reply_master[0];
  bool review_reply_master_value = false;
  bool review_reply_any_enabled = false;
  if (review_reply_master_present) {
    fpv_parse_bool(review_reply_master, &review_reply_master_value);
  }

  for (size_t i = 0; i < 5; i++) {
    bool star_enabled = false;
    if (review_reply_enabled[i] && review_reply_enabled[i][0]) {
      fpv_parse_bool(review_reply_enabled[i], &star_enabled);
    }
    settings->review_reply_enabled[i] = star_enabled;
    if (star_enabled) {
      review_reply_any_enabled = true;
    }
    if (review_reply_text[i] && review_reply_text[i][0]) {
      settings->review_reply_texts[i] = fpv_strdup(review_reply_text[i]);
      if (!settings->review_reply_texts[i]) {
        fpv_ini_destroy(ini);
        fpv_settings_reset(settings);
        return FPV_ERR_OUT_OF_MEMORY;
      }
    }
  }
  if (review_reply_master_present) {
    settings->review_reply_enabled_all = review_reply_master_value;
  } else {
    settings->review_reply_enabled_all = review_reply_any_enabled;
  }

  if (telegram_enabled && telegram_enabled[0]) {
    fpv_parse_bool(telegram_enabled, &settings->telegram_enabled);
  }
  if (telegram_token && telegram_token[0]) {
    settings->telegram_token = fpv_strdup(telegram_token);
    if (!settings->telegram_token) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (telegram_secret && telegram_secret[0]) {
    settings->telegram_secret = fpv_strdup(telegram_secret);
    if (!settings->telegram_secret) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  uint32_t delay = 4;
  if (requests_delay && requests_delay[0]) {
    uint32_t parsed_delay = 0;
    if (fpv_parse_uint32(requests_delay, &parsed_delay)) {
      delay = parsed_delay;
    }
  }
  if (delay < 1) {
    delay = 1;
  }
  if (delay > 100) {
    delay = 100;
  }
  settings->requests_delay_ms = delay * 1000U;

  if (language && language[0]) {
    settings->language = fpv_strdup(language);
    if (!settings->language) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (watermark && watermark[0]) {
    settings->watermark = fpv_strdup(watermark);
    if (!settings->watermark) {
      fpv_ini_destroy(ini);
      fpv_settings_reset(settings);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  settings->runner.disable_message_requests = settings->old_msg_mode;
  settings->runner.disable_order_requests = false;
  settings->runner.requests_delay_ms = settings->requests_delay_ms;

  fpv_settings_apply_entitlements(settings);

  fpv_ini_destroy(ini);
  return FPV_OK;
}

void fpv_settings_destroy(fpv_settings_t* settings) {
  fpv_settings_reset(settings);
}
