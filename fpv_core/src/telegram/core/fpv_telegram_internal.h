/* FunPay Vertex Telegram service internal definitions. */

#ifndef FPV_TELEGRAM_INTERNAL_H
#define FPV_TELEGRAM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telegram/core/fpv_telegram.h"


#include "core/io/fpv_cache.h"

#include "core/data/fpv_db.h"

#include "features/config/fpv_feature_config.h"

#include "features/runtime/fpv_features.h"

#include "core/io/fpv_fs.h"

#include "fpv_core/fpv_identity.h"
#include "fpv_core/fpv_ini.h"
#include "core/data/fpv_json.h"

#include "core/app/fpv_localization.h"

#include "core/base/fpv_platform.h"

#include "core/data/fpv_products.h"

#include "core/app/fpv_settings.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


#ifndef FPV_VERSION
#define FPV_VERSION "0.1.0"
#endif

typedef enum fpv_tg_state_type {
  FPV_TG_STATE_NONE = 0,
  FPV_TG_STATE_ADD_CMD,
  FPV_TG_STATE_EDIT_CMD_RESPONSE,
  FPV_TG_STATE_EDIT_CMD_NOTIFICATION,
  FPV_TG_STATE_ADD_AD_TO_LOT_MANUAL,
  FPV_TG_STATE_EDIT_LOT_DELIVERY_TEXT,
  FPV_TG_STATE_BIND_PRODUCTS_FILE,
  FPV_TG_STATE_CREATE_PRODUCTS_FILE,
  FPV_TG_STATE_ADD_PRODUCTS_TO_FILE,
  FPV_TG_STATE_SEND_FP_MESSAGE,
  FPV_TG_STATE_EDIT_WATERMARK,
  FPV_TG_STATE_MANUAL_AD_TEST,
  FPV_TG_STATE_BAN,
  FPV_TG_STATE_UNBAN,
  FPV_TG_STATE_EDIT_GREETINGS_TEXT,
  FPV_TG_STATE_EDIT_ORDER_CONFIRM_TEXT,
  FPV_TG_STATE_EDIT_REVIEW_REPLY_TEXT,
  FPV_TG_STATE_ADD_TEMPLATE,
  FPV_TG_STATE_UPLOAD_PRODUCTS_FILE,
  FPV_TG_STATE_UPLOAD_MAIN_CONFIG,
  FPV_TG_STATE_UPLOAD_AUTO_RESPONSE_CONFIG,
  FPV_TG_STATE_UPLOAD_AUTO_DELIVERY_CONFIG,
  FPV_TG_STATE_UPLOAD_IMAGE,
  FPV_TG_STATE_AUTH_LOGIN_EMAIL,
  FPV_TG_STATE_AUTH_LOGIN_PASSWORD,
  FPV_TG_STATE_AUTH_JOIN_TOKEN,
  FPV_TG_STATE_AUTH_CREATE_ORG_NAME,
  FPV_TG_STATE_AUTH_CREATE_TIMEZONE,
  FPV_TG_STATE_AUTH_CREATE_TIMEZONE_CUSTOM,
  FPV_TG_STATE_AUTH_CREATE_CURRENCY,
  FPV_TG_STATE_AUTH_CREATE_CURRENCY_CUSTOM
} fpv_tg_state_type_t;

typedef struct fpv_tg_state_data {
  int command_index;
  int offset;
  int lot_index;
  int file_index;
  int element_index;
  int previous_page;
  int stars;
  uint64_t chat_id;
  char* username;
  char* email;
  char* org_name;
  char* timezone;
  char* currency;
} fpv_tg_state_data_t;

typedef struct fpv_tg_user_state {
  int64_t chat_id;
  int64_t user_id;
  int message_id;
  fpv_tg_state_type_t type;
  fpv_tg_state_data_t data;
} fpv_tg_user_state_t;

typedef struct fpv_tg_attempt {
  int64_t user_id;
  uint32_t count;
} fpv_tg_attempt_t;

typedef struct fpv_tg_init_message {
  int64_t chat_id;
  int message_id;
} fpv_tg_init_message_t;

typedef struct fpv_tg_delivery_test {
  char* key;
  char* lot_name;
} fpv_tg_delivery_test_t;

typedef struct fpv_tg_chat_settings {
  int64_t chat_id;
  bool enabled[16];
} fpv_tg_chat_settings_t;

typedef struct fpv_tg_session {
  int64_t tg_user_id;
  char* user_id;
} fpv_tg_session_t;

typedef struct fpv_tg_keyboard {
  char* buffer;
  size_t length;
  size_t capacity;
  bool inline_keyboard;
  bool row_open;
  bool first_in_row;
  bool has_rows;
  bool resize;
} fpv_tg_keyboard_t;

typedef struct fpv_tg_message {
  int64_t chat_id;
  int message_id;
  int64_t from_id;
  char* chat_type;
  char* chat_username;
  char* from_username;
  char* text;
  char* document_file_id;
  char* document_file_name;
  int64_t document_file_size;
  char* photo_file_id;
  int64_t photo_file_size;
  bool reply_topic_created;
  bool has_document;
  bool has_photo;
} fpv_tg_message_t;

typedef struct fpv_tg_callback {
  char* id;
  char* data;
  int64_t from_id;
  char* from_username;
  int64_t chat_id;
  int message_id;
  char* chat_username;
} fpv_tg_callback_t;

struct fpv_telegram_service {
  char* token;
  char* secret;
  char* locales_dir;
  char* language;
  fpv_localizer_t* localizer;
  fpv_logger_t* logger;
  fpv_event_bus_t* bus;
  fpv_funpay_account_t* account;
  fpv_funpay_runner_t* runner;
  fpv_feature_state_t* features;
  fpv_storage_layout_t storage;
  fpv_thread_t thread;
  fpv_mutex_t mutex;
  bool running;
  uint64_t update_offset;
  uint64_t last_poll_error_ms;
  fpv_result_t last_poll_error;
  fpv_tg_chat_settings_t* notifications;
  size_t notification_count;
  fpv_identity_store_t* identity;
  fpv_db_t* session_db;
  fpv_tg_session_t* sessions;
  size_t session_count;
  fpv_string_list_t answer_templates;
  fpv_tg_user_state_t* user_states;
  size_t user_state_count;
  fpv_tg_attempt_t* attempts;
  size_t attempt_count;
  fpv_tg_init_message_t* init_messages;
  size_t init_message_count;
  fpv_tg_delivery_test_t* delivery_tests;
  size_t delivery_test_count;
  fpv_lot_t** profile_lots;
  size_t profile_lot_count;
  uint64_t profile_update_ms;
  uint64_t start_ms;
  uint64_t instance_id;
  void* control_context;
  fpv_telegram_control_fn restart_fn;
  fpv_telegram_control_fn shutdown_fn;
};

extern const char* fpv_tg_notification_ids[];
extern const size_t fpv_tg_notification_count;
extern const size_t fpv_tg_cmd_page;
extern const size_t fpv_tg_ad_page;
extern const size_t fpv_tg_fp_lot_page;
extern const size_t fpv_tg_products_page;
extern const size_t fpv_tg_template_page;
extern const int64_t fpv_tg_max_upload_size;

extern const char* fpv_tg_cbt_main;
extern const char* fpv_tg_cbt_category;
extern const char* fpv_tg_cbt_switch;
extern const char* fpv_tg_cbt_add_cmd;
extern const char* fpv_tg_cbt_cmd_list;
extern const char* fpv_tg_cbt_edit_cmd;
extern const char* fpv_tg_cbt_edit_cmd_response;
extern const char* fpv_tg_cbt_edit_cmd_notification;
extern const char* fpv_tg_cbt_switch_cmd_notification;
extern const char* fpv_tg_cbt_del_cmd;
extern const char* fpv_tg_cbt_fp_lots;
extern const char* fpv_tg_cbt_add_ad_lot;
extern const char* fpv_tg_cbt_add_ad_lot_manual;
extern const char* fpv_tg_cbt_ad_lots;
extern const char* fpv_tg_cbt_edit_ad_lot;
extern const char* fpv_tg_cbt_edit_lot_text;
extern const char* fpv_tg_cbt_bind_products;
extern const char* fpv_tg_cbt_del_ad_lot;
extern const char* fpv_tg_cbt_products_list;
extern const char* fpv_tg_cbt_edit_products_file;
extern const char* fpv_tg_cbt_upload_products_file;
extern const char* fpv_tg_cbt_create_products_file;
extern const char* fpv_tg_cbt_add_products;
extern const char* fpv_tg_cbt_download_cfg;
extern const char* fpv_tg_cbt_template_list;
extern const char* fpv_tg_cbt_template_list_ans;
extern const char* fpv_tg_cbt_edit_template;
extern const char* fpv_tg_cbt_del_template;
extern const char* fpv_tg_cbt_add_template;
extern const char* fpv_tg_cbt_send_template;
extern const char* fpv_tg_cbt_switch_tg;
extern const char* fpv_tg_cbt_request_refund;
extern const char* fpv_tg_cbt_refund_confirmed;
extern const char* fpv_tg_cbt_refund_cancelled;
extern const char* fpv_tg_cbt_ban;
extern const char* fpv_tg_cbt_unban;
extern const char* fpv_tg_cbt_shutdown;
extern const char* fpv_tg_cbt_cancel_shutdown;
extern const char* fpv_tg_cbt_send_fp_message;
extern const char* fpv_tg_cbt_upload_image;
extern const char* fpv_tg_cbt_update_profile;
extern const char* fpv_tg_cbt_manual_ad_test;
extern const char* fpv_tg_cbt_clear_state;
extern const char* fpv_tg_cbt_back_to_reply;
extern const char* fpv_tg_cbt_back_to_order;
extern const char* fpv_tg_cbt_param_disabled;
extern const char* fpv_tg_cbt_main2;
extern const char* fpv_tg_cbt_edit_greetings;
extern const char* fpv_tg_cbt_edit_order_confirm;
extern const char* fpv_tg_cbt_send_review_reply;
extern const char* fpv_tg_cbt_edit_review_reply;
extern const char* fpv_tg_cbt_edit_watermark;
extern const char* fpv_tg_cbt_extend_chat;
extern const char* fpv_tg_cbt_old_help;
extern const char* fpv_tg_cbt_empty;
extern const char* fpv_tg_cbt_lang;
extern const char* fpv_tg_cbt_auth_login;
extern const char* fpv_tg_cbt_auth_join;
extern const char* fpv_tg_cbt_auth_create;
extern const char* fpv_tg_cbt_auth_timezone;
extern const char* fpv_tg_cbt_auth_timezone_custom;
extern const char* fpv_tg_cbt_auth_currency;
extern const char* fpv_tg_cbt_auth_currency_custom;
extern const char* fpv_tg_menu_core;
extern const char* fpv_tg_menu_notify;
extern const char* fpv_tg_menu_reply;
extern const char* fpv_tg_menu_delivery;
extern const char* fpv_tg_cb_update_profile;
extern const char* fpv_tg_cb_update_adv_profile;
extern const char* fpv_tg_cb_config_loader;
extern const char* fpv_tg_cb_upload_main_config;
extern const char* fpv_tg_cb_upload_auto_response_config;
extern const char* fpv_tg_cb_upload_auto_delivery_config;
extern const char* fpv_tg_cb_switch_lot;
extern const char* fpv_tg_cb_test_auto_delivery;
extern const char* fpv_tg_cb_update_funpay_lots;
extern const char* fpv_tg_cb_download_products_file;
extern const char* fpv_tg_cb_delete_products_file;
extern const char* fpv_tg_cb_confirm_delete_products_file;

bool fpv_tg_buffer_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* data,
    size_t data_len);
bool fpv_tg_buffer_append_str(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value);
char* fpv_tg_read_file(const char* path, size_t* out_size);
bool fpv_tg_write_file(const char* path, const char* data);
bool fpv_tg_write_file_data(const char* path, const void* data, size_t size);
bool fpv_tg_json_escape_append(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value);
char* fpv_tg_make_valid_utf8(const char* text);
char* fpv_tg_strip_html_tags(const char* text);
char* fpv_tg_escape_html(const char* text);
size_t fpv_tg_count_substr(const char* text, const char* needle);
size_t fpv_tg_count_anchor_tags(const char* text);
bool fpv_tg_html_is_balanced(const char* text);
int fpv_tg_notification_index(const char* type);
bool fpv_tg_default_notification_enabled(const char* type);
const char* fpv_tg_icon_toggle(bool enabled);
const char* fpv_tg_icon_bell(bool enabled);
const char* fpv_tg_icon_lot_state(bool global_enabled, bool disabled);
bool fpv_tg_has_suffix(const char* value, const char* suffix);
bool fpv_tg_is_supported_upload(const char* filename);
bool fpv_tg_is_safe_upload_name(const char* name);
const char* fpv_tg_result_label(fpv_result_t code);
void fpv_tg_log(
    fpv_telegram_service_t* service,
    fpv_log_level_t level,
    const char* message);
void fpv_tg_log_poll_error(
    fpv_telegram_service_t* service,
    fpv_result_t result);
const char* fpv_tg_loc(const fpv_telegram_service_t* service, const char* key);
char* fpv_tg_loc_format(
    const fpv_telegram_service_t* service,
    const char* key,
    const char* const* args,
    size_t arg_count);
void fpv_tg_log_format(
    fpv_telegram_service_t* service,
    fpv_log_level_t level,
    const char* key,
    const char* const* args,
    size_t arg_count);
char* fpv_tg_build_variable_prompt(
    fpv_telegram_service_t* service,
    const char* header_key,
    const char* const* variables,
    size_t variable_count);
void fpv_tg_flush_message_buffer(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    char** buffer,
    size_t* length,
    size_t* capacity);
void fpv_tg_append_code_value(
    char** buffer,
    size_t* length,
    size_t* capacity,
    const char* value);
void fpv_tg_sleep_ms(uint32_t delay_ms);

void fpv_tg_keyboard_destroy(fpv_tg_keyboard_t* kb);
fpv_tg_keyboard_t* fpv_tg_keyboard_create(bool inline_keyboard, bool resize);
bool fpv_tg_keyboard_row_begin(fpv_tg_keyboard_t* kb);
bool fpv_tg_keyboard_row_end(fpv_tg_keyboard_t* kb);
bool fpv_tg_keyboard_add_button(
    fpv_tg_keyboard_t* kb,
    const char* text,
    const char* callback_data,
    const char* url);
char* fpv_tg_keyboard_finalize(fpv_tg_keyboard_t* kb, bool remove_keyboard);
char* fpv_tg_build_clear_state_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_keyboard_remove(void);

void fpv_tg_http_response_clear(fpv_tg_http_response_t* response);
fpv_result_t fpv_tg_http_request(
    const char* method,
    const char* url,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_tg_http_response_t* response);
struct curl_mime;
fpv_result_t fpv_tg_http_request_multipart(
    const char* url,
    struct curl_mime* mime,
    fpv_tg_http_response_t* response);
fpv_result_t fpv_tg_api_request(
    const fpv_telegram_service_t* service,
    const char* method,
    const char* endpoint,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_json_value_t** out_root);
char* fpv_tg_build_send_body(
    int64_t chat_id,
    const char* text,
    const char* reply_markup);
char* fpv_tg_build_send_body_plain(
    int64_t chat_id,
    const char* text,
    const char* reply_markup);
bool fpv_tg_send_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* text,
    const char* reply_markup,
    int* out_message_id);
bool fpv_tg_edit_message_text(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* text,
    const char* reply_markup);
bool fpv_tg_edit_message_reply_markup(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* reply_markup);
void fpv_tg_answer_callback(
    fpv_telegram_service_t* service,
    const char* callback_id,
    const char* text,
    bool show_alert);
bool fpv_tg_delete_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id);
bool fpv_tg_pin_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id);
bool fpv_tg_send_document(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* file_path,
    const char* caption,
    const char* reply_markup);
bool fpv_tg_send_photo(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const void* data,
    size_t size,
    const char* caption,
    const char* reply_markup,
    int* out_message_id);
fpv_result_t fpv_tg_get_file_path(
    fpv_telegram_service_t* service,
    const char* file_id,
    char** out_path);
fpv_result_t fpv_tg_download_file(
    fpv_telegram_service_t* service,
    const char* file_path,
    void** out_data,
    size_t* out_size);
fpv_result_t fpv_tg_download_tg_file(
    fpv_telegram_service_t* service,
    const char* file_id,
    void** out_data,
    size_t* out_size);

fpv_tg_chat_settings_t* fpv_tg_find_chat_settings(
    fpv_telegram_service_t* service,
    int64_t chat_id);
bool fpv_tg_is_notification_enabled(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type);
bool fpv_tg_set_notification(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* type,
    bool enabled);
fpv_result_t fpv_tg_save_notification_settings(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_load_notification_settings(fpv_telegram_service_t* service);
fpv_tg_session_t* fpv_tg_find_session(
    fpv_telegram_service_t* service,
    int64_t user_id);
const char* fpv_tg_get_session_user_id(
    const fpv_telegram_service_t* service,
    int64_t user_id);
bool fpv_tg_is_authorized(const fpv_telegram_service_t* service, int64_t user_id);
fpv_result_t fpv_tg_open_session_db(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_load_sessions_db(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_save_sessions_db(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_load_sessions(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_save_sessions(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_load_answer_templates(fpv_telegram_service_t* service);
fpv_result_t fpv_tg_save_answer_templates(fpv_telegram_service_t* service);
fpv_tg_user_state_t* fpv_tg_get_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id);
void fpv_tg_state_data_clear(fpv_tg_state_data_t* data);
void fpv_tg_state_data_copy(
    fpv_tg_state_data_t* dest,
    const fpv_tg_state_data_t* source);
void fpv_tg_clear_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    bool delete_message);
bool fpv_tg_set_state(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t user_id,
    int message_id,
    fpv_tg_state_type_t type,
    const fpv_tg_state_data_t* data);
fpv_result_t fpv_tg_update_profile_lots(fpv_telegram_service_t* service);
bool fpv_tg_is_valid_filename(const char* name);
size_t fpv_tg_count_products(const char* path);
bool fpv_tg_append_products(const char* path, const char* data, bool at_start);
bool fpv_tg_trim_lower(char* text);
bool fpv_tg_parse_uint64_value(const char* text, uint64_t* out);

void fpv_tg_message_clear(fpv_tg_message_t* message);
void fpv_tg_callback_clear(fpv_tg_callback_t* cb);
bool fpv_tg_parse_message(const fpv_json_value_t* value, fpv_tg_message_t* out);
bool fpv_tg_parse_callback(const fpv_json_value_t* value, fpv_tg_callback_t* out);
bool fpv_tg_format_time_hms(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len);

bool fpv_tg_parse_bool(const char* value, bool* out);
bool fpv_tg_starts_with(const char* text, const char* prefix);
bool fpv_tg_generate_key(char* buffer, size_t length);
size_t fpv_tg_get_offset(size_t element_index, size_t max_per_page);
bool fpv_tg_find_ini_section(
    fpv_ini_t* ini,
    const char* name,
    size_t* out_index);
void fpv_tg_add_navigation_buttons(
    fpv_tg_keyboard_t* kb,
    size_t offset,
    size_t max_per_page,
    size_t page_count,
    size_t total_count,
    const char* callback_base,
    const char* extra);
fpv_tg_attempt_t* fpv_tg_find_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id);
uint32_t fpv_tg_increment_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id);
void fpv_tg_clear_attempt(
    fpv_telegram_service_t* service,
    int64_t user_id);
bool fpv_tg_set_session(
    fpv_telegram_service_t* service,
    int64_t tg_user_id,
    const char* user_id);
fpv_funpay_account_t* fpv_tg_get_account(fpv_telegram_service_t* service);
void fpv_tg_setup_default_notifications(
    fpv_telegram_service_t* service,
    int64_t chat_id);
char* fpv_tg_config_path(
    const fpv_telegram_service_t* service,
    const char* name);
char* fpv_tg_products_path(
    const fpv_telegram_service_t* service,
    const char* name);
bool fpv_tg_has_entitlement(
    fpv_telegram_service_t* service,
    fpv_feature_flag_t feature);
void fpv_tg_send_feature_unavailable(
    fpv_telegram_service_t* service,
    int64_t chat_id);
bool fpv_tg_reload_main_settings(fpv_telegram_service_t* service);
void fpv_tg_set_my_commands(fpv_telegram_service_t* service);
bool fpv_tg_load_settings(
    fpv_telegram_service_t* service,
    fpv_settings_t* out_settings);
bool fpv_tg_update_main_config_value(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    const char* value);
bool fpv_tg_update_main_config_bool(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key,
    bool value);
fpv_ini_t* fpv_tg_load_ini(
    fpv_telegram_service_t* service,
    const char* name);
bool fpv_tg_save_ini(
    fpv_telegram_service_t* service,
    const char* name,
    const fpv_ini_t* ini);
void fpv_tg_free_string_array(char** items, size_t count);
int fpv_tg_string_compare(const void* left, const void* right);
char** fpv_tg_list_products_files(
    const fpv_telegram_service_t* service,
    size_t* out_count);
bool fpv_tg_file_size(const char* path, size_t* out_size);
bool fpv_tg_format_datetime(
    uint64_t timestamp_ms,
    char* buffer,
    size_t buffer_len);
bool fpv_tg_is_private_chat(const fpv_tg_message_t* message);
char* fpv_tg_trim_copy(const char* text);
char* fpv_tg_normalize_email(const char* email);
void fpv_tg_uppercase_ascii(char* text);
char* fpv_tg_trim_inplace(char* text);
bool fpv_tg_is_simple_tag(const char* text);
bool fpv_tg_ar_command_exists(fpv_ini_t* ini, const char* command);
size_t fpv_tg_split_tokens(char* value, char** tokens, size_t max_tokens);
bool fpv_tg_parse_size_value(const char* text, size_t* out);
bool fpv_tg_parse_command(
    const char* text,
    char* command,
    size_t command_size,
    const char** out_args);
char* fpv_tg_replace_username(const char* text, const char* username);
bool fpv_tg_string_list_contains(
    const fpv_string_list_t* list,
    const char* value);
bool fpv_tg_string_list_add(
    fpv_string_list_t* list,
    const char* value);
bool fpv_tg_string_list_remove(
    fpv_string_list_t* list,
    size_t index);
char* fpv_tg_blacklist_path(const fpv_telegram_service_t* service);
fpv_result_t fpv_tg_load_blacklist(
    const fpv_telegram_service_t* service,
    fpv_string_list_t* out_list);
fpv_result_t fpv_tg_save_blacklist(
    const fpv_telegram_service_t* service,
    const fpv_string_list_t* list);
bool fpv_tg_is_log_file(const char* name);
bool fpv_tg_get_file_mtime(const char* path, uint64_t* out_time);
char* fpv_tg_find_latest_log(const fpv_telegram_service_t* service);

char* fpv_tg_build_reply_keyboard(
    fpv_telegram_service_t* service,
    uint64_t chat_id,
    const char* username,
    bool again,
    bool extend);
char* fpv_tg_build_order_keyboard(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* username,
    uint64_t chat_id,
    bool confirmation,
    bool no_refund);
char* fpv_tg_build_messages_text(
    fpv_telegram_service_t* service,
    const fpv_message_t* const* messages,
    size_t message_count,
    const char* chat_name,
    uint64_t account_id,
    size_t max_messages);
char* fpv_tg_format_toggle_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled);
char* fpv_tg_format_bell_label(
    fpv_telegram_service_t* service,
    const char* key,
    bool enabled);
char* fpv_tg_build_old_keyboard(void);
char* fpv_tg_build_settings_sections(fpv_telegram_service_t* service);
char* fpv_tg_build_settings_group(
    fpv_telegram_service_t* service,
    const char* group);
char* fpv_tg_build_main_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_new_message_view_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_greeting_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_order_confirm_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_review_reply_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_notifications_settings(
    fpv_telegram_service_t* service,
    uint64_t chat_id);
char* fpv_tg_build_blacklist_settings(
    fpv_telegram_service_t* service,
    const fpv_settings_t* settings);
char* fpv_tg_build_auto_response_settings(fpv_telegram_service_t* service);
char* fpv_tg_build_auto_delivery_settings(fpv_telegram_service_t* service);
char* fpv_tg_build_configs_uploader(fpv_telegram_service_t* service);
char* fpv_tg_build_profile_keyboard(
    fpv_telegram_service_t* service,
    bool advanced);
char* fpv_tg_build_command_info_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index);
char* fpv_tg_build_commands_list_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t offset);
char* fpv_tg_build_edit_command_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index,
    size_t offset);
char* fpv_tg_build_cmd_refresh_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_build_refresh_keyboard(
    fpv_telegram_service_t* service,
    const char* callback);
char* fpv_tg_build_ar_add_error_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_build_ar_add_success_keyboard(
    fpv_telegram_service_t* service,
    size_t command_index,
    size_t offset);
char* fpv_tg_build_ar_edit_done_keyboard(
    fpv_telegram_service_t* service,
    const char* edit_callback,
    size_t command_index,
    size_t offset);
char* fpv_tg_build_products_files_list(
    fpv_telegram_service_t* service,
    size_t offset);
char* fpv_tg_build_products_file_edit_keyboard(
    fpv_telegram_service_t* service,
    size_t file_index,
    size_t offset,
    bool confirm_delete);
char* fpv_tg_build_products_file_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    const char* file_name);
char* fpv_tg_build_lots_list(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t offset);
char* fpv_tg_build_funpay_lots_list(
    fpv_telegram_service_t* service,
    size_t offset);
char* fpv_tg_build_lot_info_text(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t lot_index);
char* fpv_tg_build_edit_lot_keyboard(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    const fpv_settings_t* settings,
    size_t lot_index,
    size_t offset);

char* fpv_tg_build_desc_with_text(
    fpv_telegram_service_t* service,
    const char* key,
    const char* value);
char* fpv_tg_build_desc_with_chat_id(
    fpv_telegram_service_t* service,
    int64_t chat_id);
void fpv_tg_send_menu(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_open_main_sections(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    bool second_page);
void fpv_tg_open_settings_category(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* category);
const char* fpv_tg_category_for_section(const char* section);
bool fpv_tg_setting_allowed(
    fpv_telegram_service_t* service,
    const char* section,
    const char* key);
void fpv_tg_toggle_setting(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* section,
    const char* key);
void fpv_tg_toggle_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* type);
void fpv_tg_switch_language(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* lang);
void fpv_tg_handle_old_help(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback);
void fpv_tg_prompt_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    fpv_tg_state_type_t type,
    const char* prompt_key);
void fpv_tg_prompt_state_from_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t type,
    const char* prompt_text);
void fpv_tg_prompt_state_with_data(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t type,
    const char* prompt_text,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_document_upload(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    fpv_tg_state_type_t type);
bool fpv_tg_handle_photo_upload(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
bool fpv_tg_handle_upload_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
bool fpv_tg_handle_send_fp_message(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_misc_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
void fpv_tg_send_cfg_not_found(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* name);

void fpv_tg_send_auth_error(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    fpv_result_t result);
fpv_result_t fpv_tg_resolve_user_context(
    fpv_telegram_service_t* service,
    const char* user_id,
    fpv_organization_t** out_org,
    fpv_team_t** out_team,
    fpv_role_t* out_role);
char* fpv_tg_build_auth_menu_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_build_workspace_menu_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_build_timezone_keyboard(fpv_telegram_service_t* service);
char* fpv_tg_build_currency_keyboard(fpv_telegram_service_t* service);
void fpv_tg_send_login_menu(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_workspace_menu(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_finalize_workspace_create(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int64_t tg_user_id,
    const char* org_name,
    const char* timezone,
    const char* currency);
bool fpv_tg_handle_auth_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
bool fpv_tg_handle_auth_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback);

char* fpv_tg_build_templates_list(
    fpv_telegram_service_t* service,
    size_t offset);
char* fpv_tg_build_template_edit_keyboard(
    fpv_telegram_service_t* service,
    size_t template_index,
    size_t offset);
char* fpv_tg_build_templates_list_ans(
    fpv_telegram_service_t* service,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra);
bool fpv_tg_check_template_index(
    fpv_telegram_service_t* service,
    size_t template_index,
    int64_t chat_id,
    int message_id,
    bool edit_message);
void fpv_tg_open_templates_list_ans(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra);
void fpv_tg_open_template_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset);
void fpv_tg_delete_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    size_t offset);
void fpv_tg_send_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t template_index,
    uint64_t chat_id,
    const char* username,
    int prev_page,
    const char* extra1,
    const char* extra2);
void fpv_tg_open_templates_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_prompt_add_template(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
bool fpv_tg_handle_add_template(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_template_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);

void fpv_tg_reload_auto_response(fpv_telegram_service_t* service);
bool fpv_tg_check_ar_command_index(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t command_index,
    int64_t chat_id,
    int message_id,
    bool edit_message);
void fpv_tg_open_ar_commands_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_open_ar_command_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset);
void fpv_tg_prompt_ar_edit_text(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    fpv_tg_state_type_t state,
    const char* prompt_key,
    size_t command_index,
    size_t offset);
void fpv_tg_toggle_ar_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset);
void fpv_tg_delete_ar_command(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t command_index,
    size_t offset);
bool fpv_tg_handle_ar_add_command(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
bool fpv_tg_handle_ar_edit_response(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_ar_edit_notification(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_ar_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);

void fpv_tg_reload_auto_delivery(fpv_telegram_service_t* service);
bool fpv_tg_check_ad_lot_index(
    fpv_telegram_service_t* service,
    fpv_ini_t* ini,
    size_t lot_index,
    int64_t chat_id,
    int message_id,
    bool edit_message);
bool fpv_tg_check_products_file_index(
    fpv_telegram_service_t* service,
    size_t file_index,
    size_t file_count,
    int64_t chat_id,
    int message_id,
    bool edit_message,
    const char* back_callback);
char* fpv_tg_build_ad_edit_done_keyboard(
    fpv_telegram_service_t* service,
    const char* edit_callback,
    size_t lot_index,
    size_t offset);
void fpv_tg_open_ad_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_open_funpay_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_open_products_files_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_open_products_file_editor(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index,
    size_t offset);
void fpv_tg_open_edit_ad_lot(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset);
void fpv_tg_prompt_ad_edit_text(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset);
bool fpv_tg_add_delivery_test(
    fpv_telegram_service_t* service,
    const char* key,
    const char* lot_name);
void fpv_tg_add_ad_lot_from_funpay(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t fp_lot_index,
    size_t offset);
void fpv_tg_toggle_lot_setting(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* key,
    size_t lot_index,
    size_t offset);
void fpv_tg_create_delivery_test(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset);
void fpv_tg_delete_ad_lot(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t lot_index,
    size_t offset);
void fpv_tg_update_funpay_lots_list(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t offset);
void fpv_tg_send_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index);
void fpv_tg_confirm_delete_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    size_t file_index,
    size_t offset,
    bool confirmed);
bool fpv_tg_handle_ad_add_lot_manual(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_ad_edit_delivery_text(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_ad_bind_products(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_create_products_file(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
char* fpv_tg_build_products_payload(
    const char* text,
    size_t* out_count);
bool fpv_tg_handle_add_products(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message,
    const fpv_tg_state_data_t* data);
bool fpv_tg_handle_manual_ad_test(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);
bool fpv_tg_handle_ad_state(
    fpv_telegram_service_t* service,
    const fpv_tg_message_t* message);

void fpv_tg_update_reply_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username,
    bool again,
    bool extend);
void fpv_tg_update_order_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username,
    bool confirmation,
    bool no_refund);
void fpv_tg_extend_chat_history(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username);
void fpv_tg_confirm_refund(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username);

char* fpv_tg_build_power_off_keyboard(
    fpv_telegram_service_t* service,
    uint64_t instance_id,
    int state);
bool fpv_tg_is_profile_missing_stats(const fpv_funpay_error_t* error);
void fpv_tg_log_funpay_error(
    fpv_telegram_service_t* service,
    const char* context,
    fpv_result_t result,
    const fpv_funpay_error_t* error);
void fpv_tg_send_profile(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_update_profile_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    bool advanced);
void fpv_tg_send_sysinfo(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_all_settings(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_about(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_logs(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_delete_logs(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_blacklist(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_send_old_orders(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_change_cookie(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* token);
void fpv_tg_open_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_close_old_keyboard(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_restart(fpv_telegram_service_t* service, int64_t chat_id);
void fpv_tg_power_off(fpv_telegram_service_t* service, int64_t chat_id);
char* fpv_tg_build_empty_inline_keyboard(void);
const char* fpv_tg_power_off_state_key(int state);
void fpv_tg_cancel_shutdown_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback);
void fpv_tg_handle_shutdown_callback(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    int state,
    uint64_t instance_id);

bool fpv_tg_send_notification(
    fpv_telegram_service_t* service,
    const char* text,
    const char* reply_markup,
    const char* notification_type,
    const void* photo_data,
    size_t photo_size,
    bool pin,
    bool capture_init);
void* fpv_tg_thread_main(void* context);

char* fpv_tg_build_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance,
    uint32_t active_sales);
char* fpv_tg_build_adv_profile_text(
    fpv_telegram_service_t* service,
    const fpv_funpay_balance_t* balance);
char* fpv_tg_build_sysinfo_text(
    fpv_telegram_service_t* service,
    uint64_t chat_id);

#endif
