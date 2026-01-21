#include "features/runtime/fpv_features_internal.h"


fpv_result_t fpv_features_apply_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const char* locales_dir) {
  if (!state || !settings) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_feature_mask_t mask = fpv_feature_mask_for_tier(settings->tier);
  state->entitlements = mask;
  bool allow_auto_raise =
      fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_RAISE);
  bool allow_auto_response =
      fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_RESPONSE);
  bool allow_auto_delivery =
      fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_DELIVERY);
  bool allow_multi_delivery =
      fpv_feature_mask_has(mask, FPV_FEATURE_MULTI_DELIVERY);
  bool allow_status_manager =
      fpv_feature_mask_has(mask, FPV_FEATURE_STATUS_MANAGER);
  bool allow_stale_detector =
      fpv_feature_mask_has(mask, FPV_FEATURE_STALE_LOT_DETECTOR);
  bool allow_blacklist =
      fpv_feature_mask_has(mask, FPV_FEATURE_BUYER_BLACKLIST);
  bool allow_telegram =
      fpv_feature_mask_has(mask, FPV_FEATURE_TELEGRAM_NOTIFICATIONS);
  bool allow_watermark =
      fpv_feature_mask_has(mask, FPV_FEATURE_WATERMARK);
  bool allow_ai_reviews =
      fpv_feature_mask_has(mask, FPV_FEATURE_AI_REVIEWS);
  bool allow_auto_refund =
      fpv_feature_mask_has(mask, FPV_FEATURE_AUTO_REFUND);

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

  if (!allow_watermark && watermark) {
    fpv_free(watermark);
    watermark = NULL;
  }
  if (!allow_ai_reviews) {
    for (size_t i = 0; i < 5; i++) {
      fpv_free(review_texts[i]);
      review_texts[i] = NULL;
    }
  }

  state->flags.auto_raise = settings->auto_raise && allow_auto_raise;
  state->flags.auto_response = settings->auto_response && allow_auto_response;
  state->flags.auto_delivery = settings->auto_delivery && allow_auto_delivery;
  state->flags.multi_delivery =
      settings->multi_delivery && allow_auto_delivery && allow_multi_delivery;
  state->flags.auto_restore =
      settings->auto_restore && allow_auto_delivery && allow_status_manager;
  state->flags.auto_disable =
      settings->auto_disable && allow_auto_delivery && allow_stale_detector;
  state->flags.auto_refund = settings->auto_refund && allow_auto_refund;
  uint32_t max_stars = settings->auto_refund_max_stars;
  if (max_stars < 1) {
    max_stars = 1;
  }
  if (max_stars > 5) {
    max_stars = 5;
  }
  state->flags.auto_refund_max_stars = max_stars;
  state->flags.old_msg_mode = settings->old_msg_mode;
  state->flags.block_delivery = settings->block_delivery && allow_blacklist;
  state->flags.block_response = settings->block_response && allow_blacklist;
  state->flags.block_new_message_notification =
      settings->block_new_message_notification && allow_blacklist;
  state->flags.block_new_order_notification =
      settings->block_new_order_notification && allow_blacklist;
  state->flags.block_command_notification =
      settings->block_command_notification && allow_blacklist;
  state->flags.include_my_messages =
      settings->include_my_messages && allow_telegram;
  state->flags.include_fp_messages =
      settings->include_fp_messages && allow_telegram;
  state->flags.include_bot_messages =
      settings->include_bot_messages && allow_telegram;
  state->flags.notify_only_my_messages =
      settings->notify_only_my_messages && allow_telegram;
  state->flags.notify_only_fp_messages =
      settings->notify_only_fp_messages && allow_telegram;
  state->flags.notify_only_bot_messages =
      settings->notify_only_bot_messages && allow_telegram;
  state->flags.greetings_cache_init_chats =
      settings->greetings_cache_init_chats && allow_auto_response;
  state->flags.greetings_ignore_system_messages =
      settings->greetings_ignore_system_messages && allow_auto_response;
  state->flags.greetings_send =
      settings->greetings_send && allow_auto_response;
  state->flags.order_confirm_send_reply =
      settings->order_confirm_send_reply && allow_auto_response;
  state->flags.review_reply_enabled_all =
      settings->review_reply_enabled_all && allow_ai_reviews;
  for (size_t i = 0; i < 5; i++) {
    state->flags.review_reply_enabled[i] =
        settings->review_reply_enabled[i] && allow_ai_reviews;
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


bool fpv_features_has_entitlement(
    const fpv_feature_state_t* state,
    fpv_feature_flag_t feature) {
  if (!state) {
    return false;
  }
  return fpv_feature_mask_has(state->entitlements, feature);
}


void fpv_features_config_lock(fpv_feature_state_t* state) {
  if (state && state->config_mutex.initialized) {
    fpv_mutex_lock(&state->config_mutex);
  }
}


void fpv_features_config_unlock(fpv_feature_state_t* state) {
  if (state && state->config_mutex.initialized) {
    fpv_mutex_unlock(&state->config_mutex);
  }
}
