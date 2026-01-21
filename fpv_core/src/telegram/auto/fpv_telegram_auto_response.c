/* FunPay Vertex Telegram auto-response management. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fpv_tg_reload_auto_response(fpv_telegram_service_t* service) {
  if (!service || !service->features) {
    return;
  }
  char* path = fpv_tg_config_path(service, "auto_response.cfg");
  if (!path) {
    return;
  }
  fpv_features_reload_auto_response(service->features, path);
  fpv_free(path);
}

bool fpv_tg_check_ar_command_index(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index,
    int64_t chat_id,
    int message_id,
    bool edit_message) {
  if (!ini) {
    return false;
  }
  size_t total = fpv_ini_section_count(ini);
  if (command_index < total) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", command_index);
  char* text = fpv_tg_loc_format(
      service,
      "ar_cmd_not_found_err",
      (const char*[]){index_buf},
      1);
  char* reply_markup = fpv_tg_build_cmd_refresh_keyboard(service);
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

void fpv_tg_open_ar_commands_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* reply_markup = fpv_tg_build_commands_list_keyboard(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ar_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_open_ar_command_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  char* text = fpv_tg_build_command_info_text(service, ini, command_index);
  char* reply_markup =
      fpv_tg_build_edit_command_keyboard(service, ini, command_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_prompt_ar_edit_text(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t state,
    const char* prompt_key,
    size_t command_index,
    size_t offset) {
  if (!service || !callback || !prompt_key) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
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
      "v_message_text",
      "v_chat_id",
      "v_photo"};
  char* text = fpv_tg_build_variable_prompt(
      service,
      prompt_key,
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
  data.command_index = (int)command_index;
  data.offset = (int)offset;
  fpv_tg_set_state(service, callback->chat_id, callback->from_id,
                   prompt_id, state, &data);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_toggle_ar_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* notification =
      fpv_ini_get(ini, command, "telegramNotification");
  bool enabled = false;
  if (notification && notification[0]) {
    fpv_tg_parse_bool(notification, &enabled);
  }
  const char* next = enabled ? "0" : "1";
  bool ok = fpv_ini_set(ini, command, "telegramNotification", next) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_response(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_param_changed",
        (const char*[]){uname, id_buf, "telegramNotification", command, next},
        5);
  }
  char* text = fpv_tg_build_command_info_text(service, ini, command_index);
  char* reply_markup =
      fpv_tg_build_edit_command_keyboard(service, ini, command_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_delete_ar_command(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, callback->chat_id, "auto_response.cfg");
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_ini_destroy(ini);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  bool ok = command &&
      fpv_ini_remove_section(ini, command) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (ok) {
    fpv_tg_reload_auto_response(service);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
    const char* uname = callback->from_username ? callback->from_username : "";
    fpv_tg_log_format(
        service,
        FPV_LOG_INFO,
        "log_ar_cmd_deleted",
        (const char*[]){uname, id_buf, command},
        3);
  }
  char* reply_markup = fpv_tg_build_commands_list_keyboard(service, ini, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_ar_list"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_ini_destroy(ini);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

bool fpv_tg_handle_ar_add_command(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message) {
  if (!service || !message) {
    return true;
  }
  if (!message->text || !message->text[0]) {
    return true;
  }
  fpv_tg_clear_state(service, message->chat_id, message->from_id, true);
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  char* raw_command = fpv_strdup(message->text);
  if (!raw_command || !fpv_tg_trim_lower(raw_command)) {
    char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "ar_enter_new_cmd"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  char* raw_copy = fpv_strdup(raw_command);
  if (!raw_copy) {
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_string_list_t list;
  memset(&list, 0, sizeof(list));
  bool error = false;
  char* token = strtok(raw_copy, "|");
  while (token) {
    if (!fpv_tg_trim_lower(token)) {
      token = strtok(NULL, "|");
      continue;
    }
    if (fpv_tg_string_list_contains(&list, token)) {
      char* escaped = fpv_tg_escape_html(token);
      char* text = fpv_tg_loc_format(
          service,
          "ar_subcmd_duplicate_err",
          (const char*[]){escaped ? escaped : ""},
          1);
      char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
      fpv_tg_send_message(
          service,
          message->chat_id,
          text ? text : "",
          reply_markup,
          NULL);
      fpv_free(reply_markup);
      fpv_free(text);
      fpv_free(escaped);
      error = true;
      break;
    }
    if (fpv_tg_ar_command_exists(ini, token)) {
      char* escaped = fpv_tg_escape_html(token);
      char* text = fpv_tg_loc_format(
          service,
          "ar_cmd_already_exists_err",
          (const char*[]){escaped ? escaped : ""},
          1);
      char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
      fpv_tg_send_message(
          service,
          message->chat_id,
          text ? text : "",
          reply_markup,
          NULL);
      fpv_free(reply_markup);
      fpv_free(text);
      fpv_free(escaped);
      error = true;
      break;
    }
    if (!fpv_tg_string_list_add(&list, token)) {
      error = true;
      break;
    }
    token = strtok(NULL, "|");
  }
  if (!error && list.count == 0) {
    char* reply_markup = fpv_tg_build_ar_add_error_keyboard(service);
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "ar_enter_new_cmd"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    error = true;
  }
  fpv_string_list_destroy(&list);
  fpv_free(raw_copy);
  if (error) {
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  bool ok = fpv_ini_set(
      ini,
      raw_command,
      "response",
      "\xD0\x94\xD0\xb0\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xb0\xD0\xbd\xD0\xb4\xD0\xb5 \xD0\xBD\xD0\xb5\xD0\xBE\xD0\xb1\xD1\x85\xD0\xBE\xD0\xb4\xD0\xb8\xD0\xbc\xD0\xBE \xD0\xBD\xD0\xb0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xb8\xD1\x82\xD1\x8C \xD1\x82\xD0\xb5\xD0\xba\xD1\x81\xD1\x82 \xD0\xBE\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD0\xb0 :(") == FPV_OK &&
      fpv_ini_set(ini, raw_command, "telegramNotification", "0") == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(raw_command);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  size_t command_index = fpv_ini_section_count(ini);
  if (command_index > 0) {
    command_index--;
  }
  size_t offset = fpv_tg_get_offset(command_index, fpv_tg_cmd_page);
  char* reply_markup =
      fpv_tg_build_ar_add_success_keyboard(service, command_index, offset);
  char* escaped = fpv_tg_escape_html(raw_command);
  char* text = fpv_tg_loc_format(
      service,
      "ar_cmd_added",
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
      "log_ar_added",
      (const char*[]){uname, id_buf, raw_command},
      3);
  fpv_free(raw_command);
  fpv_ini_destroy(ini);
  return true;
}

bool fpv_tg_handle_ar_edit_response(
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
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  size_t command_index = data->command_index < 0 ? 0 : (size_t)data->command_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* response_text = fpv_tg_trim_copy(message->text);
  if (!response_text) {
    fpv_ini_destroy(ini);
    return true;
  }
  if (!response_text[0]) {
    fpv_free(response_text);
    response_text = fpv_strdup(
        "\xD0\x94\xD0\xb0\xD0\xBD\xD0\xBD\xD0\xBE\xD0\xb9 \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xb0\xD0\xbd\xD0\xb4\xD0\xb5 \xD0\xBD\xD0\xb5\xD0\xBE\xD0\xb1\xD1\x85\xD0\xBE\xD0\xb4\xD0\xb8\xD0\xbc\xD0\xBE \xD0\xBD\xD0\xb0\xD1\x81\xD1\x82\xD1\x80\xD0\xbe\xD0\xb8\xD1\x82\xD1\x8c \xD1\x82\xD0\xb5\xD0\xba\xD1\x81\xD1\x82 \xD0\xbe\xD1\x82\xD0\xb2\xD0\xb5\xD1\x82\xD0\xb0 :(");
    if (!response_text) {
      fpv_ini_destroy(ini);
      return true;
    }
  }
  bool ok = fpv_ini_set(ini, command, "response", response_text) == FPV_OK &&
      fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(response_text);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  char* reply_markup = fpv_tg_build_ar_edit_done_keyboard(
      service, fpv_tg_cbt_edit_cmd_response, command_index, offset);
  char* escaped_cmd = fpv_tg_escape_html(command);
  char* escaped_resp = fpv_tg_escape_html(response_text);
  char* text = fpv_tg_loc_format(
      service,
      "ar_response_text_changed",
      (const char*[]){escaped_cmd ? escaped_cmd : "",
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
  fpv_free(escaped_cmd);
  fpv_free(escaped_resp);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ar_response_text_changed",
      (const char*[]){uname, id_buf, command, response_text},
      4);
  fpv_free(response_text);
  fpv_ini_destroy(ini);
  return true;
}

bool fpv_tg_handle_ar_edit_notification(
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
  fpv_ini_t* ini = fpv_tg_load_ini(service, "auto_response.cfg");
  if (!ini) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    return true;
  }
  size_t command_index = data->command_index < 0 ? 0 : (size_t)data->command_index;
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  if (!fpv_tg_check_ar_command_index(
          service,
          ini,
          command_index,
          message->chat_id,
          0,
          false)) {
    fpv_ini_destroy(ini);
    return true;
  }
  const char* command = fpv_ini_section_name(ini, command_index);
  if (!command) {
    fpv_ini_destroy(ini);
    return true;
  }
  char* notification_text = fpv_tg_trim_copy(message->text);
  if (!notification_text) {
    fpv_ini_destroy(ini);
    return true;
  }
  bool ok = true;
  if (notification_text[0]) {
    ok = fpv_ini_set(ini, command, "notificationText", notification_text) == FPV_OK;
  } else {
    ok = fpv_ini_remove_entry(ini, command, "notificationText") == FPV_OK;
  }
  ok = ok && fpv_tg_save_ini(service, "auto_response.cfg", ini);
  if (!ok) {
    fpv_tg_send_cfg_not_found(service, message->chat_id, "auto_response.cfg");
    fpv_free(notification_text);
    fpv_ini_destroy(ini);
    return true;
  }
  fpv_tg_reload_auto_response(service);
  char* reply_markup = fpv_tg_build_ar_edit_done_keyboard(
      service, fpv_tg_cbt_edit_cmd_notification, command_index, offset);
  char* escaped_cmd = fpv_tg_escape_html(command);
  char* escaped_note = fpv_tg_escape_html(notification_text);
  char* text = fpv_tg_loc_format(
      service,
      "ar_notification_text_changed",
      (const char*[]){escaped_cmd ? escaped_cmd : "",
                      escaped_note ? escaped_note : ""},
      2);
  fpv_tg_send_message(
      service,
      message->chat_id,
      text ? text : "",
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped_cmd);
  fpv_free(escaped_note);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_ar_notification_text_changed",
      (const char*[]){uname, id_buf, command, notification_text},
      4);
  fpv_free(notification_text);
  fpv_ini_destroy(ini);
  return true;
}

bool fpv_tg_handle_ar_state(
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
    case FPV_TG_STATE_ADD_CMD:
      return fpv_tg_handle_ar_add_command(service, message);
    case FPV_TG_STATE_EDIT_CMD_RESPONSE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ar_edit_response(service, message, &data);
    }
    case FPV_TG_STATE_EDIT_CMD_NOTIFICATION: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_ar_edit_notification(service, message, &data);
    }
    default:
      break;
  }
  return false;
}
