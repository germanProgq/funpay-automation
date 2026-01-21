/* FunPay Vertex FunPay API internal definitions. */

#ifndef FPV_FUNPAY_INTERNAL_H
#define FPV_FUNPAY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_core/fpv_funpay.h"

#include "funpay/http/fpv_funpay_http.h"

#include "core/io/fpv_html.h"

#include "core/data/fpv_json.h"

#include "core/data/fpv_db.h"

#include "fpv_core/fpv_log.h"
#include "fpv_core/fpv_event_bus.h"
#include "core/base/fpv_platform.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


#define FPV_FUNPAY_BASE_URL "https://funpay.com/"

extern const char* FPV_FUNPAY_BOT_PREFIX;
extern const char* FPV_FUNPAY_IMAGE_TEXT;
extern const char* FPV_FUNPAY_TODAY;
extern const char* FPV_FUNPAY_YESTERDAY;
extern const char* FPV_FUNPAY_TODAY_EN;
extern const char* FPV_FUNPAY_YESTERDAY_EN;

typedef struct fpv_funpay_chat_store {
  fpv_chat_t** chats;
  size_t count;
} fpv_funpay_chat_store_t;

typedef enum fpv_funpay_subcategory_type {
  FPV_FUNPAY_SUBCATEGORY_COMMON = 0,
  FPV_FUNPAY_SUBCATEGORY_CURRENCY = 1
} fpv_funpay_subcategory_type_t;

typedef struct fpv_funpay_category {
  uint64_t id;
  char* name;
} fpv_funpay_category_t;

typedef struct fpv_funpay_subcategory {
  uint64_t id;
  fpv_funpay_subcategory_type_t type;
  uint64_t category_id;
  char* name;
} fpv_funpay_subcategory_t;

typedef struct fpv_funpay_catalog {
  fpv_funpay_category_t* categories;
  size_t category_count;
  fpv_funpay_subcategory_t* subcategories;
  size_t subcategory_count;
} fpv_funpay_catalog_t;

typedef struct fpv_funpay_account {
  char* golden_key;
  char* user_agent;
  fpv_funpay_http_client_t* http;
  fpv_db_t* session_db;
  char* csrf_token;
  char* phpsessid;
  char* username;
  char* currency;
  uint64_t id;
  uint32_t active_sales;
  uint32_t active_purchases;
  uint64_t last_update_ms;
  bool initiated;
  fpv_funpay_chat_store_t chats;
  fpv_funpay_catalog_t catalog;
  fpv_mutex_t request_mutex;
  struct fpv_funpay_runner* runner;
  fpv_logger_t* logger;
  fpv_event_bus_t* bus;
  bool debug_log_messages;
} fpv_funpay_account_t;

typedef struct fpv_funpay_order_state {
  char* id;
  fpv_order_status_t status;
} fpv_funpay_order_state_t;

typedef struct fpv_funpay_last_message {
  uint64_t chat_id;
  char* text;
  char* time_text;
} fpv_funpay_last_message_t;

typedef struct fpv_funpay_last_message_id {
  uint64_t chat_id;
  uint64_t message_id;
} fpv_funpay_last_message_id_t;

typedef struct fpv_funpay_init_message {
  uint64_t chat_id;
  char* text;
} fpv_funpay_init_message_t;

typedef struct fpv_funpay_bot_ids {
  uint64_t chat_id;
  uint64_t* ids;
  size_t count;
} fpv_funpay_bot_ids_t;

typedef struct fpv_funpay_runner {
  fpv_funpay_account_t* account;
  bool make_msg_requests;
  bool make_order_requests;
  bool first_request;
  char* last_msg_tag;
  char* last_order_tag;
  fpv_funpay_order_state_t* orders;
  size_t order_count;
  fpv_funpay_last_message_t* last_messages;
  size_t last_message_count;
  fpv_funpay_last_message_id_t* last_message_ids;
  size_t last_message_id_count;
  fpv_funpay_init_message_t* init_messages;
  size_t init_message_count;
  fpv_funpay_bot_ids_t* bot_ids;
  size_t bot_id_count;
} fpv_funpay_runner_t;

typedef struct fpv_string_builder {
  char* data;
  size_t length;
  size_t capacity;
} fpv_string_builder_t;

typedef struct fpv_funpay_chat_request {
  uint64_t chat_id;
  const char* chat_name;
} fpv_funpay_chat_request_t;

typedef struct fpv_funpay_chat_history {
  uint64_t chat_id;
  char* chat_name;
  fpv_message_t** messages;
  size_t message_count;
} fpv_funpay_chat_history_t;

typedef struct fpv_funpay_form_field {
  char* key;
  char* value;
} fpv_funpay_form_field_t;

typedef struct fpv_funpay_form {
  fpv_funpay_form_field_t* fields;
  size_t count;
} fpv_funpay_form_t;

void fpv_funpay_error_set(
    fpv_funpay_error_t* error,
    fpv_funpay_error_code_t code,
    const char* message,
    const char* url,
    const char* method,
    long status);

void fpv_funpay_sb_reset(fpv_string_builder_t* builder);
bool fpv_funpay_sb_reserve(fpv_string_builder_t* builder, size_t extra);
bool fpv_funpay_sb_append(fpv_string_builder_t* builder, const char* text);
bool fpv_funpay_sb_append_n(
    fpv_string_builder_t* builder,
    const char* text,
    size_t len);
bool fpv_funpay_sb_append_format(
    fpv_string_builder_t* builder,
    const char* format,
    ...);
char* fpv_funpay_sb_detach(fpv_string_builder_t* builder);

bool fpv_funpay_is_ascii(const char* value);
bool fpv_funpay_is_digit_ascii(unsigned char value);
char* fpv_funpay_ascii_lower(const char* value);
void fpv_funpay_trim(char* text);
char* fpv_funpay_truncate_utf8(const char* text, size_t max_chars);
char* fpv_funpay_random_tag(void);
char* fpv_funpay_url_encode(const char* value);
char* fpv_funpay_form_encode(
    const char* const* keys,
    const char* const* values,
    size_t count);
void fpv_funpay_form_clear(fpv_funpay_form_t* form);
bool fpv_funpay_form_set(
    fpv_funpay_form_t* form,
    const char* key,
    const char* value);
char* fpv_funpay_form_encode_fields(const fpv_funpay_form_t* form);
char* fpv_funpay_build_url(const char* path);
bool fpv_funpay_time_is_tag(const char* value);
uint64_t fpv_funpay_parse_order_date(const char* text);
const char* fpv_funpay_currency_from_symbol(const char* symbol);
const char* fpv_funpay_currency_from_cy(const char* cy);
bool fpv_funpay_parse_price(
    const char* text,
    double* out_price,
    const char** currency);
bool fpv_funpay_parse_user_id_from_href(
    const char* href,
    uint64_t* out_id);
int fpv_funpay_parse_rating_from_class(const char* class_value);
uint32_t fpv_funpay_parse_wait_time(const char* message);
fpv_funpay_http_header_t fpv_funpay_header(
    const char* key,
    const char* value);

bool fpv_funpay_list_contains_u64(
    const uint64_t* values,
    size_t count,
    uint64_t value);

fpv_result_t fpv_funpay_account_request(
    fpv_funpay_account_t* account,
    const char* method,
    const char* api_method,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const char* body,
    bool exclude_phpsessid,
    bool raise_not_200,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error);

const char* fpv_funpay_form_get(
    const fpv_funpay_form_t* form,
    const char* key);
bool fpv_funpay_form_has_key(const fpv_funpay_form_t* form, const char* key);
bool fpv_funpay_form_copy_common_fields(
    fpv_funpay_form_t* dest,
    const fpv_funpay_form_t* src,
    const char* const* excluded_keys,
    size_t excluded_count);
const char* fpv_funpay_form_find_first_key(
    const fpv_funpay_form_t* form,
    const char* const* keys,
    size_t key_count);
bool fpv_funpay_form_set_if_present(
    fpv_funpay_form_t* form,
    const char* key,
    const char* value);
bool fpv_funpay_form_override_title(
    fpv_funpay_form_t* form,
    const char* title,
    const char* original_title);
bool fpv_funpay_form_parse_from_html(
    xmlNode* root,
    fpv_funpay_form_t* form);

xmlNode* fpv_funpay_find_first_tag(xmlNode* node, const char* tag);
fpv_html_node_list_t fpv_funpay_find_all_tag(xmlNode* node, const char* tag);
xmlNode* fpv_funpay_find_first_by_attr(
    xmlNode* node,
    const char* tag,
    const char* attr,
    const char* value);
bool fpv_funpay_parse_uint64_attr(
    xmlNode* node,
    const char* attr,
    uint64_t* out_value);
bool fpv_funpay_parse_double_attr(
    xmlNode* node,
    const char* attr,
    double* out_value);
fpv_html_node_list_t fpv_funpay_find_all_by_attrs(
    xmlNode* node,
    const char* tag,
    const char* const* attrs,
    size_t attr_count);
xmlNode* fpv_funpay_find_first_by_class_substr(
    xmlNode* node,
    const char* tag,
    const char* class_substr);
char* fpv_funpay_find_first_attr_value(
    xmlNode* node,
    const char* tag,
    const char* attr_name);

bool fpv_funpay_debug_messages(const fpv_funpay_account_t* account);
void fpv_funpay_logf(
    fpv_funpay_account_t* account,
    fpv_log_level_t level,
    const char* format,
    ...);
void fpv_funpay_log_html_snippet(
    fpv_funpay_account_t* account,
    const char* html);

fpv_result_t fpv_funpay_parse_chat_bookmarks(
    fpv_funpay_account_t* account,
    const char* html,
    fpv_chat_t*** out_chats,
    size_t* out_count);
fpv_result_t fpv_funpay_account_request_chats(
    fpv_funpay_account_t* account,
    fpv_chat_t*** out_chats,
    size_t* out_count,
    fpv_funpay_error_t* error);
fpv_result_t fpv_funpay_parse_messages(
    fpv_funpay_account_t* account,
    const fpv_json_value_t* messages,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count);
fpv_result_t fpv_funpay_account_get_chat_histories(
    fpv_funpay_account_t* account,
    const fpv_funpay_chat_request_t* chats,
    size_t chat_count,
    fpv_funpay_chat_history_t** out_histories,
    size_t* out_history_count,
    fpv_funpay_error_t* error);
void fpv_funpay_chat_history_destroy(
    fpv_funpay_chat_history_t* histories,
    size_t history_count);

fpv_result_t fpv_funpay_account_get_orders(
    fpv_funpay_account_t* account,
    fpv_order_t*** out_orders,
    size_t* out_count,
    fpv_funpay_error_t* error);

fpv_result_t fpv_funpay_account_store_chat(
    fpv_funpay_account_t* account,
    const fpv_chat_t* chat);
fpv_chat_t** fpv_funpay_account_clone_chats(
    const fpv_funpay_account_t* account,
    size_t* count);

#endif
