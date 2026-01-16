/* FunPay Vertex Telegram service interface. */

#ifndef FPV_TELEGRAM_H
#define FPV_TELEGRAM_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_log.h"
#include "fpv_core/fpv_storage.h"

typedef struct fpv_feature_state fpv_feature_state_t;
typedef struct fpv_telegram_service fpv_telegram_service_t;

typedef void (*fpv_telegram_control_fn)(void* context);

typedef struct fpv_tg_http_response {
  long status;
  char* body;
  size_t body_size;
} fpv_tg_http_response_t;

fpv_telegram_service_t* fpv_telegram_service_create(
    const char* token,
    const char* secret,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    const char* language,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus,
    void* control_context,
    fpv_telegram_control_fn restart_fn,
    fpv_telegram_control_fn shutdown_fn);
void fpv_telegram_service_destroy(fpv_telegram_service_t* service);

bool fpv_telegram_service_start(fpv_telegram_service_t* service);
void fpv_telegram_service_stop(fpv_telegram_service_t* service);

void fpv_telegram_service_attach_account(
    fpv_telegram_service_t* service,
    fpv_funpay_account_t* account);
void fpv_telegram_service_attach_runner(
    fpv_telegram_service_t* service,
    fpv_funpay_runner_t* runner);
void fpv_telegram_service_attach_features(
    fpv_telegram_service_t* service,
    fpv_feature_state_t* features);
void fpv_telegram_service_update_language(
    fpv_telegram_service_t* service,
    const char* locales_dir,
    const char* language);
void fpv_telegram_service_update_init_messages(
    fpv_telegram_service_t* service);

void fpv_telegram_service_notify_delivery(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    const char* delivery_text,
    int64_t goods_left,
    bool success);
void fpv_telegram_service_notify_new_message(
    fpv_telegram_service_t* service,
    const fpv_message_t* const* messages,
    size_t message_count,
    uint64_t chat_id,
    const char* chat_name,
    uint64_t account_id);
void fpv_telegram_service_notify_command(
    fpv_telegram_service_t* service,
    uint64_t chat_id,
    const char* chat_name,
    const char* username,
    const char* command,
    const char* text);
void fpv_telegram_service_notify_new_order(
    fpv_telegram_service_t* service,
    const char* order_title,
    const char* order_id,
    const char* buyer_username,
    double price,
    const char* price_text,
    uint64_t chat_id,
    const char* delivery_info);
void fpv_telegram_service_notify_order_confirmed(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    uint64_t chat_id);
void fpv_telegram_service_notify_review(
    fpv_telegram_service_t* service,
    const char* order_id,
    int stars,
    const char* review_text,
    const char* reply_text,
    uint64_t chat_id,
    const char* buyer_username);
void fpv_telegram_service_notify_lots_activated(
    fpv_telegram_service_t* service,
    const char* lot_list);
void fpv_telegram_service_notify_lots_deactivated(
    fpv_telegram_service_t* service,
    const char* lot_list);
void fpv_telegram_service_notify_lots_raised(
    fpv_telegram_service_t* service,
    const char* category_name);
char* fpv_telegram_service_take_delivery_test(
    fpv_telegram_service_t* service,
    const char* key);

#if defined(FPV_ENABLE_TEST_HOOKS)
struct curl_mime;

typedef fpv_result_t (*fpv_telegram_http_mock_fn)(
    const char* method,
    const char* url,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_tg_http_response_t* response,
    void* user_data);

typedef fpv_result_t (*fpv_telegram_http_multipart_mock_fn)(
    const char* url,
    struct curl_mime* mime,
    fpv_tg_http_response_t* response,
    void* user_data);

void fpv_telegram_set_http_mock(
    fpv_telegram_http_mock_fn request,
    fpv_telegram_http_multipart_mock_fn multipart,
    void* user_data);
void fpv_telegram_clear_http_mock(void);
#endif

#endif
