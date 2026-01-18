/* FunPay Vertex core data models. */

#ifndef FPV_MODELS_H
#define FPV_MODELS_H

#include <stdbool.h>
#include <stddef.h>
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

typedef enum fpv_role {
  FPV_ROLE_UNKNOWN = 0,
  FPV_ROLE_OWNER = 1,
  FPV_ROLE_ADMIN = 2,
  FPV_ROLE_MANAGER = 3,
  FPV_ROLE_ANALYST = 4,
  FPV_ROLE_VIEWER = 5
} fpv_role_t;

typedef enum fpv_price_rule_scope {
  FPV_PRICE_SCOPE_ORGANIZATION = 1,
  FPV_PRICE_SCOPE_TEAM = 2,
  FPV_PRICE_SCOPE_ITEM = 3,
  FPV_PRICE_SCOPE_LISTING = 4
} fpv_price_rule_scope_t;

typedef enum fpv_price_undercut_type {
  FPV_UNDERCUT_NONE = 0,
  FPV_UNDERCUT_ABSOLUTE = 1,
  FPV_UNDERCUT_PERCENT = 2
} fpv_price_undercut_type_t;

typedef struct fpv_data_retention_policy {
  uint32_t audit_log_days;
  uint32_t price_history_days;
  uint32_t competitor_listing_days;
  uint32_t order_history_days;
} fpv_data_retention_policy_t;

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

typedef struct fpv_organization {
  char* id;
  char* name;
  char* timezone;
  char* currency;
  fpv_data_retention_policy_t retention;
  uint64_t created_at_ms;
  uint64_t updated_at_ms;
} fpv_organization_t;

typedef struct fpv_team {
  char* id;
  char* organization_id;
  char* name;
  bool active;
  uint64_t created_at_ms;
  uint64_t updated_at_ms;
} fpv_team_t;

typedef struct fpv_user {
  char* id;
  char* email;
  char* display_name;
  bool email_verified;
  bool active;
  uint64_t created_at_ms;
  uint64_t last_login_ms;
} fpv_user_t;

typedef struct fpv_user_role {
  char* user_id;
  char* organization_id;
  char* team_id;
  fpv_role_t role;
  uint64_t assigned_at_ms;
} fpv_user_role_t;

typedef struct fpv_account {
  char* id;
  char* organization_id;
  char* team_id;
  char* funpay_user_id;
  char* funpay_username;
  char* display_name;
  char* currency;
  bool active;
  uint64_t linked_at_ms;
  uint64_t last_sync_at_ms;
} fpv_account_t;

typedef struct fpv_item {
  char* id;
  char* organization_id;
  char* title;
  char* normalized_title;
  char* category;
  char* subcategory;
  char* description;
  char** tags;
  size_t tag_count;
  uint64_t created_at_ms;
  uint64_t updated_at_ms;
} fpv_item_t;

typedef struct fpv_listing {
  char* id;
  char* item_id;
  char* account_id;
  char* title;
  char* category;
  char* subcategory;
  char* status;
  double price;
  char* currency;
  uint32_t quantity;
  char* delivery_type;
  uint64_t last_updated_ms;
  char* description;
  char** tags;
  size_t tag_count;
} fpv_listing_t;

typedef struct fpv_competitor_listing {
  char* id;
  char* item_id;
  char* seller_id;
  double price;
  char* currency;
  bool available;
  char* delivery_type;
  double seller_rating;
  uint64_t last_seen_ms;
  char* listing_url;
} fpv_competitor_listing_t;

typedef struct fpv_price_rule {
  char* id;
  fpv_price_rule_scope_t scope;
  char* organization_id;
  char* team_id;
  char* item_id;
  char* listing_id;
  double min_margin;
  double min_price;
  fpv_price_undercut_type_t undercut_type;
  double undercut_value;
  bool enabled;
  uint64_t created_at_ms;
  uint64_t updated_at_ms;
} fpv_price_rule_t;

typedef struct fpv_price_history {
  char* id;
  char* listing_id;
  char* price_rule_id;
  double competitor_median;
  double recommended_price;
  double applied_price;
  char* currency;
  char* reason;
  uint64_t created_at_ms;
} fpv_price_history_t;

typedef struct fpv_audit_log_entry {
  char* id;
  char* organization_id;
  char* team_id;
  char* account_id;
  char* actor_user_id;
  fpv_role_t actor_role;
  char* action;
  char* target_type;
  char* target_id;
  char* summary;
  char* metadata;
  char* ip_address;
  char* user_agent;
  uint64_t created_at_ms;
} fpv_audit_log_entry_t;

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

FPV_CORE_API fpv_organization_t* fpv_organization_create(
    const char* id,
    const char* name,
    const char* timezone,
    const char* currency,
    const fpv_data_retention_policy_t* retention,
    uint64_t created_at_ms,
    uint64_t updated_at_ms);
FPV_CORE_API fpv_organization_t* fpv_organization_clone(
    const fpv_organization_t* organization);
FPV_CORE_API void fpv_organization_destroy(fpv_organization_t* organization);

FPV_CORE_API fpv_team_t* fpv_team_create(
    const char* id,
    const char* organization_id,
    const char* name,
    bool active,
    uint64_t created_at_ms,
    uint64_t updated_at_ms);
FPV_CORE_API fpv_team_t* fpv_team_clone(const fpv_team_t* team);
FPV_CORE_API void fpv_team_destroy(fpv_team_t* team);

FPV_CORE_API fpv_user_t* fpv_user_create(
    const char* id,
    const char* email,
    const char* display_name,
    bool email_verified,
    bool active,
    uint64_t created_at_ms,
    uint64_t last_login_ms);
FPV_CORE_API fpv_user_t* fpv_user_clone(const fpv_user_t* user);
FPV_CORE_API void fpv_user_destroy(fpv_user_t* user);

FPV_CORE_API fpv_user_role_t* fpv_user_role_create(
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role,
    uint64_t assigned_at_ms);
FPV_CORE_API fpv_user_role_t* fpv_user_role_clone(
    const fpv_user_role_t* role);
FPV_CORE_API void fpv_user_role_destroy(fpv_user_role_t* role);

FPV_CORE_API fpv_account_t* fpv_account_create(
    const char* id,
    const char* organization_id,
    const char* team_id,
    const char* funpay_user_id,
    const char* funpay_username,
    const char* display_name,
    const char* currency,
    bool active,
    uint64_t linked_at_ms,
    uint64_t last_sync_at_ms);
FPV_CORE_API fpv_account_t* fpv_account_clone(const fpv_account_t* account);
FPV_CORE_API void fpv_account_destroy(fpv_account_t* account);

FPV_CORE_API fpv_item_t* fpv_item_create(
    const char* id,
    const char* organization_id,
    const char* title,
    const char* normalized_title,
    const char* category,
    const char* subcategory,
    const char* description,
    const char* const* tags,
    size_t tag_count,
    uint64_t created_at_ms,
    uint64_t updated_at_ms);
FPV_CORE_API fpv_item_t* fpv_item_clone(const fpv_item_t* item);
FPV_CORE_API void fpv_item_destroy(fpv_item_t* item);

FPV_CORE_API fpv_listing_t* fpv_listing_create(
    const char* id,
    const char* item_id,
    const char* account_id,
    const char* title,
    const char* category,
    const char* subcategory,
    const char* status,
    double price,
    const char* currency,
    uint32_t quantity,
    const char* delivery_type,
    uint64_t last_updated_ms,
    const char* description,
    const char* const* tags,
    size_t tag_count);
FPV_CORE_API fpv_listing_t* fpv_listing_clone(const fpv_listing_t* listing);
FPV_CORE_API void fpv_listing_destroy(fpv_listing_t* listing);

FPV_CORE_API fpv_competitor_listing_t* fpv_competitor_listing_create(
    const char* id,
    const char* item_id,
    const char* seller_id,
    double price,
    const char* currency,
    bool available,
    const char* delivery_type,
    double seller_rating,
    uint64_t last_seen_ms,
    const char* listing_url);
FPV_CORE_API fpv_competitor_listing_t* fpv_competitor_listing_clone(
    const fpv_competitor_listing_t* listing);
FPV_CORE_API void fpv_competitor_listing_destroy(
    fpv_competitor_listing_t* listing);

FPV_CORE_API fpv_price_rule_t* fpv_price_rule_create(
    const char* id,
    fpv_price_rule_scope_t scope,
    const char* organization_id,
    const char* team_id,
    const char* item_id,
    const char* listing_id,
    double min_margin,
    double min_price,
    fpv_price_undercut_type_t undercut_type,
    double undercut_value,
    bool enabled,
    uint64_t created_at_ms,
    uint64_t updated_at_ms);
FPV_CORE_API fpv_price_rule_t* fpv_price_rule_clone(
    const fpv_price_rule_t* rule);
FPV_CORE_API void fpv_price_rule_destroy(fpv_price_rule_t* rule);

FPV_CORE_API fpv_price_history_t* fpv_price_history_create(
    const char* id,
    const char* listing_id,
    const char* price_rule_id,
    double competitor_median,
    double recommended_price,
    double applied_price,
    const char* currency,
    const char* reason,
    uint64_t created_at_ms);
FPV_CORE_API fpv_price_history_t* fpv_price_history_clone(
    const fpv_price_history_t* history);
FPV_CORE_API void fpv_price_history_destroy(fpv_price_history_t* history);

FPV_CORE_API fpv_audit_log_entry_t* fpv_audit_log_entry_create(
    const char* id,
    const char* organization_id,
    const char* team_id,
    const char* account_id,
    const char* actor_user_id,
    fpv_role_t actor_role,
    const char* action,
    const char* target_type,
    const char* target_id,
    const char* summary,
    const char* metadata,
    const char* ip_address,
    const char* user_agent,
    uint64_t created_at_ms);
FPV_CORE_API fpv_audit_log_entry_t* fpv_audit_log_entry_clone(
    const fpv_audit_log_entry_t* entry);
FPV_CORE_API void fpv_audit_log_entry_destroy(
    fpv_audit_log_entry_t* entry);

#ifdef __cplusplus
}
#endif

#endif
