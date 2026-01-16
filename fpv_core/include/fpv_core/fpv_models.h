/* FunPay Vertex core data models. */

#ifndef FPV_MODELS_H
#define FPV_MODELS_H

#include <stdbool.h>
#include <stdint.h>

#include "fpv_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_order_status {
  FPV_ORDER_UNKNOWN = 0,
  FPV_ORDER_PENDING = 1,
  FPV_ORDER_PAID = 2,
  FPV_ORDER_DELIVERED = 3,
  FPV_ORDER_CANCELLED = 4,
  FPV_ORDER_REFUNDED = 5
} fpv_order_status_t;

typedef enum fpv_message_direction {
  FPV_MESSAGE_INBOUND = 0,
  FPV_MESSAGE_OUTBOUND = 1
} fpv_message_direction_t;

typedef enum fpv_notification_severity {
  FPV_NOTIFICATION_INFO = 0,
  FPV_NOTIFICATION_WARNING = 1,
  FPV_NOTIFICATION_ERROR = 2
} fpv_notification_severity_t;

typedef struct fpv_user_profile {
  char* id;
  char* username;
  char* display_name;
  double balance;
  char* currency;
  double rating;
  uint64_t registered_at_ms;
} fpv_user_profile_t;

typedef struct fpv_order {
  char* id;
  char* lot_id;
  char* chat_id;
  char* buyer_id;
  char* buyer_username;
  fpv_order_status_t status;
  double amount;
  char* currency;
  uint64_t created_at_ms;
  uint64_t updated_at_ms;
  uint32_t quantity;
  char* title;
  char* subcategory;
} fpv_order_t;

typedef struct fpv_chat {
  char* id;
  char* title;
  char* last_message_text;
  char* last_message_time;
  uint32_t unread_count;
  char* last_message_id;
} fpv_chat_t;

typedef struct fpv_lot {
  char* id;
  char* title;
  double price;
  char* currency;
  uint32_t stock;
  bool active;
} fpv_lot_t;

typedef struct fpv_message {
  char* id;
  char* chat_id;
  char* chat_name;
  char* sender_id;
  char* sender_name;
  char* text;
  char* image_url;
  char* badge;
  fpv_message_direction_t direction;
  bool by_bot;
  uint64_t created_at_ms;
} fpv_message_t;

typedef struct fpv_notification {
  char* id;
  char* type;
  char* message;
  fpv_notification_severity_t severity;
  uint64_t created_at_ms;
} fpv_notification_t;

typedef struct fpv_plugin {
  char* id;
  char* name;
  char* version;
  bool enabled;
} fpv_plugin_t;

FPV_CORE_API fpv_user_profile_t* fpv_user_profile_create(
    const char* id,
    const char* username,
    const char* display_name,
    double balance,
    const char* currency,
    double rating,
    uint64_t registered_at_ms);
FPV_CORE_API fpv_user_profile_t* fpv_user_profile_clone(
    const fpv_user_profile_t* profile);
FPV_CORE_API void fpv_user_profile_destroy(fpv_user_profile_t* profile);

FPV_CORE_API fpv_order_t* fpv_order_create(
    const char* id,
    const char* lot_id,
    const char* chat_id,
    const char* buyer_id,
    const char* buyer_username,
    fpv_order_status_t status,
    double amount,
    const char* currency,
    uint64_t created_at_ms,
    uint64_t updated_at_ms,
    uint32_t quantity,
    const char* title,
    const char* subcategory);
FPV_CORE_API fpv_order_t* fpv_order_clone(const fpv_order_t* order);
FPV_CORE_API void fpv_order_destroy(fpv_order_t* order);

FPV_CORE_API fpv_chat_t* fpv_chat_create(
    const char* id,
    const char* title,
    const char* last_message_text,
    const char* last_message_time,
    uint32_t unread_count,
    const char* last_message_id);
FPV_CORE_API fpv_chat_t* fpv_chat_clone(const fpv_chat_t* chat);
FPV_CORE_API void fpv_chat_destroy(fpv_chat_t* chat);

FPV_CORE_API fpv_lot_t* fpv_lot_create(
    const char* id,
    const char* title,
    double price,
    const char* currency,
    uint32_t stock,
    bool active);
FPV_CORE_API fpv_lot_t* fpv_lot_clone(const fpv_lot_t* lot);
FPV_CORE_API void fpv_lot_destroy(fpv_lot_t* lot);

FPV_CORE_API fpv_message_t* fpv_message_create(
    const char* id,
    const char* chat_id,
    const char* chat_name,
    const char* sender_id,
    const char* sender_name,
    const char* text,
    const char* image_url,
    const char* badge,
    fpv_message_direction_t direction,
    bool by_bot,
    uint64_t created_at_ms);
FPV_CORE_API fpv_message_t* fpv_message_clone(const fpv_message_t* message);
FPV_CORE_API void fpv_message_destroy(fpv_message_t* message);

FPV_CORE_API fpv_notification_t* fpv_notification_create(
    const char* id,
    const char* type,
    const char* message,
    fpv_notification_severity_t severity,
    uint64_t created_at_ms);
FPV_CORE_API fpv_notification_t* fpv_notification_clone(
    const fpv_notification_t* notification);
FPV_CORE_API void fpv_notification_destroy(fpv_notification_t* notification);

FPV_CORE_API fpv_plugin_t* fpv_plugin_create(
    const char* id,
    const char* name,
    const char* version,
    bool enabled);
FPV_CORE_API fpv_plugin_t* fpv_plugin_clone(const fpv_plugin_t* plugin);
FPV_CORE_API void fpv_plugin_destroy(fpv_plugin_t* plugin);

#ifdef __cplusplus
}
#endif

#endif
