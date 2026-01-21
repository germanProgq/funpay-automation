/* FunPay Vertex Telegram helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

bool fpv_tg_parse_bool(const char* value, bool* out) {
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
  if (strcasecmp(value, "true") == 0) {
    *out = true;
    return true;
  }
  if (strcasecmp(value, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool fpv_tg_starts_with(const char* text, const char* prefix) {
  if (!text || !prefix) {
    return false;
  }
  size_t len = strlen(prefix);
  return strncmp(text, prefix, len) == 0;
}

bool fpv_tg_generate_key(char* buffer, size_t length) {
  if (!buffer || length < 2) {
    return false;
  }
  static const char charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  size_t charset_len = sizeof(charset) - 1;
  for (size_t i = 0; i + 1 < length; i++) {
    buffer[i] = charset[rand() % charset_len];
  }
  buffer[length - 1] = '\0';
  return true;
}

size_t fpv_tg_get_offset(size_t element_index, size_t max_per_page) {
  size_t elements_amount = element_index + 1;
  size_t elements_on_page = elements_amount % max_per_page;
  if (elements_on_page == 0) {
    elements_on_page = max_per_page;
  }
  if (elements_amount <= elements_on_page) {
    return 0;
  }
  return element_index - elements_on_page + 1;
}

bool fpv_tg_find_ini_section(
    fpv_ini_t* ini,
    const char* name,
    size_t* out_index) {
  if (out_index) {
    *out_index = 0;
  }
  if (!ini || !name) {
    return false;
  }
  size_t count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (section && strcmp(section, name) == 0) {
      if (out_index) {
        *out_index = i;
      }
      return true;
    }
  }
  return false;
}

void fpv_tg_add_navigation_buttons(
    fpv_tg_keyboard_t* kb,
    size_t offset,
    size_t max_per_page,
    size_t page_count,
    size_t total_count,
    const char* callback_base,
    const char* extra) {
  if (!kb || !callback_base || total_count == 0) {
    return;
  }
  bool has_prev = offset > 0;
  bool has_next = offset + page_count < total_count;
  if (!has_prev && !has_next) {
    return;
  }

  size_t back_offset = 0;
  if (has_prev) {
    back_offset = offset > max_per_page ? offset - max_per_page : 0;
  }
  size_t last_offset = fpv_tg_get_offset(total_count - 1, max_per_page);
  size_t next_offset = offset + page_count;

  char first_cb[128];
  char back_cb[128];
  char next_cb[128];
  char last_cb[128];
  const char* tail = extra ? extra : "";
  if (has_prev) {
    snprintf(first_cb, sizeof(first_cb), "%s:0%s", callback_base, tail);
    snprintf(back_cb, sizeof(back_cb), "%s:%zu%s", callback_base, back_offset, tail);
  } else {
    snprintf(first_cb, sizeof(first_cb), "%s", fpv_tg_cbt_empty);
    snprintf(back_cb, sizeof(back_cb), "%s", fpv_tg_cbt_empty);
  }
  if (has_next) {
    snprintf(next_cb, sizeof(next_cb), "%s:%zu%s", callback_base, next_offset, tail);
    snprintf(last_cb, sizeof(last_cb), "%s:%zu%s", callback_base, last_offset, tail);
  } else {
    snprintf(next_cb, sizeof(next_cb), "%s", fpv_tg_cbt_empty);
    snprintf(last_cb, sizeof(last_cb), "%s", fpv_tg_cbt_empty);
  }

  fpv_tg_keyboard_add_button(kb, "<<", first_cb, NULL);
  fpv_tg_keyboard_add_button(kb, "<", back_cb, NULL);
  fpv_tg_keyboard_add_button(kb, ">", next_cb, NULL);
  fpv_tg_keyboard_add_button(kb, ">>", last_cb, NULL);
  fpv_tg_keyboard_row_end(kb);
}

fpv_tg_attempt_t* fpv_tg_find_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service) {
    return NULL;
  }
  for (size_t i = 0; i < service->attempt_count; i++) {
    if (service->attempts[i].user_id == user_id) {
      return &service->attempts[i];
    }
  }
  return NULL;
}

uint32_t fpv_tg_increment_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return 0;
  }
  fpv_tg_attempt_t* attempt = fpv_tg_find_attempt(service, user_id);
  if (!attempt) {
    fpv_tg_attempt_t* grown = (fpv_tg_attempt_t*)realloc(
        service->attempts,
        (service->attempt_count + 1) * sizeof(*grown));
    if (!grown) {
      return 0;
    }
    service->attempts = grown;
    attempt = &service->attempts[service->attempt_count++];
    attempt->user_id = user_id;
    attempt->count = 0;
  }
  attempt->count++;
  return attempt->count;
}

void fpv_tg_clear_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id) {
  if (!service || user_id == 0) {
    return;
  }
  for (size_t i = 0; i < service->attempt_count; i++) {
    if (service->attempts[i].user_id == user_id) {
      service->attempts[i] = service->attempts[service->attempt_count - 1];
      service->attempt_count--;
      return;
    }
  }
}

bool fpv_tg_set_session(
    fpv_telegram_service_t* service,
    int64_t tg_user_id,
    const char* user_id) {
  if (!service || tg_user_id == 0 || !user_id || !user_id[0]) {
    return false;
  }
  fpv_tg_session_t* session = fpv_tg_find_session(service, tg_user_id);
  if (session) {
    char* copy = fpv_strdup(user_id);
    if (!copy) {
      return false;
    }
    fpv_free(session->user_id);
    session->user_id = copy;
  } else {
    fpv_tg_session_t* grown = (fpv_tg_session_t*)realloc(
        service->sessions,
        (service->session_count + 1) * sizeof(*grown));
    if (!grown) {
      return false;
    }
    service->sessions = grown;
    session = &service->sessions[service->session_count++];
    session->tg_user_id = tg_user_id;
    session->user_id = fpv_strdup(user_id);
    if (!session->user_id) {
      service->session_count--;
      return false;
    }
  }
  fpv_tg_clear_attempt(service, tg_user_id);
  fpv_tg_save_sessions(service);
  return true;
}

fpv_funpay_account_t* fpv_tg_get_account(fpv_telegram_service_t* service) {
  if (!service) {
    return NULL;
  }
  fpv_funpay_account_t* account = NULL;
  fpv_mutex_lock(&service->mutex);
  account = service->account;
  fpv_mutex_unlock(&service->mutex);
  return account;
}

void fpv_tg_setup_default_notifications(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  if (!service || chat_id == 0) {
    return;
  }
  if (fpv_tg_find_chat_settings(service, chat_id)) {
    return;
  }
  fpv_tg_set_notification(service, chat_id, "11", true);
  fpv_tg_set_notification(service, chat_id, "12", true);
  fpv_tg_save_notification_settings(service);
}

char* fpv_tg_config_path(
    const fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !service->storage.config_dir || !name) {
    return NULL;
  }
  return fpv_path_join(service->storage.config_dir, name);
}

char* fpv_tg_products_path(
    const fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !service->storage.products_dir || !name) {
    return NULL;
  }
  return fpv_path_join(service->storage.products_dir, name);
}

bool fpv_tg_has_entitlement(
    fpv_telegram_service_t* service,
    fpv_feature_flag_t feature) {
  if (!service) {
    return false;
  }
  fpv_feature_state_t* features = NULL;
  fpv_mutex_lock(&service->mutex);
  features = service->features;
  fpv_mutex_unlock(&service->mutex);
  if (features) {
    return fpv_features_has_entitlement(features, feature);
  }

  fpv_feature_mask_t mask = 0;
  char* config_path = fpv_tg_config_path(service, "_main.cfg");
  if (config_path) {
    fpv_settings_t settings;
    memset(&settings, 0, sizeof(settings));
    if (fpv_settings_load(config_path, &settings) == FPV_OK) {
      mask = fpv_feature_mask_for_tier(settings.tier);
    }
    fpv_settings_destroy(&settings);
    fpv_free(config_path);
  }
  return fpv_feature_mask_has(mask, feature);
}

void fpv_tg_send_feature_unavailable(
    fpv_telegram_service_t* service,
    int64_t chat_id) {
  fpv_tg_send_message(
      service,
      chat_id,
      "Feature not available in current tier.",
      NULL,
      NULL);
}

bool fpv_tg_reload_main_settings(fpv_telegram_service_t* service) {
  if (!service || !service->storage.config_dir) {
    return false;
  }
  char* config_path = fpv_tg_config_path(service, "_main.cfg");
  if (!config_path) {
    return false;
  }
  fpv_settings_t settings;
  memset(&settings, 0, sizeof(settings));
  fpv_result_t result = fpv_settings_load(config_path, &settings);
  fpv_free(config_path);
  if (result != FPV_OK) {
    fpv_settings_destroy(&settings);
    return false;
  }
  if (service->features) {
    fpv_features_update_settings(
        service->features,
        &settings,
        &service->storage,
        service->locales_dir);
  }
  if (settings.language && settings.language[0]) {
    fpv_telegram_service_update_language(
        service,
        service->locales_dir,
        settings.language);
  }
  fpv_settings_destroy(&settings);
  return true;
}

void fpv_tg_set_my_commands(fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  bool allow_blacklist =
      fpv_tg_has_entitlement(service, FPV_FEATURE_BUYER_BLACKLIST);
  bool allow_watermark =
      fpv_tg_has_entitlement(service, FPV_FEATURE_WATERMARK);
  bool allow_old_orders =
      fpv_tg_has_entitlement(service, FPV_FEATURE_OLD_ORDERS_SCANNER);
  struct {
    const char* command;
    const char* key;
  } commands[] = {
      {"menu", "cmd_menu"},
      {"all", "cmd_all"},
      {"profile", "cmd_profile"},
      {"test_lot", "cmd_test_lot"},
      {"upload_img", "cmd_upload_img"},
      {"ban", "cmd_ban"},
      {"unban", "cmd_unban"},
      {"black_list", "cmd_black_list"},
      {"watermark", "cmd_watermark"},
      {"logs", "cmd_logs"},
      {"del_logs", "cmd_del_logs"},
      {"about", "cmd_about"},
      {"old_orders", "cmd_old_orders"},
      {"sys", "cmd_sys"},
      {"keyboard", "cmd_keyboard"},
      {"change_cookie", "cmd_change_cookie"},
      {"restart", "cmd_restart"},
      {"power_off", "cmd_power_off"}};
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  bool first = true;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"commands\":[", 13);
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (!allow_blacklist &&
        (strcmp(commands[i].command, "ban") == 0 ||
         strcmp(commands[i].command, "unban") == 0 ||
         strcmp(commands[i].command, "black_list") == 0)) {
      continue;
    }
    if (!allow_watermark &&
        strcmp(commands[i].command, "watermark") == 0) {
      continue;
    }
    if (!allow_old_orders &&
        strcmp(commands[i].command, "old_orders") == 0) {
      continue;
    }
    if (!first) {
      fpv_tg_buffer_append(&buffer, &length, &capacity, ",", 1);
    }
    first = false;
    fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"command\":\"", 12);
    fpv_tg_json_escape_append(&buffer, &length, &capacity, commands[i].command);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"description\":\"", 17);
    fpv_tg_json_escape_append(
        &buffer, &length, &capacity, fpv_tg_loc(service, commands[i].key));
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"}", 2);
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "]}", 2);
  if (!buffer) {
    return;
  }
  fpv_json_value_t* root = NULL;
  fpv_tg_api_request(
      service,
      "POST",
      "setMyCommands",
      "application/json",
      buffer,
      strlen(buffer),
      &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
}

bool fpv_tg_load_settings(
    fpv_telegram_service_t* service,
    fpv_settings_t* out_settings) {
  if (!service || !out_settings) {
    return false;
  }
  char* path = fpv_tg_config_path(service, "_main.cfg");
  if (!path) {
    return false;
  }
  memset(out_settings, 0, sizeof(*out_settings));
  fpv_result_t result = fpv_settings_load(path, out_settings);
  fpv_free(path);
  if (result != FPV_OK) {
    fpv_settings_destroy(out_settings);
    return false;
  }
  return true;
}

bool fpv_tg_update_main_config_value(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    const char* value) {
  if (!service || !section || !key) {
    return false;
  }
  char* path = fpv_tg_config_path(service, "_main.cfg");
  if (!path) {
    return false;
  }
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    fpv_free(path);
    return false;
  }
  bool ok = fpv_ini_set(ini, section, key, value) == FPV_OK &&
      fpv_ini_save(ini, path) == FPV_OK;
  fpv_ini_destroy(ini);
  fpv_free(path);
  return ok;
}

bool fpv_tg_update_main_config_bool(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    bool value) {
  return fpv_tg_update_main_config_value(
      service, section, key, value ? "1" : "0");
}

fpv_ini_t* fpv_tg_load_ini(
    fpv_telegram_service_t* service,
    const char* name) {
  if (!service || !name) {
    return NULL;
  }
  char* path = fpv_tg_config_path(service, name);
  if (!path) {
    return NULL;
  }
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  fpv_free(path);
  return ini;
}

bool fpv_tg_save_ini(
    fpv_telegram_service_t* service,
    const char* name,
    const fpv_ini_t* ini) {
  if (!service || !name || !ini) {
    return false;
  }
  char* path = fpv_tg_config_path(service, name);
  if (!path) {
    return false;
  }
  bool ok = fpv_ini_save(ini, path) == FPV_OK;
  fpv_free(path);
  return ok;
}

void fpv_tg_free_string_array(char** items, size_t count) {
  if (!items) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(items[i]);
  }
  fpv_free(items);
}

int fpv_tg_string_compare(const void* left, const void* right) {
  const char* a = *(const char* const*)left;
  const char* b = *(const char* const*)right;
  if (!a && !b) {
    return 0;
  }
  if (!a) {
    return -1;
  }
  if (!b) {
    return 1;
  }
  return strcmp(a, b);
}

char** fpv_tg_list_products_files(
    const fpv_telegram_service_t* service,
    size_t* out_count) {
  if (out_count) {
    *out_count = 0;
  }
  if (!service || !service->storage.products_dir) {
    return NULL;
  }
  char** files = NULL;
  size_t count = 0;
#if defined(_WIN32)
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", service->storage.products_dir);
  WIN32_FIND_DATAA data;
  HANDLE handle = FindFirstFileA(pattern, &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return NULL;
  }
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    if (!fpv_tg_has_suffix(data.cFileName, ".txt")) {
      continue;
    }
    char* name = fpv_strdup(data.cFileName);
    if (!name) {
      continue;
    }
    char** grown = (char**)realloc(files, (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(name);
      continue;
    }
    files = grown;
    files[count++] = name;
  } while (FindNextFileA(handle, &data));
  FindClose(handle);
#else
  DIR* dir = opendir(service->storage.products_dir);
  if (!dir) {
    return NULL;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!fpv_tg_has_suffix(entry->d_name, ".txt")) {
      continue;
    }
    char* name = fpv_strdup(entry->d_name);
    if (!name) {
      continue;
    }
    char** grown = (char**)realloc(files, (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(name);
      continue;
    }
    files = grown;
    files[count++] = name;
  }
  closedir(dir);
#endif
  if (count > 1) {
    qsort(files, count, sizeof(*files), fpv_tg_string_compare);
  }
  if (out_count) {
    *out_count = count;
  }
  return files;
}

bool fpv_tg_file_size(const char* path, size_t* out_size) {
  if (out_size) {
    *out_size = 0;
  }
  if (!path) {
    return false;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return false;
  }
  long size = ftell(file);
  fclose(file);
  if (size < 0) {
    return false;
  }
  if (out_size) {
    *out_size = (size_t)size;
  }
  return true;
}

bool fpv_tg_format_datetime(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  struct tm tm_value;
#if defined(_WIN32)
  if (localtime_s(&tm_value, &seconds) != 0) {
    return false;
  }
#else
  if (!localtime_r(&seconds, &tm_value)) {
    return false;
  }
#endif
  int written = snprintf(
      buffer,
      buffer_len,
      "%02d.%02d.%04d %02d:%02d:%02d",
      tm_value.tm_mday,
      tm_value.tm_mon + 1,
      tm_value.tm_year + 1900,
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec);
  return written > 0 && (size_t)written < buffer_len;
}

bool fpv_tg_is_private_chat(const fpv_tg_message_t* message) {
  return message && message->chat_type &&
      strcmp(message->chat_type, "private") == 0;
}

char* fpv_tg_trim_copy(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  const char* start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  size_t len = (size_t)(end - start);
  char* copy = (char*)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, start, len);
  copy[len] = '\0';
  return copy;
}

char* fpv_tg_normalize_email(const char* email) {
  char* trimmed = fpv_tg_trim_copy(email);
  if (!trimmed) {
    return NULL;
  }
  for (char* ptr = trimmed; *ptr; ptr++) {
    *ptr = (char)tolower((unsigned char)*ptr);
  }
  return trimmed;
}

void fpv_tg_uppercase_ascii(char* text) {
  if (!text) {
    return;
  }
  for (char* ptr = text; *ptr; ptr++) {
    if (*ptr >= 'a' && *ptr <= 'z') {
      *ptr = (char)(*ptr - ('a' - 'A'));
    }
  }
}

char* fpv_tg_trim_inplace(char* text) {
  if (!text) {
    return NULL;
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
  return text;
}

bool fpv_tg_is_simple_tag(const char* text) {
  if (!text) {
    return false;
  }
  size_t len = strlen(text);
  if (len < 3 || text[0] != '[' || text[len - 1] != ']') {
    return false;
  }
  for (size_t i = 1; i + 1 < len; i++) {
    unsigned char ch = (unsigned char)text[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'))) {
      return false;
    }
  }
  return true;
}

bool fpv_tg_ar_command_exists(fpv_ini_t* ini, const char* command) {
  if (!ini || !command) {
    return false;
  }
  char* needle = fpv_strdup(command);
  if (!needle) {
    return false;
  }
  if (!fpv_tg_trim_lower(needle)) {
    fpv_free(needle);
    return false;
  }
  bool found = false;
  size_t sections = fpv_ini_section_count(ini);
  for (size_t i = 0; i < sections && !found; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (!section) {
      continue;
    }
    char* section_copy = fpv_strdup(section);
    if (!section_copy) {
      continue;
    }
    char* token = strtok(section_copy, "|");
    while (token) {
      char* trimmed = fpv_tg_trim_copy(token);
      if (trimmed) {
        fpv_tg_trim_lower(trimmed);
        if (trimmed[0] && strcmp(trimmed, needle) == 0) {
          found = true;
          fpv_free(trimmed);
          break;
        }
        fpv_free(trimmed);
      }
      token = strtok(NULL, "|");
    }
    fpv_free(section_copy);
  }
  fpv_free(needle);
  return found;
}

size_t fpv_tg_split_tokens(char* value, char** tokens, size_t max_tokens) {
  if (!value || !tokens || max_tokens == 0) {
    return 0;
  }
  size_t count = 0;
  char* ptr = value;
  while (count < max_tokens) {
    tokens[count++] = ptr;
    char* sep = strchr(ptr, ':');
    if (!sep) {
      break;
    }
    *sep = '\0';
    ptr = sep + 1;
  }
  return count;
}

bool fpv_tg_parse_size_value(const char* text, size_t* out) {
  if (out) {
    *out = 0;
  }
  if (!text || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  if (value > SIZE_MAX) {
    return false;
  }
  if (out) {
    *out = (size_t)value;
  }
  return true;
}

bool fpv_tg_parse_uint64_value(const char* text, uint64_t* out) {
  if (out) {
    *out = 0;
  }
  if (!text || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  if (out) {
    *out = (uint64_t)value;
  }
  return true;
}

bool fpv_tg_parse_command(
    const char* text,
    char* command,
    size_t command_size,
    const char** out_args) {
  if (out_args) {
    *out_args = NULL;
  }
  if (!text || text[0] != '/' || !command || command_size == 0) {
    return false;
  }
  const char* ptr = text + 1;
  size_t idx = 0;
  while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '@') {
    if (idx + 1 < command_size) {
      command[idx++] = *ptr;
    }
    ptr++;
  }
  command[idx] = '\0';
  while (*ptr && *ptr != ' ') {
    ptr++;
  }
  if (*ptr == ' ') {
    ptr++;
    while (*ptr && isspace((unsigned char)*ptr)) {
      ptr++;
    }
    if (out_args) {
      *out_args = ptr;
    }
  }
  return idx > 0;
}

char* fpv_tg_replace_username(const char* text, const char* username) {
  const char* needle = "$username";
  const char* repl = username ? username : "";
  size_t needle_len = strlen(needle);
  size_t repl_len = strlen(repl);
  size_t count = 0;
  const char* ptr = text ? text : "";
  while ((ptr = strstr(ptr, needle)) != NULL) {
    count++;
    ptr += needle_len;
  }
  if (count == 0) {
    return fpv_strdup(text ? text : "");
  }
  size_t base_len = strlen(text ? text : "");
  size_t total_len = base_len + count * (repl_len - needle_len);
  char* out = (char*)malloc(total_len + 1);
  if (!out) {
    return NULL;
  }
  const char* src = text ? text : "";
  char* dst = out;
  while ((ptr = strstr(src, needle)) != NULL) {
    size_t chunk = (size_t)(ptr - src);
    memcpy(dst, src, chunk);
    dst += chunk;
    memcpy(dst, repl, repl_len);
    dst += repl_len;
    src = ptr + needle_len;
  }
  size_t tail = strlen(src);
  memcpy(dst, src, tail);
  dst += tail;
  *dst = '\0';
  return out;
}

bool fpv_tg_string_list_contains(
    const fpv_string_list_t* list,
    const char* value) {
  if (!list || !value) {
    return false;
  }
  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i] && strcmp(list->items[i], value) == 0) {
      return true;
    }
  }
  return false;
}

bool fpv_tg_string_list_add(
    fpv_string_list_t* list,
    const char* value) {
  if (!list || !value) {
    return false;
  }
  char** grown = (char**)realloc(list->items, (list->count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  list->items = grown;
  list->items[list->count] = fpv_strdup(value);
  if (!list->items[list->count]) {
    return false;
  }
  list->count++;
  return true;
}

bool fpv_tg_string_list_remove(
    fpv_string_list_t* list,
    size_t index) {
  if (!list || index >= list->count) {
    return false;
  }
  fpv_free(list->items[index]);
  for (size_t i = index + 1; i < list->count; i++) {
    list->items[i - 1] = list->items[i];
  }
  list->count--;
  if (list->count == 0) {
    fpv_free(list->items);
    list->items = NULL;
    return true;
  }
  char** resized = (char**)realloc(list->items, list->count * sizeof(*resized));
  if (resized) {
    list->items = resized;
  }
  return true;
}

char* fpv_tg_blacklist_path(const fpv_telegram_service_t* service) {
  if (!service || !service->storage.cache_dir) {
    return NULL;
  }
  return fpv_path_join(service->storage.cache_dir, "blacklist.json");
}

fpv_result_t fpv_tg_load_blacklist(
    const fpv_telegram_service_t* service,
    fpv_string_list_t* out_list) {
  if (!service || !out_list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_tg_blacklist_path(service);
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_load_strings(path, out_list);
  fpv_free(path);
  return result;
}

fpv_result_t fpv_tg_save_blacklist(
    const fpv_telegram_service_t* service,
    const fpv_string_list_t* list) {
  if (!service || !list) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* path = fpv_tg_blacklist_path(service);
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_cache_save_strings(path, list);
  fpv_free(path);
  return result;
}

bool fpv_tg_is_log_file(const char* name) {
  return name && fpv_tg_has_suffix(name, ".log");
}

bool fpv_tg_get_file_mtime(const char* path, uint64_t* out_time) {
  if (out_time) {
    *out_time = 0;
  }
  if (!path) {
    return false;
  }
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA info;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) {
    return false;
  }
  ULARGE_INTEGER value;
  value.LowPart = info.ftLastWriteTime.dwLowDateTime;
  value.HighPart = info.ftLastWriteTime.dwHighDateTime;
  if (out_time) {
    *out_time = value.QuadPart;
  }
  return true;
#else
  struct stat info;
  if (stat(path, &info) != 0) {
    return false;
  }
  if (out_time) {
    *out_time = (uint64_t)info.st_mtime;
  }
  return true;
#endif
}

char* fpv_tg_find_latest_log(const fpv_telegram_service_t* service) {
  if (!service || !service->storage.logs_dir) {
    return NULL;
  }
  char* best_path = NULL;
  uint64_t best_time = 0;
#if defined(_WIN32)
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*", service->storage.logs_dir);
  WIN32_FIND_DATAA data;
  HANDLE handle = FindFirstFileA(pattern, &data);
  if (handle == INVALID_HANDLE_VALUE) {
    return NULL;
  }
  do {
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    if (!fpv_tg_is_log_file(data.cFileName)) {
      continue;
    }
    char* full = fpv_path_join(service->storage.logs_dir, data.cFileName);
    if (!full) {
      continue;
    }
    uint64_t modified = 0;
    fpv_tg_get_file_mtime(full, &modified);
    if (!best_path || modified >= best_time) {
      fpv_free(best_path);
      best_path = full;
      best_time = modified;
    } else {
      fpv_free(full);
    }
  } while (FindNextFileA(handle, &data));
  FindClose(handle);
#else
  DIR* dir = opendir(service->storage.logs_dir);
  if (!dir) {
    return NULL;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!fpv_tg_is_log_file(entry->d_name)) {
      continue;
    }
    char* full = fpv_path_join(service->storage.logs_dir, entry->d_name);
    if (!full) {
      continue;
    }
    uint64_t modified = 0;
    fpv_tg_get_file_mtime(full, &modified);
    if (!best_path || modified >= best_time) {
      fpv_free(best_path);
      best_path = full;
      best_time = modified;
    } else {
      fpv_free(full);
    }
  }
  closedir(dir);
#endif
  return best_path;
}
