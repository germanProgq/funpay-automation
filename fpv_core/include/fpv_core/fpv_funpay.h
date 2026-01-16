/* FunPay Vertex FunPay API integration. */

#ifndef FPV_FUNPAY_H
#define FPV_FUNPAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_logger fpv_logger_t;

typedef enum fpv_funpay_error_code {
  FPV_FUNPAY_OK = 0,
  FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED = 1,
  FPV_FUNPAY_ERR_RUNNER_ALREADY_ATTACHED = 2,
  FPV_FUNPAY_ERR_REQUEST_FAILED = 3,
  FPV_FUNPAY_ERR_UNAUTHORIZED = 4,
  FPV_FUNPAY_ERR_PARSE = 5,
  FPV_FUNPAY_ERR_NOT_FOUND = 6,
  FPV_FUNPAY_ERR_RATE_LIMIT = 7,
  FPV_FUNPAY_ERR_NETWORK = 8
} fpv_funpay_error_code_t;

typedef struct fpv_funpay_error {
  fpv_funpay_error_code_t code;
  long http_status;
  char* url;
  char* method;
  char* message;
} fpv_funpay_error_t;

FPV_CORE_API void fpv_funpay_error_clear(fpv_funpay_error_t* error);

typedef struct fpv_funpay_proxy_config {
  bool enabled;
  const char* host;
  uint16_t port;
  const char* username;
  const char* password;
} fpv_funpay_proxy_config_t;

typedef struct fpv_funpay_account_config {
  const char* golden_key;
  const char* user_agent;
  uint32_t timeout_ms;
  fpv_funpay_proxy_config_t proxy;
} fpv_funpay_account_config_t;

typedef struct fpv_funpay_balance {
  double total_rub;
  double available_rub;
  double total_usd;
  double available_usd;
  double total_eur;
  double available_eur;
} fpv_funpay_balance_t;

FPV_CORE_API void fpv_funpay_balance_clear(fpv_funpay_balance_t* balance);

typedef struct fpv_funpay_review {
  int stars;
  char* text;
  char* reply;
  bool has_review;
  bool has_reply;
} fpv_funpay_review_t;

typedef struct fpv_funpay_order_detail {
  char* id;
  fpv_order_status_t status;
  double amount;
  char* currency;
  uint32_t quantity;
  char* title;
  char* description;
  char* buyer_username;
  uint64_t buyer_id;
  char* seller_username;
  uint64_t seller_id;
  fpv_funpay_review_t review;
} fpv_funpay_order_detail_t;

FPV_CORE_API void fpv_funpay_order_detail_clear(
    fpv_funpay_order_detail_t* detail);

typedef struct fpv_funpay_lot_section {
  uint64_t id;
  bool is_currency;
} fpv_funpay_lot_section_t;

typedef struct fpv_funpay_account fpv_funpay_account_t;
typedef struct fpv_funpay_runner fpv_funpay_runner_t;

FPV_CORE_API fpv_funpay_account_t* fpv_funpay_account_create(
    const fpv_funpay_account_config_t* config,
    fpv_funpay_error_t* error);
FPV_CORE_API void fpv_funpay_account_set_logger(
    fpv_funpay_account_t* account,
    fpv_logger_t* logger,
    bool debug_messages);
FPV_CORE_API void fpv_funpay_account_destroy(fpv_funpay_account_t* account);
FPV_CORE_API fpv_result_t fpv_funpay_account_refresh(
    fpv_funpay_account_t* account,
    fpv_funpay_error_t* error);
FPV_CORE_API bool fpv_funpay_account_is_initiated(
    const fpv_funpay_account_t* account);
FPV_CORE_API uint64_t fpv_funpay_account_id(
    const fpv_funpay_account_t* account);
FPV_CORE_API const char* fpv_funpay_account_username(
    const fpv_funpay_account_t* account);
FPV_CORE_API const char* fpv_funpay_account_currency(
    const fpv_funpay_account_t* account);
FPV_CORE_API uint32_t fpv_funpay_account_active_sales(
    const fpv_funpay_account_t* account);
FPV_CORE_API uint32_t fpv_funpay_account_active_purchases(
    const fpv_funpay_account_t* account);
FPV_CORE_API uint64_t fpv_funpay_account_last_update_ms(
    const fpv_funpay_account_t* account);
FPV_CORE_API const char* fpv_funpay_account_csrf_token(
    const fpv_funpay_account_t* account);
FPV_CORE_API fpv_result_t fpv_funpay_account_set_golden_key(
    fpv_funpay_account_t* account,
    const char* golden_key);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_balance(
    fpv_funpay_account_t* account,
    fpv_funpay_balance_t* balance,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_order_detail(
    fpv_funpay_account_t* account,
    const char* order_id,
    fpv_funpay_order_detail_t* detail,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_send_review(
    fpv_funpay_account_t* account,
    const char* order_id,
    const char* text,
    int rating,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_refund(
    fpv_funpay_account_t* account,
    const char* order_id,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_upload_image(
    fpv_funpay_account_t* account,
    const void* data,
    size_t size,
    const char* filename,
    uint64_t* out_image_id,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_chat_t* fpv_funpay_account_find_chat_by_name(
    fpv_funpay_account_t* account,
    const char* name,
    bool refresh,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_send_message(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    const char* text,
    fpv_message_t** out_message,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_send_image(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    uint64_t image_id,
    fpv_message_t** out_message,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_chat_history(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_orders_page(
    fpv_funpay_account_t* account,
    const char* state_filter,
    const char* continue_from,
    fpv_order_t*** out_orders,
    size_t* out_count,
    char** out_continue,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_trade_lots(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id,
    bool is_currency,
    fpv_lot_t*** out_lots,
    size_t* out_count,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_lot_sections(
    fpv_funpay_account_t* account,
    fpv_funpay_lot_section_t** out_sections,
    size_t* out_count,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_set_lot_active(
    fpv_funpay_account_t* account,
    uint64_t lot_id,
    bool active,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_account_get_lot_subcategories(
    fpv_funpay_account_t* account,
    uint64_t** out_ids,
    size_t* out_count,
    fpv_funpay_error_t* error);
FPV_CORE_API bool fpv_funpay_account_get_subcategory_category(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id,
    uint64_t* out_category_id,
    const char** out_category_name);
FPV_CORE_API fpv_result_t fpv_funpay_account_raise_lots(
    fpv_funpay_account_t* account,
    uint64_t category_id,
    const uint64_t* subcategory_ids,
    size_t subcategory_count,
    uint32_t* out_wait_seconds,
    fpv_funpay_error_t* error);

typedef enum fpv_funpay_event_type {
  FPV_FUNPAY_EVENT_INITIAL_CHAT = 1,
  FPV_FUNPAY_EVENT_CHATS_LIST_CHANGED = 2,
  FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED = 3,
  FPV_FUNPAY_EVENT_NEW_MESSAGE = 4,
  FPV_FUNPAY_EVENT_INITIAL_ORDER = 5,
  FPV_FUNPAY_EVENT_ORDERS_LIST_CHANGED = 6,
  FPV_FUNPAY_EVENT_NEW_ORDER = 7,
  FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED = 8
} fpv_funpay_event_type_t;

typedef struct fpv_funpay_event {
  fpv_funpay_event_type_t type;
  char* runner_tag;
  uint64_t timestamp_ms;
  fpv_chat_t* chat;
  fpv_chat_t** chats;
  size_t chat_count;
  fpv_message_t* message;
  fpv_order_t* order;
  uint32_t purchases;
  uint32_t sales;
} fpv_funpay_event_t;

typedef struct fpv_funpay_event_batch {
  fpv_funpay_event_t** events;
  size_t count;
} fpv_funpay_event_batch_t;

FPV_CORE_API void fpv_funpay_event_destroy(fpv_funpay_event_t* event);
FPV_CORE_API void fpv_funpay_event_batch_destroy(
    fpv_funpay_event_batch_t* batch);

typedef struct fpv_funpay_runner_config {
  bool disable_message_requests;
  bool disable_order_requests;
  uint32_t requests_delay_ms;
} fpv_funpay_runner_config_t;

FPV_CORE_API fpv_funpay_runner_t* fpv_funpay_runner_create(
    fpv_funpay_account_t* account,
    const fpv_funpay_runner_config_t* config,
    fpv_funpay_error_t* error);
FPV_CORE_API void fpv_funpay_runner_destroy(fpv_funpay_runner_t* runner);
FPV_CORE_API fpv_result_t fpv_funpay_runner_poll(
    fpv_funpay_runner_t* runner,
    fpv_funpay_event_batch_t* batch,
    fpv_funpay_error_t* error);
FPV_CORE_API fpv_result_t fpv_funpay_runner_mark_by_bot(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id);
FPV_CORE_API fpv_result_t fpv_funpay_runner_update_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* message_text,
    const char* message_time);
FPV_CORE_API void fpv_funpay_runner_set_message_requests(
    fpv_funpay_runner_t* runner,
    bool enabled);

#ifdef __cplusplus
}
#endif

#endif
