/* FunPay Vertex Telegram auto-delivery management. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fpv_tg_reload_auto_delivery(fpv_telegram_service_t* service) {
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

bool fpv_tg_check_ad_lot_index(
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

bool fpv_tg_check_products_file_index(
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

char* fpv_tg_build_ad_edit_done_keyboard(
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

void fpv_tg_open_ad_lots_list(
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

void fpv_tg_open_funpay_lots_list(
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

void fpv_tg_open_products_files_list(
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

void fpv_tg_open_products_file_editor(
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

void fpv_tg_open_edit_ad_lot(
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

void fpv_tg_prompt_ad_edit_text(
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

bool fpv_tg_add_delivery_test(
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

void fpv_tg_add_ad_lot_from_funpay(
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

void fpv_tg_toggle_lot_setting(
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

void fpv_tg_create_delivery_test(
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

void fpv_tg_delete_ad_lot(
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

void fpv_tg_update_funpay_lots_list(
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

void fpv_tg_send_products_file(
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

void fpv_tg_confirm_delete_products_file(
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

bool fpv_tg_handle_ad_add_lot_manual(
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

bool fpv_tg_handle_ad_edit_delivery_text(
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

bool fpv_tg_handle_ad_bind_products(
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

bool fpv_tg_handle_create_products_file(
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

char* fpv_tg_build_products_payload(
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

bool fpv_tg_handle_add_products(
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

bool fpv_tg_handle_manual_ad_test(
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

bool fpv_tg_handle_ad_state(
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
