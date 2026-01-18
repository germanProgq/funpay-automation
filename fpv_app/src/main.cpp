/* FunPay Vertex GTK application entry point. */

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

extern "C" {
#include "fpv_core/fpv_chat_store.h"
#include "fpv_core/fpv_core.h"
#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_identity.h"
#include "fpv_core/fpv_ini.h"
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
  GtkWidget* funpay_auto_delivery;
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
  gboolean running;
} AppContext;

static void apply_css(GtkWidget* window);
static void show_message_dialog(
    GtkWindow* parent,
    const char* title,
    const char* message);
static gchar* format_timestamp(uint64_t timestamp_ms);
static gchar* format_time_short(uint64_t timestamp_ms);
static gchar* sanitize_utf8(const char* text);
static const char* status_label(fpv_core_status_t status);
static const char* log_level_label(fpv_log_level_t level);
static const char* order_status_label(fpv_order_status_t status);
static const char* notification_severity_label(
    fpv_notification_severity_t severity);
static const char* role_label(fpv_role_t role);
static gboolean parse_ini_bool(const char* value, gboolean fallback);
static double clamp_adjustment_value(GtkAdjustment* adjustment, double value);
static gboolean on_slow_scroll(
    GtkEventControllerScroll* controller,
    double dx,
    double dy,
    gpointer user_data);
static void install_slow_scrolling(GtkWidget* scroller);
static void on_toggle_revealer(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data);
static void clear_list_box(GtkWidget* list);
static void refresh_metrics(AppContext* context);
static void refresh_chat_list(AppContext* context);
static void refresh_message_list(AppContext* context);
static void clear_chat_state(AppContext* context);
static void load_cached_chats(AppContext* context);
static void refresh_order_list(AppContext* context);
static void refresh_lot_list(AppContext* context);
static void refresh_plugin_list(AppContext* context);
static void refresh_auto_response_list(AppContext* context);
static void refresh_auto_delivery_list(AppContext* context);
static void refresh_settings_from_file(AppContext* context);
static void refresh_identity_labels(AppContext* context);
static void add_setting_row(
    GtkWidget* grid,
    int row,
    const char* label_text,
    GtkWidget* widget);
static void add_setting_row_label(
    GtkWidget* grid,
    int row,
    GtkWidget* label,
    GtkWidget* widget);
static GtkWidget* build_star_label_widget(size_t count, const char* suffix);
static void update_status(
    AppContext* context,
    fpv_core_status_t status,
    const char* detail);
static void save_settings_to_file(GtkButton* button, gpointer user_data);
static void show_setup_wizard(AppContext* context);
static void begin_auth_flow(AppContext* context);
static void apply_authenticated_context(
    AppContext* context,
    fpv_user_t* user,
    fpv_organization_t* org,
    fpv_team_t* team,
    fpv_role_t role);
static void show_login_dialog(AppContext* context);
static void show_onboarding_dialog(AppContext* context);
static void show_invite_dialog(AppContext* context);
static void show_join_dialog(AppContext* context);
static void show_link_dialog(AppContext* context);
static gboolean poll_events(gpointer user_data);
static void handle_event(AppContext* context, const fpv_event_t* event);
static void start_core(GtkButton* button, gpointer user_data);
static void stop_core(GtkButton* button, gpointer user_data);
static GtkWidget* build_main_layout(AppContext* context);
static void on_activate(GtkApplication* app, gpointer user_data);

static gchar* format_timestamp(uint64_t timestamp_ms) {
  GDateTime* dt = g_date_time_new_from_unix_local(
      (gint64)(timestamp_ms / 1000ULL));
  if (!dt) {
    return g_strdup("0000-00-00 00:00:00.000");
  }

  gchar* base = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
  g_date_time_unref(dt);
  if (!base) {
    return g_strdup("0000-00-00 00:00:00.000");
  }

  const gint ms = (gint)(timestamp_ms % 1000ULL);
  gchar* full = g_strdup_printf("%s.%03d", base, ms);
  g_free(base);
  return full;
}

static gchar* format_time_short(uint64_t timestamp_ms) {
  GDateTime* dt = g_date_time_new_from_unix_local(
      (gint64)(timestamp_ms / 1000ULL));
  if (!dt) {
    return g_strdup("--:--");
  }

  gchar* text = g_date_time_format(dt, "%H:%M");
  g_date_time_unref(dt);
  if (!text) {
    return g_strdup("--:--");
  }
  return text;
}

static gchar* sanitize_utf8(const char* text) {
  if (!text) {
    return g_strdup("");
  }
  if (g_utf8_validate(text, -1, NULL)) {
    return g_strdup(text);
  }
  return g_utf8_make_valid(text, -1);
}

static const char* status_label(fpv_core_status_t status) {
  switch (status) {
    case FPV_CORE_STOPPED:
      return "Stopped";
    case FPV_CORE_STARTING:
      return "Starting";
    case FPV_CORE_RUNNING:
      return "Running";
    case FPV_CORE_STOPPING:
      return "Stopping";
    case FPV_CORE_ERROR:
      return "Error";
    default:
      return "Unknown";
  }
}

static const char* log_level_label(fpv_log_level_t level) {
  switch (level) {
    case FPV_LOG_DEBUG:
      return "debug";
    case FPV_LOG_INFO:
      return "info";
    case FPV_LOG_WARNING:
      return "warning";
    case FPV_LOG_ERROR:
      return "error";
    default:
      return "log";
  }
}

static const char* role_label(fpv_role_t role) {
  switch (role) {
    case FPV_ROLE_OWNER:
      return "Owner";
    case FPV_ROLE_ADMIN:
      return "Admin";
    case FPV_ROLE_MANAGER:
      return "Manager";
    case FPV_ROLE_ANALYST:
      return "Analyst";
    case FPV_ROLE_VIEWER:
      return "Viewer";
    default:
      return "Unknown";
  }
}

static const char* order_status_label(fpv_order_status_t status) {
  switch (status) {
    case FPV_ORDER_PENDING:
      return "Pending";
    case FPV_ORDER_PAID:
      return "Paid";
    case FPV_ORDER_DELIVERED:
      return "Delivered";
    case FPV_ORDER_CANCELLED:
      return "Cancelled";
    case FPV_ORDER_REFUNDED:
      return "Refunded";
    default:
      return "Unknown";
  }
}

static const char* notification_severity_label(
    fpv_notification_severity_t severity) {
  switch (severity) {
    case FPV_NOTIFICATION_INFO:
      return "info";
    case FPV_NOTIFICATION_WARNING:
      return "warning";
    case FPV_NOTIFICATION_ERROR:
      return "error";
    default:
      return "notice";
  }
}

static gboolean parse_ini_bool(const char* value, gboolean fallback) {
  if (!value || !value[0]) {
    return fallback;
  }
  if (value[0] == '1' && value[1] == '\0') {
    return TRUE;
  }
  if (value[0] == '0' && value[1] == '\0') {
    return FALSE;
  }
  if (g_ascii_strcasecmp(value, "true") == 0) {
    return TRUE;
  }
  if (g_ascii_strcasecmp(value, "false") == 0) {
    return FALSE;
  }
  return fallback;
}

static double clamp_adjustment_value(GtkAdjustment* adjustment, double value) {
  if (!adjustment) {
    return value;
  }
  double lower = gtk_adjustment_get_lower(adjustment);
  double upper = gtk_adjustment_get_upper(adjustment) -
                 gtk_adjustment_get_page_size(adjustment);
  if (upper < lower) {
    upper = lower;
  }
  if (value < lower) {
    return lower;
  }
  if (value > upper) {
    return upper;
  }
  return value;
}

static gboolean on_slow_scroll(
    GtkEventControllerScroll* controller,
    double dx,
    double dy,
    gpointer user_data) {
  GtkScrolledWindow* scroller = GTK_SCROLLED_WINDOW(user_data);
  if (!scroller) {
    return FALSE;
  }

  GtkAdjustment* hadj = gtk_scrolled_window_get_hadjustment(scroller);
  GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(scroller);
  const double scale = 0.6;
  double hdelta = 0.0;
  double vdelta = 0.0;

  GdkScrollUnit unit = gtk_event_controller_scroll_get_unit(controller);
  if (unit == GDK_SCROLL_UNIT_WHEEL) {
    double hstep = hadj ? gtk_adjustment_get_step_increment(hadj) : 0.0;
    double vstep = vadj ? gtk_adjustment_get_step_increment(vadj) : 0.0;
    if (hstep <= 0.0) {
      hstep = 24.0;
    }
    if (vstep <= 0.0) {
      vstep = 24.0;
    }
    hdelta = dx * hstep * scale;
    vdelta = dy * vstep * scale;
  } else {
    hdelta = dx * scale;
    vdelta = dy * scale;
  }

  if (hadj && hdelta != 0.0) {
    double value = gtk_adjustment_get_value(hadj) + hdelta;
    gtk_adjustment_set_value(hadj, clamp_adjustment_value(hadj, value));
  }
  if (vadj && vdelta != 0.0) {
    double value = gtk_adjustment_get_value(vadj) + vdelta;
    gtk_adjustment_set_value(vadj, clamp_adjustment_value(vadj, value));
  }
  return TRUE;
}

static void install_slow_scrolling(GtkWidget* scroller) {
  if (!scroller) {
    return;
  }
  gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroller), TRUE);
  GtkEventController* controller = gtk_event_controller_scroll_new(
      GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(controller, "scroll", G_CALLBACK(on_slow_scroll), scroller);
  gtk_widget_add_controller(scroller, controller);
}

static void on_toggle_revealer(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)pspec;
  GtkSwitch* toggle = GTK_SWITCH(object);
  GtkRevealer* revealer = GTK_REVEALER(user_data);
  if (!toggle || !revealer) {
    return;
  }
  gtk_revealer_set_reveal_child(revealer, gtk_switch_get_active(toggle));
}

static void apply_css(GtkWidget* window) {
  const char* css =
      "window { background: #f7f7f5; }"
      ".page-title { font-weight: 600; font-size: 16px; }"
      ".status { font-weight: 600; font-size: 14px; }"
      ".log-row { padding: 6px 12px; }"
      ".log-time { color: #6b6b6b; font-size: 11px; }"
      ".log-level { color: #2f2f2f; font-weight: 600; }"
      ".log-message { color: #1f1f1f; }"
      ".metric { padding: 12px; background: #ffffff; border: 1px solid #e2e2e2; }"
      ".metric-value { font-weight: 600; font-size: 18px; }"
      ".muted { color: #666666; }"
      "row:selected,"
      "row:selected:focus,"
      "row:selected:focus-within {"
      "  background-color: #e6e1da;"
      "  background-image: none;"
      "}"
      "row:selected label,"
      "row:selected:focus label,"
      "row:selected:focus-within label { color: #1f1f1f; }"
      "row:selected .muted,"
      "row:selected:focus .muted,"
      "row:selected:focus-within .muted { color: #1f1f1f; }"
      "entry selection { background-color: #e6e1da; color: #1f1f1f; }"
      "label selection { background-color: #e6e1da; color: #1f1f1f; }"
      ".rating-star { color: #f6b400; font-weight: 600; }";

  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, css);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void show_message_dialog(
    GtkWindow* parent,
    const char* title,
    const char* message) {
  GtkWidget* dialog = gtk_window_new();
  if (parent) {
    gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
  }
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_title(GTK_WINDOW(dialog), title ? title : "Message");
  gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 160);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);

  GtkWidget* label = gtk_label_new(message ? message : "");
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);

  GtkWidget* button = gtk_button_new_with_label("OK");
  gtk_widget_set_halign(button, GTK_ALIGN_END);
  g_signal_connect_swapped(button, "clicked", G_CALLBACK(gtk_window_close), dialog);

  gtk_box_append(GTK_BOX(box), label);
  gtk_box_append(GTK_BOX(box), button);
  gtk_window_set_child(GTK_WINDOW(dialog), box);
  gtk_window_present(GTK_WINDOW(dialog));
}

typedef struct DialogTask {
  AppContext* context;
  gchar* title;
  gchar* message;
} DialogTask;

static gboolean dialog_task_run(gpointer data) {
  DialogTask* task = (DialogTask*)data;
  if (task && task->context) {
    show_message_dialog(
        GTK_WINDOW(task->context->window),
        task->title,
        task->message);
  }
  if (task) {
    g_free(task->title);
    g_free(task->message);
    g_free(task);
  }
  return G_SOURCE_REMOVE;
}

static void schedule_dialog(
    AppContext* context,
    const char* title,
    const char* message) {
  if (!context) {
    return;
  }
  DialogTask* task = (DialogTask*)g_new0(DialogTask, 1);
  task->context = context;
  task->title = g_strdup(title ? title : "Message");
  task->message = g_strdup(message ? message : "");
  g_main_context_invoke(NULL, dialog_task_run, task);
}

static void clear_list_box(GtkWidget* list) {
  GtkWidget* child = gtk_widget_get_first_child(list);
  while (child) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list), child);
    child = next;
  }
}

static void append_log_row(
    AppContext* context,
    const char* level,
    const char* message,
    uint64_t timestamp_ms) {
  if (!context || !context->log_list) {
    return;
  }
  gchar* timestamp = format_timestamp(timestamp_ms);
  gchar* safe_message = sanitize_utf8(message);
  GtkWidget* row = gtk_list_box_row_new();
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget* time_label = gtk_label_new(timestamp);
  GtkWidget* level_label = gtk_label_new(level);
  GtkWidget* message_label = gtk_label_new(safe_message);

  gtk_widget_add_css_class(row, "log-row");
  gtk_widget_add_css_class(time_label, "log-time");
  gtk_widget_add_css_class(level_label, "log-level");
  gtk_widget_add_css_class(message_label, "log-message");

  gtk_label_set_xalign(GTK_LABEL(time_label), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(level_label), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(message_label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(message_label), TRUE);
  gtk_widget_set_hexpand(message_label, TRUE);

  gtk_box_append(GTK_BOX(box), time_label);
  gtk_box_append(GTK_BOX(box), level_label);
  gtk_box_append(GTK_BOX(box), message_label);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
  gtk_list_box_append(GTK_LIST_BOX(context->log_list), row);

  g_free(timestamp);
  g_free(safe_message);

  context->event_count++;
  while (context->event_count > 500) {
    GtkWidget* child = gtk_widget_get_first_child(context->log_list);
    if (!child) {
      context->event_count = 0;
      break;
    }
    gtk_list_box_remove(GTK_LIST_BOX(context->log_list), child);
    context->event_count--;
  }
}

static void refresh_metrics(AppContext* context) {
  if (!context) {
    return;
  }

  guint chat_count = context->chats ? g_hash_table_size(context->chats) : 0;
  guint order_count = context->orders ? g_hash_table_size(context->orders) : 0;
  guint lot_count = context->lots ? g_hash_table_size(context->lots) : 0;
  guint plugin_count = context->plugins ? g_hash_table_size(context->plugins) : 0;
  guint message_count = 0;

  if (context->messages_by_chat) {
    GHashTableIter iter;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, context->messages_by_chat);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
      GPtrArray* list = (GPtrArray*)value;
      if (list) {
        message_count += (guint)list->len;
      }
    }
  }

  if (context->metrics_chats) {
    gchar* text = g_strdup_printf("%u", chat_count);
    gtk_label_set_text(GTK_LABEL(context->metrics_chats), text);
    g_free(text);
  }
  if (context->metrics_orders) {
    gchar* text = g_strdup_printf("%u", order_count);
    gtk_label_set_text(GTK_LABEL(context->metrics_orders), text);
    g_free(text);
  }
  if (context->metrics_messages) {
    gchar* text = g_strdup_printf("%u", message_count);
    gtk_label_set_text(GTK_LABEL(context->metrics_messages), text);
    g_free(text);
  }
  if (context->metrics_lots) {
    gchar* text = g_strdup_printf("%u", lot_count);
    gtk_label_set_text(GTK_LABEL(context->metrics_lots), text);
    g_free(text);
  }
  if (context->metrics_plugins) {
    gchar* text = g_strdup_printf("%u", plugin_count);
    gtk_label_set_text(GTK_LABEL(context->metrics_plugins), text);
    g_free(text);
  }
}

static gint compare_chat_items(gconstpointer a, gconstpointer b) {
  const fpv_chat_t* left = *(const fpv_chat_t* const*)a;
  const fpv_chat_t* right = *(const fpv_chat_t* const*)b;
  if (!left && !right) {
    return 0;
  }
  if (!left) {
    return 1;
  }
  if (!right) {
    return -1;
  }
  if (left->unread_count != right->unread_count) {
    return left->unread_count > right->unread_count ? -1 : 1;
  }
  return g_strcmp0(left->title, right->title);
}

static void refresh_chat_list(AppContext* context) {
  if (!context || !context->chat_list || !context->chats) {
    return;
  }

  clear_list_box(context->chat_list);

  GPtrArray* chats = g_ptr_array_new();
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->chats);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    if (value) {
      g_ptr_array_add(chats, value);
    }
  }
  g_ptr_array_sort(chats, compare_chat_items);

  for (guint i = 0; i < chats->len; i++) {
    fpv_chat_t* chat = (fpv_chat_t*)g_ptr_array_index(chats, i);
    if (!chat) {
      continue;
    }
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar* title_text = sanitize_utf8(chat->title ? chat->title : "Chat");
    gchar* subtitle_text = sanitize_utf8(
        chat->last_message_text ? chat->last_message_text : "");
    GtkWidget* title = gtk_label_new(title_text);
    GtkWidget* subtitle = gtk_label_new(subtitle_text);

    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_widget_add_css_class(subtitle, "muted");

    if (chat->unread_count > 0) {
      gchar* counter = g_strdup_printf("Unread: %u", chat->unread_count);
      GtkWidget* badge = gtk_label_new(counter);
      gtk_label_set_xalign(GTK_LABEL(badge), 1.0f);
      gtk_widget_add_css_class(badge, "muted");
      GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_widget_set_hexpand(header, TRUE);
      gtk_box_append(GTK_BOX(header), title);
      gtk_box_append(GTK_BOX(header), badge);
      gtk_box_append(GTK_BOX(box), header);
      g_free(counter);
    } else {
      gtk_box_append(GTK_BOX(box), title);
    }

    gtk_box_append(GTK_BOX(box), subtitle);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->chat_list), row);

    g_free(title_text);
    g_free(subtitle_text);

    if (chat->id && chat->id[0]) {
      g_object_set_data_full(
          G_OBJECT(row),
          "chat-id",
          g_strdup(chat->id),
          g_free);
    }
    if (chat->title) {
      g_object_set_data_full(
          G_OBJECT(row),
          "chat-name",
          g_strdup(chat->title),
          g_free);
    }
  }

  g_ptr_array_free(chats, TRUE);
}

static void refresh_message_list(AppContext* context) {
  if (!context || !context->message_list) {
    return;
  }

  clear_list_box(context->message_list);

  if (!context->active_chat_id || !context->messages_by_chat) {
    return;
  }

  GPtrArray* list = (GPtrArray*)g_hash_table_lookup(
      context->messages_by_chat,
      context->active_chat_id);
  if (!list) {
    return;
  }

  for (guint i = 0; i < list->len; i++) {
    fpv_message_t* message = (fpv_message_t*)g_ptr_array_index(list, i);
    if (!message) {
      continue;
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gchar* time_text = format_time_short(message->created_at_ms);
    gchar* sender = sanitize_utf8(
        message->sender_name ? message->sender_name : "Unknown");
    gchar* header = g_strdup_printf("%s · %s", sender, time_text);
    gchar* text = sanitize_utf8(message->text ? message->text : "");
    GtkWidget* header_label = gtk_label_new(header);
    GtkWidget* text_label = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(header_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(text_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(text_label), TRUE);
    gtk_widget_add_css_class(header_label, "muted");

    gtk_box_append(GTK_BOX(box), header_label);
    gtk_box_append(GTK_BOX(box), text_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->message_list), row);

    g_free(time_text);
    g_free(sender);
    g_free(header);
    g_free(text);
  }
}

static void clear_chat_state(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->chats) {
    g_hash_table_remove_all(context->chats);
  }
  if (context->messages_by_chat) {
    g_hash_table_remove_all(context->messages_by_chat);
  }
  g_free(context->active_chat_id);
  g_free(context->active_chat_name);
  context->active_chat_id = NULL;
  context->active_chat_name = NULL;
  if (context->message_header) {
    gtk_label_set_text(GTK_LABEL(context->message_header), "Messages");
  }
  if (context->message_send_button) {
    gtk_widget_set_sensitive(
        context->message_send_button,
        context->running && context->active_chat_id != NULL);
  }
  refresh_chat_list(context);
  refresh_message_list(context);
  refresh_metrics(context);
}

static void load_cached_chats(AppContext* context) {
  if (!context || !context->chat_store || !context->current_org ||
      !context->current_org->id || !context->chats) {
    return;
  }

  fpv_chat_t** chats = NULL;
  size_t count = 0;
  fpv_result_t result = fpv_chat_store_load_chats(
      context->chat_store,
      context->current_org->id,
      &chats,
      &count);
  if (result != FPV_OK || !chats || count == 0) {
    if (chats) {
      for (size_t i = 0; i < count; i++) {
        fpv_chat_destroy(chats[i]);
      }
      free(chats);
    }
    return;
  }

  for (size_t i = 0; i < count; i++) {
    fpv_chat_t* chat = chats[i];
    if (!chat || !chat->id || !chat->id[0]) {
      fpv_chat_destroy(chat);
      continue;
    }
    g_hash_table_replace(context->chats, g_strdup(chat->id), chat);
  }
  free(chats);
  refresh_chat_list(context);
  refresh_metrics(context);
}

static gint compare_order_items(gconstpointer a, gconstpointer b) {
  const fpv_order_t* left = *(const fpv_order_t* const*)a;
  const fpv_order_t* right = *(const fpv_order_t* const*)b;
  if (!left && !right) {
    return 0;
  }
  if (!left) {
    return 1;
  }
  if (!right) {
    return -1;
  }
  if (left->updated_at_ms != right->updated_at_ms) {
    return left->updated_at_ms > right->updated_at_ms ? -1 : 1;
  }
  return g_strcmp0(left->id, right->id);
}

static void refresh_order_list(AppContext* context) {
  if (!context || !context->order_list || !context->orders) {
    return;
  }

  clear_list_box(context->order_list);

  GPtrArray* orders = g_ptr_array_new();
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->orders);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    if (value) {
      g_ptr_array_add(orders, value);
    }
  }
  g_ptr_array_sort(orders, compare_order_items);

  for (guint i = 0; i < orders->len; i++) {
    fpv_order_t* order = (fpv_order_t*)g_ptr_array_index(orders, i);
    if (!order) {
      continue;
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gchar* title_text = sanitize_utf8(order->title ? order->title : "Order");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    const char* currency = order->currency ? order->currency : "";
    gchar* buyer = sanitize_utf8(
        order->buyer_username ? order->buyer_username : "");
    gchar* details = g_strdup_printf(
        "#%s · %s · %.2f %s · Buyer: %s",
        order->id ? order->id : "",
        order_status_label(order->status),
        order->amount,
        currency,
        buyer);
    GtkWidget* detail_label = gtk_label_new(details);
    gtk_label_set_xalign(GTK_LABEL(detail_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(detail_label), TRUE);
    gtk_widget_add_css_class(detail_label, "muted");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), detail_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->order_list), row);

    g_free(details);
    g_free(title_text);
    g_free(buyer);
  }

  g_ptr_array_free(orders, TRUE);
}

static gint compare_lot_items(gconstpointer a, gconstpointer b) {
  const fpv_lot_t* left = *(const fpv_lot_t* const*)a;
  const fpv_lot_t* right = *(const fpv_lot_t* const*)b;
  if (!left && !right) {
    return 0;
  }
  if (!left) {
    return 1;
  }
  if (!right) {
    return -1;
  }
  return g_strcmp0(left->title, right->title);
}

typedef struct LotToggleTask {
  AppContext* context;
  uint64_t lot_id;
  gboolean active;
  GtkWidget* toggle;
  gboolean success;
} LotToggleTask;

static gboolean lot_toggle_complete(gpointer data) {
  LotToggleTask* task = (LotToggleTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  GtkWidget* toggle = task->toggle;
  if (toggle) {
    if (!task->success) {
      gtk_switch_set_active(GTK_SWITCH(toggle), !task->active);
    }
    g_object_set_data(G_OBJECT(toggle), "busy", NULL);
  }
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer lot_toggle_thread(gpointer data) {
  LotToggleTask* task = (LotToggleTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task && task->context) {
      task->success = FALSE;
      g_main_context_invoke(NULL, lot_toggle_complete, task);
    } else if (task) {
      g_free(task);
    }
    return NULL;
  }
  fpv_result_t result =
      fpv_core_set_lot_active(task->context->core, task->lot_id, task->active);
  task->success = result == FPV_OK;
  if (result != FPV_OK) {
    schedule_dialog(task->context, "Lots", "Failed to update lot state.");
  }
  g_main_context_invoke(NULL, lot_toggle_complete, task);
  return NULL;
}

static void on_lot_toggle_changed(
    GObject* object,
    GParamSpec* spec,
    gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkWidget* toggle = GTK_WIDGET(object);
  if (!context || !toggle) {
    return;
  }
  gpointer busy = g_object_get_data(G_OBJECT(toggle), "busy");
  if (busy) {
    return;
  }
  const gchar* id_text = (const gchar*)g_object_get_data(
      G_OBJECT(toggle),
      "lot-id");
  if (!id_text || !id_text[0]) {
    return;
  }
  guint64 lot_id = g_ascii_strtoull(id_text, NULL, 10);
  if (lot_id == 0) {
    return;
  }
  gboolean active = gtk_switch_get_active(GTK_SWITCH(toggle));

  LotToggleTask* task = (LotToggleTask*)g_new0(LotToggleTask, 1);
  task->context = context;
  task->lot_id = lot_id;
  task->active = active;
  task->toggle = toggle;

  g_object_set_data(G_OBJECT(toggle), "busy", task);
  g_thread_new("fpv-lot-toggle", lot_toggle_thread, task);
}

static void refresh_lot_list(AppContext* context) {
  if (!context || !context->lot_list || !context->lots) {
    return;
  }

  clear_list_box(context->lot_list);

  GPtrArray* lots = g_ptr_array_new();
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->lots);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    if (value) {
      g_ptr_array_add(lots, value);
    }
  }
  g_ptr_array_sort(lots, compare_lot_items);

  for (guint i = 0; i < lots->len; i++) {
    fpv_lot_t* lot = (fpv_lot_t*)g_ptr_array_index(lots, i);
    if (!lot) {
      continue;
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gchar* title_text = sanitize_utf8(lot->title ? lot->title : "Lot");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    gchar* meta = g_strdup_printf(
        "ID: %s · Stock: %u",
        lot->id ? lot->id : "",
        lot->stock);
    GtkWidget* meta_label = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_widget_add_css_class(meta_label, "muted");

    GtkWidget* toggle = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(toggle), lot->active ? TRUE : FALSE);
    if (lot->id) {
      g_object_set_data_full(
          G_OBJECT(toggle),
          "lot-id",
          g_strdup(lot->id),
          g_free);
    }
    g_signal_connect(toggle, "notify::active", G_CALLBACK(on_lot_toggle_changed), context);

    gtk_box_append(GTK_BOX(info), title);
    gtk_box_append(GTK_BOX(info), meta_label);

    gtk_widget_set_hexpand(info, TRUE);
    gtk_box_append(GTK_BOX(box), info);
    gtk_box_append(GTK_BOX(box), toggle);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->lot_list), row);

    g_free(meta);
    g_free(title_text);
  }

  g_ptr_array_free(lots, TRUE);
}

static gint compare_plugin_items(gconstpointer a, gconstpointer b) {
  const fpv_plugin_t* left = *(const fpv_plugin_t* const*)a;
  const fpv_plugin_t* right = *(const fpv_plugin_t* const*)b;
  if (!left && !right) {
    return 0;
  }
  if (!left) {
    return 1;
  }
  if (!right) {
    return -1;
  }
  return g_strcmp0(left->name, right->name);
}

static void refresh_plugin_list(AppContext* context) {
  if (!context || !context->plugin_list || !context->plugins) {
    return;
  }

  clear_list_box(context->plugin_list);

  GPtrArray* plugins = g_ptr_array_new();
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->plugins);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    if (value) {
      g_ptr_array_add(plugins, value);
    }
  }
  g_ptr_array_sort(plugins, compare_plugin_items);

  for (guint i = 0; i < plugins->len; i++) {
    fpv_plugin_t* plugin = (fpv_plugin_t*)g_ptr_array_index(plugins, i);
    if (!plugin) {
      continue;
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gchar* title_text = sanitize_utf8(
        plugin->name ? plugin->name : "Plugin");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    gchar* meta = g_strdup_printf(
        "Version: %s · %s",
        plugin->version ? plugin->version : "",
        plugin->enabled ? "Enabled" : "Disabled");
    GtkWidget* meta_label = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_widget_add_css_class(meta_label, "muted");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), meta_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->plugin_list), row);

    g_free(meta);
    g_free(title_text);
  }

  g_ptr_array_free(plugins, TRUE);
}

static gboolean dir_has_marker(const gchar* dir, const gchar* marker) {
  if (!dir || !marker || !marker[0]) {
    return FALSE;
  }
  gchar* path = g_build_filename(dir, marker, NULL);
  if (!path) {
    return FALSE;
  }
  gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR);
  g_free(path);
  return exists;
}

static gchar* find_resource_dir(
    const gchar* subdir,
    const gchar* base_dir,
    const gchar* marker) {
  if (!subdir || !subdir[0]) {
    return NULL;
  }

  gchar* cwd = g_get_current_dir();
  if (cwd) {
    gchar* path = g_build_filename(cwd, subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      g_free(cwd);
      return path;
    }
    g_free(path);
    g_free(cwd);
  }

  if (base_dir && base_dir[0]) {
    gchar* path = g_build_filename(base_dir, subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      return path;
    }
    g_free(path);
  }

  const gchar* const* system_dirs = g_get_system_data_dirs();
  for (size_t i = 0; system_dirs && system_dirs[i]; i++) {
    gchar* path = g_build_filename(system_dirs[i], "funpay_vertex", subdir, NULL);
    if (path &&
        g_file_test(path, G_FILE_TEST_IS_DIR) &&
        (!marker || dir_has_marker(path, marker))) {
      return path;
    }
    g_free(path);
  }

  return NULL;
}

static gboolean has_config_files(const gchar* config_dir) {
  if (!config_dir || !config_dir[0]) {
    return FALSE;
  }
  gchar* main_cfg = g_build_filename(config_dir, "_main.cfg", NULL);
  gboolean exists = g_file_test(main_cfg, G_FILE_TEST_IS_REGULAR);
  g_free(main_cfg);
  return exists;
}

static gboolean copy_file_if_missing(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  if (g_file_test(dest, G_FILE_TEST_IS_REGULAR)) {
    return TRUE;
  }
  GFile* src_file = g_file_new_for_path(src);
  GFile* dest_file = g_file_new_for_path(dest);
  gboolean ok = g_file_copy(
      src_file,
      dest_file,
      G_FILE_COPY_OVERWRITE,
      NULL,
      NULL,
      NULL,
      error);
  g_object_unref(src_file);
  g_object_unref(dest_file);
  return ok;
}

static gboolean copy_file_overwrite(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  GFile* src_file = g_file_new_for_path(src);
  GFile* dest_file = g_file_new_for_path(dest);
  gboolean ok = g_file_copy(
      src_file,
      dest_file,
      G_FILE_COPY_OVERWRITE,
      NULL,
      NULL,
      NULL,
      error);
  g_object_unref(src_file);
  g_object_unref(dest_file);
  return ok;
}

static gboolean copy_directory_recursive(
    const gchar* src,
    const gchar* dest,
    GError** error) {
  if (!src || !dest) {
    return FALSE;
  }
  if (!g_file_test(src, G_FILE_TEST_IS_DIR)) {
    return FALSE;
  }
  if (g_mkdir_with_parents(dest, 0755) != 0) {
    return FALSE;
  }

  GDir* dir = g_dir_open(src, 0, error);
  if (!dir) {
    return FALSE;
  }

  const gchar* name = NULL;
  while ((name = g_dir_read_name(dir)) != NULL) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
      continue;
    }
    gchar* src_path = g_build_filename(src, name, NULL);
    gchar* dest_path = g_build_filename(dest, name, NULL);
    if (g_file_test(src_path, G_FILE_TEST_IS_DIR)) {
      if (!copy_directory_recursive(src_path, dest_path, error)) {
        g_free(src_path);
        g_free(dest_path);
        g_dir_close(dir);
        return FALSE;
      }
    } else {
      GFile* src_file = g_file_new_for_path(src_path);
      GFile* dest_file = g_file_new_for_path(dest_path);
      gboolean ok = g_file_copy(
          src_file,
          dest_file,
          G_FILE_COPY_OVERWRITE,
          NULL,
          NULL,
          NULL,
          error);
      g_object_unref(src_file);
      g_object_unref(dest_file);
      if (!ok) {
        g_free(src_path);
        g_free(dest_path);
        g_dir_close(dir);
        return FALSE;
      }
    }
    g_free(src_path);
    g_free(dest_path);
  }

  g_dir_close(dir);
  return TRUE;
}

static void sync_main_config(const gchar* config_dir) {
  if (!config_dir || !config_dir[0]) {
    return;
  }

  const gchar* filenames[] = {
      "_main.cfg",
      "auto_response.cfg",
      "auto_delivery.cfg"
  };
  const size_t file_count = sizeof(filenames) / sizeof(filenames[0]);

  gchar* base_dir = g_path_get_dirname(config_dir);
  gchar* template_dir = find_resource_dir("configs", base_dir, "_main.cfg");
  g_free(base_dir);
  if (!template_dir || !g_file_test(template_dir, G_FILE_TEST_IS_DIR)) {
    g_free(template_dir);
    return;
  }

#if defined(__APPLE__)
  gchar* mac_dir = NULL;
  const gchar* home_dir = g_get_home_dir();
  if (home_dir && home_dir[0]) {
    mac_dir = g_build_filename(
        home_dir,
        "Library",
        "Application Support",
        "funpay_vertex",
        "configs",
        NULL);
    g_mkdir_with_parents(mac_dir, 0755);
  }
#endif

  for (size_t i = 0; i < file_count; i++) {
    gchar* src = g_build_filename(template_dir, filenames[i], NULL);
    gchar* dest = g_build_filename(config_dir, filenames[i], NULL);
    gboolean has_src = g_file_test(src, G_FILE_TEST_IS_REGULAR);
    if (has_src) {
      copy_file_if_missing(src, dest, NULL);
    }
#if defined(__APPLE__)
    if (mac_dir && has_src) {
      gchar* mac_dest = g_build_filename(mac_dir, filenames[i], NULL);
      copy_file_if_missing(src, mac_dest, NULL);
      g_free(mac_dest);
    }
#endif
    g_free(src);
    g_free(dest);
  }

  g_free(template_dir);
#if defined(__APPLE__)
  g_free(mac_dir);
#endif
}

typedef struct DatabaseConfig {
  gchar* url;
  gboolean docker;
  gchar* docker_image;
  gchar* docker_name;
  gchar* docker_volume;
} DatabaseConfig;

typedef struct PostgresUrlParts {
  gchar* user;
  gchar* password;
  gchar* host;
  gchar* database;
  guint port;
} PostgresUrlParts;

static void database_config_clear(DatabaseConfig* config) {
  if (!config) {
    return;
  }
  g_free(config->url);
  g_free(config->docker_image);
  g_free(config->docker_name);
  g_free(config->docker_volume);
  memset(config, 0, sizeof(*config));
}

static void postgres_url_parts_clear(PostgresUrlParts* parts) {
  if (!parts) {
    return;
  }
  g_free(parts->user);
  g_free(parts->password);
  g_free(parts->host);
  g_free(parts->database);
  memset(parts, 0, sizeof(*parts));
}

static gboolean parse_postgres_url(const char* url, PostgresUrlParts* parts) {
  if (!url || !parts) {
    return FALSE;
  }
  memset(parts, 0, sizeof(*parts));
  GError* error = NULL;
  GUri* uri = g_uri_parse(url, G_URI_FLAGS_NONE, &error);
  if (!uri) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  const char* scheme = g_uri_get_scheme(uri);
  if (!scheme ||
      (g_ascii_strcasecmp(scheme, "postgres") != 0 &&
       g_ascii_strcasecmp(scheme, "postgresql") != 0)) {
    g_uri_unref(uri);
    return FALSE;
  }
  const char* host = g_uri_get_host(uri);
  const char* path = g_uri_get_path(uri);
  if (!host || !host[0] || !path || !path[0] || strcmp(path, "/") == 0) {
    g_uri_unref(uri);
    return FALSE;
  }
  gchar* database = g_uri_unescape_string(path[0] == '/' ? path + 1 : path, NULL);
  if (!database || !database[0]) {
    g_free(database);
    g_uri_unref(uri);
    return FALSE;
  }
  parts->host = g_strdup(host);
  parts->database = database;
  gint port = g_uri_get_port(uri);
  parts->port = port > 0 ? (guint)port : 5432;

  const char* userinfo = g_uri_get_userinfo(uri);
  if (userinfo && userinfo[0]) {
    gchar* decoded = g_uri_unescape_string(userinfo, NULL);
    const char* info = decoded ? decoded : userinfo;
    const char* colon = strchr(info, ':');
    if (colon) {
      parts->user = g_strndup(info, (gsize)(colon - info));
      parts->password = g_strdup(colon + 1);
    } else {
      parts->user = g_strdup(info);
    }
    g_free(decoded);
  }

  g_uri_unref(uri);
  if (!parts->host || !parts->database) {
    postgres_url_parts_clear(parts);
    return FALSE;
  }
  return TRUE;
}

static gboolean is_local_host(const char* host) {
  if (!host || !host[0]) {
    return FALSE;
  }
  return g_strcmp0(host, "localhost") == 0 ||
         g_strcmp0(host, "127.0.0.1") == 0 ||
         g_strcmp0(host, "::1") == 0;
}

static gboolean spawn_and_capture(
    char* const argv[],
    gchar** out_stdout,
    gchar** out_stderr) {
  if (out_stdout) {
    *out_stdout = NULL;
  }
  if (out_stderr) {
    *out_stderr = NULL;
  }
  GError* error = NULL;
  int status = 0;
  gboolean ok = g_spawn_sync(
      NULL,
      const_cast<gchar**>(argv),
      NULL,
      G_SPAWN_SEARCH_PATH,
      NULL,
      NULL,
      out_stdout,
      out_stderr,
      &status,
      &error);
  if (!ok) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  if (!g_spawn_check_wait_status(status, &error)) {
    if (error) {
      g_error_free(error);
    }
    return FALSE;
  }
  return TRUE;
}

static gboolean stdout_has_line(const gchar* output, const char* name) {
  if (!output || !name || !name[0]) {
    return FALSE;
  }
  gchar** lines = g_strsplit(output, "\n", -1);
  gboolean found = FALSE;
  for (size_t i = 0; lines && lines[i]; i++) {
    if (g_strcmp0(lines[i], name) == 0) {
      found = TRUE;
      break;
    }
  }
  g_strfreev(lines);
  return found;
}

static gboolean docker_container_present(const char* name, gboolean running_only) {
  if (!name || !name[0]) {
    return FALSE;
  }
  char filter[256];
  snprintf(filter, sizeof(filter), "name=^%s$", name);
  gchar* stdout_text = NULL;
  gchar* stderr_text = NULL;
  gboolean ok = FALSE;
  if (running_only) {
    char* const argv[] = {
        (char*)"docker",
        (char*)"ps",
        (char*)"--filter",
        filter,
        (char*)"--format",
        (char*)"{{.Names}}",
        NULL};
    ok = spawn_and_capture(argv, &stdout_text, &stderr_text);
  } else {
    char* const argv[] = {
        (char*)"docker",
        (char*)"ps",
        (char*)"-a",
        (char*)"--filter",
        filter,
        (char*)"--format",
        (char*)"{{.Names}}",
        NULL};
    ok = spawn_and_capture(argv, &stdout_text, &stderr_text);
  }
  gboolean present = ok && stdout_has_line(stdout_text, name);
  g_free(stdout_text);
  g_free(stderr_text);
  return present;
}

static gboolean docker_start_container(const char* name) {
  char* const argv[] = {(char*)"docker", (char*)"start", (char*)name, NULL};
  return spawn_and_capture(argv, NULL, NULL);
}

static gboolean docker_run_postgres(
    const DatabaseConfig* config,
    const PostgresUrlParts* parts) {
  const char* name = config->docker_name ? config->docker_name : "fpv-postgres";
  const char* image = config->docker_image ? config->docker_image : "postgres:16";
  const char* volume =
      config->docker_volume ? config->docker_volume : "fpv-postgres-data";

  char port_arg[32];
  snprintf(port_arg, sizeof(port_arg), "%u:5432", parts->port);

  char user_arg[128];
  char pass_arg[128];
  char db_arg[128];
  snprintf(user_arg, sizeof(user_arg), "POSTGRES_USER=%s", parts->user);
  snprintf(pass_arg, sizeof(pass_arg), "POSTGRES_PASSWORD=%s", parts->password);
  snprintf(db_arg, sizeof(db_arg), "POSTGRES_DB=%s", parts->database);

  char volume_arg[256];
  snprintf(volume_arg, sizeof(volume_arg), "%s:/var/lib/postgresql/data", volume);

  char* const argv[] = {
      (char*)"docker",
      (char*)"run",
      (char*)"-d",
      (char*)"--name",
      (char*)name,
      (char*)"--restart",
      (char*)"unless-stopped",
      (char*)"-e",
      user_arg,
      (char*)"-e",
      pass_arg,
      (char*)"-e",
      db_arg,
      (char*)"-p",
      port_arg,
      (char*)"-v",
      volume_arg,
      (char*)image,
      NULL};

  return spawn_and_capture(argv, NULL, NULL);
}

static gboolean docker_wait_postgres_ready(
    const char* name,
    const char* user,
    const char* database) {
  for (int attempt = 0; attempt < 60; attempt++) {
    char* const argv[] = {
        (char*)"docker",
        (char*)"exec",
        (char*)name,
        (char*)"pg_isready",
        (char*)"-U",
        (char*)user,
        (char*)"-d",
        (char*)database,
        NULL};
    if (spawn_and_capture(argv, NULL, NULL)) {
      return TRUE;
    }
    g_usleep(500000);
  }
  return FALSE;
}

static void configure_database_from_config(
    const gchar* config_dir,
    const gchar* base_dir) {
  gchar* config_path = NULL;
  if (config_dir && config_dir[0]) {
    config_path = g_build_filename(config_dir, "_main.cfg", NULL);
    if (!g_file_test(config_path, G_FILE_TEST_IS_REGULAR)) {
      g_free(config_path);
      config_path = NULL;
    }
  }
  if (!config_path) {
    gchar* template_dir = find_resource_dir("configs", base_dir, "_main.cfg");
    if (template_dir && template_dir[0]) {
      config_path = g_build_filename(template_dir, "_main.cfg", NULL);
    }
    g_free(template_dir);
  }
  if (!config_path) {
    return;
  }

  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(config_path, &error);
  g_free(config_path);
  if (!ini) {
    return;
  }

  DatabaseConfig config;
  memset(&config, 0, sizeof(config));
  const char* url = fpv_ini_get(ini, "Database", "url");
  const char* docker = fpv_ini_get(ini, "Database", "docker");
  const char* docker_image = fpv_ini_get(ini, "Database", "dockerImage");
  const char* docker_name = fpv_ini_get(ini, "Database", "dockerName");
  const char* docker_volume = fpv_ini_get(ini, "Database", "dockerVolume");

  if (url && url[0]) {
    config.url = g_strdup(url);
  }
  config.docker = parse_ini_bool(docker, FALSE);
  if (docker_image && docker_image[0]) {
    config.docker_image = g_strdup(docker_image);
  }
  if (docker_name && docker_name[0]) {
    config.docker_name = g_strdup(docker_name);
  }
  if (docker_volume && docker_volume[0]) {
    config.docker_volume = g_strdup(docker_volume);
  }

  if (config.url && config.url[0]) {
    g_setenv("FPV_DB_URL", config.url, TRUE);
  }

  if (config.docker && config.url && config.url[0]) {
    PostgresUrlParts parts;
    if (parse_postgres_url(config.url, &parts) &&
        parts.user && parts.user[0] &&
        parts.password && parts.password[0] &&
        parts.database && parts.database[0] &&
        is_local_host(parts.host)) {
      if (g_find_program_in_path("docker")) {
        const char* name =
            config.docker_name ? config.docker_name : "fpv-postgres";
        gboolean exists = docker_container_present(name, FALSE);
        gboolean running = docker_container_present(name, TRUE);
        if (!exists) {
          docker_run_postgres(&config, &parts);
        } else if (!running) {
          docker_start_container(name);
        }
        docker_wait_postgres_ready(name, parts.user, parts.database);
      }
    }
    postgres_url_parts_clear(&parts);
  }

  database_config_clear(&config);
  fpv_ini_destroy(ini);
}

static gchar* resolve_config_dir(const gchar* selection) {
  if (!selection) {
    return NULL;
  }

  gchar* direct = g_build_filename(selection, "_main.cfg", NULL);
  if (g_file_test(direct, G_FILE_TEST_IS_REGULAR)) {
    g_free(direct);
    return g_strdup(selection);
  }
  g_free(direct);

  gchar* nested = g_build_filename(selection, "configs", "_main.cfg", NULL);
  if (g_file_test(nested, G_FILE_TEST_IS_REGULAR)) {
    gchar* config_dir = g_build_filename(selection, "configs", NULL);
    g_free(nested);
    return config_dir;
  }
  g_free(nested);

  return NULL;
}

static gboolean import_from_path(
    AppContext* context,
    const gchar* selection,
    gchar** out_error) {
  if (!context || !selection) {
    return FALSE;
  }

  gchar* config_src = resolve_config_dir(selection);
  if (!config_src) {
    if (out_error) {
      *out_error = g_strdup("No configs found in the selected folder.");
    }
    return FALSE;
  }

  const gchar* filenames[] = {
      "_main.cfg",
      "auto_response.cfg",
      "auto_delivery.cfg"
  };
  const size_t file_count = sizeof(filenames) / sizeof(filenames[0]);

  for (size_t i = 0; i < file_count; i++) {
    gchar* src = g_build_filename(config_src, filenames[i], NULL);
    gchar* dest = g_build_filename(context->config_dir, filenames[i], NULL);
    if (g_file_test(src, G_FILE_TEST_IS_REGULAR)) {
      if (!copy_file_overwrite(src, dest, NULL)) {
        if (out_error) {
          *out_error = g_strdup("Failed to copy config files.");
        }
        g_free(src);
        g_free(dest);
        g_free(config_src);
        return FALSE;
      }
    }
    g_free(src);
    g_free(dest);
  }

  gchar* root = NULL;
  if (g_strcmp0(config_src, selection) == 0) {
    root = g_strdup(selection);
  } else if (g_str_has_suffix(config_src, G_DIR_SEPARATOR_S "configs")) {
    root = g_path_get_dirname(config_src);
  } else {
    root = g_strdup(selection);
  }

  gchar* storage_src = g_build_filename(root, "storage", NULL);
  if (g_file_test(storage_src, G_FILE_TEST_IS_DIR)) {
    if (!copy_directory_recursive(storage_src, context->data_dir, NULL)) {
      if (out_error) {
        *out_error = g_strdup("Failed to import storage data.");
      }
      g_free(storage_src);
      g_free(root);
      g_free(config_src);
      return FALSE;
    }
  }

  g_free(storage_src);
  g_free(root);
  g_free(config_src);
  sync_main_config(context->config_dir);
  return TRUE;
}

typedef struct SetupWizard {
  AppContext* context;
  GtkWidget* window;
} SetupWizard;

static void finish_setup(AppContext* context) {
  if (!context) {
    return;
  }
  refresh_settings_from_file(context);
  begin_auth_flow(context);
}

static void on_setup_create(GtkButton* button, gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    return;
  }
  sync_main_config(wizard->context->config_dir);
  if (!has_config_files(wizard->context->config_dir)) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Default configs not found in the app directory.");
    return;
  }
  gtk_window_close(GTK_WINDOW(wizard->window));
  finish_setup(wizard->context);
  g_free(wizard);
}

static void on_setup_import_finish(
    GObject* source,
    GAsyncResult* result,
    gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
  if (!wizard || !wizard->context) {
    g_object_unref(dialog);
    return;
  }

  GError* error = NULL;
  GFile* file = gtk_file_dialog_select_folder_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      show_message_dialog(
          GTK_WINDOW(wizard->window),
          "Setup",
          error->message ? error->message : "Import failed.");
    }
    g_clear_error(&error);
    g_object_unref(dialog);
    return;
  }

  gchar* selection = g_file_get_path(file);
  g_object_unref(file);
  if (!selection) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        "Selected folder is not available.");
    g_object_unref(dialog);
    return;
  }

  gchar* import_error = NULL;
  if (!import_from_path(wizard->context, selection, &import_error)) {
    show_message_dialog(
        GTK_WINDOW(wizard->window),
        "Setup",
        import_error ? import_error : "Import failed.");
    g_free(import_error);
    g_free(selection);
    g_object_unref(dialog);
    return;
  }

  g_free(selection);
  gtk_window_close(GTK_WINDOW(wizard->window));
  finish_setup(wizard->context);
  g_free(wizard);
  g_object_unref(dialog);
}

static void on_setup_import(GtkButton* button, gpointer user_data) {
  SetupWizard* wizard = (SetupWizard*)user_data;
  if (!wizard || !wizard->context) {
    return;
  }

  GtkFileDialog* dialog = gtk_file_dialog_new();
  g_object_ref(dialog);
  gtk_file_dialog_select_folder(
      dialog,
      GTK_WINDOW(wizard->window),
      NULL,
      on_setup_import_finish,
      wizard);
}

static void show_setup_wizard(AppContext* context) {
  if (!context || !context->window) {
    return;
  }

  SetupWizard* wizard = (SetupWizard*)g_new0(SetupWizard, 1);
  wizard->context = context;

  GtkWidget* dialog = gtk_window_new();
  wizard->window = dialog;
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
  gtk_window_set_title(GTK_WINDOW(dialog), "First Setup");
  gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 240);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);

  GtkWidget* title = gtk_label_new("Set up FunPay Vertex");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* detail = gtk_label_new(
      "Create new configuration files or import an existing setup "
      "from another FunPay Vertex installation.");
  gtk_label_set_wrap(GTK_LABEL(detail), TRUE);
  gtk_label_set_xalign(GTK_LABEL(detail), 0.0f);

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* create_button = gtk_button_new_with_label("Create New");
  GtkWidget* import_button = gtk_button_new_with_label("Import Existing");

  g_signal_connect(create_button, "clicked", G_CALLBACK(on_setup_create), wizard);
  g_signal_connect(import_button, "clicked", G_CALLBACK(on_setup_import), wizard);

  gtk_box_append(GTK_BOX(actions), create_button);
  gtk_box_append(GTK_BOX(actions), import_button);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), detail);
  gtk_box_append(GTK_BOX(box), actions);

  gtk_window_set_child(GTK_WINDOW(dialog), box);
  gtk_window_present(GTK_WINDOW(dialog));
}

static const char* auth_error_message(fpv_result_t result) {
  switch (result) {
    case FPV_ERR_INVALID_ARGUMENT:
      return "Check the input values.";
    case FPV_ERR_NOT_FOUND:
      return "Account not found.";
    case FPV_ERR_INVALID_STATE:
      return "Credentials or invite are invalid.";
    case FPV_ERR_OUT_OF_MEMORY:
      return "Out of memory.";
    case FPV_ERR_IO:
      return "Storage error.";
    default:
      return "Sign-in failed.";
  }
}

static void apply_authenticated_context(
    AppContext* context,
    fpv_user_t* user,
    fpv_organization_t* org,
    fpv_team_t* team,
    fpv_role_t role) {
  if (!context) {
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  fpv_user_destroy(context->current_user);
  fpv_organization_destroy(context->current_org);
  fpv_team_destroy(context->current_team);
  context->current_user = user;
  context->current_org = org;
  context->current_team = team;
  context->current_role = role;

  clear_chat_state(context);
  refresh_identity_labels(context);
  refresh_auto_response_list(context);
  refresh_auto_delivery_list(context);
  refresh_settings_from_file(context);
  load_cached_chats(context);

  gtk_window_present(GTK_WINDOW(context->window));
  const char* golden_key = gtk_editable_get_text(
      GTK_EDITABLE(context->settings.funpay_golden_key));
  if (golden_key && golden_key[0]) {
    start_core(NULL, context);
  }
}

static void begin_auth_flow(AppContext* context) {
  if (!context || !context->identity) {
    return;
  }
  if (fpv_identity_store_has_users(context->identity)) {
    show_login_dialog(context);
  } else {
    show_onboarding_dialog(context);
  }
}

typedef struct AuthDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* password_entry;
  GtkWidget* status_label;
} AuthDialog;

static void on_auth_sign_in(GtkButton* button, gpointer user_data) {
  AuthDialog* dialog = (AuthDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  if (!email || !email[0] || !password || !password[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Email and password are required.");
    return;
  }
  fpv_user_t* user = NULL;
  fpv_result_t result =
      fpv_identity_authenticate(dialog->context->identity, email, password, &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_role_t role = FPV_ROLE_UNKNOWN;
  result = fpv_identity_resolve_user_context(
      dialog->context->identity,
      user->id,
      &org,
      &team,
      &role);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Workspace not found.");
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
}

static void on_auth_join(GtkButton* button, gpointer user_data) {
  AuthDialog* dialog = (AuthDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  show_join_dialog(context);
}

static void show_login_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  AuthDialog* dialog = (AuthDialog*)g_new0(AuthDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Sign in");
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 220);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Welcome back");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* login_button = gtk_button_new_with_label("Sign in");
  GtkWidget* join_button = gtk_button_new_with_label("Join with invite");
  gtk_box_append(GTK_BOX(actions), login_button);
  gtk_box_append(GTK_BOX(actions), join_button);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), actions);

  dialog->email_entry = email_entry;
  dialog->password_entry = password_entry;
  dialog->status_label = status;

  g_signal_connect(login_button, "clicked", G_CALLBACK(on_auth_sign_in), dialog);
  g_signal_connect(join_button, "clicked", G_CALLBACK(on_auth_join), dialog);

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-auth-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct JoinDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* name_entry;
  GtkWidget* password_entry;
  GtkWidget* confirm_entry;
  GtkWidget* token_entry;
  GtkWidget* status_label;
} JoinDialog;

static void on_join_submit(GtkButton* button, gpointer user_data) {
  JoinDialog* dialog = (JoinDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* name = gtk_editable_get_text(GTK_EDITABLE(dialog->name_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  const char* confirm =
      gtk_editable_get_text(GTK_EDITABLE(dialog->confirm_entry));
  const char* token = gtk_editable_get_text(GTK_EDITABLE(dialog->token_entry));
  if (!email || !email[0] || !password || !password[0] || !token || !token[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Email, password, and invite token are required.");
    return;
  }
  if (strcmp(password, confirm ? confirm : "") != 0) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Passwords do not match.");
    return;
  }
  fpv_identity_invite_t* invite = NULL;
  fpv_result_t result =
      fpv_identity_validate_invite(dialog->context->identity, token, &invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    return;
  }
  const char* display_name = name && name[0] ? name : email;
  fpv_user_t* user = NULL;
  result = fpv_identity_create_user(
      dialog->context->identity,
      email,
      display_name,
      password,
      true,
      &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return;
  }
  fpv_identity_invite_t* accepted = NULL;
  fpv_user_role_t* assigned = NULL;
  result = fpv_identity_accept_invite(
      dialog->context->identity, token, user->id, &accepted, &assigned);
  fpv_user_role_destroy(assigned);
  fpv_identity_invite_destroy(accepted);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_role_t role = FPV_ROLE_UNKNOWN;
  result = fpv_identity_resolve_user_context(
      dialog->context->identity,
      user->id,
      &org,
      &team,
      &role);
  fpv_identity_invite_destroy(invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Workspace not found.");
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
}

static void show_join_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  JoinDialog* dialog = (JoinDialog*)g_new0(JoinDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Join workspace");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Accept invite");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* name_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
  GtkWidget* confirm_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_entry), "Confirm password");
  gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);
  GtkWidget* token_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(token_entry), "Invite token");

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* create_button = gtk_button_new_with_label("Join workspace");
  g_signal_connect(create_button, "clicked", G_CALLBACK(on_join_submit), dialog);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), name_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), confirm_entry);
  gtk_box_append(GTK_BOX(box), token_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), create_button);

  dialog->email_entry = email_entry;
  dialog->name_entry = name_entry;
  dialog->password_entry = password_entry;
  dialog->confirm_entry = confirm_entry;
  dialog->token_entry = token_entry;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-join-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct OnboardingDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* name_entry;
  GtkWidget* password_entry;
  GtkWidget* confirm_entry;
  GtkWidget* org_entry;
  GtkWidget* timezone_entry;
  GtkWidget* currency_entry;
  GtkWidget* status_label;
} OnboardingDialog;

static void on_onboarding_submit(GtkButton* button, gpointer user_data) {
  OnboardingDialog* dialog = (OnboardingDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* name = gtk_editable_get_text(GTK_EDITABLE(dialog->name_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  const char* confirm =
      gtk_editable_get_text(GTK_EDITABLE(dialog->confirm_entry));
  const char* org_name = gtk_editable_get_text(GTK_EDITABLE(dialog->org_entry));
  const char* timezone =
      gtk_editable_get_text(GTK_EDITABLE(dialog->timezone_entry));
  const char* currency =
      gtk_editable_get_text(GTK_EDITABLE(dialog->currency_entry));
  if (!email || !email[0] || !password || !password[0] ||
      !org_name || !org_name[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Email, password, and workspace name are required.");
    return;
  }
  if (strcmp(password, confirm ? confirm : "") != 0) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Passwords do not match.");
    return;
  }
  const char* display_name = name && name[0] ? name : email;
  fpv_user_t* user = NULL;
  fpv_result_t result = fpv_identity_create_user(
      dialog->context->identity,
      email,
      display_name,
      password,
      true,
      &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_user_role_t* owner_role = NULL;
  const char* tz = timezone && timezone[0] ? timezone : "UTC";
  const char* cur = currency && currency[0] ? currency : "RUB";
  result = fpv_identity_create_organization(
      dialog->context->identity,
      org_name,
      tz,
      cur,
      NULL,
      user->id,
      &org,
      &team,
      &owner_role);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    fpv_user_role_destroy(owner_role);
    return;
  }
  fpv_role_t role = owner_role ? owner_role->role : FPV_ROLE_OWNER;
  fpv_user_role_destroy(owner_role);

  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
  show_link_dialog(context);
}

static void show_onboarding_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  OnboardingDialog* dialog = (OnboardingDialog*)g_new0(OnboardingDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Create workspace");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Welcome to FunPay Vertex");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* name_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
  GtkWidget* confirm_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_entry), "Confirm password");
  gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);

  GtkWidget* org_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(org_entry), "Workspace name");
  GtkWidget* timezone_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(timezone_entry), "Timezone (e.g., UTC)");
  GtkWidget* currency_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(currency_entry), "Currency (e.g., RUB)");

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* create_button = gtk_button_new_with_label("Create workspace");
  g_signal_connect(create_button, "clicked", G_CALLBACK(on_onboarding_submit), dialog);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), name_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), confirm_entry);
  gtk_box_append(GTK_BOX(box), org_entry);
  gtk_box_append(GTK_BOX(box), timezone_entry);
  gtk_box_append(GTK_BOX(box), currency_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), create_button);

  dialog->email_entry = email_entry;
  dialog->name_entry = name_entry;
  dialog->password_entry = password_entry;
  dialog->confirm_entry = confirm_entry;
  dialog->org_entry = org_entry;
  dialog->timezone_entry = timezone_entry;
  dialog->currency_entry = currency_entry;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-onboarding-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct InviteDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* role_combo;
  GtkWidget* team_combo;
  GtkWidget* token_entry;
  GtkWidget* copy_button;
  GtkWidget* status_label;
} InviteDialog;

static fpv_role_t role_from_id(const char* role_id) {
  if (!role_id) {
    return FPV_ROLE_UNKNOWN;
  }
  if (strcmp(role_id, "admin") == 0) {
    return FPV_ROLE_ADMIN;
  }
  if (strcmp(role_id, "manager") == 0) {
    return FPV_ROLE_MANAGER;
  }
  if (strcmp(role_id, "analyst") == 0) {
    return FPV_ROLE_ANALYST;
  }
  if (strcmp(role_id, "viewer") == 0) {
    return FPV_ROLE_VIEWER;
  }
  return FPV_ROLE_UNKNOWN;
}

static gboolean copy_invite_token(InviteDialog* dialog, gboolean show_status) {
  if (!dialog || !dialog->token_entry) {
    return FALSE;
  }
  const char* token = gtk_editable_get_text(GTK_EDITABLE(dialog->token_entry));
  if (!token || !token[0]) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Create an invite first.");
    }
    return FALSE;
  }
#if defined(__APPLE__)
  GError* error = NULL;
  GSubprocess* process =
      g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE, &error, "pbcopy", NULL);
  if (process) {
    gboolean ok =
        g_subprocess_communicate_utf8(process, token, NULL, NULL, NULL, &error);
    g_object_unref(process);
    if (ok) {
      if (show_status && dialog->status_label) {
        // gtk_label_set_text(GTK_LABEL(dialog->status_label), "Invite token copied.");
      }
      return TRUE;
    }
    if (error) {
      g_error_free(error);
      error = NULL;
    }
  } else if (error) {
    g_error_free(error);
    error = NULL;
  }
#endif
  GdkDisplay* display = gtk_widget_get_display(dialog->token_entry);
  if (!display) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Clipboard unavailable.");
    }
    return FALSE;
  }
  GdkClipboard* clipboard = gdk_display_get_clipboard(display);
  if (!clipboard) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Clipboard unavailable.");
    }
    return FALSE;
  }
  gdk_clipboard_set_text(clipboard, token);
  if (show_status && dialog->status_label) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Invite token copied.");
  }
  return TRUE;
}

static void on_invite_copy(GtkButton* button, gpointer user_data) {
  (void)button;
  InviteDialog* dialog = (InviteDialog*)user_data;
  copy_invite_token(dialog, TRUE);
}

static gboolean on_invite_token_key(
    GtkEventControllerKey* controller,
    guint keyval,
    guint keycode,
    GdkModifierType state,
    gpointer user_data) {
  (void)controller;
  (void)keycode;
  InviteDialog* dialog = (InviteDialog*)user_data;
  if (!dialog) {
    return FALSE;
  }
  if ((state & (GDK_CONTROL_MASK | GDK_META_MASK | GDK_SUPER_MASK)) == 0) {
    return FALSE;
  }
  if (gdk_keyval_to_lower(keyval) != GDK_KEY_c) {
    return FALSE;
  }
  copy_invite_token(dialog, TRUE);
  return TRUE;
}

static void on_invite_create(GtkButton* button, gpointer user_data) {
  InviteDialog* dialog = (InviteDialog*)user_data;
  if (!dialog || !dialog->context || !dialog->context->current_org) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  if (!email || !email[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Email is required.");
    return;
  }
  const char* role_id =
      gtk_combo_box_get_active_id(GTK_COMBO_BOX(dialog->role_combo));
  fpv_role_t role = role_from_id(role_id);
  if (role == FPV_ROLE_UNKNOWN) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Select a role.");
    return;
  }
  const char* team_id =
      gtk_combo_box_get_active_id(GTK_COMBO_BOX(dialog->team_combo));
  if (team_id && !team_id[0]) {
    team_id = NULL;
  }
  if (dialog->context->current_role == FPV_ROLE_MANAGER && !team_id) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Managers can only invite to their team.");
    return;
  }
  fpv_identity_invite_t* invite = NULL;
  char* token = NULL;
  fpv_result_t result = fpv_identity_create_invite(
      dialog->context->identity,
      dialog->context->current_org->id,
      team_id,
      email,
      role,
      dialog->context->current_user ? dialog->context->current_user->id : NULL,
      0,
      &invite,
      &token);
  fpv_identity_invite_destroy(invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    if (dialog->token_entry) {
      gtk_editable_set_text(GTK_EDITABLE(dialog->token_entry), "");
    }
    if (dialog->copy_button) {
      gtk_widget_set_sensitive(dialog->copy_button, FALSE);
    }
    free(token);
    return;
  }
  const char* token_text = token ? token : "";
  gtk_editable_set_text(GTK_EDITABLE(dialog->token_entry), token_text);
  if (dialog->copy_button) {
    gtk_widget_set_sensitive(dialog->copy_button, token_text[0] != '\0');
  }
  gboolean copied = copy_invite_token(dialog, FALSE);
  gtk_label_set_text(
      GTK_LABEL(dialog->status_label),
      (token_text[0] != '\0' && copied)
          ? "Invite created. Token copied to clipboard."
          : "Invite created.");
  free(token);
}

static void show_invite_dialog(AppContext* context) {
  if (!context || !context->window || !context->current_org) {
    return;
  }
  InviteDialog* dialog = (InviteDialog*)g_new0(InviteDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Invite team member");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 280);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");

  GtkWidget* role_combo = gtk_combo_box_text_new();
  fpv_role_t current_role = context->current_role;
  if (current_role == FPV_ROLE_OWNER) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "admin", "Admin");
  }
  if (current_role == FPV_ROLE_OWNER || current_role == FPV_ROLE_ADMIN) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "manager", "Manager");
  }
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "analyst", "Analyst");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(role_combo), "viewer", "Viewer");
  gtk_combo_box_set_active(GTK_COMBO_BOX(role_combo), 0);

  GtkWidget* team_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(team_combo), "", "Organization");
  fpv_team_t** teams = NULL;
  size_t team_count = 0;
  if (fpv_identity_list_teams(
          context->identity,
          context->current_org->id,
          &teams,
          &team_count) == FPV_OK) {
    for (size_t i = 0; i < team_count; i++) {
      const char* name = teams[i] && teams[i]->name ? teams[i]->name : "Team";
      const char* id = teams[i] && teams[i]->id ? teams[i]->id : "";
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(team_combo), id, name);
    }
  }
  for (size_t i = 0; i < team_count; i++) {
    fpv_team_destroy(teams[i]);
  }
  free(teams);
  gtk_combo_box_set_active(GTK_COMBO_BOX(team_combo), 0);
  if (current_role == FPV_ROLE_MANAGER &&
      context->current_team &&
      context->current_team->id) {
    gtk_combo_box_set_active_id(
        GTK_COMBO_BOX(team_combo),
        context->current_team->id);
    gtk_widget_set_sensitive(team_combo, FALSE);
  }

  GtkWidget* token_entry = gtk_entry_new();
  gtk_editable_set_editable(GTK_EDITABLE(token_entry), FALSE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(token_entry), "Invite token");
  gtk_widget_set_hexpand(token_entry, TRUE);

  GtkEventController* token_controller = gtk_event_controller_key_new();
  g_signal_connect(
      token_controller,
      "key-pressed",
      G_CALLBACK(on_invite_token_key),
      dialog);
  gtk_widget_add_controller(token_entry, token_controller);

  GtkWidget* copy_button = gtk_button_new_with_label("Copy");
  gtk_widget_set_sensitive(copy_button, FALSE);
  g_signal_connect(copy_button, "clicked", G_CALLBACK(on_invite_copy), dialog);

  GtkWidget* token_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(token_row), token_entry);
  gtk_box_append(GTK_BOX(token_row), copy_button);

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* invite_button = gtk_button_new_with_label("Create invite");
  g_signal_connect(invite_button, "clicked", G_CALLBACK(on_invite_create), dialog);

  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), role_combo);
  gtk_box_append(GTK_BOX(box), team_combo);
  gtk_box_append(GTK_BOX(box), token_row);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), invite_button);

  dialog->email_entry = email_entry;
  dialog->role_combo = role_combo;
  dialog->team_combo = team_combo;
  dialog->token_entry = token_entry;
  dialog->copy_button = copy_button;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-invite-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

static void on_invite_open(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_invite_dialog(context);
}

static void on_link_open(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_link_dialog(context);
}

typedef struct LinkDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* golden_entry;
  GtkWidget* agent_entry;
  GtkWidget* proxy_enable;
  GtkWidget* proxy_ip;
  GtkWidget* proxy_port;
  GtkWidget* proxy_login;
  GtkWidget* proxy_password;
  GtkWidget* status_label;
  GtkWidget* link_button;
} LinkDialog;

typedef struct LinkTask {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* status_label;
  GtkWidget* link_button;
  gchar* golden_key;
  gchar* user_agent;
  gboolean proxy_enabled;
  gchar* proxy_ip;
  guint proxy_port;
  gchar* proxy_login;
  gchar* proxy_password;
} LinkTask;

typedef struct LinkResult {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* status_label;
  GtkWidget* link_button;
  gboolean success;
  gchar* message;
} LinkResult;

static gboolean save_funpay_settings(
    AppContext* context,
    const char* golden_key,
    const char* user_agent,
    gboolean proxy_enabled,
    const char* proxy_ip,
    guint proxy_port,
    const char* proxy_login,
    const char* proxy_password,
    gchar** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!context || !context->config_dir || !golden_key || !golden_key[0]) {
    if (out_error) {
      *out_error = g_strdup("Golden key is required.");
    }
    return FALSE;
  }
  gchar* path = g_build_filename(context->config_dir, "_main.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    if (out_error) {
      *out_error = g_strdup("Failed to load settings file.");
    }
    g_free(path);
    return FALSE;
  }

  fpv_result_t result = fpv_ini_set(ini, "FunPay", "golden_key", golden_key);
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "FunPay", "user_agent", user_agent ? user_agent : "");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "enable", proxy_enabled ? "1" : "0");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(ini, "Proxy", "ip", proxy_ip ? proxy_ip : "");
  }
  gchar* port_value = g_strdup_printf("%u", proxy_port);
  if (result == FPV_OK) {
    result = fpv_ini_set(ini, "Proxy", "port", port_value);
  }
  g_free(port_value);
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "login", proxy_login ? proxy_login : "");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "password", proxy_password ? proxy_password : "");
  }

  if (result == FPV_OK) {
    result = fpv_ini_save(ini, path);
  }
  fpv_ini_destroy(ini);
  g_free(path);
  if (result != FPV_OK) {
    if (out_error) {
      *out_error = g_strdup("Failed to save settings file.");
    }
    return FALSE;
  }
  return TRUE;
}

static gboolean link_task_finish(gpointer data) {
  LinkResult* result = (LinkResult*)data;
  if (!result) {
    return G_SOURCE_REMOVE;
  }
  if (result->success) {
    gtk_label_set_text(
        GTK_LABEL(result->status_label),
        result->message ? result->message : "Linked.");
    refresh_settings_from_file(result->context);
    refresh_identity_labels(result->context);
    const char* key = gtk_editable_get_text(
        GTK_EDITABLE(result->context->settings.funpay_golden_key));
    if (key && key[0] && !result->context->running) {
      start_core(NULL, result->context);
    }
    gtk_window_close(GTK_WINDOW(result->window));
  } else {
    gtk_label_set_text(
        GTK_LABEL(result->status_label),
        result->message ? result->message : "Linking failed.");
    if (result->link_button) {
      gtk_widget_set_sensitive(result->link_button, TRUE);
    }
    if (result->window) {
      gtk_window_set_deletable(GTK_WINDOW(result->window), TRUE);
    }
  }
  g_free(result->message);
  g_free(result);
  return G_SOURCE_REMOVE;
}

static gpointer link_task_run(gpointer data) {
  LinkTask* task = (LinkTask*)data;
  if (!task) {
    return NULL;
  }
  LinkResult* result = (LinkResult*)g_new0(LinkResult, 1);
  result->context = task->context;
  result->window = task->window;
  result->status_label = task->status_label;
  result->link_button = task->link_button;

  fpv_funpay_account_t* account = NULL;
  fpv_result_t refresh_result = FPV_OK;
  fpv_result_t link_result = FPV_OK;
  const char* username = NULL;
  const char* currency = NULL;
  char id_buf[32] = {0};

  fpv_funpay_account_config_t config;
  memset(&config, 0, sizeof(config));
  config.golden_key = task->golden_key;
  config.user_agent = task->user_agent;
  config.timeout_ms = 15000;
  config.proxy.enabled = task->proxy_enabled;
  config.proxy.host = task->proxy_ip;
  config.proxy.port = (uint16_t)task->proxy_port;
  config.proxy.username = task->proxy_login;
  config.proxy.password = task->proxy_password;

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  account = fpv_funpay_account_create(&config, &error);
  if (!account) {
    result->message = g_strdup(error.message ? error.message : "Authorization failed.");
    goto done;
  }
  refresh_result = fpv_funpay_account_refresh(account, &error);
  if (refresh_result != FPV_OK || !fpv_funpay_account_is_initiated(account)) {
    result->message = g_strdup(error.message ? error.message : "FunPay refresh failed.");
    goto done;
  }
  snprintf(id_buf, sizeof(id_buf), "%" PRIu64, fpv_funpay_account_id(account));
  username = fpv_funpay_account_username(account);
  currency = fpv_funpay_account_currency(account);
  link_result = fpv_identity_link_account(
      task->context->identity,
      task->context->current_org ? task->context->current_org->id : NULL,
      task->context->current_team ? task->context->current_team->id : NULL,
      id_buf,
      username,
      username,
      currency,
      NULL);
  if (link_result != FPV_OK) {
    result->message = g_strdup(auth_error_message(link_result));
    goto done;
  }
  result->success = TRUE;
  result->message = g_strdup("FunPay account linked.");

done:
  if (account) {
    fpv_funpay_account_destroy(account);
  }
  fpv_funpay_error_clear(&error);
  g_idle_add(link_task_finish, result);
  g_free(task->golden_key);
  g_free(task->user_agent);
  g_free(task->proxy_ip);
  g_free(task->proxy_login);
  g_free(task->proxy_password);
  g_free(task);
  return NULL;
}

static void on_link_submit(GtkButton* button, gpointer user_data) {
  LinkDialog* dialog = (LinkDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* golden_key =
      gtk_editable_get_text(GTK_EDITABLE(dialog->golden_entry));
  const char* user_agent =
      gtk_editable_get_text(GTK_EDITABLE(dialog->agent_entry));
  gboolean proxy_enabled =
      gtk_switch_get_active(GTK_SWITCH(dialog->proxy_enable));
  const char* proxy_ip =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_ip));
  guint proxy_port =
      (guint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(dialog->proxy_port));
  const char* proxy_login =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_login));
  const char* proxy_password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_password));

  gchar* error = NULL;
  if (!save_funpay_settings(
          dialog->context,
          golden_key,
          user_agent,
          proxy_enabled,
          proxy_ip,
          proxy_port,
          proxy_login,
          proxy_password,
          &error)) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       error ? error : "Failed to save settings.");
    g_free(error);
    return;
  }
  g_free(error);

  gtk_widget_set_sensitive(dialog->link_button, FALSE);
  gtk_label_set_text(GTK_LABEL(dialog->status_label), "Linking...");
  gtk_window_set_deletable(GTK_WINDOW(dialog->window), FALSE);

  LinkTask* task = (LinkTask*)g_new0(LinkTask, 1);
  task->context = dialog->context;
  task->window = dialog->window;
  task->status_label = dialog->status_label;
  task->link_button = dialog->link_button;
  task->golden_key = g_strdup(golden_key);
  task->user_agent = g_strdup(user_agent ? user_agent : "");
  task->proxy_enabled = proxy_enabled;
  task->proxy_ip = g_strdup(proxy_ip ? proxy_ip : "");
  task->proxy_port = proxy_port;
  task->proxy_login = g_strdup(proxy_login ? proxy_login : "");
  task->proxy_password = g_strdup(proxy_password ? proxy_password : "");

  GThread* thread = g_thread_new("fpv-link", link_task_run, task);
  if (thread) {
    g_thread_unref(thread);
  }
}

static void show_link_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  refresh_settings_from_file(context);
  LinkDialog* dialog = (LinkDialog*)g_new0(LinkDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Link FunPay account");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 360);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* golden_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(golden_entry), "Golden key");
  GtkWidget* agent_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(agent_entry), "User agent");

  GtkWidget* proxy_enable = gtk_switch_new();
  GtkWidget* proxy_ip = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_ip), "Proxy IP");
  GtkWidget* proxy_port = gtk_spin_button_new_with_range(0, 65535, 1);
  GtkWidget* proxy_login = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_login), "Proxy login");
  GtkWidget* proxy_password = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_password), "Proxy password");
  gtk_entry_set_visibility(GTK_ENTRY(proxy_password), FALSE);

  const char* current_key =
      gtk_editable_get_text(GTK_EDITABLE(context->settings.funpay_golden_key));
  const char* current_agent =
      gtk_editable_get_text(GTK_EDITABLE(context->settings.funpay_user_agent));
  gtk_editable_set_text(GTK_EDITABLE(golden_entry), current_key ? current_key : "");
  gtk_editable_set_text(GTK_EDITABLE(agent_entry), current_agent ? current_agent : "");
  gtk_switch_set_active(
      GTK_SWITCH(proxy_enable),
      gtk_switch_get_active(GTK_SWITCH(context->settings.proxy_enable)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_ip),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_ip)));
  gtk_spin_button_set_value(
      GTK_SPIN_BUTTON(proxy_port),
      gtk_spin_button_get_value(GTK_SPIN_BUTTON(context->settings.proxy_port)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_login),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_login)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_password),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_password)));

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* link_button = gtk_button_new_with_label("Link account");
  g_signal_connect(link_button, "clicked", G_CALLBACK(on_link_submit), dialog);

  gtk_box_append(GTK_BOX(box), golden_entry);
  gtk_box_append(GTK_BOX(box), agent_entry);
  GtkWidget* proxy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* proxy_label = gtk_label_new("Enable proxy");
  gtk_label_set_xalign(GTK_LABEL(proxy_label), 0.0f);
  gtk_widget_set_hexpand(proxy_label, TRUE);
  gtk_box_append(GTK_BOX(proxy_row), proxy_label);
  gtk_box_append(GTK_BOX(proxy_row), proxy_enable);
  gtk_box_append(GTK_BOX(box), proxy_row);
  gtk_box_append(GTK_BOX(box), proxy_ip);
  gtk_box_append(GTK_BOX(box), proxy_port);
  gtk_box_append(GTK_BOX(box), proxy_login);
  gtk_box_append(GTK_BOX(box), proxy_password);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), link_button);

  dialog->golden_entry = golden_entry;
  dialog->agent_entry = agent_entry;
  dialog->proxy_enable = proxy_enable;
  dialog->proxy_ip = proxy_ip;
  dialog->proxy_port = proxy_port;
  dialog->proxy_login = proxy_login;
  dialog->proxy_password = proxy_password;
  dialog->status_label = status;
  dialog->link_button = link_button;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-link-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

static void update_status(
    AppContext* context,
    fpv_core_status_t status,
    const char* detail) {
  if (!context || !context->status_label) {
    return;
  }

  gchar* text = NULL;
  gchar* safe_detail = NULL;
  gboolean running = status == FPV_CORE_RUNNING;

  if (detail && detail[0] != '\0') {
    safe_detail = sanitize_utf8(detail);
    text = g_strdup_printf("%s - %s", status_label(status), safe_detail);
  } else {
    text = g_strdup(status_label(status));
  }

  gtk_label_set_text(GTK_LABEL(context->status_label), text);
  g_free(safe_detail);
  g_free(text);

  context->running = running;
  gtk_widget_set_sensitive(context->start_button, !running);
  gtk_widget_set_sensitive(context->stop_button, running);
  gtk_widget_set_sensitive(
      context->message_send_button,
      running && context->active_chat_id != NULL);
}

static void handle_event(AppContext* context, const fpv_event_t* event) {
  if (!context || !event) {
    return;
  }

  switch (event->type) {
    case FPV_EVENT_CORE_STATUS: {
      const fpv_core_status_event_t* payload =
          fpv_event_as_core_status(event);
      if (payload) {
        update_status(context, payload->status, payload->detail);
      }
      break;
    }
    case FPV_EVENT_LOG: {
      const fpv_log_entry_t* payload = fpv_event_as_log(event);
      if (payload) {
        append_log_row(
            context,
            log_level_label(payload->level),
            payload->message ? payload->message : "",
            event->timestamp_ms);
      }
      break;
    }
    case FPV_EVENT_NOTIFICATION: {
      const fpv_notification_t* payload = fpv_event_as_notification(event);
      if (payload) {
        append_log_row(
            context,
            notification_severity_label(payload->severity),
            payload->message ? payload->message : "",
            event->timestamp_ms);
      }
      break;
    }
    case FPV_EVENT_CHAT: {
      const fpv_chat_t* payload = fpv_event_as_chat(event);
      if (payload && payload->id) {
        fpv_chat_t* clone = fpv_chat_clone(payload);
        g_hash_table_replace(
            context->chats,
            g_strdup(payload->id),
            clone);
        refresh_chat_list(context);
        if (context->chat_store && context->current_org &&
            context->current_org->id) {
          fpv_chat_store_upsert_chat(
              context->chat_store,
              context->current_org->id,
              payload,
              event->timestamp_ms);
        }
      }
      break;
    }
    case FPV_EVENT_MESSAGE: {
      const fpv_message_t* payload = fpv_event_as_message(event);
      if (payload && payload->chat_id) {
        fpv_message_t* clone = fpv_message_clone(payload);
        GPtrArray* list = (GPtrArray*)g_hash_table_lookup(
            context->messages_by_chat,
            payload->chat_id);
        if (!list) {
          list = g_ptr_array_new_with_free_func(
              (GDestroyNotify)fpv_message_destroy);
          g_hash_table_replace(
              context->messages_by_chat,
              g_strdup(payload->chat_id),
              list);
        }
        g_ptr_array_add(list, clone);
        while (list->len > 200) {
          g_ptr_array_remove_index(list, 0);
        }
        if (context->active_chat_id &&
            strcmp(context->active_chat_id, payload->chat_id) == 0) {
          refresh_message_list(context);
        }
        if (context->chat_store && context->current_org &&
            context->current_org->id && payload->id) {
          fpv_chat_store_upsert_message(
              context->chat_store,
              context->current_org->id,
              payload);
        }
      }
      break;
    }
    case FPV_EVENT_ORDER: {
      const fpv_order_t* payload = fpv_event_as_order(event);
      if (payload && payload->id) {
        fpv_order_t* clone = fpv_order_clone(payload);
        g_hash_table_replace(
            context->orders,
            g_strdup(payload->id),
            clone);
        refresh_order_list(context);
      }
      break;
    }
    case FPV_EVENT_LOT: {
      const fpv_lot_t* payload = fpv_event_as_lot(event);
      if (payload && payload->id) {
        fpv_lot_t* clone = fpv_lot_clone(payload);
        g_hash_table_replace(
            context->lots,
            g_strdup(payload->id),
            clone);
        refresh_lot_list(context);
      }
      break;
    }
    case FPV_EVENT_PLUGIN: {
      const fpv_plugin_t* payload = fpv_event_as_plugin(event);
      if (payload && payload->id) {
        fpv_plugin_t* clone = fpv_plugin_clone(payload);
        g_hash_table_replace(
            context->plugins,
            g_strdup(payload->id),
            clone);
        refresh_plugin_list(context);
      }
      break;
    }
    default:
      break;
  }

  refresh_metrics(context);
}

static gboolean poll_events(gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core || !context->bus) {
    return G_SOURCE_REMOVE;
  }

  if (fpv_core_status(context->core) == FPV_CORE_RUNNING) {
    fpv_core_tick(
        context->core,
        (uint64_t)(g_get_real_time() / 1000ULL));
  }

  fpv_event_batch_t batch = fpv_event_bus_drain(context->bus);
  if (!batch.events || batch.count == 0) {
    fpv_event_batch_destroy(&batch);
    return G_SOURCE_CONTINUE;
  }

  for (size_t i = 0; i < batch.count; i++) {
    handle_event(context, batch.events[i]);
  }

  fpv_event_batch_destroy(&batch);
  return G_SOURCE_CONTINUE;
}

static void start_core(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core) {
    return;
  }
  if (context->running) {
    return;
  }

  const uint64_t now_ms = (uint64_t)(g_get_real_time() / 1000ULL);
  fpv_result_t result = fpv_core_start(context->core, now_ms);
  if (result != FPV_OK) {
    append_log_row(context, "error", "Core start failed.", now_ms);
    update_status(context, FPV_CORE_ERROR, "start failed");
  }
}

static void stop_core(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core) {
    return;
  }
  if (!context->running) {
    return;
  }

  const uint64_t now_ms = (uint64_t)(g_get_real_time() / 1000ULL);
  fpv_result_t result = fpv_core_stop(context->core, now_ms);
  if (result != FPV_OK) {
    append_log_row(context, "error", "Core stop failed.", now_ms);
    update_status(context, FPV_CORE_ERROR, "stop failed");
  }
}

typedef struct ChatHistoryTask {
  AppContext* context;
  gchar* chat_id;
  gchar* chat_name;
  gchar* org_id;
  gboolean load_cached;
} ChatHistoryTask;

typedef struct ChatCacheTask {
  AppContext* context;
  gchar* chat_id;
} ChatCacheTask;

static gboolean chat_history_cache_complete(gpointer data) {
  ChatCacheTask* task = (ChatCacheTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->chat_id && task->context->active_chat_id &&
      strcmp(task->context->active_chat_id, task->chat_id) == 0) {
    refresh_message_list(task->context);
  }
  refresh_metrics(task->context);
  g_free(task->chat_id);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gboolean chat_history_complete(gpointer data) {
  ChatHistoryTask* task = (ChatHistoryTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->chat_id && task->context->active_chat_id &&
      strcmp(task->context->active_chat_id, task->chat_id) == 0) {
    refresh_message_list(task->context);
  }
  refresh_metrics(task->context);
  g_free(task->chat_id);
  g_free(task->chat_name);
  g_free(task->org_id);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer chat_history_thread(gpointer data) {
  ChatHistoryTask* task = (ChatHistoryTask*)data;
  if (!task || !task->context) {
    if (task) {
      g_main_context_invoke(NULL, chat_history_complete, task);
    }
    return NULL;
  }

  if (task->load_cached && task->context->chat_store &&
      task->org_id && task->org_id[0] && task->chat_id && task->chat_id[0]) {
    fpv_message_t** cached = NULL;
    size_t cached_count = 0;
    fpv_result_t cache_result = fpv_chat_store_load_messages(
        task->context->chat_store,
        task->org_id,
        task->chat_id,
        200,
        &cached,
        &cached_count);
    if (cache_result == FPV_OK && cached_count > 0) {
      GPtrArray* list = g_ptr_array_new_with_free_func(
          (GDestroyNotify)fpv_message_destroy);
      if (list) {
        for (size_t i = 0; i < cached_count; i++) {
          g_ptr_array_add(list, cached[i]);
        }
        g_hash_table_replace(
            task->context->messages_by_chat,
            g_strdup(task->chat_id),
            list);
        ChatCacheTask* cache_task = (ChatCacheTask*)g_new0(ChatCacheTask, 1);
        if (cache_task) {
          cache_task->context = task->context;
          cache_task->chat_id = g_strdup(task->chat_id);
          if (cache_task->chat_id) {
            g_main_context_invoke(
                NULL, chat_history_cache_complete, cache_task);
          } else {
            g_free(cache_task);
          }
        }
      } else {
        for (size_t i = 0; i < cached_count; i++) {
          fpv_message_destroy(cached[i]);
        }
      }
    } else if (cached_count > 0) {
      for (size_t i = 0; i < cached_count; i++) {
        fpv_message_destroy(cached[i]);
      }
    }
    if (cached) {
      free(cached);
    }
  }

  if (!task->context->core) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }

  if (!task->chat_id || !task->chat_id[0]) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }

  guint64 chat_id = g_ascii_strtoull(task->chat_id, NULL, 10);
  if (chat_id == 0) {
    g_main_context_invoke(NULL, chat_history_complete, task);
    return NULL;
  }
  fpv_message_t** messages = NULL;
  size_t count = 0;
  fpv_result_t result = fpv_core_fetch_chat_history(
      task->context->core,
      chat_id,
      task->chat_name,
      &messages,
      &count);
  if (result == FPV_OK) {
    if (task->context->chat_store && task->org_id && task->org_id[0] &&
        messages && count > 0) {
      fpv_chat_store_upsert_messages(
          task->context->chat_store,
          task->org_id,
          (const fpv_message_t* const*)messages,
          count);
    }
    GPtrArray* list = g_ptr_array_new_with_free_func(
        (GDestroyNotify)fpv_message_destroy);
    if (list) {
      for (size_t i = 0; i < count; i++) {
        g_ptr_array_add(list, messages[i]);
      }
      g_hash_table_replace(
          task->context->messages_by_chat,
          g_strdup(task->chat_id),
          list);
    } else {
      for (size_t i = 0; i < count; i++) {
        fpv_message_destroy(messages[i]);
      }
    }
  }
  if (messages) {
    free(messages);
  }
  g_main_context_invoke(NULL, chat_history_complete, task);
  return NULL;
}

static void on_chat_selected(
    GtkListBox* box,
    GtkListBoxRow* row,
    gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context) {
    return;
  }

  g_free(context->active_chat_id);
  g_free(context->active_chat_name);
  context->active_chat_id = NULL;
  context->active_chat_name = NULL;

  if (!row) {
    gtk_label_set_text(GTK_LABEL(context->message_header), "Messages");
    refresh_message_list(context);
    gtk_widget_set_sensitive(context->message_send_button, FALSE);
    return;
  }

  const gchar* chat_id = (const gchar*)g_object_get_data(
      G_OBJECT(row), "chat-id");
  const gchar* chat_name = (const gchar*)g_object_get_data(
      G_OBJECT(row), "chat-name");
  if (!chat_id || !chat_id[0]) {
    return;
  }

  context->active_chat_id = g_strdup(chat_id);
  if (chat_name) {
    context->active_chat_name = g_strdup(chat_name);
  }

  gchar* header = g_strdup_printf(
      "Messages · %s",
      chat_name ? chat_name : chat_id);
  gtk_label_set_text(GTK_LABEL(context->message_header), header);
  g_free(header);

  refresh_message_list(context);
  gtk_widget_set_sensitive(context->message_send_button, context->running);

  gboolean load_cached = TRUE;
  if (context->messages_by_chat) {
    GPtrArray* existing = (GPtrArray*)g_hash_table_lookup(
        context->messages_by_chat,
        chat_id);
    if (existing && existing->len > 0) {
      load_cached = FALSE;
    }
  }

  ChatHistoryTask* task = (ChatHistoryTask*)g_new0(ChatHistoryTask, 1);
  task->context = context;
  task->chat_id = g_strdup(chat_id);
  task->chat_name = chat_name ? g_strdup(chat_name) : NULL;
  task->org_id = context->current_org && context->current_org->id
      ? g_strdup(context->current_org->id)
      : NULL;
  task->load_cached = load_cached;
  g_thread_new("fpv-chat-history", chat_history_thread, task);
}

typedef struct MessageSendTask {
  AppContext* context;
  gchar* chat_id;
  gchar* chat_name;
  gchar* text;
  fpv_result_t result;
} MessageSendTask;

static gboolean message_send_complete(gpointer data) {
  MessageSendTask* task = (MessageSendTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(task->context->window),
        "Messages",
        "Failed to send message.");
  }
  g_free(task->chat_id);
  g_free(task->chat_name);
  g_free(task->text);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer message_send_thread(gpointer data) {
  MessageSendTask* task = (MessageSendTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task) {
      task->result = FPV_ERR_INVALID_STATE;
      g_main_context_invoke(NULL, message_send_complete, task);
    }
    return NULL;
  }
  guint64 chat_id = g_ascii_strtoull(task->chat_id, NULL, 10);
  fpv_result_t result = fpv_core_send_message(
      task->context->core,
      chat_id,
      task->chat_name,
      task->text,
      true);
  task->result = result;
  g_main_context_invoke(NULL, message_send_complete, task);
  return NULL;
}

static void on_send_message(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->message_entry) {
    return;
  }
  if (!context->active_chat_id) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Message",
        "Select a chat first.");
    return;
  }
  if (!context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Message",
        "Start the core before sending messages.");
    return;
  }
  const char* text = gtk_editable_get_text(
      GTK_EDITABLE(context->message_entry));
  if (!text || !text[0]) {
    return;
  }

  MessageSendTask* task = (MessageSendTask*)g_new0(MessageSendTask, 1);
  task->context = context;
  task->chat_id = g_strdup(context->active_chat_id);
  task->chat_name = context->active_chat_name
      ? g_strdup(context->active_chat_name)
      : NULL;
  task->text = g_strdup(text);

  gtk_editable_set_text(GTK_EDITABLE(context->message_entry), "");
  g_thread_new("fpv-send-message", message_send_thread, task);
}

static void on_message_entry_activate(GtkEntry* entry, gpointer user_data) {
  on_send_message(NULL, user_data);
}

typedef struct LotsRefreshTask {
  AppContext* context;
} LotsRefreshTask;

static gboolean lots_refresh_complete(gpointer data) {
  LotsRefreshTask* task = (LotsRefreshTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  gtk_widget_set_sensitive(task->context->lot_refresh_button, TRUE);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer lots_refresh_thread(gpointer data) {
  LotsRefreshTask* task = (LotsRefreshTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task && task->context) {
      g_main_context_invoke(NULL, lots_refresh_complete, task);
    } else if (task) {
      g_free(task);
    }
    return NULL;
  }
  fpv_core_refresh_lots(task->context->core);
  g_main_context_invoke(NULL, lots_refresh_complete, task);
  return NULL;
}

static void on_refresh_lots(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->core) {
    return;
  }
  if (!context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Lots",
        "Start the core before refreshing lots.");
    return;
  }
  gtk_widget_set_sensitive(context->lot_refresh_button, FALSE);
  LotsRefreshTask* task = (LotsRefreshTask*)g_new0(LotsRefreshTask, 1);
  task->context = context;
  g_thread_new("fpv-refresh-lots", lots_refresh_thread, task);
}

static void refresh_auto_response_list(AppContext* context) {
  if (!context || !context->auto_response_list || !context->config_dir) {
    return;
  }

  clear_list_box(context->auto_response_list);

  gchar* path = g_build_filename(context->config_dir, "auto_response.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("Auto-response config not available.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_response_list), row);
    g_free(path);
    return;
  }

  size_t section_count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < section_count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    const char* response = fpv_ini_get(ini, section, "response");
    const char* notification = fpv_ini_get(ini, section, "telegramNotification");

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar* title_text = sanitize_utf8(section ? section : "");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    gchar* response_text = sanitize_utf8(response ? response : "");
    gchar* meta = g_strdup_printf(
        "%s%s",
        response_text,
        parse_ini_bool(notification, FALSE) ? " · Telegram" : "");
    GtkWidget* meta_label = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(meta_label), TRUE);
    gtk_widget_add_css_class(meta_label, "muted");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), meta_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_response_list), row);

    if (section) {
      g_object_set_data_full(
          G_OBJECT(row),
          "section",
          g_strdup(section),
          g_free);
    }

    g_free(meta);
    g_free(title_text);
    g_free(response_text);
  }

  fpv_ini_destroy(ini);
  g_free(path);
}

static void refresh_auto_delivery_list(AppContext* context) {
  if (!context || !context->auto_delivery_list || !context->config_dir) {
    return;
  }

  clear_list_box(context->auto_delivery_list);

  gchar* path = g_build_filename(context->config_dir, "auto_delivery.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("Auto-delivery config not available.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);
    g_free(path);
    return;
  }

  size_t section_count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < section_count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    const char* response = fpv_ini_get(ini, section, "response");
    const char* products = fpv_ini_get(ini, section, "productsFileName");
    const char* disable = fpv_ini_get(ini, section, "disable");

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar* title_text = sanitize_utf8(section ? section : "");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    gchar* response_text = sanitize_utf8(response ? response : "");
    gchar* meta = g_strdup_printf(
        "%s%s%s",
        response_text,
        products && products[0] ? " · Products" : "",
        parse_ini_bool(disable, FALSE) ? " · Disabled" : "");
    GtkWidget* meta_label = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(meta_label), TRUE);
    gtk_widget_add_css_class(meta_label, "muted");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), meta_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);

    if (section) {
      g_object_set_data_full(
          G_OBJECT(row),
          "section",
          g_strdup(section),
          g_free);
    }

    g_free(meta);
    g_free(title_text);
    g_free(response_text);
  }

  fpv_ini_destroy(ini);
  g_free(path);
}

typedef struct AutoResponseDialog {
  AppContext* context;
  GtkWidget* dialog;
  GtkWidget* commands_entry;
  GtkWidget* response_entry;
  GtkWidget* telegram_switch;
  GtkWidget* notification_entry;
  gchar* original_section;
} AutoResponseDialog;

static gboolean auto_response_dialog_close(
    GtkWindow* window,
    gpointer user_data) {
  AutoResponseDialog* data = (AutoResponseDialog*)user_data;
  if (data) {
    g_free(data->original_section);
    g_free(data);
  }
  return FALSE;
}

static void auto_response_dialog_save(GtkButton* button, gpointer user_data) {
  AutoResponseDialog* data = (AutoResponseDialog*)user_data;
  if (!data || !data->context) {
    return;
  }

  const char* commands = gtk_editable_get_text(
      GTK_EDITABLE(data->commands_entry));
  const char* response = gtk_editable_get_text(
      GTK_EDITABLE(data->response_entry));
  const char* notification_text = gtk_editable_get_text(
      GTK_EDITABLE(data->notification_entry));
  if (!commands || !commands[0] || !response || !response[0]) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Response",
        "Command and response are required.");
    return;
  }

  gchar* path = g_build_filename(
      data->context->config_dir,
      "auto_response.cfg",
      NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Response",
        "Failed to load auto_response.cfg.");
    g_free(path);
    return;
  }

  if (data->original_section &&
      strcmp(data->original_section, commands) != 0) {
    fpv_ini_remove_section(ini, data->original_section);
  }

  fpv_ini_set(ini, commands, "response", response);
  fpv_ini_set(ini, commands, "telegramNotification",
              gtk_switch_get_active(GTK_SWITCH(data->telegram_switch))
                  ? "1" : "0");
  if (notification_text && notification_text[0]) {
    fpv_ini_set(ini, commands, "notificationText", notification_text);
  } else {
    fpv_ini_remove_entry(ini, commands, "notificationText");
  }

  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);
  if (save_result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Response",
        "Failed to save auto_response.cfg.");
    return;
  }

  refresh_auto_response_list(data->context);
  if (data->context->running) {
    fpv_core_reload_auto_response(data->context->core);
  }

  gtk_window_close(GTK_WINDOW(data->dialog));
}

static void show_auto_response_dialog(
    AppContext* context,
    const char* section_name) {
  if (!context) {
    return;
  }

  GtkWidget* dialog = gtk_window_new();
  gtk_window_set_title(
      GTK_WINDOW(dialog),
      section_name ? "Edit Auto Response" : "Add Auto Response");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 320);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);

  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

  GtkWidget* commands_entry = gtk_entry_new();
  GtkWidget* response_entry = gtk_entry_new();
  GtkWidget* telegram_switch = gtk_switch_new();
  GtkWidget* notification_entry = gtk_entry_new();

  add_setting_row(grid, 0, "Commands", commands_entry);
  add_setting_row(grid, 1, "Response", response_entry);
  add_setting_row(grid, 2, "Telegram notification", telegram_switch);
  add_setting_row(grid, 3, "Notification text", notification_entry);

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget* save_button = gtk_button_new_with_label("Save");

  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), save_button);

  gtk_box_append(GTK_BOX(root), grid);
  gtk_box_append(GTK_BOX(root), actions);
  gtk_window_set_child(GTK_WINDOW(dialog), root);

  AutoResponseDialog* dialog_data = (AutoResponseDialog*)g_new0(
      AutoResponseDialog, 1);
  dialog_data->context = context;
  dialog_data->dialog = dialog;
  dialog_data->commands_entry = commands_entry;
  dialog_data->response_entry = response_entry;
  dialog_data->telegram_switch = telegram_switch;
  dialog_data->notification_entry = notification_entry;
  dialog_data->original_section = section_name ? g_strdup(section_name) : NULL;

  if (section_name) {
    gchar* path = g_build_filename(
        context->config_dir,
        "auto_response.cfg",
        NULL);
    fpv_ini_error_t error;
    fpv_ini_t* ini = fpv_ini_load(path, &error);
    if (ini) {
      const char* response = fpv_ini_get(ini, section_name, "response");
      const char* notification = fpv_ini_get(
          ini, section_name, "telegramNotification");
      const char* notification_text = fpv_ini_get(
          ini, section_name, "notificationText");
      gtk_editable_set_text(GTK_EDITABLE(commands_entry), section_name);
      gtk_editable_set_text(
          GTK_EDITABLE(response_entry),
          response ? response : "");
      gtk_switch_set_active(
          GTK_SWITCH(telegram_switch),
          parse_ini_bool(notification, FALSE));
      gtk_editable_set_text(
          GTK_EDITABLE(notification_entry),
          notification_text ? notification_text : "");
      fpv_ini_destroy(ini);
    }
    g_free(path);
  }

  g_signal_connect_swapped(
      cancel_button,
      "clicked",
      G_CALLBACK(gtk_window_close),
      dialog);
  g_signal_connect(
      save_button,
      "clicked",
      G_CALLBACK(auto_response_dialog_save),
      dialog_data);
  g_signal_connect(
      dialog,
      "close-request",
      G_CALLBACK(auto_response_dialog_close),
      dialog_data);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void on_auto_response_add(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_auto_response_dialog(context, NULL);
}

static void on_auto_response_edit(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkListBoxRow* row = gtk_list_box_get_selected_row(
      GTK_LIST_BOX(context->auto_response_list));
  if (!row) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Response",
        "Select a command to edit.");
    return;
  }
  const gchar* section = (const gchar*)g_object_get_data(
      G_OBJECT(row), "section");
  if (section) {
    show_auto_response_dialog(context, section);
  }
}

static void on_auto_response_delete(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkListBoxRow* row = gtk_list_box_get_selected_row(
      GTK_LIST_BOX(context->auto_response_list));
  if (!row) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Response",
        "Select a command to delete.");
    return;
  }
  const gchar* section = (const gchar*)g_object_get_data(
      G_OBJECT(row), "section");
  if (!section) {
    return;
  }

  gchar* path = g_build_filename(context->config_dir, "auto_response.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Response",
        "Failed to load auto_response.cfg.");
    g_free(path);
    return;
  }
  fpv_ini_remove_section(ini, section);
  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);
  if (save_result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Response",
        "Failed to save auto_response.cfg.");
    return;
  }

  refresh_auto_response_list(context);
  if (context->running) {
    fpv_core_reload_auto_response(context->core);
  }
}

static void on_auto_response_reload(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  refresh_auto_response_list(context);
  if (context->running) {
    fpv_core_reload_auto_response(context->core);
  }
}

typedef struct AutoDeliveryDialog {
  AppContext* context;
  GtkWidget* dialog;
  GtkWidget* lot_entry;
  GtkWidget* response_entry;
  GtkWidget* products_entry;
  GtkWidget* disable_switch;
  GtkWidget* restore_switch;
  GtkWidget* auto_disable_switch;
  GtkWidget* auto_delivery_switch;
  GtkWidget* multi_delivery_switch;
  gchar* original_section;
} AutoDeliveryDialog;

static gboolean auto_delivery_dialog_close(
    GtkWindow* window,
    gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  if (data) {
    g_free(data->original_section);
    g_free(data);
  }
  return FALSE;
}

static void auto_delivery_dialog_save(GtkButton* button, gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  if (!data || !data->context) {
    return;
  }

  const char* lot_name = gtk_editable_get_text(
      GTK_EDITABLE(data->lot_entry));
  const char* response = gtk_editable_get_text(
      GTK_EDITABLE(data->response_entry));
  const char* products = gtk_editable_get_text(
      GTK_EDITABLE(data->products_entry));
  if (!lot_name || !lot_name[0] || !response || !response[0]) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Lot name and response are required.");
    return;
  }
  if (products && products[0] && !strstr(response, "$product")) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Response must include $product when products file is set.");
    return;
  }
  if (products && products[0]) {
    gchar* products_path = g_build_filename(
        data->context->data_dir,
        "products",
        products,
        NULL);
    gboolean exists = g_file_test(products_path, G_FILE_TEST_IS_REGULAR);
    g_free(products_path);
    if (!exists) {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          "Products file not found in storage/products.");
      return;
    }
  }

  gchar* path = g_build_filename(
      data->context->config_dir,
      "auto_delivery.cfg",
      NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Failed to load auto_delivery.cfg.");
    g_free(path);
    return;
  }

  if (data->original_section &&
      strcmp(data->original_section, lot_name) != 0) {
    fpv_ini_remove_section(ini, data->original_section);
  }

  fpv_ini_set(ini, lot_name, "response", response);
  if (products && products[0]) {
    fpv_ini_set(ini, lot_name, "productsFileName", products);
  } else {
    fpv_ini_remove_entry(ini, lot_name, "productsFileName");
  }

  fpv_ini_set(ini, lot_name, "disable",
              gtk_switch_get_active(GTK_SWITCH(data->disable_switch))
                  ? "1" : "0");
  fpv_ini_set(ini, lot_name, "disableAutoRestore",
              gtk_switch_get_active(GTK_SWITCH(data->restore_switch))
                  ? "1" : "0");
  fpv_ini_set(ini, lot_name, "disableAutoDisable",
              gtk_switch_get_active(GTK_SWITCH(data->auto_disable_switch))
                  ? "1" : "0");
  fpv_ini_set(ini, lot_name, "disableAutoDelivery",
              gtk_switch_get_active(GTK_SWITCH(data->auto_delivery_switch))
                  ? "1" : "0");
  fpv_ini_set(ini, lot_name, "disableMultiDelivery",
              gtk_switch_get_active(GTK_SWITCH(data->multi_delivery_switch))
                  ? "1" : "0");

  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);
  if (save_result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Failed to save auto_delivery.cfg.");
    return;
  }

  refresh_auto_delivery_list(data->context);
  if (data->context->running) {
    fpv_core_reload_auto_delivery(data->context->core);
  }

  gtk_window_close(GTK_WINDOW(data->dialog));
}

static void show_auto_delivery_dialog(
    AppContext* context,
    const char* section_name) {
  if (!context) {
    return;
  }

  GtkWidget* dialog = gtk_window_new();
  gtk_window_set_title(
      GTK_WINDOW(dialog),
      section_name ? "Edit Auto Delivery" : "Add Auto Delivery");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 380);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);

  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

  GtkWidget* lot_entry = gtk_entry_new();
  GtkWidget* response_entry = gtk_entry_new();
  GtkWidget* products_entry = gtk_entry_new();
  GtkWidget* disable_switch = gtk_switch_new();
  GtkWidget* restore_switch = gtk_switch_new();
  GtkWidget* auto_disable_switch = gtk_switch_new();
  GtkWidget* auto_delivery_switch = gtk_switch_new();
  GtkWidget* multi_delivery_switch = gtk_switch_new();

  add_setting_row(grid, 0, "Lot name", lot_entry);
  add_setting_row(grid, 1, "Response", response_entry);
  add_setting_row(grid, 2, "Products file", products_entry);
  add_setting_row(grid, 3, "Disable", disable_switch);
  add_setting_row(grid, 4, "Disable auto restore", restore_switch);
  add_setting_row(grid, 5, "Disable auto disable", auto_disable_switch);
  add_setting_row(grid, 6, "Disable auto delivery", auto_delivery_switch);
  add_setting_row(grid, 7, "Disable multi delivery", multi_delivery_switch);

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget* save_button = gtk_button_new_with_label("Save");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), save_button);

  gtk_box_append(GTK_BOX(root), grid);
  gtk_box_append(GTK_BOX(root), actions);
  gtk_window_set_child(GTK_WINDOW(dialog), root);

  AutoDeliveryDialog* dialog_data = (AutoDeliveryDialog*)g_new0(
      AutoDeliveryDialog, 1);
  dialog_data->context = context;
  dialog_data->dialog = dialog;
  dialog_data->lot_entry = lot_entry;
  dialog_data->response_entry = response_entry;
  dialog_data->products_entry = products_entry;
  dialog_data->disable_switch = disable_switch;
  dialog_data->restore_switch = restore_switch;
  dialog_data->auto_disable_switch = auto_disable_switch;
  dialog_data->auto_delivery_switch = auto_delivery_switch;
  dialog_data->multi_delivery_switch = multi_delivery_switch;
  dialog_data->original_section = section_name ? g_strdup(section_name) : NULL;

  if (section_name) {
    gchar* path = g_build_filename(
        context->config_dir,
        "auto_delivery.cfg",
        NULL);
    fpv_ini_error_t error;
    fpv_ini_t* ini = fpv_ini_load(path, &error);
    if (ini) {
      const char* response = fpv_ini_get(ini, section_name, "response");
      const char* products = fpv_ini_get(ini, section_name, "productsFileName");
      const char* disable = fpv_ini_get(ini, section_name, "disable");
      const char* restore = fpv_ini_get(ini, section_name, "disableAutoRestore");
      const char* auto_disable = fpv_ini_get(ini, section_name, "disableAutoDisable");
      const char* auto_delivery = fpv_ini_get(ini, section_name, "disableAutoDelivery");
      const char* multi_delivery = fpv_ini_get(ini, section_name, "disableMultiDelivery");

      gtk_editable_set_text(GTK_EDITABLE(lot_entry), section_name);
      gtk_editable_set_text(
          GTK_EDITABLE(response_entry),
          response ? response : "");
      gtk_editable_set_text(
          GTK_EDITABLE(products_entry),
          products ? products : "");
      gtk_switch_set_active(
          GTK_SWITCH(disable_switch),
          parse_ini_bool(disable, FALSE));
      gtk_switch_set_active(
          GTK_SWITCH(restore_switch),
          parse_ini_bool(restore, FALSE));
      gtk_switch_set_active(
          GTK_SWITCH(auto_disable_switch),
          parse_ini_bool(auto_disable, FALSE));
      gtk_switch_set_active(
          GTK_SWITCH(auto_delivery_switch),
          parse_ini_bool(auto_delivery, FALSE));
      gtk_switch_set_active(
          GTK_SWITCH(multi_delivery_switch),
          parse_ini_bool(multi_delivery, FALSE));
      fpv_ini_destroy(ini);
    }
    g_free(path);
  }

  g_signal_connect_swapped(
      cancel_button,
      "clicked",
      G_CALLBACK(gtk_window_close),
      dialog);
  g_signal_connect(
      save_button,
      "clicked",
      G_CALLBACK(auto_delivery_dialog_save),
      dialog_data);
  g_signal_connect(
      dialog,
      "close-request",
      G_CALLBACK(auto_delivery_dialog_close),
      dialog_data);

  gtk_window_present(GTK_WINDOW(dialog));
}

static void on_auto_delivery_add(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_auto_delivery_dialog(context, NULL);
}

static void on_auto_delivery_edit(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkListBoxRow* row = gtk_list_box_get_selected_row(
      GTK_LIST_BOX(context->auto_delivery_list));
  if (!row) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Delivery",
        "Select a lot to edit.");
    return;
  }
  const gchar* section = (const gchar*)g_object_get_data(
      G_OBJECT(row), "section");
  if (section) {
    show_auto_delivery_dialog(context, section);
  }
}

static void on_auto_delivery_delete(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkListBoxRow* row = gtk_list_box_get_selected_row(
      GTK_LIST_BOX(context->auto_delivery_list));
  if (!row) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Delivery",
        "Select a lot to delete.");
    return;
  }
  const gchar* section = (const gchar*)g_object_get_data(
      G_OBJECT(row), "section");
  if (!section) {
    return;
  }

  gchar* path = g_build_filename(context->config_dir, "auto_delivery.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Delivery",
        "Failed to load auto_delivery.cfg.");
    g_free(path);
    return;
  }
  fpv_ini_remove_section(ini, section);
  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);
  if (save_result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Delivery",
        "Failed to save auto_delivery.cfg.");
    return;
  }

  refresh_auto_delivery_list(context);
  if (context->running) {
    fpv_core_reload_auto_delivery(context->core);
  }
}

static void on_auto_delivery_reload(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  refresh_auto_delivery_list(context);
  if (context->running) {
    fpv_core_reload_auto_delivery(context->core);
  }
}

static void refresh_settings_from_file(AppContext* context) {
  if (!context || !context->config_dir) {
    return;
  }

  gchar* path = g_build_filename(context->config_dir, "_main.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    gtk_label_set_text(GTK_LABEL(context->settings_status),
                       "Settings not available.");
    g_free(path);
    return;
  }

  gtk_label_set_text(GTK_LABEL(context->settings_status), "");

  const char* value = NULL;
  gchar* safe = NULL;

  value = fpv_ini_get(ini, "FunPay", "golden_key");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.funpay_golden_key), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "FunPay", "user_agent");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.funpay_user_agent), safe);
  g_free(safe);

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_auto_raise),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoRaise"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_auto_response),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoResponse"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_auto_delivery),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoDelivery"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_multi_delivery),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "multiDelivery"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_auto_restore),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoRestore"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_auto_disable),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoDisable"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.funpay_old_msg_mode),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "oldMsgGetMode"), FALSE));

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.telegram_enabled),
      parse_ini_bool(fpv_ini_get(ini, "Telegram", "enabled"), FALSE));

  value = fpv_ini_get(ini, "Telegram", "token");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.telegram_token), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "Telegram", "secretKey");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.telegram_secret), safe);
  g_free(safe);

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.block_delivery),
      parse_ini_bool(fpv_ini_get(ini, "BlockList", "blockDelivery"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.block_response),
      parse_ini_bool(fpv_ini_get(ini, "BlockList", "blockResponse"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.block_new_message),
      parse_ini_bool(
          fpv_ini_get(ini, "BlockList", "blockNewMessageNotification"),
          FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.block_new_order),
      parse_ini_bool(
          fpv_ini_get(ini, "BlockList", "blockNewOrderNotification"),
          FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.block_command),
      parse_ini_bool(
          fpv_ini_get(ini, "BlockList", "blockCommandNotification"),
          FALSE));

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.include_my),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "includeMyMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.include_fp),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "includeFPMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.include_bot),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "includeBotMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.notify_only_my),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "notifyOnlyMyMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.notify_only_fp),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "notifyOnlyFPMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.notify_only_bot),
      parse_ini_bool(fpv_ini_get(ini, "NewMessageView", "notifyOnlyBotMessages"), FALSE));

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.greetings_cache_init),
      parse_ini_bool(fpv_ini_get(ini, "Greetings", "cacheInitChats"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.greetings_ignore_system),
      parse_ini_bool(fpv_ini_get(ini, "Greetings", "ignoreSystemMessages"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.greetings_send),
      parse_ini_bool(fpv_ini_get(ini, "Greetings", "sendGreetings"), FALSE));

  value = fpv_ini_get(ini, "Greetings", "greetingsText");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.greetings_text), safe);
  g_free(safe);

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.order_confirm_send),
      parse_ini_bool(fpv_ini_get(ini, "OrderConfirm", "sendReply"), FALSE));

  value = fpv_ini_get(ini, "OrderConfirm", "replyText");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.order_confirm_text), safe);
  g_free(safe);

  const char* review_reply_master = fpv_ini_get(ini, "ReviewReply", "enabled");
  gboolean review_reply_master_active =
      parse_ini_bool(review_reply_master, FALSE);
  gboolean review_reply_master_present =
      review_reply_master && review_reply_master[0];
  gboolean review_reply_any_enabled = FALSE;

  for (size_t i = 0; i < 5; i++) {
    char key_enabled[32];
    char key_text[32];
    snprintf(key_enabled, sizeof(key_enabled), "star%zuReply", i + 1);
    snprintf(key_text, sizeof(key_text), "star%zuReplyText", i + 1);
    gboolean star_enabled = parse_ini_bool(
        fpv_ini_get(ini, "ReviewReply", key_enabled),
        FALSE);
    gtk_switch_set_active(
        GTK_SWITCH(context->settings.review_reply_enabled[i]),
        star_enabled);
    if (star_enabled) {
      review_reply_any_enabled = TRUE;
    }
    value = fpv_ini_get(ini, "ReviewReply", key_text);
    safe = sanitize_utf8(value ? value : "");
    gtk_editable_set_text(
        GTK_EDITABLE(context->settings.review_reply_texts[i]),
        safe);
    g_free(safe);
  }
  if (!review_reply_master_present) {
    review_reply_master_active = review_reply_any_enabled;
  }
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.review_reply_enabled_all),
      review_reply_master_active);

  gtk_switch_set_active(
      GTK_SWITCH(context->settings.proxy_enable),
      parse_ini_bool(fpv_ini_get(ini, "Proxy", "enable"), FALSE));
  gtk_switch_set_active(
      GTK_SWITCH(context->settings.proxy_check),
      parse_ini_bool(fpv_ini_get(ini, "Proxy", "check"), FALSE));

  value = fpv_ini_get(ini, "Proxy", "ip");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.proxy_ip), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "Proxy", "port");
  if (value && value[0]) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->settings.proxy_port),
        (double)g_ascii_strtoll(value, NULL, 10));
  } else {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(context->settings.proxy_port), 0.0);
  }

  value = fpv_ini_get(ini, "Proxy", "login");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.proxy_login), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "Proxy", "password");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.proxy_password), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "Other", "watermark");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.other_watermark), safe);
  g_free(safe);

  value = fpv_ini_get(ini, "Other", "requestsDelay");
  if (value && value[0]) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->settings.other_requests_delay),
        (double)g_ascii_strtoll(value, NULL, 10));
  } else {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->settings.other_requests_delay),
        4.0);
  }

  value = fpv_ini_get(ini, "Other", "language");
  safe = sanitize_utf8(value ? value : "");
  gtk_editable_set_text(GTK_EDITABLE(context->settings.other_language), safe);
  g_free(safe);

  fpv_ini_destroy(ini);
  g_free(path);
}

static void refresh_identity_labels(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->settings.org_name) {
    const char* name =
        context->current_org && context->current_org->name
            ? context->current_org->name
            : "Workspace not set";
    gtk_label_set_text(GTK_LABEL(context->settings.org_name), name);
  }
  if (context->settings.org_role) {
    gtk_label_set_text(
        GTK_LABEL(context->settings.org_role),
        role_label(context->current_role));
  }
  if (context->settings.org_account) {
    const char* label = "Not linked";
    fpv_account_t* account = NULL;
    if (context->identity && context->current_org) {
      if (fpv_identity_get_account_for_org(
              context->identity,
              context->current_org->id,
              &account) == FPV_OK &&
          account) {
        if (account->funpay_username && account->funpay_username[0]) {
          label = account->funpay_username;
        } else if (account->funpay_user_id && account->funpay_user_id[0]) {
          label = account->funpay_user_id;
        } else {
          label = "Linked";
        }
      }
    }
    gtk_label_set_text(GTK_LABEL(context->settings.org_account), label);
    fpv_account_destroy(account);
  }
  if (context->settings.org_invite_button) {
    gboolean can_invite = FALSE;
    if (context->identity && context->current_user && context->current_org) {
      can_invite = fpv_identity_user_has_role(
          context->identity,
          context->current_user->id,
          context->current_org->id,
          NULL,
          FPV_ROLE_ADMIN);
      if (!can_invite && context->current_team) {
        can_invite = fpv_identity_user_has_role(
            context->identity,
            context->current_user->id,
            context->current_org->id,
            context->current_team->id,
            FPV_ROLE_MANAGER);
      }
    }
    gtk_widget_set_sensitive(context->settings.org_invite_button, can_invite);
  }
  if (context->settings.org_link_button) {
    gboolean can_link = FALSE;
    if (context->identity && context->current_user && context->current_org) {
      can_link = fpv_identity_user_has_role(
          context->identity,
          context->current_user->id,
          context->current_org->id,
          NULL,
          FPV_ROLE_ADMIN);
    }
    gtk_widget_set_sensitive(context->settings.org_link_button, can_link);
  }
}

static void save_settings_to_file(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->config_dir) {
    return;
  }

  gchar* path = g_build_filename(context->config_dir, "_main.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    show_message_dialog(GTK_WINDOW(context->window),
                        "Settings",
                        "Failed to load settings file.");
    g_free(path);
    return;
  }

  const char* golden_key = gtk_editable_get_text(
      GTK_EDITABLE(context->settings.funpay_golden_key));
  if (!golden_key || !golden_key[0]) {
    show_message_dialog(GTK_WINDOW(context->window),
                        "Settings",
                        "Golden key is required.");
    fpv_ini_destroy(ini);
    g_free(path);
    return;
  }

  fpv_ini_set(ini, "FunPay", "golden_key", golden_key);
  fpv_ini_set(ini, "FunPay", "user_agent",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.funpay_user_agent)));
  fpv_ini_set(ini, "FunPay", "autoRaise",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_auto_raise)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "autoResponse",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_auto_response)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "autoDelivery",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_auto_delivery)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "multiDelivery",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_multi_delivery)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "autoRestore",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_auto_restore)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "autoDisable",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_auto_disable)) ? "1" : "0");
  fpv_ini_set(ini, "FunPay", "oldMsgGetMode",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.funpay_old_msg_mode)) ? "1" : "0");

  fpv_ini_set(ini, "Telegram", "enabled",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.telegram_enabled)) ? "1" : "0");
  fpv_ini_set(ini, "Telegram", "token",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.telegram_token)));
  fpv_ini_set(ini, "Telegram", "secretKey",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.telegram_secret)));

  fpv_ini_set(ini, "BlockList", "blockDelivery",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.block_delivery)) ? "1" : "0");
  fpv_ini_set(ini, "BlockList", "blockResponse",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.block_response)) ? "1" : "0");
  fpv_ini_set(ini, "BlockList", "blockNewMessageNotification",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.block_new_message)) ? "1" : "0");
  fpv_ini_set(ini, "BlockList", "blockNewOrderNotification",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.block_new_order)) ? "1" : "0");
  fpv_ini_set(ini, "BlockList", "blockCommandNotification",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.block_command)) ? "1" : "0");

  fpv_ini_set(ini, "NewMessageView", "includeMyMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.include_my)) ? "1" : "0");
  fpv_ini_set(ini, "NewMessageView", "includeFPMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.include_fp)) ? "1" : "0");
  fpv_ini_set(ini, "NewMessageView", "includeBotMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.include_bot)) ? "1" : "0");
  fpv_ini_set(ini, "NewMessageView", "notifyOnlyMyMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.notify_only_my)) ? "1" : "0");
  fpv_ini_set(ini, "NewMessageView", "notifyOnlyFPMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.notify_only_fp)) ? "1" : "0");
  fpv_ini_set(ini, "NewMessageView", "notifyOnlyBotMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.notify_only_bot)) ? "1" : "0");

  fpv_ini_set(ini, "Greetings", "cacheInitChats",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.greetings_cache_init)) ? "1" : "0");
  fpv_ini_set(ini, "Greetings", "ignoreSystemMessages",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.greetings_ignore_system)) ? "1" : "0");
  fpv_ini_set(ini, "Greetings", "sendGreetings",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.greetings_send)) ? "1" : "0");
  fpv_ini_set(ini, "Greetings", "greetingsText",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.greetings_text)));

  fpv_ini_set(ini, "OrderConfirm", "sendReply",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.order_confirm_send)) ? "1" : "0");
  fpv_ini_set(ini, "OrderConfirm", "replyText",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.order_confirm_text)));

  fpv_ini_set(ini, "ReviewReply", "enabled",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.review_reply_enabled_all)) ? "1" : "0");
  for (size_t i = 0; i < 5; i++) {
    char key_enabled[32];
    char key_text[32];
    snprintf(key_enabled, sizeof(key_enabled), "star%zuReply", i + 1);
    snprintf(key_text, sizeof(key_text), "star%zuReplyText", i + 1);
    fpv_ini_set(ini, "ReviewReply", key_enabled,
                gtk_switch_get_active(
                    GTK_SWITCH(context->settings.review_reply_enabled[i])) ? "1" : "0");
    fpv_ini_set(ini, "ReviewReply", key_text,
                gtk_editable_get_text(GTK_EDITABLE(
                    context->settings.review_reply_texts[i])));
  }

  fpv_ini_set(ini, "Proxy", "enable",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.proxy_enable)) ? "1" : "0");
  fpv_ini_set(ini, "Proxy", "check",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.proxy_check)) ? "1" : "0");
  fpv_ini_set(ini, "Proxy", "ip",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.proxy_ip)));
  gchar* port_text = g_strdup_printf(
      "%d",
      gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->settings.proxy_port)));
  fpv_ini_set(ini, "Proxy", "port", port_text);
  g_free(port_text);
  fpv_ini_set(ini, "Proxy", "login",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.proxy_login)));
  fpv_ini_set(ini, "Proxy", "password",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.proxy_password)));

  fpv_ini_set(ini, "Other", "watermark",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.other_watermark)));
  gchar* delay_text = g_strdup_printf(
      "%d",
      gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->settings.other_requests_delay)));
  fpv_ini_set(ini, "Other", "requestsDelay", delay_text);
  g_free(delay_text);
  fpv_ini_set(ini, "Other", "language",
              gtk_editable_get_text(GTK_EDITABLE(
                  context->settings.other_language)));

  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);

  if (save_result != FPV_OK) {
    show_message_dialog(GTK_WINDOW(context->window),
                        "Settings",
                        "Failed to save settings file.");
    return;
  }

  if (context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Settings",
        "Settings saved. Stop and start the core to apply changes.");
  } else {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Settings",
        "Settings saved.");
  }
}

static GtkWidget* build_metric_card(const char* title, GtkWidget** value_out) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(box, "metric");

  GtkWidget* title_label = gtk_label_new(title);
  gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
  gtk_widget_add_css_class(title_label, "muted");

  GtkWidget* value_label = gtk_label_new("0");
  gtk_label_set_xalign(GTK_LABEL(value_label), 0.0f);
  gtk_widget_add_css_class(value_label, "metric-value");

  gtk_box_append(GTK_BOX(box), title_label);
  gtk_box_append(GTK_BOX(box), value_label);

  if (value_out) {
    *value_out = value_label;
  }

  return box;
}

static GtkWidget* build_dashboard_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

  GtkWidget* title = gtk_label_new("Dashboard");
  gtk_widget_add_css_class(title, "page-title");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  GtkWidget* metrics = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(metrics), 12);
  gtk_grid_set_row_spacing(GTK_GRID(metrics), 12);

  GtkWidget* chat_card = build_metric_card("Chats", &context->metrics_chats);
  GtkWidget* order_card = build_metric_card("Orders", &context->metrics_orders);
  GtkWidget* message_card = build_metric_card("Messages", &context->metrics_messages);
  GtkWidget* lot_card = build_metric_card("Lots", &context->metrics_lots);
  GtkWidget* plugin_card = build_metric_card("Plugins", &context->metrics_plugins);

  gtk_grid_attach(GTK_GRID(metrics), chat_card, 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(metrics), order_card, 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(metrics), message_card, 2, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(metrics), lot_card, 0, 1, 1, 1);
  gtk_grid_attach(GTK_GRID(metrics), plugin_card, 1, 1, 1, 1);

  GtkWidget* log_title = gtk_label_new("Activity");
  gtk_label_set_xalign(GTK_LABEL(log_title), 0.0f);
  gtk_widget_add_css_class(log_title, "page-title");

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->log_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->log_list),
      GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->log_list);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), metrics);
  gtk_box_append(GTK_BOX(root), log_title);
  gtk_box_append(GTK_BOX(root), scroller);

  return root;
}

static GtkWidget* build_messages_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

  GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  GtkWidget* left_title = gtk_label_new("Chats");
  gtk_label_set_xalign(GTK_LABEL(left_title), 0.0f);
  gtk_widget_add_css_class(left_title, "page-title");

  GtkWidget* chat_scroller = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(chat_scroller, TRUE);
  gtk_widget_set_vexpand(chat_scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(chat_scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  // install_slow_scrolling(chat_scroller);

  context->chat_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->chat_list),
      GTK_SELECTION_SINGLE);
  g_signal_connect(
      context->chat_list,
      "row-selected",
      G_CALLBACK(on_chat_selected),
      context);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(chat_scroller),
      context->chat_list);

  gtk_box_append(GTK_BOX(left), left_title);
  gtk_box_append(GTK_BOX(left), chat_scroller);
  gtk_widget_set_size_request(left, 260, -1);

  GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  context->message_header = gtk_label_new("Messages");
  gtk_label_set_xalign(GTK_LABEL(context->message_header), 0.0f);
  gtk_widget_add_css_class(context->message_header, "page-title");

  GtkWidget* message_scroller = gtk_scrolled_window_new();
  gtk_widget_set_hexpand(message_scroller, TRUE);
  gtk_widget_set_vexpand(message_scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(message_scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  // install_slow_scrolling(message_scroller);

  context->message_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->message_list),
      GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(message_scroller),
      context->message_list);

  GtkWidget* input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  context->message_entry = gtk_entry_new();
  gtk_widget_set_hexpand(context->message_entry, TRUE);
  g_signal_connect(
      context->message_entry,
      "activate",
      G_CALLBACK(on_message_entry_activate),
      context);

  context->message_send_button = gtk_button_new_with_label("Send");
  g_signal_connect(
      context->message_send_button,
      "clicked",
      G_CALLBACK(on_send_message),
      context);

  gtk_box_append(GTK_BOX(input_box), context->message_entry);
  gtk_box_append(GTK_BOX(input_box), context->message_send_button);

  gtk_box_append(GTK_BOX(right), context->message_header);
  gtk_box_append(GTK_BOX(right), message_scroller);
  gtk_box_append(GTK_BOX(right), input_box);

  gtk_box_append(GTK_BOX(root), left);
  gtk_box_append(GTK_BOX(root), right);

  return root;
}

static GtkWidget* build_orders_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* title = gtk_label_new("Orders");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->order_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->order_list),
      GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->order_list);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), scroller);
  return root;
}

static GtkWidget* build_lots_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Lots");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  context->lot_refresh_button = gtk_button_new_with_label("Refresh");
  g_signal_connect(
      context->lot_refresh_button,
      "clicked",
      G_CALLBACK(on_refresh_lots),
      context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), context->lot_refresh_button);

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->lot_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->lot_list),
      GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->lot_list);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), scroller);
  return root;
}

static GtkWidget* build_plugins_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* title = gtk_label_new("Plugins");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->plugin_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->plugin_list),
      GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->plugin_list);

  gtk_box_append(GTK_BOX(root), title);
  gtk_box_append(GTK_BOX(root), scroller);
  return root;
}

static GtkWidget* build_auto_response_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Auto Response");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget* add_button = gtk_button_new_with_label("Add");
  GtkWidget* edit_button = gtk_button_new_with_label("Edit");
  GtkWidget* delete_button = gtk_button_new_with_label("Delete");
  GtkWidget* reload_button = gtk_button_new_with_label("Reload");

  g_signal_connect(add_button, "clicked", G_CALLBACK(on_auto_response_add), context);
  g_signal_connect(edit_button, "clicked", G_CALLBACK(on_auto_response_edit), context);
  g_signal_connect(delete_button, "clicked", G_CALLBACK(on_auto_response_delete), context);
  g_signal_connect(reload_button, "clicked", G_CALLBACK(on_auto_response_reload), context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), add_button);
  gtk_box_append(GTK_BOX(header), edit_button);
  gtk_box_append(GTK_BOX(header), delete_button);
  gtk_box_append(GTK_BOX(header), reload_button);

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->auto_response_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->auto_response_list),
      GTK_SELECTION_SINGLE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->auto_response_list);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), scroller);
  return root;
}

static GtkWidget* build_auto_delivery_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Auto Delivery");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget* add_button = gtk_button_new_with_label("Add");
  GtkWidget* edit_button = gtk_button_new_with_label("Edit");
  GtkWidget* delete_button = gtk_button_new_with_label("Delete");
  GtkWidget* reload_button = gtk_button_new_with_label("Reload");

  g_signal_connect(add_button, "clicked", G_CALLBACK(on_auto_delivery_add), context);
  g_signal_connect(edit_button, "clicked", G_CALLBACK(on_auto_delivery_edit), context);
  g_signal_connect(delete_button, "clicked", G_CALLBACK(on_auto_delivery_delete), context);
  g_signal_connect(reload_button, "clicked", G_CALLBACK(on_auto_delivery_reload), context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), add_button);
  gtk_box_append(GTK_BOX(header), edit_button);
  gtk_box_append(GTK_BOX(header), delete_button);
  gtk_box_append(GTK_BOX(header), reload_button);

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  context->auto_delivery_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->auto_delivery_list),
      GTK_SELECTION_SINGLE);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(scroller),
      context->auto_delivery_list);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), scroller);
  return root;
}

static void add_setting_row(
    GtkWidget* grid,
    int row,
    const char* label_text,
    GtkWidget* widget) {
  GtkWidget* label = gtk_label_new(label_text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_widget_set_margin_bottom(label, 4);
  gtk_widget_set_margin_top(label, 4);
  gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
  if (GTK_IS_SWITCH(widget)) {
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_hexpand(widget, FALSE);
  } else if (GTK_IS_ENTRY(widget) || GTK_IS_SPIN_BUTTON(widget)) {
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(widget, TRUE);
  } else {
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(widget, TRUE);
  }
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
}

static void add_setting_row_label(
    GtkWidget* grid,
    int row,
    GtkWidget* label,
    GtkWidget* widget) {
  if (!label || !widget) {
    return;
  }
  if (GTK_IS_LABEL(label)) {
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  }
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_bottom(label, 4);
  gtk_widget_set_margin_top(label, 4);

  gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
  if (GTK_IS_SWITCH(widget)) {
    gtk_widget_set_halign(widget, GTK_ALIGN_END);
    gtk_widget_set_hexpand(widget, FALSE);
  } else if (GTK_IS_ENTRY(widget) || GTK_IS_SPIN_BUTTON(widget)) {
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(widget, TRUE);
  } else {
    gtk_widget_set_halign(widget, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(widget, TRUE);
  }

  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
}

static GtkWidget* build_star_label_widget(size_t count, const char* suffix) {
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GString* stars = g_string_new(NULL);
  if (count == 0) {
    g_string_append(stars, "★");
  } else {
    for (size_t i = 0; i < count; i++) {
      if (i > 0) {
        g_string_append_c(stars, ' ');
      }
      g_string_append(stars, "★");
    }
  }
  GtkWidget* star_label = gtk_label_new(stars->str);
  gtk_widget_add_css_class(star_label, "rating-star");
  gtk_box_append(GTK_BOX(box), star_label);
  g_string_free(stars, TRUE);

  if (suffix && suffix[0]) {
    GtkWidget* suffix_label = gtk_label_new(suffix);
    gtk_label_set_xalign(GTK_LABEL(suffix_label), 0.0f);
    gtk_box_append(GTK_BOX(box), suffix_label);
  }
  return box;
}

static void set_settings_grid_margins(GtkWidget* grid) {
  if (!grid) {
    return;
  }
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);
  gtk_widget_set_margin_start(grid, 12);
  gtk_widget_set_margin_end(grid, 12);
}

static GtkWidget* build_settings_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Settings");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget* save_button = gtk_button_new_with_label("Save");
  g_signal_connect(
      save_button,
      "clicked",
      G_CALLBACK(save_settings_to_file),
      context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), save_button);

  context->settings_status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(context->settings_status), 0.0f);
  gtk_widget_add_css_class(context->settings_status, "muted");

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroller, TRUE);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  install_slow_scrolling(scroller);

  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_start(content, 12);
  gtk_widget_set_margin_end(content, 12);
  gtk_widget_set_margin_bottom(content, 12);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), content);

  GtkWidget* org_frame = gtk_frame_new("Organization");
  GtkWidget* org_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(org_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(org_grid), 12);
  set_settings_grid_margins(org_grid);
  gtk_frame_set_child(GTK_FRAME(org_frame), org_grid);

  context->settings.org_name = gtk_label_new("");
  context->settings.org_role = gtk_label_new("");
  context->settings.org_account = gtk_label_new("");
  context->settings.org_invite_button = gtk_button_new_with_label("Invite member");
  context->settings.org_link_button = gtk_button_new_with_label("Link FunPay account");

  gtk_label_set_xalign(GTK_LABEL(context->settings.org_name), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(context->settings.org_role), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(context->settings.org_account), 0.0f);
  gtk_widget_add_css_class(context->settings.org_name, "muted");
  gtk_widget_add_css_class(context->settings.org_role, "muted");
  gtk_widget_add_css_class(context->settings.org_account, "muted");

  GtkWidget* org_name_label = gtk_label_new("Workspace");
  GtkWidget* org_role_label = gtk_label_new("Role");
  GtkWidget* org_account_label = gtk_label_new("Linked account");
  GtkWidget* org_invite_label = gtk_label_new("Invite");
  GtkWidget* org_link_label = gtk_label_new("Link");

  add_setting_row_label(org_grid, 0, org_name_label, context->settings.org_name);
  add_setting_row_label(org_grid, 1, org_role_label, context->settings.org_role);
  add_setting_row_label(org_grid, 2, org_account_label, context->settings.org_account);
  add_setting_row_label(org_grid, 3, org_invite_label, context->settings.org_invite_button);
  add_setting_row_label(org_grid, 4, org_link_label, context->settings.org_link_button);

  g_signal_connect(
      context->settings.org_invite_button,
      "clicked",
      G_CALLBACK(on_invite_open),
      context);
  g_signal_connect(
      context->settings.org_link_button,
      "clicked",
      G_CALLBACK(on_link_open),
      context);

  GtkWidget* funpay_frame = gtk_frame_new("FunPay");
  GtkWidget* funpay_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(funpay_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(funpay_grid), 12);
  set_settings_grid_margins(funpay_grid);
  gtk_frame_set_child(GTK_FRAME(funpay_frame), funpay_grid);

  context->settings.funpay_golden_key = gtk_entry_new();
  context->settings.funpay_user_agent = gtk_entry_new();
  context->settings.funpay_auto_raise = gtk_switch_new();
  context->settings.funpay_auto_response = gtk_switch_new();
  context->settings.funpay_auto_delivery = gtk_switch_new();
  context->settings.funpay_multi_delivery = gtk_switch_new();
  context->settings.funpay_auto_restore = gtk_switch_new();
  context->settings.funpay_auto_disable = gtk_switch_new();
  context->settings.funpay_old_msg_mode = gtk_switch_new();

  add_setting_row(funpay_grid, 0, "Golden key", context->settings.funpay_golden_key);
  add_setting_row(funpay_grid, 1, "User agent", context->settings.funpay_user_agent);
  add_setting_row(funpay_grid, 2, "Auto raise", context->settings.funpay_auto_raise);
  add_setting_row(funpay_grid, 3, "Auto response", context->settings.funpay_auto_response);
  add_setting_row(funpay_grid, 4, "Auto delivery", context->settings.funpay_auto_delivery);
  add_setting_row(funpay_grid, 5, "Multi delivery", context->settings.funpay_multi_delivery);
  add_setting_row(funpay_grid, 6, "Auto restore", context->settings.funpay_auto_restore);
  add_setting_row(funpay_grid, 7, "Auto disable", context->settings.funpay_auto_disable);
  add_setting_row(funpay_grid, 8, "Old message mode", context->settings.funpay_old_msg_mode);

  GtkWidget* telegram_frame = gtk_frame_new("Telegram");
  GtkWidget* telegram_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* telegram_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(telegram_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(telegram_grid), 12);
  set_settings_grid_margins(telegram_grid);
  gtk_widget_set_margin_bottom(telegram_grid, 0);

  GtkWidget* telegram_details_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(telegram_details_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(telegram_details_grid), 12);
  set_settings_grid_margins(telegram_details_grid);
  gtk_widget_set_margin_top(telegram_details_grid, 0);

  GtkWidget* telegram_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(
      GTK_REVEALER(telegram_revealer),
      GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_transition_duration(
      GTK_REVEALER(telegram_revealer),
      200);
  gtk_revealer_set_reveal_child(GTK_REVEALER(telegram_revealer), FALSE);
  gtk_revealer_set_child(
      GTK_REVEALER(telegram_revealer),
      telegram_details_grid);

  gtk_box_append(GTK_BOX(telegram_box), telegram_grid);
  gtk_box_append(GTK_BOX(telegram_box), telegram_revealer);
  gtk_frame_set_child(GTK_FRAME(telegram_frame), telegram_box);

  context->settings.telegram_enabled = gtk_switch_new();
  context->settings.telegram_token = gtk_entry_new();
  context->settings.telegram_secret = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(context->settings.telegram_secret), FALSE);

  add_setting_row(telegram_grid, 0, "Enabled", context->settings.telegram_enabled);
  add_setting_row(telegram_details_grid, 0, "Token", context->settings.telegram_token);
  add_setting_row(telegram_details_grid, 1, "Secret key", context->settings.telegram_secret);
  g_signal_connect(
      context->settings.telegram_enabled,
      "notify::active",
      G_CALLBACK(on_toggle_revealer),
      telegram_revealer);

  GtkWidget* block_frame = gtk_frame_new("Block List");
  GtkWidget* block_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(block_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(block_grid), 12);
  set_settings_grid_margins(block_grid);
  gtk_frame_set_child(GTK_FRAME(block_frame), block_grid);

  context->settings.block_delivery = gtk_switch_new();
  context->settings.block_response = gtk_switch_new();
  context->settings.block_new_message = gtk_switch_new();
  context->settings.block_new_order = gtk_switch_new();
  context->settings.block_command = gtk_switch_new();

  add_setting_row(block_grid, 0, "Block delivery", context->settings.block_delivery);
  add_setting_row(block_grid, 1, "Block response", context->settings.block_response);
  add_setting_row(block_grid, 2, "Block new message notification",
                  context->settings.block_new_message);
  add_setting_row(block_grid, 3, "Block new order notification",
                  context->settings.block_new_order);
  add_setting_row(block_grid, 4, "Block command notification",
                  context->settings.block_command);

  GtkWidget* view_frame = gtk_frame_new("New Message View");
  GtkWidget* view_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(view_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(view_grid), 12);
  set_settings_grid_margins(view_grid);
  gtk_frame_set_child(GTK_FRAME(view_frame), view_grid);

  context->settings.include_my = gtk_switch_new();
  context->settings.include_fp = gtk_switch_new();
  context->settings.include_bot = gtk_switch_new();
  context->settings.notify_only_my = gtk_switch_new();
  context->settings.notify_only_fp = gtk_switch_new();
  context->settings.notify_only_bot = gtk_switch_new();

  add_setting_row(view_grid, 0, "Include my messages", context->settings.include_my);
  add_setting_row(view_grid, 1, "Include FunPay messages", context->settings.include_fp);
  add_setting_row(view_grid, 2, "Include bot messages", context->settings.include_bot);
  add_setting_row(view_grid, 3, "Notify only my messages", context->settings.notify_only_my);
  add_setting_row(view_grid, 4, "Notify only FunPay messages", context->settings.notify_only_fp);
  add_setting_row(view_grid, 5, "Notify only bot messages", context->settings.notify_only_bot);

  GtkWidget* greet_frame = gtk_frame_new("Greetings");
  GtkWidget* greet_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(greet_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(greet_grid), 12);
  set_settings_grid_margins(greet_grid);
  gtk_frame_set_child(GTK_FRAME(greet_frame), greet_grid);

  context->settings.greetings_cache_init = gtk_switch_new();
  context->settings.greetings_ignore_system = gtk_switch_new();
  context->settings.greetings_send = gtk_switch_new();
  context->settings.greetings_text = gtk_entry_new();

  add_setting_row(greet_grid, 0, "Cache init chats", context->settings.greetings_cache_init);
  add_setting_row(greet_grid, 1, "Ignore system messages",
                  context->settings.greetings_ignore_system);
  add_setting_row(greet_grid, 2, "Send greetings", context->settings.greetings_send);
  add_setting_row(greet_grid, 3, "Greetings text", context->settings.greetings_text);

  GtkWidget* order_frame = gtk_frame_new("Order Confirm");
  GtkWidget* order_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(order_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(order_grid), 12);
  set_settings_grid_margins(order_grid);
  gtk_frame_set_child(GTK_FRAME(order_frame), order_grid);

  context->settings.order_confirm_send = gtk_switch_new();
  context->settings.order_confirm_text = gtk_entry_new();

  add_setting_row(order_grid, 0, "Send reply", context->settings.order_confirm_send);
  add_setting_row(order_grid, 1, "Reply text", context->settings.order_confirm_text);

  GtkWidget* review_frame = gtk_frame_new("Review Reply");
  GtkWidget* review_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* review_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(review_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(review_grid), 12);
  set_settings_grid_margins(review_grid);
  gtk_widget_set_margin_bottom(review_grid, 0);

  GtkWidget* review_details_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(review_details_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(review_details_grid), 12);
  set_settings_grid_margins(review_details_grid);
  gtk_widget_set_margin_top(review_details_grid, 0);

  GtkWidget* review_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(
      GTK_REVEALER(review_revealer),
      GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_transition_duration(
      GTK_REVEALER(review_revealer),
      200);
  gtk_revealer_set_reveal_child(GTK_REVEALER(review_revealer), FALSE);
  gtk_revealer_set_child(
      GTK_REVEALER(review_revealer),
      review_details_grid);

  gtk_box_append(GTK_BOX(review_box), review_grid);
  gtk_box_append(GTK_BOX(review_box), review_revealer);
  gtk_frame_set_child(GTK_FRAME(review_frame), review_box);

  context->settings.review_reply_enabled_all = gtk_switch_new();
  add_setting_row(review_grid, 0, "Review replies enabled",
                  context->settings.review_reply_enabled_all);
  g_signal_connect(
      context->settings.review_reply_enabled_all,
      "notify::active",
      G_CALLBACK(on_toggle_revealer),
      review_revealer);

  for (size_t i = 0; i < 5; i++) {
    context->settings.review_reply_enabled[i] = gtk_switch_new();
    context->settings.review_reply_texts[i] = gtk_entry_new();
    GtkWidget* label = build_star_label_widget(i + 1, NULL);
    add_setting_row_label(review_details_grid, (int)i * 2, label,
                          context->settings.review_reply_enabled[i]);
    label = build_star_label_widget(i + 1, "Reply text");
    add_setting_row_label(review_details_grid, (int)i * 2 + 1, label,
                          context->settings.review_reply_texts[i]);
  }

  GtkWidget* proxy_frame = gtk_frame_new("Proxy");
  GtkWidget* proxy_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* proxy_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(proxy_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(proxy_grid), 12);
  set_settings_grid_margins(proxy_grid);
  gtk_widget_set_margin_bottom(proxy_grid, 0);

  GtkWidget* proxy_details_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(proxy_details_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(proxy_details_grid), 12);
  set_settings_grid_margins(proxy_details_grid);
  gtk_widget_set_margin_top(proxy_details_grid, 0);

  GtkWidget* proxy_revealer = gtk_revealer_new();
  gtk_revealer_set_transition_type(
      GTK_REVEALER(proxy_revealer),
      GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
  gtk_revealer_set_transition_duration(
      GTK_REVEALER(proxy_revealer),
      200);
  gtk_revealer_set_reveal_child(GTK_REVEALER(proxy_revealer), FALSE);
  gtk_revealer_set_child(
      GTK_REVEALER(proxy_revealer),
      proxy_details_grid);

  gtk_box_append(GTK_BOX(proxy_box), proxy_grid);
  gtk_box_append(GTK_BOX(proxy_box), proxy_revealer);
  gtk_frame_set_child(GTK_FRAME(proxy_frame), proxy_box);

  context->settings.proxy_enable = gtk_switch_new();
  context->settings.proxy_check = gtk_switch_new();
  context->settings.proxy_ip = gtk_entry_new();
  context->settings.proxy_port = gtk_spin_button_new_with_range(0, 65535, 1);
  context->settings.proxy_login = gtk_entry_new();
  context->settings.proxy_password = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(context->settings.proxy_password), FALSE);

  add_setting_row(proxy_grid, 0, "Enable", context->settings.proxy_enable);
  add_setting_row(proxy_details_grid, 0, "Check", context->settings.proxy_check);
  add_setting_row(proxy_details_grid, 1, "IP", context->settings.proxy_ip);
  add_setting_row(proxy_details_grid, 2, "Port", context->settings.proxy_port);
  add_setting_row(proxy_details_grid, 3, "Login", context->settings.proxy_login);
  add_setting_row(proxy_details_grid, 4, "Password", context->settings.proxy_password);
  g_signal_connect(
      context->settings.proxy_enable,
      "notify::active",
      G_CALLBACK(on_toggle_revealer),
      proxy_revealer);

  GtkWidget* other_frame = gtk_frame_new("Other");
  GtkWidget* other_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(other_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(other_grid), 12);
  set_settings_grid_margins(other_grid);
  gtk_frame_set_child(GTK_FRAME(other_frame), other_grid);

  context->settings.other_watermark = gtk_entry_new();
  context->settings.other_requests_delay = gtk_spin_button_new_with_range(1, 100, 1);
  context->settings.other_language = gtk_entry_new();

  add_setting_row(other_grid, 0, "Watermark", context->settings.other_watermark);
  add_setting_row(other_grid, 1, "Requests delay (s)",
                  context->settings.other_requests_delay);
  add_setting_row(other_grid, 2, "Language", context->settings.other_language);

  gtk_box_append(GTK_BOX(content), org_frame);
  gtk_box_append(GTK_BOX(content), funpay_frame);
  gtk_box_append(GTK_BOX(content), telegram_frame);
  gtk_box_append(GTK_BOX(content), block_frame);
  gtk_box_append(GTK_BOX(content), view_frame);
  gtk_box_append(GTK_BOX(content), greet_frame);
  gtk_box_append(GTK_BOX(content), order_frame);
  gtk_box_append(GTK_BOX(content), review_frame);
  gtk_box_append(GTK_BOX(content), proxy_frame);
  gtk_box_append(GTK_BOX(content), other_frame);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), context->settings_status);
  gtk_box_append(GTK_BOX(root), scroller);

  return root;
}

static GtkWidget* build_main_layout(AppContext* context) {
  GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget* title = gtk_label_new("FunPay Vertex");
  gtk_widget_add_css_class(title, "status");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

  context->status_label = gtk_label_new("Stopped");
  gtk_widget_add_css_class(context->status_label, "status");
  gtk_label_set_xalign(GTK_LABEL(context->status_label), 0.0f);
  gtk_widget_set_hexpand(context->status_label, TRUE);

  context->start_button = gtk_button_new_with_label("Start");
  context->stop_button = gtk_button_new_with_label("Stop");
  gtk_widget_add_css_class(context->start_button, "suggested-action");
  gtk_widget_add_css_class(context->stop_button, "destructive-action");

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), context->status_label);
  gtk_box_append(GTK_BOX(header), context->start_button);
  gtk_box_append(GTK_BOX(header), context->stop_button);

  GtkWidget* content_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

  GtkWidget* stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
  gtk_stack_set_transition_duration(GTK_STACK(stack), 200);

  GtkWidget* sidebar = gtk_stack_sidebar_new();
  gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
  gtk_widget_set_size_request(sidebar, 180, -1);

  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_dashboard_page(context),
      "dashboard",
      "Dashboard");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_messages_page(context),
      "messages",
      "Messages");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_orders_page(context),
      "orders",
      "Orders");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_lots_page(context),
      "lots",
      "Lots");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_auto_delivery_page(context),
      "auto_delivery",
      "Auto Delivery");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_auto_response_page(context),
      "auto_response",
      "Auto Response");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_plugins_page(context),
      "plugins",
      "Plugins");
  gtk_stack_add_titled(
      GTK_STACK(stack),
      build_settings_page(context),
      "settings",
      "Settings");

  gtk_box_append(GTK_BOX(content_box), sidebar);
  gtk_box_append(GTK_BOX(content_box), stack);

  gtk_box_append(GTK_BOX(main_box), header);
  gtk_box_append(GTK_BOX(main_box), content_box);

  g_signal_connect(
      context->start_button,
      "clicked",
      G_CALLBACK(start_core),
      context);
  g_signal_connect(
      context->stop_button,
      "clicked",
      G_CALLBACK(stop_core),
      context);

  return main_box;
}

static void app_context_destroy(gpointer data) {
  AppContext* context = (AppContext*)data;
  if (!context) {
    return;
  }

  if (context->poll_id != 0) {
    g_source_remove(context->poll_id);
    context->poll_id = 0;
  }

  if (context->core && context->running) {
    fpv_core_stop(context->core, (uint64_t)(g_get_real_time() / 1000ULL));
  }

  fpv_core_destroy(context->core);
  fpv_event_bus_destroy(context->bus);
  fpv_identity_store_destroy(context->identity);
  fpv_chat_store_destroy(context->chat_store);
  fpv_user_destroy(context->current_user);
  fpv_organization_destroy(context->current_org);
  fpv_team_destroy(context->current_team);

  if (context->chats) {
    g_hash_table_destroy(context->chats);
  }
  if (context->orders) {
    g_hash_table_destroy(context->orders);
  }
  if (context->lots) {
    g_hash_table_destroy(context->lots);
  }
  if (context->plugins) {
    g_hash_table_destroy(context->plugins);
  }
  if (context->messages_by_chat) {
    g_hash_table_destroy(context->messages_by_chat);
  }

  g_free(context->active_chat_id);
  g_free(context->active_chat_name);
  g_free(context->base_dir);
  g_free(context->data_dir);
  g_free(context->config_dir);
  g_free(context->logs_dir);
  g_free(context->plugins_dir);
  g_free(context->locales_dir);
  g_free(context);
}

static void on_activate(GtkApplication* app, gpointer user_data) {
  AppContext* context = g_new0(AppContext, 1);

  context->bus = fpv_event_bus_create();
  if (!context->bus) {
    show_message_dialog(NULL, "Startup", "Event bus initialization failed.");
    g_free(context);
    return;
  }

  const gchar* data_root = g_get_user_data_dir();
  context->base_dir = g_build_filename(data_root, "funpay_vertex", NULL);
  context->data_dir = g_build_filename(context->base_dir, "storage", NULL);
  context->config_dir = g_build_filename(context->base_dir, "configs", NULL);
  context->logs_dir = g_build_filename(context->base_dir, "logs", NULL);
  context->plugins_dir = g_build_filename(context->base_dir, "plugins", NULL);
  context->locales_dir = find_resource_dir("locales", context->base_dir, "eng.loc");

  g_mkdir_with_parents(context->data_dir, 0755);
  g_mkdir_with_parents(context->config_dir, 0755);
  g_mkdir_with_parents(context->logs_dir, 0755);
  g_mkdir_with_parents(context->plugins_dir, 0755);

  configure_database_from_config(context->config_dir, context->base_dir);

  fpv_result_t identity_result = FPV_OK;
  context->identity = fpv_identity_store_open(context->data_dir, &identity_result);
  if (!context->identity) {
    show_message_dialog(NULL, "Startup", "Identity store initialization failed.");
    fpv_event_bus_destroy(context->bus);
    g_free(context);
    return;
  }

  fpv_result_t chat_store_result = FPV_OK;
  context->chat_store = fpv_chat_store_open(context->data_dir, NULL, &chat_store_result);
  if (!context->chat_store) {
    show_message_dialog(NULL, "Startup", "Chat store initialization failed.");
    fpv_identity_store_destroy(context->identity);
    fpv_event_bus_destroy(context->bus);
    g_free(context);
    return;
  }


  const char* lang = g_getenv("LANG");
  const char* locale = "eng";
  if (lang && (g_str_has_prefix(lang, "ru") || g_str_has_prefix(lang, "RU"))) {
    locale = "ru";
  }

  fpv_core_config_t config;
  config.data_dir = context->data_dir;
  config.config_dir = context->config_dir;
  config.logs_dir = context->logs_dir;
  config.plugins_dir = context->plugins_dir;
  config.locales_dir = context->locales_dir;
  config.locale = locale;

  context->core = fpv_core_create(&config, context->bus);
  if (!context->core) {
    show_message_dialog(NULL, "Startup", "Core initialization failed.");
    fpv_event_bus_destroy(context->bus);
    fpv_identity_store_destroy(context->identity);
    g_free(context);
    return;
  }

  context->chats = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_chat_destroy);
  context->orders = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_order_destroy);
  context->lots = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_lot_destroy);
  context->plugins = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)fpv_plugin_destroy);
  context->messages_by_chat = g_hash_table_new_full(
      g_str_hash,
      g_str_equal,
      g_free,
      (GDestroyNotify)g_ptr_array_unref);

  context->window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(context->window), "FunPay Vertex");
  gtk_window_set_default_size(GTK_WINDOW(context->window), 1120, 720);

  GtkWidget* layout = build_main_layout(context);
  gtk_window_set_child(GTK_WINDOW(context->window), layout);
  gtk_widget_set_margin_top(layout, 12);
  gtk_widget_set_margin_bottom(layout, 12);
  gtk_widget_set_margin_start(layout, 12);
  gtk_widget_set_margin_end(layout, 12);

  update_status(context, FPV_CORE_STOPPED, NULL);
  apply_css(context->window);

  context->poll_id = g_timeout_add(250, poll_events, context);
  g_object_set_data_full(
      G_OBJECT(app),
      "fpv-context",
      context,
      app_context_destroy);

  if (!has_config_files(context->config_dir)) {
    gtk_widget_set_visible(context->window, FALSE);
    show_setup_wizard(context);
  } else {
    refresh_settings_from_file(context);
    begin_auth_flow(context);
  }
}

int main(int argc, char* argv[]) {
  GtkApplication* app = gtk_application_new(
      "com.funpay.vertex",
      G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
