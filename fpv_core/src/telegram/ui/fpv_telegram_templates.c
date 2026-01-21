/* FunPay Vertex Telegram template management. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* fpv_tg_build_templates_list(
    fpv_telegram_service_t* service,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = service->answer_templates.count;
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  for (size_t i = offset; i < total && page_count < fpv_tg_template_page; i++) {
    const char* tmpl = service->answer_templates.items[i];
    if (!tmpl) {
      continue;
    }
    char cb_buf[64];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_edit_template, i, offset);
    fpv_tg_keyboard_add_button(kb, tmpl, cb_buf, NULL);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_template_page, page_count, total, fpv_tg_cbt_template_list, NULL);
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_add_template, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), fpv_tg_cbt_main, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_template_edit_keyboard(
    fpv_telegram_service_t* service,
    size_t template_index,
    size_t offset) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  char cb_buf[64];
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%zu", fpv_tg_cbt_del_template, template_index, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_delete"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  snprintf(cb_buf, sizeof(cb_buf), "%s:%zu", fpv_tg_cbt_template_list, offset);
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

char* fpv_tg_build_templates_list_ans(
    fpv_telegram_service_t* service,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra) {
  if (!service) {
    return NULL;
  }
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (!kb) {
    return NULL;
  }
  size_t total = service->answer_templates.count;
  if (offset >= total && total > 0) {
    offset = 0;
  }
  size_t page_count = 0;
  const char* extra_tail = extra ? extra : "";
  for (size_t i = offset; i < total && page_count < fpv_tg_template_page; i++) {
    const char* tmpl = service->answer_templates.items[i];
    if (!tmpl) {
      continue;
    }
    char* label = fpv_tg_replace_username(tmpl, username);
    char cb_buf[256];
    snprintf(cb_buf, sizeof(cb_buf), "%s:%zu:%" PRIu64 ":%s:%d%s",
             fpv_tg_cbt_send_template, i, chat_id,
             username ? username : "", prev_page, extra_tail);
    fpv_tg_keyboard_add_button(kb, label ? label : "", cb_buf, NULL);
    fpv_free(label);
    fpv_tg_keyboard_row_end(kb);
    page_count++;
  }
  char extra_buf[256];
  if (extra_tail && extra_tail[0]) {
    snprintf(extra_buf, sizeof(extra_buf), ":%" PRIu64 ":%s:%d%s",
             chat_id, username ? username : "", prev_page, extra_tail);
  } else {
    snprintf(extra_buf, sizeof(extra_buf), ":%" PRIu64 ":%s:%d",
             chat_id, username ? username : "", prev_page);
  }
  fpv_tg_add_navigation_buttons(
      kb, offset, fpv_tg_template_page, page_count, total,
      fpv_tg_cbt_template_list_ans, extra_buf);

  char cb_buf[256];
  if (prev_page == 0 || prev_page == 1) {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":%s:%d%s",
             fpv_tg_cbt_back_to_reply, chat_id, username ? username : "",
             prev_page, extra_tail);
  } else {
    snprintf(cb_buf, sizeof(cb_buf), "%s:%" PRIu64 ":%s%s",
             fpv_tg_cbt_back_to_order, chat_id, username ? username : "",
             extra_tail);
  }
  fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), cb_buf, NULL);
  fpv_tg_keyboard_row_end(kb);
  return fpv_tg_keyboard_finalize(kb, false);
}

bool fpv_tg_check_template_index(
    fpv_telegram_service_t* service,
    size_t template_index,
    int64_t chat_id,
    int message_id,
    bool edit_message) {
  if (!service) {
    return false;
  }
  if (template_index < service->answer_templates.count) {
    return true;
  }
  char index_buf[32];
  snprintf(index_buf, sizeof(index_buf), "%zu", template_index);
  char* text = fpv_tg_loc_format(
      service,
      "tmplt_not_found_err",
      (const char*[]){index_buf},
      1);
  char cb_buf[32];
  snprintf(cb_buf, sizeof(cb_buf), "%s:0", fpv_tg_cbt_template_list);
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

void fpv_tg_open_templates_list_ans(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_templates_list_ans(
      service, offset, chat_id, username, prev_page, extra);
  fpv_tg_edit_message_reply_markup(
      service,
      callback->chat_id,
      callback->message_id,
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_open_template_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  if (!fpv_tg_check_template_index(
          service,
          template_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* tmpl = service->answer_templates.items[template_index];
  char* escaped = fpv_tg_escape_html(tmpl ? tmpl : "");
  char* text = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append_str(&text, &length, &capacity, "<code>");
  fpv_tg_buffer_append_str(&text, &length, &capacity, escaped ? escaped : "");
  fpv_tg_buffer_append_str(&text, &length, &capacity, "</code>");
  if (!text) {
    text = fpv_strdup("");
  }
  char* reply_markup =
      fpv_tg_build_template_edit_keyboard(service, template_index, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      text ? text : "",
      reply_markup);
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_free(escaped);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_delete_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  if (!fpv_tg_check_template_index(
          service,
          template_index,
          callback->chat_id,
          callback->message_id,
          true)) {
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  const char* tmpl = service->answer_templates.items[template_index];
  char* tmpl_copy = tmpl ? fpv_strdup(tmpl) : fpv_strdup("");
  fpv_tg_string_list_remove(&service->answer_templates, template_index);
  fpv_tg_save_answer_templates(service);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, callback->from_id);
  const char* uname = callback->from_username ? callback->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_tmplt_deleted",
      (const char*[]){uname, id_buf, tmpl_copy ? tmpl_copy : ""},
      3);
  fpv_free(tmpl_copy);
  char* reply_markup = fpv_tg_build_templates_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_tmplt"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_send_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra1,
    const char* extra2) {
  if (!service || !callback) {
    return;
  }
  if (template_index >= service->answer_templates.count) {
    char index_buf[32];
    snprintf(index_buf, sizeof(index_buf), "%zu", template_index);
    char* text = fpv_tg_loc_format(
        service,
        "tmplt_not_found_err",
        (const char*[]){index_buf},
        1);
    fpv_tg_send_message(service, callback->chat_id, text ? text : "", NULL, NULL);
    fpv_free(text);
    char* reply_markup = NULL;
    if (prev_page == 0 || prev_page == 1) {
      bool extend = false;
      fpv_tg_parse_bool(extra1, &extend);
      reply_markup = fpv_tg_build_reply_keyboard(
          service,
          chat_id,
          username,
          prev_page == 1,
          extend);
    } else if (prev_page == 2 && extra1 && extra1[0]) {
      bool no_refund = false;
      fpv_tg_parse_bool(extra2, &no_refund);
      reply_markup = fpv_tg_build_order_keyboard(
          service,
          extra1,
          username,
          chat_id,
          false,
          no_refund);
    }
    if (reply_markup) {
      fpv_tg_edit_message_reply_markup(
          service,
          callback->chat_id,
          callback->message_id,
          reply_markup);
      fpv_free(reply_markup);
    }
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  const char* tmpl = service->answer_templates.items[template_index];
  char* text = fpv_tg_replace_username(tmpl ? tmpl : "", username);
  fpv_result_t result = FPV_ERR_INVALID_ARGUMENT;
  if (service->features) {
    result = fpv_features_send_message(
        service->features,
        chat_id,
        username,
        text ? text : "",
        true);
  }
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
  char* reply_markup = fpv_tg_build_reply_keyboard(
      service,
      chat_id,
      username,
      true,
      true);
  if (result == FPV_OK) {
    char* escaped = fpv_tg_escape_html(text ? text : "");
    char* response = fpv_tg_loc_format(
        service,
        "tmplt_msg_sent",
        (const char*[]){chat_buf, username ? username : "", escaped ? escaped : ""},
        3);
    fpv_tg_send_message(
        service,
        callback->chat_id,
        response ? response : "",
        reply_markup,
        NULL);
    fpv_free(response);
    fpv_free(escaped);
  } else {
    char* response = fpv_tg_loc_format(
        service,
        "msg_sending_error",
        (const char*[]){chat_buf, username ? username : ""},
        2);
    fpv_tg_send_message(
        service,
        callback->chat_id,
        response ? response : "",
        reply_markup,
        NULL);
    fpv_free(response);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_open_templates_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup = fpv_tg_build_templates_list(service, offset);
  fpv_tg_edit_message_text(
      service,
      callback->chat_id,
      callback->message_id,
      fpv_tg_loc(service, "desc_tmplt"),
      reply_markup);
  fpv_free(reply_markup);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_prompt_add_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset) {
  if (!service || !callback) {
    return;
  }
  static const char* vars[] = {"v_username", "v_photo"};
  char* text = fpv_tg_build_variable_prompt(
      service,
      "V_new_template",
      vars,
      sizeof(vars) / sizeof(vars[0]));
  fpv_tg_state_data_t data;
  memset(&data, 0, sizeof(data));
  data.offset = (int)offset;
  fpv_tg_prompt_state_with_data(
      service,
      callback,
      FPV_TG_STATE_ADD_TEMPLATE,
      text ? text : "",
      &data);
  fpv_free(text);
}

bool fpv_tg_handle_add_template(
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
  size_t offset = data->offset < 0 ? 0 : (size_t)data->offset;
  char* template_text = fpv_tg_trim_copy(message->text);
  if (!template_text) {
    return true;
  }
  if (fpv_tg_string_list_contains(&service->answer_templates, template_text)) {
    fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
    if (kb) {
      char back_cb[32];
      char add_cb[32];
      snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_template_list, offset);
      snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_template, offset);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
      fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add_another"), add_cb, NULL);
      fpv_tg_keyboard_row_end(kb);
    }
    char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
    fpv_tg_send_message(
        service,
        message->chat_id,
        fpv_tg_loc(service, "tmplt_already_exists_err"),
        reply_markup,
        NULL);
    fpv_free(reply_markup);
    fpv_free(template_text);
    return true;
  }
  if (!fpv_tg_string_list_add(&service->answer_templates, template_text)) {
    fpv_free(template_text);
    return true;
  }
  fpv_tg_save_answer_templates(service);
  char id_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRId64, message->from_id);
  const char* uname = message->from_username ? message->from_username : "";
  fpv_tg_log_format(
      service,
      FPV_LOG_INFO,
      "log_tmplt_added",
      (const char*[]){uname, id_buf, template_text},
      3);
  fpv_tg_keyboard_t* kb = fpv_tg_keyboard_create(true, false);
  if (kb) {
    char back_cb[32];
    char add_cb[32];
    snprintf(back_cb, sizeof(back_cb), "%s:%zu", fpv_tg_cbt_template_list, offset);
    snprintf(add_cb, sizeof(add_cb), "%s:%zu", fpv_tg_cbt_add_template, offset);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "gl_back"), back_cb, NULL);
    fpv_tg_keyboard_add_button(kb, fpv_tg_loc(service, "tmplt_add_more"), add_cb, NULL);
    fpv_tg_keyboard_row_end(kb);
  }
  char* reply_markup = kb ? fpv_tg_keyboard_finalize(kb, false) : NULL;
  fpv_tg_send_message(
      service,
      message->chat_id,
      fpv_tg_loc(service, "tmplt_added"),
      reply_markup,
      NULL);
  fpv_free(reply_markup);
  fpv_free(template_text);
  return true;
}

bool fpv_tg_handle_template_state(
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
    case FPV_TG_STATE_ADD_TEMPLATE: {
      fpv_tg_state_data_t data = state->data;
      return fpv_tg_handle_add_template(service, message, &data);
    }
    default:
      break;
  }
  return false;
}
