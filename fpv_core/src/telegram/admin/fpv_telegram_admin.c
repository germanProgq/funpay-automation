/* FunPay Vertex Telegram admin and diagnostics helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

char* fpv_tg_build_power_off_keyboard(
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

bool fpv_tg_is_profile_missing_stats(const fpv_funpay_error_t* error) {
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

void fpv_tg_log_funpay_error(
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

void fpv_tg_send_profile(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_update_profile_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    bool advanced) {
  if (!service || !callback) {
    return;
  }
  if (advanced &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_SALES_STATS)) {
    fpv_tg_send_feature_unavailable(service, callback->chat_id);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
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

void fpv_tg_send_sysinfo(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_send_all_settings(fpv_telegram_service_t* service, int64_t chat_id) {
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
  bool allow_blacklist =
      fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST);
  bool allow_ai_reviews =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AI_REVIEWS);
  bool allow_watermark =
      fpv_tg_has_entitlement(service, FPV_FEATURE_WATERMARK);

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

  if (allow_blacklist) {
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
  }

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

  if (allow_ai_reviews) {
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
  }

  if (allow_watermark) {
    fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
    fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "cmd_watermark"));
    fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
    fpv_tg_append_code_value(&text, &length, &capacity, settings.watermark);
  }
  fpv_tg_buffer_append_str(&text, &length, &capacity, "<b>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, fpv_tg_loc(service, "cmd_language"));
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</b>\n");
  fpv_tg_append_code_value(&text, &length, &capacity, settings.language);
  fpv_tg_flush_message_buffer(service, chat_id, &text, &length, &capacity);
  fpv_settings_destroy(&settings);
}

void fpv_tg_send_about(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_send_logs(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_delete_logs(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_send_blacklist(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (!fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST)) {
    fpv_tg_send_feature_unavailable(service, chat_id);
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

void fpv_tg_send_old_orders(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (!fpv_tg_has_entitlement(service, FPV_FEATURE_OLD_ORDERS_SCANNER)) {
    fpv_tg_send_feature_unavailable(service, chat_id);
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

void fpv_tg_change_cookie(
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

void fpv_tg_open_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_close_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_restart(fpv_telegram_service_t* service, int64_t chat_id) {
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

void fpv_tg_power_off(fpv_telegram_service_t* service, int64_t chat_id) {
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

char* fpv_tg_build_empty_inline_keyboard(void) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  return fpv_tg_keyboard_finalize(kb, false);
}

const char* fpv_tg_power_off_state_key(int state) {
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

void fpv_tg_cancel_shutdown_callback(
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

void fpv_tg_handle_shutdown_callback(
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
