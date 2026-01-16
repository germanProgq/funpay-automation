/* FunPay Vertex core event bus API. */

#ifndef FPV_EVENT_BUS_H
#define FPV_EVENT_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"
#include "fpv_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_event_type {
  FPV_EVENT_CORE_STATUS = 1,
  FPV_EVENT_LOG = 2,
  FPV_EVENT_USER_PROFILE = 3,
  FPV_EVENT_ORDER = 4,
  FPV_EVENT_CHAT = 5,
  FPV_EVENT_LOT = 6,
  FPV_EVENT_MESSAGE = 7,
  FPV_EVENT_NOTIFICATION = 8,
  FPV_EVENT_PLUGIN = 9
} fpv_event_type_t;

typedef enum fpv_log_level {
  FPV_LOG_DEBUG = 0,
  FPV_LOG_INFO = 1,
  FPV_LOG_WARNING = 2,
  FPV_LOG_ERROR = 3
} fpv_log_level_t;

typedef struct fpv_log_entry {
  fpv_log_level_t level;
  char* component;
  char* message;
} fpv_log_entry_t;

typedef struct fpv_core_status_event {
  fpv_core_status_t status;
  char* detail;
} fpv_core_status_event_t;

typedef struct fpv_event {
  fpv_event_type_t type;
  uint64_t timestamp_ms;
  void* payload;
} fpv_event_t;

typedef struct fpv_event_batch {
  fpv_event_t** events;
  size_t count;
} fpv_event_batch_t;

typedef struct fpv_event_bus fpv_event_bus_t;

FPV_CORE_API fpv_event_bus_t* fpv_event_bus_create(void);
FPV_CORE_API void fpv_event_bus_destroy(fpv_event_bus_t* bus);

FPV_CORE_API fpv_result_t fpv_event_bus_publish(
    fpv_event_bus_t* bus,
    fpv_event_t* event);
FPV_CORE_API fpv_event_batch_t fpv_event_bus_drain(fpv_event_bus_t* bus);
FPV_CORE_API void fpv_event_batch_destroy(fpv_event_batch_t* batch);

FPV_CORE_API fpv_event_t* fpv_event_create_core_status(
    fpv_core_status_t status,
    const char* detail,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_log(
    fpv_log_level_t level,
    const char* component,
    const char* message,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_user_profile(
    const fpv_user_profile_t* profile,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_order(
    const fpv_order_t* order,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_chat(
    const fpv_chat_t* chat,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_lot(
    const fpv_lot_t* lot,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_message(
    const fpv_message_t* message,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_notification(
    const fpv_notification_t* notification,
    uint64_t timestamp_ms);
FPV_CORE_API fpv_event_t* fpv_event_create_plugin(
    const fpv_plugin_t* plugin,
    uint64_t timestamp_ms);

FPV_CORE_API const fpv_core_status_event_t* fpv_event_as_core_status(
    const fpv_event_t* event);
FPV_CORE_API const fpv_log_entry_t* fpv_event_as_log(const fpv_event_t* event);
FPV_CORE_API const fpv_user_profile_t* fpv_event_as_user_profile(
    const fpv_event_t* event);
FPV_CORE_API const fpv_order_t* fpv_event_as_order(const fpv_event_t* event);
FPV_CORE_API const fpv_chat_t* fpv_event_as_chat(const fpv_event_t* event);
FPV_CORE_API const fpv_lot_t* fpv_event_as_lot(const fpv_event_t* event);
FPV_CORE_API const fpv_message_t* fpv_event_as_message(
    const fpv_event_t* event);
FPV_CORE_API const fpv_notification_t* fpv_event_as_notification(
    const fpv_event_t* event);
FPV_CORE_API const fpv_plugin_t* fpv_event_as_plugin(
    const fpv_event_t* event);

FPV_CORE_API void fpv_event_destroy(fpv_event_t* event);

#ifdef __cplusplus
}
#endif

#endif
