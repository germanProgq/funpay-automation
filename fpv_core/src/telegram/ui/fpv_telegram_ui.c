/* FunPay Vertex Telegram UI builders. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* fpv_tg_build_reply_keyboard(
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

char* fpv_tg_build_order_keyboard(
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

char* fpv_tg_build_messages_text(
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

char* fpv_tg_format_toggle_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled) {
  const char* icon = fpv_tg_icon_toggle(enabled);
  return fpv_tg_loc_format(service, key, (const char*[]){icon}, 1);
}

char* fpv_tg_format_bell_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled) {
  const char* icon = fpv_tg_icon_bell(enabled);
  return fpv_tg_loc_format(service, key, (const char*[]){icon}, 1);
}

char* fpv_tg_build_old_keyboard(void) {
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

char* fpv_tg_build_settings_sections(fpv_telegram_service_t* service) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  bool allow_auto_response =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RESPONSE);
  bool allow_auto_delivery =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_DELIVERY);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_core);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_global"), cb_buf, NULL);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_notify);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_notifications"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  bool has_secondary = false;
  if (allow_auto_response) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_reply);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autoresponse"), cb_buf, NULL);
    has_secondary = true;
  }
  if (allow_auto_delivery) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%s", fpv_tg_cbt_category, fpv_tg_menu_delivery);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autodelivery"), cb_buf, NULL);
    has_secondary = true;
  }
  if (has_secondary) {
    fpv_tg_keyboard_row_end(kb);
  }
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_settings_group(
    fpv_telegram_service_t* service,
    const char* group) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !group) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  bool allow_auto_response =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RESPONSE);
  bool allow_auto_delivery =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_DELIVERY);
  bool allow_blacklist =
      fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST);
  bool allow_ai_reviews =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AI_REVIEWS);
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
    if (allow_blacklist) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:bl", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_blacklist"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
  } else if (strcmp(group, fpv_tg_menu_notify) == 0) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:tg", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(
        kb, fpv_tg_loc(service, "mm_notifications_settings"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    snprintf(cb_buf, sizeof(cb_buf), "%s:mv", fpv_tg_cbt_category);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_new_msg_view"), cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  } else if (strcmp(group, fpv_tg_menu_reply) == 0) {
    if (allow_auto_response) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:ar", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_autoresponse"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
      snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_template_list);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_templates"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
      snprintf(cb_buf, sizeof(cb_buf), "%s:gr", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_greetings"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    if (allow_ai_reviews) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:rr", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_review_reply"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
  } else if (strcmp(group, fpv_tg_menu_delivery) == 0) {
    if (allow_auto_delivery) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:ad", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(
          kb, fpv_tg_loc(service, "mm_autodelivery_settings"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    if (allow_auto_response) {
      snprintf(cb_buf, sizeof(cb_buf), "%s:oc", fpv_tg_cbt_category);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "mm_order_confirm"), cb_buf, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
  } else {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }

  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_main_settings(
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

char* fpv_tg_build_new_message_view_settings(
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

char* fpv_tg_build_greeting_settings(
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

char* fpv_tg_build_order_confirm_settings(
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

char* fpv_tg_build_review_reply_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb || !settings) {
    fpv_tg_keyboard_destroy(kb);
    return NULL;
  }
  if (!fpv_tg_has_entitlement(service, FPV_FEATURE_AI_REVIEWS)) {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main2, NULL);
    fpv_tg_keyboard_row_end(kb);
    return fpv_tg_keyboard_finalize(kb, false);
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

char* fpv_tg_build_notifications_settings(
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

  {
    bool enabled = fpv_tg_is_notification_enabled(service, (int64_t)chat_id, "14");
    const char* icon = fpv_tg_icon_bell(enabled);
    char label_buf[64];
    snprintf(label_buf, sizeof(label_buf), "%s SRAS monitor", icon);
    snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":14", fpv_tg_cbt_switch_tg, chat_id);
    fpv_tg_keyboard_add_button(kb, label_buf, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
  }

  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_blacklist_settings(
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

char* fpv_tg_build_auto_response_settings(fpv_telegram_service_t* service) {
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

char* fpv_tg_build_auto_delivery_settings(fpv_telegram_service_t* service) {
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

char* fpv_tg_build_configs_uploader(fpv_telegram_service_t* service) {
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

char* fpv_tg_build_profile_keyboard(
    fpv_telegram_service_t* service,
    bool advanced) {
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  bool allow_advanced =
      fpv_tg_has_entitlement(service, FPV_FEATURE_SALES_STATS);
  if (advanced) {
    if (!allow_advanced) {
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"),
                                 fpv_tg_cb_update_profile, NULL);
      fpv_tg_keyboard_row_end(kb);
      return fpv_tg_keyboard_finalize(kb, false);
    }
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"),
                               fpv_tg_cb_update_adv_profile, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"),
                               fpv_tg_cb_update_profile, NULL);
  } else {
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_refresh"),
                               fpv_tg_cb_update_profile, NULL);
    if (allow_advanced) {
      fpv_tg_keyboard_add_button(kb, "\xE2\x96\xB6\xEF\xB8\x8F \xD0\x95\xD1\x89\xD0\xB5",
                                 fpv_tg_cb_update_adv_profile, NULL);
    }
  }
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_command_info_text(
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

char* fpv_tg_build_commands_list_keyboard(
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

char* fpv_tg_build_edit_command_keyboard(
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

char* fpv_tg_build_cmd_refresh_keyboard(fpv_telegram_service_t* service) {
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

char* fpv_tg_build_refresh_keyboard(
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

char* fpv_tg_build_ar_add_error_keyboard(fpv_telegram_service_t* service) {
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

char* fpv_tg_build_ar_add_success_keyboard(
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

char* fpv_tg_build_ar_edit_done_keyboard(
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

char* fpv_tg_build_products_files_list(
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

char* fpv_tg_build_products_file_edit_keyboard(
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

char* fpv_tg_build_products_file_text(
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

char* fpv_tg_build_lots_list(
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

char* fpv_tg_build_funpay_lots_list(
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

char* fpv_tg_build_lot_info_text(
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

char* fpv_tg_build_edit_lot_keyboard(
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
  bool allow_auto_delivery =
      fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_DELIVERY);
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

  const char* icon_delivery = fpv_tg_icon_lot_state(allow_auto_delivery, disabled);
  char* label = fpv_tg_loc_format(
      service, "ea_delivery", (const char*[]){icon_delivery}, 1);
  snprintf(cb_buf, sizeof(cb_buf), "%s:disable:%zu:%zu", fpv_tg_cb_switch_lot, lot_index, offset);
  fpv_tg_keyboard_add_button(kb, label,
                             allow_auto_delivery ? cb_buf : fpv_tg_cbt_param_disabled,
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
