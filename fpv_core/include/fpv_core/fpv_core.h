/* FunPay Vertex core runtime API. */

#ifndef FPV_CORE_H
#define FPV_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"
#include "fpv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_event_bus fpv_event_bus_t;
typedef struct fpv_core fpv_core_t;

typedef struct fpv_core_config {
  const char* data_dir;
  const char* config_dir;
  const char* logs_dir;
  const char* plugins_dir;
  const char* locales_dir;
  const char* locale;
} fpv_core_config_t;

FPV_CORE_API fpv_core_t* fpv_core_create(
    const fpv_core_config_t* config,
    fpv_event_bus_t* bus);
FPV_CORE_API void fpv_core_destroy(fpv_core_t* core);

FPV_CORE_API fpv_result_t fpv_core_start(fpv_core_t* core, uint64_t now_ms);
FPV_CORE_API fpv_result_t fpv_core_stop(fpv_core_t* core, uint64_t now_ms);
FPV_CORE_API fpv_result_t fpv_core_tick(fpv_core_t* core, uint64_t now_ms);
FPV_CORE_API fpv_core_status_t fpv_core_status(const fpv_core_t* core);
FPV_CORE_API fpv_result_t fpv_core_send_message(
    fpv_core_t* core,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark);
FPV_CORE_API fpv_result_t fpv_core_fetch_chat_history(
    fpv_core_t* core,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count);
FPV_CORE_API fpv_result_t fpv_core_reload_auto_response(fpv_core_t* core);
FPV_CORE_API fpv_result_t fpv_core_reload_auto_delivery(fpv_core_t* core);
FPV_CORE_API fpv_result_t fpv_core_refresh_lots(fpv_core_t* core);
FPV_CORE_API fpv_result_t fpv_core_set_lot_active(
    fpv_core_t* core,
    uint64_t lot_id,
    bool active);
FPV_CORE_API fpv_result_t fpv_core_clone_lot(
    fpv_core_t* core,
    uint64_t lot_id,
    const char* title,
    const char* original_title,
    uint64_t* out_lot_id);
FPV_CORE_API fpv_result_t fpv_core_clone_lot_from_url(
    fpv_core_t* core,
    const char* lot_url,
    const char* title,
    uint64_t* out_lot_id);

#ifdef __cplusplus
}
#endif

#endif
