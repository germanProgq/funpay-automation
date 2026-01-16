/* FunPay Vertex phase 4 feature engine. */

#ifndef FPV_FEATURES_H
#define FPV_FEATURES_H

#include <stdbool.h>
#include <stdint.h>

#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_log.h"
#include "fpv_core/fpv_scheduler.h"
#include "fpv_core/fpv_storage.h"
#include "fpv_feature_config.h"
#include "fpv_settings.h"

typedef struct fpv_feature_state fpv_feature_state_t;
typedef struct fpv_telegram_service fpv_telegram_service_t;

fpv_result_t fpv_features_init(
    fpv_feature_state_t** out_state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    fpv_logger_t* logger);
void fpv_features_destroy(fpv_feature_state_t* state);
fpv_result_t fpv_features_update_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir);
fpv_result_t fpv_features_reload_auto_response(
    fpv_feature_state_t* state,
    const char* path);
fpv_result_t fpv_features_reload_auto_delivery(
    fpv_feature_state_t* state,
    const char* path,
    const char* products_dir);
fpv_result_t fpv_features_reload_blacklist(fpv_feature_state_t* state);
void fpv_features_set_telegram(
    fpv_feature_state_t* state,
    fpv_telegram_service_t* telegram);
fpv_result_t fpv_features_send_message(
    fpv_feature_state_t* state,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark);

void fpv_features_attach(
    fpv_feature_state_t* state,
    fpv_funpay_account_t* account,
    fpv_funpay_runner_t* runner,
    fpv_scheduler_t* scheduler,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus);

void fpv_features_handle_event(
    fpv_feature_state_t* state,
    const fpv_funpay_event_t* event);

void fpv_features_schedule_background(fpv_feature_state_t* state);

#endif
