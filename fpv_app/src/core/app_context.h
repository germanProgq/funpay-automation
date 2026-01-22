#ifndef FPV_APP_SRC_APP_CONTEXT_H_
#define FPV_APP_SRC_APP_CONTEXT_H_

#include <gtk/gtk.h>

#include <stddef.h>

extern "C" {
#include "fpv_core/fpv_chat_store.h"
#include "fpv_core/fpv_core.h"
#include "fpv_core/fpv_entitlements.h"
#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_identity.h"
#include "fpv_core/fpv_ini.h"
#include "fpv_core/fpv_models.h"
}

typedef struct SettingsWidgets {
  GtkWidget* org_name;
  GtkWidget* org_role;
  GtkWidget* org_account;
  GtkWidget* org_invite_button;
  GtkWidget* org_link_button;

  GtkWidget* funpay_golden_key;
  GtkWidget* funpay_user_agent;
  GtkWidget* funpay_auto_raise;
  GtkWidget* funpay_auto_response;
  GtkWidget* funpay_multi_delivery;
  GtkWidget* funpay_auto_restore;
  GtkWidget* funpay_auto_disable;
  GtkWidget* funpay_old_msg_mode;

  GtkWidget* telegram_enabled;
  GtkWidget* telegram_token;
  GtkWidget* telegram_secret;

  GtkWidget* block_delivery;
  GtkWidget* block_response;
  GtkWidget* block_new_message;
  GtkWidget* block_new_order;
  GtkWidget* block_command;

  GtkWidget* include_my;
  GtkWidget* include_fp;
  GtkWidget* include_bot;
  GtkWidget* notify_only_my;
  GtkWidget* notify_only_fp;
  GtkWidget* notify_only_bot;

  GtkWidget* greetings_cache_init;
  GtkWidget* greetings_ignore_system;
  GtkWidget* greetings_send;
  GtkWidget* greetings_text;

  GtkWidget* order_confirm_send;
  GtkWidget* order_confirm_text;

  GtkWidget* review_reply_enabled_all;
  GtkWidget* review_reply_enabled[5];
  GtkWidget* review_reply_texts[5];
  GtkWidget* review_auto_refund;
  GtkWidget* review_auto_refund_max_stars;

  GtkWidget* proxy_enable;
  GtkWidget* proxy_ip;
  GtkWidget* proxy_port;
  GtkWidget* proxy_login;
  GtkWidget* proxy_password;
  GtkWidget* proxy_check;

  GtkWidget* other_watermark;
  GtkWidget* other_requests_delay;
  GtkWidget* other_language;
} SettingsWidgets;

typedef struct AdminWidgets {
  GtkWidget* status_label;

  GtkWidget* org_name;
  GtkWidget* org_timezone;
  GtkWidget* org_currency;
  GtkWidget* org_tier;
  GtkWidget* retention_audit;
  GtkWidget* retention_price;
  GtkWidget* retention_competitor;
  GtkWidget* retention_order;
  GtkWidget* approval_required;
  GtkWidget* org_save_button;

  GtkWidget* team_combo;
  GtkWidget* team_create_entry;
  GtkWidget* team_create_button;
  GtkWidget* team_categories;
  GtkWidget* team_alerts;
  GtkWidget* scope_org;
  GtkWidget* scope_team;
  GtkWidget* scope_item;
  GtkWidget* scope_listing;
  GtkWidget* team_save_button;

  GtkWidget* members_list;
  GtkWidget* invites_list;
  GtkWidget* access_review_label;
  GtkWidget* access_review_note;
  GtkWidget* access_review_button;

  GtkWidget* price_change_list;
} AdminWidgets;

typedef struct AppContext {
  fpv_event_bus_t* bus;
  fpv_core_t* core;
  fpv_identity_store_t* identity;
  fpv_chat_store_t* chat_store;

  fpv_user_t* current_user;
  fpv_organization_t* current_org;
  fpv_team_t* current_team;
  fpv_role_t current_role;

  GtkWidget* window;
  GtkWidget* status_label;
  GtkWidget* start_button;
  GtkWidget* stop_button;

  GtkWidget* log_list;
  GtkWidget* chat_list;
  GtkWidget* message_list;
  GtkWidget* message_entry;
  GtkWidget* message_send_button;
  GtkWidget* message_header;
  GtkWidget* order_list;
  GtkWidget* lot_list;
  GtkWidget* lot_refresh_button;
  GtkWidget* plugin_list;
  GtkWidget* auto_response_list;
  GtkWidget* auto_delivery_list;
  GtkWidget* metrics_chats;
  GtkWidget* metrics_orders;
  GtkWidget* metrics_messages;
  GtkWidget* metrics_lots;
  GtkWidget* metrics_plugins;
  GtkWidget* settings_status;

  SettingsWidgets settings;
  AdminWidgets admin;

  GHashTable* chats;
  GHashTable* orders;
  GHashTable* lots;
  GHashTable* plugins;
  GHashTable* messages_by_chat;

  gchar* active_chat_id;
  gchar* active_chat_name;

  gchar* base_dir;
  gchar* data_dir;
  gchar* config_dir;
  gchar* logs_dir;
  gchar* plugins_dir;
  gchar* locales_dir;

  guint poll_id;
  size_t event_count;
  GPtrArray* chat_refresh_queue;
  guint chat_refresh_id;
  guint chat_refresh_index;
  guint64 chat_refresh_last_active_ms;
  guint64 chat_refresh_last_background_ms;
  gboolean chat_refresh_in_flight;
  gboolean chat_refresh_warmup;
  gboolean running;
  gboolean closing;
} AppContext;

#endif  // FPV_APP_SRC_APP_CONTEXT_H_
