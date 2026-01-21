/* FunPay Vertex Telegram menus and state prompts. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fpv_tg_send_cfg_not_found(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* name);

char* fpv_tg_build_desc_with_text(
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

char* fpv_tg_build_desc_with_chat_id(
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

void fpv_tg_send_menu(fpv_telegram_service_t* service, int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  char* reply_markup = fpv_tg_build_settings_sections(service);
  fpv_tg_send_message(service, chat_id,
                      fpv_tg_loc(service, "desc_main"),
                      reply_markup, NULL);
  fpv_free(reply_markup);
}

void fpv_tg_open_main_sections(
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

void fpv_tg_open_settings_category(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* category) {
  if (!service || !callback || !category) {
    return;
  }
  const char* unavailable = "Feature not available in current tier.";
  if (strcmp(category, "rr") == 0 &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_AI_REVIEWS)) {
    fpv_tg_answer_callback(service, callback->id, unavailable, true);
    return;
  }
  if (strcmp(category, "bl") == 0 &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST)) {
    fpv_tg_answer_callback(service, callback->id, unavailable, true);
    return;
  }
  if (strcmp(category, "ad") == 0 &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_DELIVERY)) {
    fpv_tg_answer_callback(service, callback->id, unavailable, true);
    return;
  }
  if (strcmp(category, "ar") == 0 &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RESPONSE)) {
    fpv_tg_answer_callback(service, callback->id, unavailable, true);
    return;
  }
  if ((strcmp(category, "tg") == 0 || strcmp(category, "mv") == 0) &&
      !fpv_tg_has_entitlement(service, FPV_FEATURE_TELEGRAM_NOTIFICATIONS)) {
    fpv_tg_answer_callback(service, callback->id, unavailable, true);
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

const char* fpv_tg_category_for_section(const char* section) {
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

bool fpv_tg_setting_allowed(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key) {
  if (!section || !key) {
    return false;
  }
  if (strcmp(section, "FunPay") == 0) {
    if (strcmp(key, "autoRaise") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RAISE);
    }
    if (strcmp(key, "autoResponse") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RESPONSE);
    }
    if (strcmp(key, "autoDelivery") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_DELIVERY);
    }
    if (strcmp(key, "multiDelivery") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_MULTI_DELIVERY);
    }
    if (strcmp(key, "autoRestore") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_STATUS_MANAGER);
    }
    if (strcmp(key, "autoDisable") == 0) {
      return fpv_tg_has_entitlement(service, FPV_FEATURE_STALE_LOT_DETECTOR);
    }
  }
  if (strcmp(section, "BlockList") == 0) {
    return fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST);
  }
  if (strcmp(section, "ReviewReply") == 0) {
    return fpv_tg_has_entitlement(service, FPV_FEATURE_AI_REVIEWS);
  }
  if (strcmp(section, "NewMessageView") == 0) {
    return fpv_tg_has_entitlement(service, FPV_FEATURE_TELEGRAM_NOTIFICATIONS);
  }
  if (strcmp(section, "Greetings") == 0 || strcmp(section, "OrderConfirm") == 0) {
    return fpv_tg_has_entitlement(service, FPV_FEATURE_AUTO_RESPONSE);
  }
  return true;
}

void fpv_tg_toggle_setting(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* section,
    const char* key) {
  if (!service || !callback || !section || !key) {
    return;
  }
  if (!fpv_tg_setting_allowed(service, section, key)) {
    fpv_tg_answer_callback(
        service,
        callback->id,
        "Feature not available in current tier.",
        true);
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

void fpv_tg_toggle_notification(
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

void fpv_tg_switch_language(
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

void fpv_tg_handle_old_help(
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

void fpv_tg_prompt_state(
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

void fpv_tg_prompt_state_from_callback(
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

void fpv_tg_prompt_state_with_data(
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

bool fpv_tg_handle_document_upload(
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

bool fpv_tg_handle_photo_upload(
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

bool fpv_tg_handle_upload_state(
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

bool fpv_tg_handle_send_fp_message(
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
bool fpv_tg_handle_misc_state(
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

void fpv_tg_send_cfg_not_found(
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
