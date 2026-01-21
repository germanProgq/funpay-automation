#include "helpers/app_helpers.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

gchar* format_timestamp(uint64_t timestamp_ms) {
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

gchar* format_time_short(uint64_t timestamp_ms) {
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

gchar* sanitize_utf8(const char* text) {
  if (!text) {
    return g_strdup("");
  }
  if (g_utf8_validate(text, -1, NULL)) {
    return g_strdup(text);
  }
  return g_utf8_make_valid(text, -1);
}

char* app_strdup(const char* value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char* copy = (char*)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len);
  copy[len] = '\0';
  return copy;
}

const char* status_label(fpv_core_status_t status) {
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

const char* log_level_label(fpv_log_level_t level) {
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

const char* role_label(fpv_role_t role) {
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

const char* role_id(fpv_role_t role) {
  switch (role) {
    case FPV_ROLE_OWNER:
      return "owner";
    case FPV_ROLE_ADMIN:
      return "admin";
    case FPV_ROLE_MANAGER:
      return "manager";
    case FPV_ROLE_ANALYST:
      return "analyst";
    case FPV_ROLE_VIEWER:
      return "viewer";
    default:
      return "unknown";
  }
}

fpv_feature_mask_t app_feature_mask(const AppContext* context) {
  if (!context || !context->current_org) {
    return 0;
  }
  return fpv_feature_mask_for_tier(context->current_org->tier);
}

gboolean app_has_feature(
    const AppContext* context,
    fpv_feature_flag_t feature) {
  fpv_feature_mask_t mask = app_feature_mask(context);
  return fpv_feature_mask_has(mask, feature) ? TRUE : FALSE;
}

void append_list_message(GtkWidget* list, const char* message) {
  if (!list) {
    return;
  }
  GtkWidget* row = gtk_list_box_row_new();
  GtkWidget* label = gtk_label_new(message ? message : "");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
  gtk_list_box_append(GTK_LIST_BOX(list), row);
}

const char* order_status_label(fpv_order_status_t status) {
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

const char* notification_severity_label(
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

gboolean parse_ini_bool(const char* value, gboolean fallback) {
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

double clamp_adjustment_value(GtkAdjustment* adjustment, double value) {
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

gboolean on_slow_scroll(
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

void install_slow_scrolling(GtkWidget* scroller) {
  if (!scroller) {
    return;
  }
  gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroller), TRUE);
  GtkEventController* controller = gtk_event_controller_scroll_new(
      GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(controller, "scroll", G_CALLBACK(on_slow_scroll), scroller);
  gtk_widget_add_controller(scroller, controller);
}

void on_toggle_revealer(
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

void apply_css(GtkWidget* window) {
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
  gtk_css_provider_load_from_data(provider, css, -1);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

void show_message_dialog(
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

gboolean dialog_task_run(gpointer data) {
  DialogTask* task = (DialogTask*)data;
  if (task && task->context && !task->context->closing) {
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

void schedule_dialog(
    AppContext* context,
    const char* title,
    const char* message) {
  if (!context || context->closing) {
    return;
  }
  DialogTask* task = (DialogTask*)g_new0(DialogTask, 1);
  task->context = context;
  task->title = g_strdup(title ? title : "Message");
  task->message = g_strdup(message ? message : "");
  g_main_context_invoke(NULL, dialog_task_run, task);
}

void clear_list_box(GtkWidget* list) {
  GtkWidget* child = gtk_widget_get_first_child(list);
  while (child) {
    GtkWidget* next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(GTK_LIST_BOX(list), child);
    child = next;
  }
}

void append_log_row(
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

void add_setting_row(
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

void add_setting_row_label(
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

GtkStringList* fpv_dropdown_get_list(GtkWidget* dropdown) {
  if (!dropdown) {
    return NULL;
  }
  GListModel* model = gtk_drop_down_get_model(GTK_DROP_DOWN(dropdown));
  if (!model || !GTK_IS_STRING_LIST(model)) {
    return NULL;
  }
  return GTK_STRING_LIST(model);
}

GPtrArray* fpv_dropdown_get_ids(GtkWidget* dropdown) {
  if (!dropdown) {
    return NULL;
  }
  GPtrArray* ids = (GPtrArray*)g_object_get_data(G_OBJECT(dropdown), "fpv-ids");
  if (!ids) {
    ids = g_ptr_array_new_with_free_func(g_free);
    g_object_set_data_full(
        G_OBJECT(dropdown),
        "fpv-ids",
        ids,
        (GDestroyNotify)g_ptr_array_unref);
  }
  return ids;
}

GtkWidget* fpv_dropdown_new(void) {
  GtkStringList* list = gtk_string_list_new(NULL);
  GtkExpression* expression =
      gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string");
  GtkWidget* dropdown = gtk_drop_down_new(G_LIST_MODEL(list), expression);
  /* GtkDropDown takes ownership of the model and expression. */
  fpv_dropdown_get_ids(dropdown);
  return dropdown;
}

void fpv_dropdown_clear(GtkWidget* dropdown) {
  if (!dropdown) {
    return;
  }
  GtkStringList* list = fpv_dropdown_get_list(dropdown);
  if (list) {
    guint count = g_list_model_get_n_items(G_LIST_MODEL(list));
    if (count > 0) {
      gtk_string_list_splice(list, 0, count, NULL);
    }
  }
  GPtrArray* ids = fpv_dropdown_get_ids(dropdown);
  if (ids && ids->len > 0) {
    g_ptr_array_remove_range(ids, 0, ids->len);
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), GTK_INVALID_LIST_POSITION);
}

void fpv_dropdown_append(
    GtkWidget* dropdown,
    const char* id,
    const char* label) {
  if (!dropdown) {
    return;
  }
  GtkStringList* list = fpv_dropdown_get_list(dropdown);
  if (list) {
    gtk_string_list_append(list, label ? label : "");
  }
  GPtrArray* ids = fpv_dropdown_get_ids(dropdown);
  if (ids) {
    g_ptr_array_add(ids, g_strdup(id ? id : ""));
  }
}

void fpv_dropdown_set_active_id(GtkWidget* dropdown, const char* id) {
  if (!dropdown) {
    return;
  }
  GPtrArray* ids = fpv_dropdown_get_ids(dropdown);
  const char* target = id ? id : "";
  if (!ids) {
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), GTK_INVALID_LIST_POSITION);
    return;
  }
  for (guint i = 0; i < ids->len; i++) {
    const char* stored = (const char*)g_ptr_array_index(ids, i);
    if (g_strcmp0(stored, target) == 0) {
      gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), i);
      return;
    }
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), GTK_INVALID_LIST_POSITION);
}

void fpv_dropdown_set_active_index(GtkWidget* dropdown, guint index) {
  if (!dropdown) {
    return;
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), index);
}

const char* fpv_dropdown_get_active_id(GtkWidget* dropdown) {
  if (!dropdown) {
    return NULL;
  }
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
  if (selected == GTK_INVALID_LIST_POSITION) {
    return NULL;
  }
  GPtrArray* ids = fpv_dropdown_get_ids(dropdown);
  if (!ids || selected >= ids->len) {
    return NULL;
  }
  return (const char*)g_ptr_array_index(ids, selected);
}
