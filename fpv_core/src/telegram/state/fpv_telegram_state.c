/* FunPay Vertex Telegram session and state helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_tg_chat_settings_t* fpv_tg_find_chat_settings(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->notification_count; i++) {
    if (service->notifications[i].chat_id == chat_id) {
      return &service->notifications[i];
    }
  }
  return NULL;
}

bool fpv_tg_is_notification_enabled(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type) {
  int idx = fpv_tg_notification_index(type);
  if (idx < 0) {
    return false;
  }
  fpv_tg_chat_settings_t* settings = fpv_tg_find_chat_settings(service, chat_id);
  if (!settings) {
    return false;
  }
  return settings->enabled[idx];
}

bool fpv_tg_set_notification(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type,
    bool enabled) {
  int idx = fpv_tg_notification_index(type);
  if (!service || idx < 0) {
    return false;
  }
  fpv_tg_chat_settings_t* settings = fpv_tg_find_chat_settings(service, chat_id);
  if (!settings) {
    fpv_tg_chat_settings_t* grown =
        (fpv_tg_chat_settings_t*)realloc(
            service->notifications,
            (service->notification_count + 1) * sizeof(*grown));
    if (!grown) {
      return false;
    }
    service->notifications = grown;
    settings = &service->notifications[service->notification_count++];
    memset(settings, 0, sizeof(*settings));
    settings->chat_id = chat_id;
    for (size_t i = 0; i < fpv_tg_notification_count; i++) {
      settings->enabled[i] = fpv_tg_default_notification_enabled(
          fpv_tg_notification_ids[i]);
    }
  }
  settings->enabled[idx] = enabled;
  return true;
}

fpv_result_t fpv_tg_save_notification_settings(
    fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "notifications.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{", 1);
  for (size_t i = 0; i < service->notification_count; i++) {
    if (i > 0) {
      fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    char chat_buf[32];
    snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, service->notifications[i].chat_id);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
    fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\":{", 3);
    bool first = true;
    for (size_t n = 0; n < fpv_tg_notification_count; n++) {
      if (!first) {
        fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
      }
      first = false;
      fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
      fpv_tg_buffer_append(&buffer, &length, &capacity,
                           fpv_tg_notification_ids[n],
                           strlen(fpv_tg_notification_ids[n]));
      fpv_tg_buffer_append(&buffer, &length, &capacity, "\":", 2);
      fpv_tg_buffer_append(&buffer, &length, &capacity,
                           service->notifications[i].enabled[n] ? "1" : "0",
                           1);
    }
    fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  if (!buffer) {
    fpv_free(path);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_tg_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  fpv_free(buffer);
  fpv_free(path);
  return result;
}

fpv_result_t fpv_tg_load_notification_settings(
    fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "notifications.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_fs_exists(path)) {
    fpv_free(path);
    return FPV_OK;
  }
  size_t size = 0;
  char* content = fpv_tg_read_file(path, &size);
  fpv_free(path);
  if (!content) {
    return FPV_ERR_IO;
  }
  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  fpv_result_t parse_result = fpv_json_parse(content, size, &json, &error);
  fpv_free(content);
  if (parse_result != FPV_OK || !json) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  if (!fpv_json_is_type(json, FPV_JSON_OBJECT)) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  size_t count = fpv_json_object_size(json);
  if (count == 0) {
    fpv_json_destroy(json);
    return FPV_OK;
  }

  fpv_tg_chat_settings_t* settings =
      (fpv_tg_chat_settings_t*)calloc(count, sizeof(*settings));
  if (!settings) {
    fpv_json_destroy(json);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t stored = 0;
  for (size_t idx = 0; idx < count; idx++) {
    const char* key = fpv_json_object_key(json, idx);
    const fpv_json_value_t* value = fpv_json_object_value(json, idx);
    int64_t chat_id = 0;
    if (!key || !key[0]) {
      continue;
    }
    chat_id = strtoll(key, NULL, 10);
    if (chat_id == 0 || !fpv_json_is_type(value, FPV_JSON_OBJECT)) {
      continue;
    }
    settings[stored].chat_id = chat_id;
    for (size_t i = 0; i < fpv_tg_notification_count; i++) {
      bool enabled =
          fpv_tg_default_notification_enabled(fpv_tg_notification_ids[i]);
      const fpv_json_value_t* entry =
          fpv_json_object_get(value, fpv_tg_notification_ids[i]);
      if (fpv_json_is_type(entry, FPV_JSON_BOOL)) {
        enabled = fpv_json_bool(entry, false);
      } else if (fpv_json_is_type(entry, FPV_JSON_NUMBER)) {
        int64_t flag = 0;
        if (fpv_json_number_to_int64(entry, &flag)) {
          enabled = flag != 0;
        }
      }
      settings[stored].enabled[i] = enabled;
    }
    stored++;
  }
  fpv_json_destroy(json);
  service->notifications = settings;
  service->notification_count = stored;
  return FPV_OK;
}

fpv_tg_session_t* fpv_tg_find_session(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return NULL;
  }
  for (size_t i = 0; i < service->session_count; i++) {
    if (service->sessions[i].tg_user_id == user_id) {
      return &service->sessions[i];
    }
  }
  return NULL;
}

const char* fpv_tg_get_session_user_id(
    const fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return NULL;
  }
  for (size_t i = 0; i < service->session_count; i++) {
    if (service->sessions[i].tg_user_id == user_id) {
      return service->sessions[i].user_id;
    }
  }
  return NULL;
}

bool fpv_tg_is_authorized(
    const fpv_telegram_service_t* service,
    int64_t user_id) {
  return fpv_tg_get_session_user_id(service, user_id) != NULL;
}

fpv_result_t fpv_tg_open_session_db(fpv_telegram_service_t* service) {
  if (!service) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* db_url = getenv("FPV_DB_URL");
  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = db_url;
  config.data_dir = service->storage.data_dir;

  fpv_db_t* db = NULL;
  fpv_result_t result = fpv_db_open(&config, &db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_migrate(db);
  if (result != FPV_OK) {
    fpv_db_close(db);
    return result;
  }
  service->session_db = db;
  return FPV_OK;
}

fpv_result_t fpv_tg_load_sessions_db(fpv_telegram_service_t* service) {
  if (!service || !service->session_db) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(
      service->session_db,
      "SELECT tg_user_id, user_id FROM fpv_tg_sessions;",
      NULL,
      0,
      &rows);
  if (result != FPV_OK || !rows) {
    fpv_db_result_destroy(rows);
    return result != FPV_OK ? result : FPV_ERR_INTERNAL;
  }
  if (rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }
  fpv_tg_session_t* sessions =
      (fpv_tg_session_t*)calloc(rows->row_count, sizeof(*sessions));
  if (!sessions) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t stored = 0;
  for (size_t i = 0; i < rows->row_count; i++) {
    char** row = rows->rows ? rows->rows[i] : NULL;
    if (!row || !row[0] || !row[1]) {
      continue;
    }
    uint64_t tg_id = 0;
    if (!fpv_tg_parse_uint64_value(row[0], &tg_id) ||
        tg_id == 0 || tg_id > INT64_MAX) {
      continue;
    }
    sessions[stored].tg_user_id = (int64_t)tg_id;
    sessions[stored].user_id = fpv_strdup(row[1]);
    if (!sessions[stored].user_id) {
      for (size_t j = 0; j < stored; j++) {
        fpv_free(sessions[j].user_id);
      }
      fpv_free(sessions);
      fpv_db_result_destroy(rows);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    stored++;
  }
  fpv_db_result_destroy(rows);
  if (stored == 0) {
    fpv_free(sessions);
    return FPV_OK;
  }
  service->sessions = sessions;
  service->session_count = stored;
  return FPV_OK;
}

fpv_result_t fpv_tg_save_sessions_db(fpv_telegram_service_t* service) {
  if (!service || !service->session_db) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_db_begin(service->session_db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_exec(service->session_db, "DELETE FROM fpv_tg_sessions;");
  if (result != FPV_OK) {
    fpv_db_rollback(service->session_db);
    return result;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char updated_buf[32];
  snprintf(updated_buf, sizeof(updated_buf), "%" PRIu64, now_ms);
  for (size_t i = 0; i < service->session_count; i++) {
    char tg_buf[32];
    snprintf(tg_buf, sizeof(tg_buf), "%" PRId64, service->sessions[i].tg_user_id);
    const char* params[] = {tg_buf, service->sessions[i].user_id, updated_buf};
    result = fpv_db_exec_params(
        service->session_db,
        "INSERT INTO fpv_tg_sessions (tg_user_id, user_id, updated_at_ms) "
        "VALUES ($1, $2, $3) "
        "ON CONFLICT (tg_user_id) DO UPDATE SET "
        "user_id = excluded.user_id, updated_at_ms = excluded.updated_at_ms;",
        params,
        3);
    if (result != FPV_OK) {
      fpv_db_rollback(service->session_db);
      return result;
    }
  }
  return fpv_db_commit(service->session_db);
}

fpv_result_t fpv_tg_load_sessions(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t db_result = FPV_OK;
  if (service->session_db) {
    db_result = fpv_tg_load_sessions_db(service);
    if (db_result == FPV_OK && service->session_count > 0) {
      return FPV_OK;
    }
  }
  char* path = fpv_path_join(service->storage.cache_dir, "tg_sessions.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_fs_exists(path)) {
    fpv_free(path);
    return FPV_OK;
  }
  size_t size = 0;
  char* content = fpv_tg_read_file(path, &size);
  fpv_free(path);
  if (!content) {
    return FPV_ERR_IO;
  }

  fpv_json_error_t error;
  fpv_json_value_t* json = NULL;
  fpv_result_t parse_result = fpv_json_parse(content, size, &json, &error);
  fpv_free(content);
  if (parse_result != FPV_OK || !json) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  if (!fpv_json_is_type(json, FPV_JSON_ARRAY)) {
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  size_t count = fpv_json_array_size(json);
  if (count == 0) {
    fpv_json_destroy(json);
    return FPV_OK;
  }

  fpv_tg_session_t* sessions =
      (fpv_tg_session_t*)calloc(count, sizeof(*sessions));
  if (!sessions) {
    fpv_json_destroy(json);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t stored = 0;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* entry = fpv_json_array_get(json, i);
    if (!entry || !fpv_json_is_type(entry, FPV_JSON_OBJECT)) {
      continue;
    }
    int64_t tg_user_id = 0;
    const char* user_id =
        fpv_json_string(fpv_json_object_get(entry, "user_id"));
    if (!fpv_json_number_to_int64(
            fpv_json_object_get(entry, "tg_user_id"),
            &tg_user_id) ||
        tg_user_id == 0 || !user_id || !user_id[0]) {
      continue;
    }
    sessions[stored].tg_user_id = tg_user_id;
    sessions[stored].user_id = fpv_strdup(user_id);
    if (!sessions[stored].user_id) {
      for (size_t j = 0; j < stored; j++) {
        fpv_free(sessions[j].user_id);
      }
      fpv_free(sessions);
      fpv_json_destroy(json);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    stored++;
  }

  fpv_json_destroy(json);
  service->sessions = sessions;
  service->session_count = stored;
  if (service->session_db && stored > 0) {
    fpv_tg_save_sessions_db(service);
  }
  return FPV_OK;
}

fpv_result_t fpv_tg_save_sessions(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t db_result = FPV_ERR_INVALID_ARGUMENT;
  if (service->session_db) {
    db_result = fpv_tg_save_sessions_db(service);
  }
  char* path = fpv_path_join(service->storage.cache_dir, "tg_sessions.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  bool ok = fpv_tg_buffer_append(&buffer, &length, &capacity, "[", 1);
  for (size_t i = 0; i < service->session_count; i++) {
    if (!ok) {
      break;
    }
    if (i > 0) {
      ok = fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    ok = ok && fpv_tg_buffer_append(&buffer, &length, &capacity,
                                    "{\"tg_user_id\":", 14);
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRId64, service->sessions[i].tg_user_id);
    ok = ok && fpv_tg_buffer_append(&buffer, &length, &capacity,
                                    id_buf, strlen(id_buf));
    ok = ok && fpv_tg_buffer_append(&buffer, &length, &capacity,
                                    ",\"user_id\":\"", 12);
    ok = ok && fpv_tg_json_escape_append(&buffer, &length, &capacity,
                                         service->sessions[i].user_id);
    ok = ok && fpv_tg_buffer_append(&buffer, &length, &capacity, "\"}", 2);
  }
  ok = ok && fpv_tg_buffer_append(&buffer, &length, &capacity, "]", 1);

  fpv_result_t result = FPV_ERR_OUT_OF_MEMORY;
  if (buffer && ok) {
    result = fpv_tg_write_file(path, buffer) ? FPV_OK : FPV_ERR_IO;
  }
  fpv_free(buffer);
  fpv_free(path);
  if (service->session_db) {
    if (db_result == FPV_OK || result == FPV_OK) {
      return FPV_OK;
    }
    return db_result;
  }
  return result;
}

fpv_result_t fpv_tg_load_answer_templates(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "answer_templates.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_load_strings(path, &service->answer_templates);
  fpv_free(path);
  return result;
}

fpv_result_t fpv_tg_save_answer_templates(fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_path_join(service->storage.cache_dir, "answer_templates.json");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_save_strings(path, &service->answer_templates);
  fpv_free(path);
  return result;
}

fpv_tg_user_state_t* fpv_tg_get_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_tg_user_state_t* state = &service->user_states[i];
    if (state->chat_id == chat_id && state->user_id == user_id) {
      return state;
    }
  }
  return NULL;
}

void fpv_tg_state_data_clear(fpv_tg_state_data_t* data) {
  if (!data) {
    return;
  }
  fpv_free(data->username);
  fpv_free(data->email);
  fpv_free(data->org_name);
  fpv_free(data->timezone);
  fpv_free(data->currency);
  memset(data, 0, sizeof(*data));
}

void fpv_tg_state_data_copy(
    fpv_tg_state_data_t* dest,
    const fpv_tg_state_data_t* src) {
  if (!dest) {
    return;
  }
  fpv_tg_state_data_clear(dest);
  if (!src) {
    return;
  }
  *dest = *src;
  dest->username = src->username ? fpv_strdup(src->username) : NULL;
  dest->email = src->email ? fpv_strdup(src->email) : NULL;
  dest->org_name = src->org_name ? fpv_strdup(src->org_name) : NULL;
  dest->timezone = src->timezone ? fpv_strdup(src->timezone) : NULL;
  dest->currency = src->currency ? fpv_strdup(src->currency) : NULL;
  if ((src->username && !dest->username) ||
      (src->email && !dest->email) ||
      (src->org_name && !dest->org_name) ||
      (src->timezone && !dest->timezone) ||
      (src->currency && !dest->currency)) {
    fpv_tg_state_data_clear(dest);
  }
}

void fpv_tg_clear_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    bool delete_message) {
  if (!service) {
    return;
  }
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_tg_user_state_t* state = &service->user_states[i];
    if (state->chat_id == chat_id && state->user_id == user_id) {
      if (delete_message && state->message_id > 0) {
        fpv_tg_delete_message(service, chat_id, state->message_id);
      }
      fpv_tg_state_data_clear(&state->data);
      *state = service->user_states[service->user_state_count - 1];
      service->user_state_count--;
      return;
    }
  }
}

bool fpv_tg_set_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    int message_id,
    fpv_tg_state_type_t type,
    const fpv_tg_state_data_t* data) {
  if (!service) {
    return false;
  }
  fpv_tg_user_state_t* state = fpv_tg_get_state(service, chat_id, user_id);
  if (!state) {
    fpv_tg_user_state_t* grown = (fpv_tg_user_state_t*)realloc(
        service->user_states,
        (service->user_state_count + 1) * sizeof(*grown));
    if (!grown) {
      return false;
    }
    service->user_states = grown;
    state = &service->user_states[service->user_state_count++];
    memset(state, 0, sizeof(*state));
    state->chat_id = chat_id;
    state->user_id = user_id;
  }
  fpv_tg_state_data_copy(&state->data, data);
  state->message_id = message_id;
  state->type = type;
  return true;
}

fpv_result_t fpv_tg_update_profile_lots(fpv_telegram_service_t* service) {
  if (!service || !service->account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < service->profile_lot_count; i++) {
    fpv_lot_destroy(service->profile_lots[i]);
  }
  fpv_free(service->profile_lots);
  service->profile_lots = NULL;
  service->profile_lot_count = 0;

  fpv_funpay_lot_section_t* sections = NULL;
  size_t section_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_sections(
      service->account, &sections, &section_count, &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || section_count == 0) {
    fpv_free(sections);
    return result;
  }

  fpv_lot_t** all_lots = NULL;
  size_t all_count = 0;
  for (size_t i = 0; i < section_count; i++) {
    if (sections[i].is_currency) {
      continue;
    }
    fpv_lot_t** lots = NULL;
    size_t lot_count = 0;
    memset(&error, 0, sizeof(error));
    result = fpv_funpay_account_get_trade_lots(
        service->account,
        sections[i].id,
        sections[i].is_currency,
        &lots,
        &lot_count,
        &error);
    fpv_funpay_error_clear(&error);
    if (result != FPV_OK || lot_count == 0) {
      if (lots) {
        for (size_t j = 0; j < lot_count; j++) {
          fpv_lot_destroy(lots[j]);
        }
        fpv_free(lots);
      }
      continue;
    }
    fpv_lot_t** grown = (fpv_lot_t**)realloc(
        all_lots, (all_count + lot_count) * sizeof(*grown));
    if (!grown) {
      for (size_t j = 0; j < lot_count; j++) {
        fpv_lot_destroy(lots[j]);
      }
      fpv_free(lots);
      continue;
    }
    all_lots = grown;
    for (size_t j = 0; j < lot_count; j++) {
      all_lots[all_count++] = lots[j];
    }
    fpv_free(lots);
  }
  fpv_free(sections);
  service->profile_lots = all_lots;
  service->profile_lot_count = all_count;
  service->profile_update_ms = fpv_time_now_ms();
  return FPV_OK;
}

bool fpv_tg_is_valid_filename(const char* name) {
  if (!name || !name[0]) {
    return false;
  }
  for (const unsigned char* ptr = (const unsigned char*)name; *ptr; ptr++) {
    if (*ptr >= 0x80) {
      continue;
    }
    if (!(isalnum(*ptr) || *ptr == '_' || *ptr == '-' || *ptr == ' ')) {
      return false;
    }
  }
  return true;
}

size_t fpv_tg_count_products(const char* path) {
  if (!path || !fpv_fs_exists(path)) {
    return 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  char buffer[512];
  while (fgets(buffer, sizeof(buffer), file)) {
    char* ptr = buffer;
    while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')) {
      ptr++;
    }
    if (*ptr) {
      count++;
    }
  }
  fclose(file);
  return count;
}

bool fpv_tg_append_products(const char* path, const char* data, bool at_start) {
  if (!path || !data) {
    return false;
  }
  if (!at_start) {
    FILE* file = fopen(path, "ab");
    if (!file) {
      return false;
    }
    size_t len = strlen(data);
    if (len > 0) {
      fwrite("\n", 1, 1, file);
      fwrite(data, 1, len, file);
    }
    fclose(file);
    return true;
  }

  size_t size = 0;
  char* current = fpv_tg_read_file(path, &size);
  if (!current) {
    current = fpv_strdup("");
  }
  size_t data_len = strlen(data);
  size_t new_len = data_len + 1 + size;
  char* merged = (char*)malloc(new_len + 1);
  if (!merged) {
    fpv_free(current);
    return false;
  }
  memcpy(merged, data, data_len);
  merged[data_len] = '\n';
  memcpy(merged + data_len + 1, current, size);
  merged[new_len] = '\0';
  fpv_free(current);
  bool ok = fpv_tg_write_file(path, merged);
  fpv_free(merged);
  return ok;
}

bool fpv_tg_trim_lower(char* text) {
  if (!text) {
    return false;
  }
  char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  size_t len = (size_t)(end - start);
  if (start != text) {
    memmove(text, start, len);
  }
  text[len] = '\0';
  for (char* ptr = text; *ptr; ptr++) {
    if (*ptr >= 'A' && *ptr <= 'Z') {
      *ptr = (char)(*ptr - 'A' + 'a');
    }
  }
  return len > 0;
}
