#ifndef FPV_APP_SRC_APP_HELPERS_H_
#define FPV_APP_SRC_APP_HELPERS_H_

#include "core/app_context.h"

#include <stdint.h>

// Formatting and labels.
gchar* format_timestamp(uint64_t timestamp_ms);
gchar* format_time_short(uint64_t timestamp_ms);
gchar* sanitize_utf8(const char* text);
gchar* normalize_whitespace(const char* text);
char* app_strdup(const char* value);
const char* status_label(fpv_core_status_t status);
const char* log_level_label(fpv_log_level_t level);
const char* role_label(fpv_role_t role);
const char* role_id(fpv_role_t role);
const char* order_status_label(fpv_order_status_t status);
const char* notification_severity_label(fpv_notification_severity_t severity);

gboolean app_has_feature(const AppContext* context, fpv_feature_flag_t feature);

// UI helpers.
void append_list_message(GtkWidget* list, const char* message);
void clear_list_box(GtkWidget* list);
void append_log_row(
    AppContext* context,
    const char* level,
    const char* message,
    uint64_t timestamp_ms);

void show_message_dialog(GtkWindow* parent, const char* title, const char* message);
void schedule_dialog(AppContext* context, const char* title, const char* message);
void apply_css(GtkWidget* window);
void install_slow_scrolling(GtkWidget* scroller);
void on_toggle_revealer(GObject* object, GParamSpec* pspec, gpointer user_data);

gboolean parse_ini_bool(const char* value, gboolean fallback);
double clamp_adjustment_value(GtkAdjustment* adjustment, double value);

void add_setting_row(
    GtkWidget* grid,
    int row,
    const char* label_text,
    GtkWidget* widget);
void add_setting_row_label(
    GtkWidget* grid,
    int row,
    GtkWidget* label,
    GtkWidget* widget);

GtkWidget* fpv_dropdown_new(void);
void fpv_dropdown_clear(GtkWidget* dropdown);
void fpv_dropdown_append(
    GtkWidget* dropdown,
    const char* id,
    const char* label);
void fpv_dropdown_set_active_id(GtkWidget* dropdown, const char* id);
void fpv_dropdown_set_active_index(GtkWidget* dropdown, guint index);
const char* fpv_dropdown_get_active_id(GtkWidget* dropdown);

#endif  // FPV_APP_SRC_APP_HELPERS_H_
