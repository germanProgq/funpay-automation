#include "settings/app_settings.h"

#include "helpers/app_helpers.h"
#include "lists/app_lists.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <gtk/gtk.h>

void apply_settings_entitlements(AppContext* context);

void refresh_auto_response_list(AppContext* context) {
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
  apply_settings_entitlements(context);
}

void append_meta_piece(GString* meta, const char* text) {
  if (!meta || !text || !text[0]) {
    return;
  }
  if (meta->len > 0) {
    g_string_append(meta, " · ");
  }
  g_string_append(meta, text);
}

void refresh_auto_delivery_list(AppContext* context) {
  if (!context || !context->auto_delivery_list || !context->config_dir) {
    return;
  }

  clear_list_box(context->auto_delivery_list);

  gboolean added_row = FALSE;
  gchar* path = g_build_filename(context->config_dir, "auto_delivery.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  gboolean config_available = ini != NULL;
  if (!config_available) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("Auto-delivery config not available.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);
    added_row = TRUE;
  }

  GPtrArray* lots = g_ptr_array_new();
  if (context->lots) {
    GHashTableIter iter;
    gpointer value = NULL;
    g_hash_table_iter_init(&iter, context->lots);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
      if (value) {
        g_ptr_array_add(lots, value);
      }
    }
    g_ptr_array_sort(lots, compare_lot_items);
  }

  GHashTable* matched_sections = NULL;
  if (config_available) {
    matched_sections = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        NULL);
  }

  size_t section_count = config_available ? fpv_ini_section_count(ini) : 0;
  for (guint i = 0; i < lots->len; i++) {
    fpv_lot_t* lot = (fpv_lot_t*)g_ptr_array_index(lots, i);
    if (!lot) {
      continue;
    }
    const char* title_raw = lot->title ? lot->title : "";
    const char* section_match = NULL;
    if (config_available && title_raw[0]) {
      for (size_t s = 0; s < section_count; s++) {
        const char* section = fpv_ini_section_name(ini, s);
        if (section && section[0] && strstr(title_raw, section)) {
          section_match = section;
          break;
        }
      }
    }
    if (section_match && matched_sections) {
      g_hash_table_add(matched_sections, g_strdup(section_match));
    }

    const char* response = NULL;
    const char* products = NULL;
    const char* disable = NULL;
    const char* timed = NULL;
    const char* timer_hours = NULL;
    if (section_match) {
      response = fpv_ini_get(ini, section_match, "response");
      products = fpv_ini_get(ini, section_match, "productsFileName");
      disable = fpv_ini_get(ini, section_match, "disable");
      timed = fpv_ini_get(ini, section_match, "timed");
      timer_hours = fpv_ini_get(ini, section_match, "timerHours");
    }

    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gchar* title_text = sanitize_utf8(title_raw[0] ? title_raw : "Lot");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    GString* meta = g_string_new(NULL);
    if (lot->id && lot->id[0]) {
      gchar* id_text = g_strdup_printf("ID: %s", lot->id);
      append_meta_piece(meta, id_text);
      g_free(id_text);
    }
    gchar* response_text = sanitize_utf8(response ? response : "");
    append_meta_piece(meta, response_text);
    if (products && products[0]) {
      append_meta_piece(meta, "Products");
    }
    if (parse_ini_bool(timed, FALSE)) {
      if (timer_hours && timer_hours[0]) {
        gchar* timed_text = g_strdup_printf("Timed %s h", timer_hours);
        append_meta_piece(meta, timed_text);
        g_free(timed_text);
      } else {
        append_meta_piece(meta, "Timed");
      }
    }
    if (parse_ini_bool(disable, FALSE)) {
      append_meta_piece(meta, "Disabled");
    }
    if (!section_match) {
      append_meta_piece(meta, "Not configured");
    }

    GtkWidget* meta_label = gtk_label_new(meta->str);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_label_set_wrap(GTK_LABEL(meta_label), TRUE);
    gtk_widget_add_css_class(meta_label, "muted");

    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), meta_label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);
    added_row = TRUE;

    const char* section_key = section_match;
    if (!section_key || !section_key[0]) {
      section_key = title_raw && title_raw[0] ? title_raw : lot->id;
    }
    if (section_key && section_key[0]) {
      g_object_set_data_full(
          G_OBJECT(row),
          "section",
          g_strdup(section_key),
          g_free);
    }

    g_string_free(meta, TRUE);
    g_free(title_text);
    g_free(response_text);
  }

  if (config_available) {
    for (size_t i = 0; i < section_count; i++) {
      const char* section = fpv_ini_section_name(ini, i);
      if (!section || !section[0]) {
        continue;
      }
      if (matched_sections &&
          g_hash_table_contains(matched_sections, section)) {
        continue;
      }

      const char* response = fpv_ini_get(ini, section, "response");
      const char* products = fpv_ini_get(ini, section, "productsFileName");
      const char* disable = fpv_ini_get(ini, section, "disable");

      GtkWidget* row = gtk_list_box_row_new();
      GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
      gchar* title_text = sanitize_utf8(section);
      GtkWidget* title = gtk_label_new(title_text);
      gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

      GString* meta = g_string_new(NULL);
      gchar* response_text = sanitize_utf8(response ? response : "");
      append_meta_piece(meta, response_text);
      if (products && products[0]) {
        append_meta_piece(meta, "Products");
      }
      if (parse_ini_bool(disable, FALSE)) {
        append_meta_piece(meta, "Disabled");
      }
      append_meta_piece(meta, "Missing lot");

      GtkWidget* meta_label = gtk_label_new(meta->str);
      gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
      gtk_label_set_wrap(GTK_LABEL(meta_label), TRUE);
      gtk_widget_add_css_class(meta_label, "muted");

      gtk_box_append(GTK_BOX(box), title);
      gtk_box_append(GTK_BOX(box), meta_label);
      gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
      gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);
      added_row = TRUE;

      g_object_set_data_full(
          G_OBJECT(row),
          "section",
          g_strdup(section),
          g_free);

      g_string_free(meta, TRUE);
      g_free(title_text);
      g_free(response_text);
    }
  }

  if (!added_row) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("No lots or auto-delivery entries.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->auto_delivery_list), row);
  }

  if (matched_sections) {
    g_hash_table_destroy(matched_sections);
  }
  g_ptr_array_free(lots, TRUE);
  if (ini) {
    fpv_ini_destroy(ini);
  }
  g_free(path);
  apply_settings_entitlements(context);
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

gboolean auto_response_dialog_close(
    GtkWindow* window,
    gpointer user_data) {
  AutoResponseDialog* data = (AutoResponseDialog*)user_data;
  if (data) {
    g_free(data->original_section);
    g_free(data);
  }
  return FALSE;
}

void auto_response_dialog_save(GtkButton* button, gpointer user_data) {
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

void show_auto_response_dialog(
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

void on_auto_response_add(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_auto_response_dialog(context, NULL);
}

void on_auto_response_edit(GtkButton* button, gpointer user_data) {
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

void on_auto_response_delete(GtkButton* button, gpointer user_data) {
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

void on_auto_response_reload(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  refresh_auto_response_list(context);
  if (context->running) {
    fpv_core_reload_auto_response(context->core);
  }
}

enum {
  AD_ROW_LOT = 0,
  AD_ROW_RESPONSE = 1,
  AD_ROW_PRODUCTS = 2,
  AD_ROW_PREVIEW = 3,
  AD_ROW_TIMED = 4,
  AD_ROW_TIMER = 5,
  AD_ROW_ENABLED = 6,
  AD_ROW_RESTORE = 7,
  AD_ROW_AUTO_DISABLE = 8,
  AD_ROW_AUTO_DELIVERY = 9,
  AD_ROW_MULTI = 10,
  AD_ROW_MULTI_COUNT = 11
};

typedef struct AutoDeliveryDialog {
  AppContext* context;
  GtkWidget* dialog;
  GtkWidget* grid;
  GtkWidget* lot_entry;
  GtkWidget* response_entry;
  GtkTextBuffer* products_buffer;
  GtkTextBuffer* preview_buffer;
  gchar* products_filename;
  gchar* lot_id;
  GtkWidget* timed_switch;
  GtkWidget* timer_hours;
  GtkWidget* lot_enabled_switch;
  GtkWidget* restore_switch;
  GtkWidget* auto_disable_switch;
  GtkWidget* auto_delivery_switch;
  GtkWidget* multi_delivery_switch;
  GtkWidget* multi_delivery_count;
  gchar* original_section;
} AutoDeliveryDialog;

static void auto_delivery_update_preview(AutoDeliveryDialog* data);

gboolean auto_delivery_dialog_close(
    GtkWindow* window,
    gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  if (data) {
    g_free(data->products_filename);
    g_free(data->lot_id);
    g_free(data->original_section);
    g_free(data);
  }
  return FALSE;
}

static void auto_delivery_set_row_visible(
    GtkWidget* grid,
    int row,
    gboolean visible) {
  if (!grid) {
    return;
  }
  GtkWidget* label = gtk_grid_get_child_at(GTK_GRID(grid), 0, row);
  GtkWidget* widget = gtk_grid_get_child_at(GTK_GRID(grid), 1, row);
  if (label) {
    gtk_widget_set_visible(label, visible);
  }
  if (widget) {
    gtk_widget_set_visible(widget, visible);
  }
}

static gboolean auto_delivery_is_enabled(AutoDeliveryDialog* data) {
  if (!data || !data->auto_delivery_switch) {
    return FALSE;
  }
  return gtk_switch_get_active(GTK_SWITCH(data->auto_delivery_switch));
}

static void auto_delivery_update_timer_visibility(AutoDeliveryDialog* data) {
  if (!data || !data->timed_switch) {
    return;
  }
  gboolean timed = gtk_switch_get_active(GTK_SWITCH(data->timed_switch));
  gboolean visible = auto_delivery_is_enabled(data) && timed;
  auto_delivery_set_row_visible(data->grid, AD_ROW_TIMER, visible);
}

static void auto_delivery_update_multi_visibility(AutoDeliveryDialog* data) {
  if (!data || !data->multi_delivery_switch) {
    return;
  }
  gboolean enabled = gtk_switch_get_active(GTK_SWITCH(data->multi_delivery_switch));
  gboolean visible = auto_delivery_is_enabled(data) && enabled;
  auto_delivery_set_row_visible(data->grid, AD_ROW_MULTI_COUNT, visible);
}

static void auto_delivery_update_delivery_visibility(AutoDeliveryDialog* data) {
  if (!data) {
    return;
  }
  gboolean enabled = auto_delivery_is_enabled(data);
  auto_delivery_set_row_visible(data->grid, AD_ROW_RESPONSE, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_PRODUCTS, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_PREVIEW, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_TIMED, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_TIMER, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_RESTORE, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_AUTO_DISABLE, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_MULTI, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_MULTI_COUNT, enabled);
  auto_delivery_set_row_visible(data->grid, AD_ROW_ENABLED, TRUE);
  auto_delivery_set_row_visible(data->grid, AD_ROW_AUTO_DELIVERY, TRUE);
  auto_delivery_update_timer_visibility(data);
  auto_delivery_update_multi_visibility(data);
}

static void auto_delivery_timed_switch_changed(
    GtkSwitch* widget,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)widget;
  (void)pspec;
  auto_delivery_update_timer_visibility((AutoDeliveryDialog*)user_data);
}

static void auto_delivery_multi_switch_changed(
    GtkSwitch* widget,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)widget;
  (void)pspec;
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  auto_delivery_update_multi_visibility(data);
  auto_delivery_update_preview(data);
}

static void auto_delivery_multi_count_changed(
    GtkSpinButton* button,
    gpointer user_data) {
  (void)button;
  auto_delivery_update_preview((AutoDeliveryDialog*)user_data);
}

static void auto_delivery_delivery_switch_changed(
    GtkSwitch* widget,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)widget;
  (void)pspec;
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  auto_delivery_update_delivery_visibility(data);
  auto_delivery_update_preview(data);
}

typedef struct AutoDeliveryImportContext {
  GtkWindow* parent;
  GtkTextBuffer* buffer;
} AutoDeliveryImportContext;

gboolean auto_delivery_products_has_content(const char* text) {
  if (!text) {
    return FALSE;
  }
  for (const char* ptr = text; *ptr; ptr++) {
    if (*ptr != '\n' && *ptr != '\r') {
      return TRUE;
    }
  }
  return FALSE;
}

gchar* auto_delivery_unescape_newlines(const char* text) {
  if (!text) {
    return g_strdup("");
  }
  GString* out = g_string_new(NULL);
  const char* cursor = text;
  const char* escaped = strstr(cursor, "\\n");
  while (escaped) {
    g_string_append_len(out, cursor, (gssize)(escaped - cursor));
    g_string_append_c(out, '\n');
    cursor = escaped + 2;
    escaped = strstr(cursor, "\\n");
  }
  g_string_append(out, cursor);
  return g_string_free(out, FALSE);
}

gchar* auto_delivery_extract_first_product(const char* text) {
  if (!text) {
    return NULL;
  }
  const char* start = text;
  for (const char* ptr = text; ; ptr++) {
    if (*ptr == '\n' || *ptr == '\0') {
      const char* end = ptr;
      if (end > start && end[-1] == '\r') {
        end--;
      }
      if (end > start) {
        return g_strndup(start, (gsize)(end - start));
      }
      start = ptr + 1;
      if (*ptr == '\0') {
        break;
      }
    }
  }
  return NULL;
}

static gchar* auto_delivery_extract_products(
    const char* text,
    size_t count) {
  if (!text || count == 0) {
    return g_strdup("");
  }
  gchar** lines = g_strsplit(text, "\n", -1);
  if (!lines) {
    return g_strdup("");
  }
  GString* out = g_string_new(NULL);
  size_t collected = 0;
  for (size_t i = 0; lines[i] && collected < count; i++) {
    gchar* line = lines[i];
    size_t len = strlen(line);
    while (len > 0 && line[len - 1] == '\r') {
      len--;
    }
    if (len == 0) {
      continue;
    }
    gchar* trimmed = g_strndup(line, len);
    gchar* unescaped = auto_delivery_unescape_newlines(trimmed);
    g_free(trimmed);
    if (unescaped && unescaped[0]) {
      if (collected > 0) {
        g_string_append_c(out, '\n');
      }
      g_string_append(out, unescaped);
      collected++;
    }
    g_free(unescaped);
  }
  g_strfreev(lines);
  return g_string_free(out, FALSE);
}

gchar* auto_delivery_replace_all(
    const char* text,
    const char* token,
    const char* value) {
  if (!text) {
    return g_strdup("");
  }
  if (!token || !token[0]) {
    return g_strdup(text);
  }
  const char* replacement = value ? value : "";
  size_t token_len = strlen(token);
  GString* out = g_string_new(NULL);
  const char* cursor = text;
  while (cursor) {
    const char* found = strstr(cursor, token);
    if (!found) {
      g_string_append(out, cursor);
      break;
    }
    g_string_append_len(out, cursor, (gssize)(found - cursor));
    g_string_append(out, replacement);
    cursor = found + token_len;
  }
  return g_string_free(out, FALSE);
}

static void auto_delivery_update_preview(AutoDeliveryDialog* data) {
  if (!data || !data->preview_buffer || !data->response_entry ||
      !data->products_buffer) {
    return;
  }
  const char* response = gtk_editable_get_text(GTK_EDITABLE(data->response_entry));
  size_t preview_count = 1;
  if (data->multi_delivery_switch &&
      gtk_switch_get_active(GTK_SWITCH(data->multi_delivery_switch)) &&
      data->multi_delivery_count) {
    int count = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(data->multi_delivery_count));
    if (count > 1) {
      preview_count = (size_t)count;
    }
  }
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_start_iter(data->products_buffer, &start);
  gtk_text_buffer_get_end_iter(data->products_buffer, &end);
  gchar* products_text = gtk_text_buffer_get_text(
      data->products_buffer, &start, &end, FALSE);
  gchar* products = auto_delivery_extract_products(products_text, preview_count);
  gchar* normalized = auto_delivery_replace_all(
      response ? response : "",
      "$products",
      "$product");
  gchar* preview = NULL;
  if (!response || !response[0]) {
    if (products && products[0]) {
      preview = g_strdup("Add a response to preview delivery.");
    } else {
      preview = g_strdup("Add a response and products to preview delivery.");
    }
  } else {
    preview = auto_delivery_replace_all(normalized, "$product",
                                        products ? products : "");
  }
  gtk_text_buffer_set_text(
      data->preview_buffer,
      preview ? preview : "",
      -1);
  g_free(preview);
  g_free(normalized);
  g_free(products);
  g_free(products_text);
}

void auto_delivery_preview_changed(
    GtkEditable* editable,
    gpointer user_data) {
  (void)editable;
  auto_delivery_update_preview((AutoDeliveryDialog*)user_data);
}

void auto_delivery_preview_buffer_changed(
    GtkTextBuffer* buffer,
    gpointer user_data) {
  (void)buffer;
  auto_delivery_update_preview((AutoDeliveryDialog*)user_data);
}

static gchar* auto_delivery_sanitize_section_name(const char* name) {
  gchar* normalized = normalize_whitespace(name);
  if (!normalized) {
    return g_strdup("");
  }
  size_t len = strlen(normalized);
  if (len >= 2 && normalized[0] == '[' && normalized[len - 1] == ']') {
    normalized[len - 1] = '\0';
    memmove(normalized, normalized + 1, len - 1);
  }
  return normalized;
}

static gchar* auto_delivery_resolve_lot_name(
    AppContext* context,
    const char* raw_name) {
  gchar* sanitized = auto_delivery_sanitize_section_name(raw_name);
  if (!context || !context->lots || !sanitized || !sanitized[0]) {
    return sanitized;
  }

  gchar* best = NULL;
  size_t best_len = 0;
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->lots);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    fpv_lot_t* lot = (fpv_lot_t*)value;
    if (!lot || !lot->title) {
      continue;
    }
    gchar* title_norm = auto_delivery_sanitize_section_name(lot->title);
    if (!title_norm || !title_norm[0]) {
      g_free(title_norm);
      continue;
    }
    if (strcmp(title_norm, sanitized) == 0) {
      g_free(sanitized);
      return title_norm;
    }
    if (strstr(sanitized, title_norm)) {
      size_t title_len = strlen(title_norm);
      if (title_len > best_len) {
        g_free(best);
        best = title_norm;
        best_len = title_len;
        continue;
      }
    }
    g_free(title_norm);
  }

  if (best) {
    g_free(sanitized);
    return best;
  }
  return sanitized;
}

static gchar* auto_delivery_find_section_key(
    const fpv_ini_t* ini,
    const char* section_name) {
  if (!ini || !section_name || !section_name[0]) {
    return NULL;
  }

  gchar* normalized = auto_delivery_sanitize_section_name(section_name);
  if (!normalized || !normalized[0]) {
    g_free(normalized);
    return NULL;
  }

  const char* best = NULL;
  size_t best_len = 0;
  size_t section_count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < section_count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (!section || !section[0]) {
      continue;
    }
    if (strcmp(section, section_name) == 0) {
      g_free(normalized);
      return g_strdup(section);
    }
    gchar* section_norm = auto_delivery_sanitize_section_name(section);
    if (!section_norm || !section_norm[0]) {
      g_free(section_norm);
      continue;
    }
    if (strcmp(section_norm, normalized) == 0) {
      g_free(section_norm);
      g_free(normalized);
      return g_strdup(section);
    }
    if (strstr(section_norm, normalized) || strstr(normalized, section_norm)) {
      size_t section_len = strlen(section_norm);
      if (section_len > best_len) {
        best = section;
        best_len = section_len;
      }
    }
    g_free(section_norm);
  }

  g_free(normalized);
  return best ? g_strdup(best) : NULL;
}

static fpv_lot_t* auto_delivery_find_lot(
    AppContext* context,
    const char* raw_name) {
  if (!context || !context->lots || !raw_name || !raw_name[0]) {
    return NULL;
  }

  fpv_lot_t* direct = (fpv_lot_t*)g_hash_table_lookup(
      context->lots,
      raw_name);
  if (direct) {
    return direct;
  }

  gchar* normalized = auto_delivery_sanitize_section_name(raw_name);
  if (!normalized || !normalized[0]) {
    g_free(normalized);
    return NULL;
  }

  fpv_lot_t* by_id = (fpv_lot_t*)g_hash_table_lookup(
      context->lots,
      normalized);
  if (by_id) {
    g_free(normalized);
    return by_id;
  }

  fpv_lot_t* best = NULL;
  size_t best_len = 0;
  GHashTableIter iter;
  gpointer value = NULL;
  g_hash_table_iter_init(&iter, context->lots);
  while (g_hash_table_iter_next(&iter, NULL, &value)) {
    fpv_lot_t* lot = (fpv_lot_t*)value;
    if (!lot || !lot->title) {
      continue;
    }
    gchar* title_norm = auto_delivery_sanitize_section_name(lot->title);
    if (!title_norm || !title_norm[0]) {
      g_free(title_norm);
      continue;
    }
    if (strcmp(title_norm, normalized) == 0) {
      g_free(title_norm);
      g_free(normalized);
      return lot;
    }
    if (strstr(normalized, title_norm) || strstr(title_norm, normalized)) {
      size_t title_len = strlen(title_norm);
      if (title_len > best_len) {
        best = lot;
        best_len = title_len;
      }
    }
    g_free(title_norm);
  }

  g_free(normalized);
  return best;
}

gchar* auto_delivery_generate_products_filename(void) {
  gchar* uuid = g_uuid_string_random();
  if (!uuid) {
    return NULL;
  }
  gchar* filename = g_strdup_printf("auto_delivery_%s.txt", uuid);
  g_free(uuid);
  return filename;
}

#if GTK_CHECK_VERSION(4, 10, 0)
void auto_delivery_import_finish(
    GObject* source,
    GAsyncResult* result,
    gpointer user_data) {
  AutoDeliveryImportContext* context = (AutoDeliveryImportContext*)user_data;
  GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
  if (!context || !context->buffer || !context->parent) {
    g_object_unref(dialog);
    g_free(context);
    return;
  }

  GError* error = NULL;
  GFile* file = gtk_file_dialog_open_finish(dialog, result, &error);
  if (!file) {
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      show_message_dialog(
          context->parent,
          "Auto Delivery",
          error->message ? error->message : "File import failed.");
    }
    g_clear_error(&error);
    g_object_unref(dialog);
    g_object_unref(context->buffer);
    g_object_unref(context->parent);
    g_free(context);
    return;
  }

  gchar* contents = NULL;
  gsize length = 0;
  if (!g_file_load_contents(
          file,
          NULL,
          &contents,
          &length,
          NULL,
          &error)) {
    show_message_dialog(
        context->parent,
        "Auto Delivery",
        error && error->message ? error->message : "Failed to read file.");
    g_clear_error(&error);
  } else {
    gtk_text_buffer_set_text(
        context->buffer,
        contents ? contents : "",
        (gint)length);
  }

  g_free(contents);
  g_object_unref(file);
  g_object_unref(dialog);
  g_object_unref(context->buffer);
  g_object_unref(context->parent);
  g_free(context);
}

void on_auto_delivery_import_clicked(GtkButton* button, gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  (void)button;
  if (!data || !data->dialog || !data->products_buffer) {
    return;
  }

  AutoDeliveryImportContext* context =
      (AutoDeliveryImportContext*)g_new0(AutoDeliveryImportContext, 1);
  if (!context) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Out of memory.");
    return;
  }

  context->parent = GTK_WINDOW(data->dialog);
  context->buffer = data->products_buffer;
  g_object_ref(context->parent);
  g_object_ref(context->buffer);

  GtkFileDialog* dialog = gtk_file_dialog_new();
  g_object_ref(dialog);
  gtk_file_dialog_open(
      dialog,
      GTK_WINDOW(data->dialog),
      NULL,
      auto_delivery_import_finish,
      context);
}
#else
void auto_delivery_import_response(
    GtkNativeDialog* dialog,
    gint response,
    gpointer user_data) {
  AutoDeliveryImportContext* context = (AutoDeliveryImportContext*)user_data;
  if (!context || !context->buffer || !context->parent) {
    g_object_unref(dialog);
    g_free(context);
    return;
  }

  if (response != GTK_RESPONSE_ACCEPT) {
    g_object_unref(dialog);
    g_object_unref(context->buffer);
    g_object_unref(context->parent);
    g_free(context);
    return;
  }

  GFile* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
  if (!file) {
    show_message_dialog(
        context->parent,
        "Auto Delivery",
        "File import failed.");
    g_object_unref(dialog);
    g_object_unref(context->buffer);
    g_object_unref(context->parent);
    g_free(context);
    return;
  }

  GError* error = NULL;
  gchar* contents = NULL;
  gsize length = 0;
  if (!g_file_load_contents(
          file,
          NULL,
          &contents,
          &length,
          NULL,
          &error)) {
    show_message_dialog(
        context->parent,
        "Auto Delivery",
        error && error->message ? error->message : "Failed to read file.");
    g_clear_error(&error);
  } else {
    gtk_text_buffer_set_text(
        context->buffer,
        contents ? contents : "",
        (gint)length);
  }

  g_free(contents);
  g_object_unref(file);
  g_object_unref(dialog);
  g_object_unref(context->buffer);
  g_object_unref(context->parent);
  g_free(context);
}

void on_auto_delivery_import_clicked(GtkButton* button, gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  (void)button;
  if (!data || !data->dialog || !data->products_buffer) {
    return;
  }

  AutoDeliveryImportContext* context =
      (AutoDeliveryImportContext*)g_new0(AutoDeliveryImportContext, 1);
  if (!context) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Out of memory.");
    return;
  }

  context->parent = GTK_WINDOW(data->dialog);
  context->buffer = data->products_buffer;
  g_object_ref(context->parent);
  g_object_ref(context->buffer);

  GtkFileChooserNative* dialog = gtk_file_chooser_native_new(
      "Select File",
      GTK_WINDOW(data->dialog),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "_Open",
      "_Cancel");
  g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(auto_delivery_import_response),
      context);
  gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}
#endif

typedef struct AutoDeliveryLotToggleTask {
  AppContext* context;
  uint64_t lot_id;
  char* lot_id_str;
  gboolean active;
  gboolean previous_active;
  gboolean success;
} AutoDeliveryLotToggleTask;

static gboolean auto_delivery_lot_toggle_complete(gpointer data) {
  AutoDeliveryLotToggleTask* task = (AutoDeliveryLotToggleTask*)data;
  if (!task || !task->context) {
    g_free(task ? task->lot_id_str : NULL);
    g_free(task);
    return G_SOURCE_REMOVE;
  }

  if (!task->success && task->lot_id_str && task->context->lots) {
    fpv_lot_t* lot = (fpv_lot_t*)g_hash_table_lookup(
        task->context->lots,
        task->lot_id_str);
    if (lot) {
      lot->active = task->previous_active ? true : false;
    }
  }

  refresh_lot_list(task->context);
  g_free(task->lot_id_str);
  g_free(task);
  return G_SOURCE_REMOVE;
}

static gpointer auto_delivery_lot_toggle_thread(gpointer data) {
  AutoDeliveryLotToggleTask* task = (AutoDeliveryLotToggleTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task) {
      task->success = FALSE;
      g_main_context_invoke(NULL, auto_delivery_lot_toggle_complete, task);
    }
    return NULL;
  }

  fpv_result_t result = fpv_core_set_lot_active(
      task->context->core,
      task->lot_id,
      task->active);
  task->success = result == FPV_OK;
  if (result != FPV_OK) {
    schedule_dialog(task->context, "Lots", "Failed to update lot state.");
  }
  g_main_context_invoke(NULL, auto_delivery_lot_toggle_complete, task);
  return NULL;
}

static void auto_delivery_queue_lot_toggle(
    AppContext* context,
    const char* lot_id_str,
    gboolean active) {
  if (!context || !context->core || !lot_id_str || !lot_id_str[0]) {
    return;
  }

  guint64 lot_id = g_ascii_strtoull(lot_id_str, NULL, 10);
  if (lot_id == 0) {
    return;
  }

  gboolean previous_active = !active;
  if (context->lots) {
    fpv_lot_t* lot = (fpv_lot_t*)g_hash_table_lookup(
        context->lots,
        lot_id_str);
    if (lot) {
      previous_active = lot->active ? TRUE : FALSE;
      if (previous_active == active) {
        return;
      }
      lot->active = active ? true : false;
    }
  }

  refresh_lot_list(context);

  AutoDeliveryLotToggleTask* task =
      (AutoDeliveryLotToggleTask*)g_new0(AutoDeliveryLotToggleTask, 1);
  if (!task) {
    return;
  }
  task->context = context;
  task->lot_id = lot_id;
  task->lot_id_str = g_strdup(lot_id_str);
  task->active = active;
  task->previous_active = previous_active;

  g_thread_new("fpv-lot-toggle", auto_delivery_lot_toggle_thread, task);
}

void auto_delivery_dialog_save(GtkButton* button, gpointer user_data) {
  AutoDeliveryDialog* data = (AutoDeliveryDialog*)user_data;
  if (!data || !data->context) {
    return;
  }

  const char* lot_name = gtk_editable_get_text(
      GTK_EDITABLE(data->lot_entry));
  const char* response = gtk_editable_get_text(
      GTK_EDITABLE(data->response_entry));
  gchar* safe_lot_name = auto_delivery_resolve_lot_name(
      data->context,
      lot_name);
  gboolean timed = gtk_switch_get_active(GTK_SWITCH(data->timed_switch));
  gint timer_hours = gtk_spin_button_get_value_as_int(
      GTK_SPIN_BUTTON(data->timer_hours));
  GtkTextIter start;
  GtkTextIter end;
  gchar* products_text = NULL;
  gboolean has_products = FALSE;
  if (data->products_buffer) {
    gtk_text_buffer_get_bounds(data->products_buffer, &start, &end);
    products_text = gtk_text_buffer_get_text(
        data->products_buffer,
        &start,
        &end,
        FALSE);
    has_products = auto_delivery_products_has_content(products_text);
  }
  if (!safe_lot_name || !safe_lot_name[0] || !response || !response[0]) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Lot name and response are required.");
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }
  if (timed && !has_products) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Timed lots require a products list.");
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }
  if (timed && timer_hours <= 0) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Timed lots require a timer greater than 0 hours.");
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }
  gboolean uses_product_token = FALSE;
  if (response) {
    uses_product_token = strstr(response, "$product") ||
        strstr(response, "$products");
  }
  if (!timed && has_products && !uses_product_token) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Response must include $product or $products when products are set.");
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }

  const char* products_filename = data->products_filename;
  if (has_products && (!products_filename || !products_filename[0])) {
    g_free(data->products_filename);
    data->products_filename = auto_delivery_generate_products_filename();
    products_filename = data->products_filename;
  }
  if (has_products) {
    if (!products_filename || !products_filename[0]) {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          "Failed to create products file name.");
      g_free(safe_lot_name);
      g_free(products_text);
      return;
    }
    if (!data->context->data_dir) {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          "Storage directory is not available.");
      g_free(safe_lot_name);
      g_free(products_text);
      return;
    }
    gchar* products_dir = g_build_filename(
        data->context->data_dir,
        "products",
        NULL);
    if (g_mkdir_with_parents(products_dir, 0755) != 0) {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          "Failed to create products directory.");
      g_free(products_dir);
      g_free(safe_lot_name);
      g_free(products_text);
      return;
    }
    gchar* products_path = g_build_filename(
        products_dir,
        products_filename,
        NULL);
    GError* write_error = NULL;
    if (!g_file_set_contents(
            products_path,
            products_text ? products_text : "",
            -1,
            &write_error)) {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          write_error && write_error->message
              ? write_error->message
              : "Failed to write products file.");
      g_clear_error(&write_error);
      g_free(products_path);
      g_free(products_dir);
      g_free(safe_lot_name);
      g_free(products_text);
      return;
    }
    g_free(products_path);
    g_free(products_dir);
  }

  gchar* path = g_build_filename(
      data->context->config_dir,
      "auto_delivery.cfg",
      NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini && error.code == FPV_ERR_IO) {
    g_mkdir_with_parents(data->context->config_dir, 0755);
    GError* write_error = NULL;
    if (g_file_set_contents(path, "", -1, &write_error)) {
      ini = fpv_ini_load(path, &error);
    } else {
      show_message_dialog(
          GTK_WINDOW(data->dialog),
          "Auto Delivery",
          write_error && write_error->message
              ? write_error->message
              : "Failed to create auto_delivery.cfg.");
      g_clear_error(&write_error);
    }
  }
  if (!ini) {
    gchar* message = g_strdup_printf(
        "Failed to load auto_delivery.cfg (code=%d line=%zu).",
        (int)error.code,
        error.line);
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        message);
    g_free(message);
    g_free(path);
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }

  if (data->original_section &&
      strcmp(data->original_section, safe_lot_name) != 0) {
    fpv_ini_remove_section(ini, data->original_section);
  }

  fpv_ini_set(ini, safe_lot_name, "response", response);
  if (has_products) {
    fpv_ini_set(ini, safe_lot_name, "productsFileName", products_filename);
  } else {
    fpv_ini_remove_entry(ini, safe_lot_name, "productsFileName");
  }

  gboolean auto_delivery_enabled =
      gtk_switch_get_active(GTK_SWITCH(data->auto_delivery_switch));
  fpv_ini_set(ini, safe_lot_name, "disable",
              auto_delivery_enabled ? "0" : "1");
  fpv_ini_set(ini, safe_lot_name, "timed", timed ? "1" : "0");
  if (timed) {
    char timer_buf[16];
    snprintf(timer_buf, sizeof(timer_buf), "%d", timer_hours);
    fpv_ini_set(ini, safe_lot_name, "timerHours", timer_buf);
  } else {
    fpv_ini_remove_entry(ini, safe_lot_name, "timerHours");
  }
  fpv_ini_set(ini, safe_lot_name, "disableAutoRestore",
              gtk_switch_get_active(GTK_SWITCH(data->restore_switch))
                  ? "0" : "1");
  fpv_ini_set(ini, safe_lot_name, "disableAutoDisable",
              gtk_switch_get_active(GTK_SWITCH(data->auto_disable_switch))
                  ? "0" : "1");
  fpv_ini_set(ini, safe_lot_name, "disableAutoDelivery",
              auto_delivery_enabled ? "0" : "1");
  fpv_ini_set(ini, safe_lot_name, "disableMultiDelivery",
              gtk_switch_get_active(GTK_SWITCH(data->multi_delivery_switch))
                  ? "0" : "1");
  if (data->multi_delivery_count) {
    int multi_count = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(data->multi_delivery_count));
    if (multi_count > 0) {
      char count_buf[16];
      snprintf(count_buf, sizeof(count_buf), "%d", multi_count);
      fpv_ini_set(ini, safe_lot_name, "multiDeliveryCount", count_buf);
    } else {
      fpv_ini_remove_entry(ini, safe_lot_name, "multiDeliveryCount");
    }
  }

  fpv_result_t save_result = fpv_ini_save(ini, path);
  fpv_ini_destroy(ini);
  g_free(path);
  if (save_result != FPV_OK) {
    show_message_dialog(
        GTK_WINDOW(data->dialog),
        "Auto Delivery",
        "Failed to save auto_delivery.cfg.");
    g_free(safe_lot_name);
    g_free(products_text);
    return;
  }

  refresh_auto_delivery_list(data->context);
  if (data->context->running) {
    fpv_core_reload_auto_delivery(data->context->core);
  }

  if (app_has_feature(data->context, FPV_FEATURE_STATUS_MANAGER) &&
      data->lot_enabled_switch &&
      gtk_widget_get_sensitive(data->lot_enabled_switch)) {
    const char* lot_name_current = gtk_editable_get_text(
        GTK_EDITABLE(data->lot_entry));
    fpv_lot_t* lot = auto_delivery_find_lot(data->context, lot_name_current);
    const char* lot_id = lot && lot->id ? lot->id : NULL;
    if (!lot_id &&
        data->lot_id &&
        data->original_section &&
        lot_name_current &&
        strcmp(lot_name_current, data->original_section) == 0) {
      lot_id = data->lot_id;
    }
    if (lot_id && lot_id[0]) {
      gboolean desired_active =
          gtk_switch_get_active(GTK_SWITCH(data->lot_enabled_switch));
      gboolean current_active = lot ? (lot->active ? TRUE : FALSE) : !desired_active;
      if (!lot || current_active != desired_active) {
        auto_delivery_queue_lot_toggle(data->context, lot_id, desired_active);
      }
    } else if (lot_name_current && lot_name_current[0]) {
      schedule_dialog(
          data->context,
          "Lots",
          "Lot not found; status not updated.");
    }
  }

  g_free(safe_lot_name);
  g_free(products_text);
  gtk_window_close(GTK_WINDOW(data->dialog));
}

void show_auto_delivery_dialog(
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
  gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 760);

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
  GtkWidget* products_view = gtk_text_view_new();
  GtkTextBuffer* products_buffer =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(products_view));
  gtk_text_view_set_wrap_mode(
      GTK_TEXT_VIEW(products_view),
      GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(products_view), TRUE);
  GtkWidget* products_scroller = gtk_scrolled_window_new();
  gtk_widget_set_size_request(products_scroller, -1, 160);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(products_scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(products_scroller),
      products_view);
  GtkWidget* products_actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* products_import = gtk_button_new_with_label("Upload file");
  gtk_box_append(GTK_BOX(products_actions), products_import);
  GtkWidget* products_hint = gtk_label_new(
      "One product per line. Use \\n for line breaks.");
  gtk_label_set_xalign(GTK_LABEL(products_hint), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(products_hint), TRUE);
  gtk_widget_add_css_class(products_hint, "muted");
  GtkWidget* products_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand(products_box, TRUE);
  gtk_widget_set_vexpand(products_box, TRUE);
  gtk_box_append(GTK_BOX(products_box), products_scroller);
  gtk_box_append(GTK_BOX(products_box), products_actions);
  gtk_box_append(GTK_BOX(products_box), products_hint);

  GtkWidget* preview_view = gtk_text_view_new();
  GtkTextBuffer* preview_buffer =
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(preview_view));
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(preview_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview_view), TRUE);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(preview_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview_view), FALSE);
  GtkWidget* preview_scroller = gtk_scrolled_window_new();
  gtk_widget_set_size_request(preview_scroller, -1, 140);
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(preview_scroller),
      GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(
      GTK_SCROLLED_WINDOW(preview_scroller),
      preview_view);
  GtkWidget* preview_hint = gtk_label_new(
      "Preview replaces $product (or $products) with the first product.");
  gtk_label_set_xalign(GTK_LABEL(preview_hint), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(preview_hint), TRUE);
  gtk_widget_add_css_class(preview_hint, "muted");
  GtkWidget* preview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand(preview_box, TRUE);
  gtk_box_append(GTK_BOX(preview_box), preview_scroller);
  gtk_box_append(GTK_BOX(preview_box), preview_hint);
  GtkWidget* timed_switch = gtk_switch_new();
  GtkWidget* timer_hours = gtk_spin_button_new_with_range(1, 720, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(timer_hours), TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(timer_hours), 24);
  GtkWidget* lot_enabled_switch = gtk_switch_new();
  GtkWidget* restore_switch = gtk_switch_new();
  GtkWidget* auto_disable_switch = gtk_switch_new();
  GtkWidget* auto_delivery_switch = gtk_switch_new();
  GtkWidget* multi_delivery_switch = gtk_switch_new();
  GtkWidget* multi_delivery_count = gtk_spin_button_new_with_range(1, 100, 1);
  gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(multi_delivery_count), TRUE);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(multi_delivery_count), 1);

  gtk_switch_set_active(GTK_SWITCH(lot_enabled_switch), TRUE);
  gtk_widget_set_sensitive(
      lot_enabled_switch,
      app_has_feature(context, FPV_FEATURE_STATUS_MANAGER));
  gtk_switch_set_active(GTK_SWITCH(restore_switch), TRUE);
  gtk_switch_set_active(GTK_SWITCH(auto_disable_switch), TRUE);
  gtk_switch_set_active(GTK_SWITCH(auto_delivery_switch), TRUE);
  gtk_switch_set_active(GTK_SWITCH(multi_delivery_switch), TRUE);

  add_setting_row(grid, AD_ROW_LOT, "Lot name", lot_entry);
  add_setting_row(grid, AD_ROW_RESPONSE, "Response", response_entry);
  GtkWidget* products_label = gtk_label_new("Products");
  gtk_label_set_xalign(GTK_LABEL(products_label), 0.0f);
  gtk_widget_set_margin_bottom(products_label, 4);
  gtk_widget_set_margin_top(products_label, 4);
  gtk_widget_set_valign(products_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), products_label, 0, AD_ROW_PRODUCTS, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), products_box, 1, AD_ROW_PRODUCTS, 1, 1);
  GtkWidget* preview_label = gtk_label_new("Preview");
  gtk_label_set_xalign(GTK_LABEL(preview_label), 0.0f);
  gtk_widget_set_margin_bottom(preview_label, 4);
  gtk_widget_set_margin_top(preview_label, 4);
  gtk_widget_set_valign(preview_label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), preview_label, 0, AD_ROW_PREVIEW, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), preview_box, 1, AD_ROW_PREVIEW, 1, 1);
  add_setting_row(grid, AD_ROW_TIMED, "Auto reload", timed_switch);
  GtkWidget* timer_hours_label = gtk_label_new("Reload interval (hours)");
  add_setting_row_label(grid, AD_ROW_TIMER, timer_hours_label, timer_hours);
  add_setting_row(grid, AD_ROW_ENABLED, "Enabled", lot_enabled_switch);
  add_setting_row(grid, AD_ROW_RESTORE, "Auto restore", restore_switch);
  add_setting_row(grid, AD_ROW_AUTO_DISABLE, "Auto disable", auto_disable_switch);
  add_setting_row(grid, AD_ROW_AUTO_DELIVERY, "Auto delivery", auto_delivery_switch);
  add_setting_row(grid, AD_ROW_MULTI, "Multi delivery", multi_delivery_switch);
  GtkWidget* multi_delivery_count_label = gtk_label_new("Items per delivery");
  add_setting_row_label(grid, AD_ROW_MULTI_COUNT, multi_delivery_count_label,
                        multi_delivery_count);

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget* save_button = gtk_button_new_with_label("Save");
  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), save_button);

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroller),
      GTK_POLICY_NEVER,
      GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_kinetic_scrolling(
      GTK_SCROLLED_WINDOW(scroller),
      TRUE);
  gtk_scrolled_window_set_min_content_height(
      GTK_SCROLLED_WINDOW(scroller),
      360);
  gtk_scrolled_window_set_propagate_natural_height(
      GTK_SCROLLED_WINDOW(scroller),
      FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), grid);
  gtk_widget_set_hexpand(scroller, TRUE);
  gtk_widget_set_vexpand(scroller, TRUE);

  gtk_box_append(GTK_BOX(root), scroller);
  gtk_box_append(GTK_BOX(root), actions);
  gtk_window_set_child(GTK_WINDOW(dialog), root);

  AutoDeliveryDialog* dialog_data = (AutoDeliveryDialog*)g_new0(
      AutoDeliveryDialog, 1);
  dialog_data->context = context;
  dialog_data->dialog = dialog;
  dialog_data->grid = grid;
  dialog_data->lot_entry = lot_entry;
  dialog_data->response_entry = response_entry;
  dialog_data->products_buffer = products_buffer;
  dialog_data->preview_buffer = preview_buffer;
  dialog_data->products_filename = NULL;
  dialog_data->lot_id = NULL;
  dialog_data->timed_switch = timed_switch;
  dialog_data->timer_hours = timer_hours;
  dialog_data->lot_enabled_switch = lot_enabled_switch;
  dialog_data->restore_switch = restore_switch;
  dialog_data->auto_disable_switch = auto_disable_switch;
  dialog_data->auto_delivery_switch = auto_delivery_switch;
  dialog_data->multi_delivery_switch = multi_delivery_switch;
  dialog_data->multi_delivery_count = multi_delivery_count;
  dialog_data->original_section = NULL;

  g_signal_connect(
      products_import,
      "clicked",
      G_CALLBACK(on_auto_delivery_import_clicked),
      dialog_data);
  g_signal_connect(
      response_entry,
      "changed",
      G_CALLBACK(auto_delivery_preview_changed),
      dialog_data);
  g_signal_connect(
      products_buffer,
      "changed",
      G_CALLBACK(auto_delivery_preview_buffer_changed),
      dialog_data);
  g_signal_connect(
      timed_switch,
      "notify::active",
      G_CALLBACK(auto_delivery_timed_switch_changed),
      dialog_data);
  g_signal_connect(
      multi_delivery_switch,
      "notify::active",
      G_CALLBACK(auto_delivery_multi_switch_changed),
      dialog_data);
  g_signal_connect(
      multi_delivery_count,
      "value-changed",
      G_CALLBACK(auto_delivery_multi_count_changed),
      dialog_data);
  g_signal_connect(
      auto_delivery_switch,
      "notify::active",
      G_CALLBACK(auto_delivery_delivery_switch_changed),
      dialog_data);

  if (section_name) {
    gchar* path = g_build_filename(
        context->config_dir,
        "auto_delivery.cfg",
        NULL);
    fpv_ini_error_t error;
    fpv_ini_t* ini = fpv_ini_load(path, &error);
    if (ini) {
      gchar* section_key = auto_delivery_find_section_key(ini, section_name);
      const char* target_section = section_key ? section_key : section_name;
      if (section_key) {
        dialog_data->original_section = g_strdup(section_key);
      }
      gtk_editable_set_text(GTK_EDITABLE(lot_entry), target_section);

      const char* response = fpv_ini_get(ini, target_section, "response");
      const char* products = fpv_ini_get(
          ini, target_section, "productsFileName");
      const char* disable = fpv_ini_get(ini, target_section, "disable");
      const char* timed = fpv_ini_get(ini, target_section, "timed");
      const char* timer_hours = fpv_ini_get(
          ini, target_section, "timerHours");
      const char* restore = fpv_ini_get(
          ini, target_section, "disableAutoRestore");
      const char* auto_disable = fpv_ini_get(
          ini, target_section, "disableAutoDisable");
      const char* auto_delivery = fpv_ini_get(
          ini, target_section, "disableAutoDelivery");
      const char* multi_delivery = fpv_ini_get(
          ini, target_section, "disableMultiDelivery");
      const char* multi_delivery_count_value = fpv_ini_get(
          ini, target_section, "multiDeliveryCount");

      gtk_editable_set_text(
          GTK_EDITABLE(response_entry),
          response ? response : "");
      if (products && products[0]) {
        dialog_data->products_filename = g_strdup(products);
        if (context->data_dir) {
          gchar* products_path = g_build_filename(
              context->data_dir,
              "products",
              products,
              NULL);
          gchar* contents = NULL;
          gsize length = 0;
          GError* load_error = NULL;
          if (!g_file_get_contents(
                  products_path,
                  &contents,
                  &length,
                  &load_error)) {
            show_message_dialog(
                GTK_WINDOW(dialog),
                "Auto Delivery",
                load_error && load_error->message
                    ? load_error->message
                    : "Failed to read products file.");
            g_clear_error(&load_error);
          } else {
            gtk_text_buffer_set_text(
                products_buffer,
                contents ? contents : "",
                (gint)length);
          }
          g_free(contents);
          g_free(products_path);
        }
      }
      gtk_switch_set_active(
          GTK_SWITCH(timed_switch),
          parse_ini_bool(timed, FALSE));
      if (timer_hours && timer_hours[0]) {
        char* end = NULL;
        long parsed = strtol(timer_hours, &end, 10);
        if (end && *end == '\0' && parsed > 0) {
          gtk_spin_button_set_value(
              GTK_SPIN_BUTTON(timer_hours),
              (gdouble)parsed);
        }
      }
      gtk_switch_set_active(
          GTK_SWITCH(restore_switch),
          !parse_ini_bool(restore, FALSE));
      gtk_switch_set_active(
          GTK_SWITCH(auto_disable_switch),
          !parse_ini_bool(auto_disable, FALSE));
      gboolean disable_delivery =
          parse_ini_bool(disable, FALSE) ||
          parse_ini_bool(auto_delivery, FALSE);
      gtk_switch_set_active(
          GTK_SWITCH(auto_delivery_switch),
          !disable_delivery);
      gtk_switch_set_active(
          GTK_SWITCH(multi_delivery_switch),
          !parse_ini_bool(multi_delivery, FALSE));
      if (multi_delivery_count_value && multi_delivery_count_value[0]) {
        char* end = NULL;
        long parsed = strtol(multi_delivery_count_value, &end, 10);
        if (end && *end == '\0' && parsed > 0) {
          gtk_spin_button_set_value(
              GTK_SPIN_BUTTON(multi_delivery_count),
              (gdouble)parsed);
        }
      }
      g_free(section_key);
      fpv_ini_destroy(ini);
    } else {
      gtk_editable_set_text(GTK_EDITABLE(lot_entry), section_name);
    }
    g_free(path);
  }
  const char* lot_name_current = gtk_editable_get_text(GTK_EDITABLE(lot_entry));
  fpv_lot_t* lot = auto_delivery_find_lot(context, lot_name_current);
  if (lot) {
    gtk_switch_set_active(
        GTK_SWITCH(lot_enabled_switch),
        lot->active ? TRUE : FALSE);
    g_free(dialog_data->lot_id);
    dialog_data->lot_id = lot->id ? g_strdup(lot->id) : NULL;
  }
  auto_delivery_update_delivery_visibility(dialog_data);

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

void on_auto_delivery_add(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_auto_delivery_dialog(context, NULL);
}

void on_auto_delivery_edit(GtkButton* button, gpointer user_data) {
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

void on_auto_delivery_delete(GtkButton* button, gpointer user_data) {
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
  if (!ini && error.code == FPV_ERR_IO) {
    g_mkdir_with_parents(context->config_dir, 0755);
    GError* write_error = NULL;
    if (g_file_set_contents(path, "", -1, &write_error)) {
      ini = fpv_ini_load(path, &error);
    } else {
      show_message_dialog(
          GTK_WINDOW(context->window),
          "Auto Delivery",
          write_error && write_error->message
              ? write_error->message
              : "Failed to create auto_delivery.cfg.");
      g_clear_error(&write_error);
    }
  }
  if (!ini) {
    gchar* message = g_strdup_printf(
        "Failed to load auto_delivery.cfg (code=%d line=%zu).",
        (int)error.code,
        error.line);
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Auto Delivery",
        message);
    g_free(message);
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

void on_auto_delivery_reload(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  refresh_auto_delivery_list(context);
  if (context->running) {
    fpv_core_reload_auto_delivery(context->core);
  }
}

void refresh_settings_from_file(AppContext* context) {
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
      GTK_SWITCH(context->settings.review_auto_refund),
      parse_ini_bool(fpv_ini_get(ini, "FunPay", "autoRefund"), FALSE));
  value = fpv_ini_get(ini, "FunPay", "autoRefundMaxStars");
  if (value && value[0]) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->settings.review_auto_refund_max_stars),
        (double)g_ascii_strtoll(value, NULL, 10));
  } else {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->settings.review_auto_refund_max_stars),
        2.0);
  }

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
  apply_settings_entitlements(context);
}

void apply_settings_entitlements(AppContext* context) {
  if (!context) {
    return;
  }
  gboolean allow_auto_raise = app_has_feature(context, FPV_FEATURE_AUTO_RAISE);
  gboolean allow_auto_response = app_has_feature(context, FPV_FEATURE_AUTO_RESPONSE);
  gboolean allow_auto_delivery = app_has_feature(context, FPV_FEATURE_AUTO_DELIVERY);
  gboolean allow_multi_delivery =
      allow_auto_delivery &&
      app_has_feature(context, FPV_FEATURE_MULTI_DELIVERY);
  gboolean allow_auto_restore =
      allow_auto_delivery &&
      app_has_feature(context, FPV_FEATURE_STATUS_MANAGER);
  gboolean allow_auto_disable =
      allow_auto_delivery &&
      app_has_feature(context, FPV_FEATURE_STALE_LOT_DETECTOR);
  gboolean allow_telegram =
      app_has_feature(context, FPV_FEATURE_TELEGRAM_NOTIFICATIONS);
  gboolean allow_blacklist =
      app_has_feature(context, FPV_FEATURE_BUYER_BLACKLIST);
  gboolean allow_proxy =
      app_has_feature(context, FPV_FEATURE_PROXY_IPV4);
  gboolean allow_watermark =
      app_has_feature(context, FPV_FEATURE_WATERMARK);
  gboolean allow_ai_reviews =
      app_has_feature(context, FPV_FEATURE_AI_REVIEWS);
  gboolean allow_auto_refund =
      app_has_feature(context, FPV_FEATURE_AUTO_REFUND);

  if (context->settings.funpay_auto_raise) {
    if (!allow_auto_raise) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.funpay_auto_raise), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.funpay_auto_raise, allow_auto_raise);
  }
  if (context->settings.funpay_auto_response) {
    if (!allow_auto_response) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.funpay_auto_response), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.funpay_auto_response, allow_auto_response);
  }
  if (context->settings.funpay_multi_delivery) {
    if (!allow_multi_delivery) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.funpay_multi_delivery), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.funpay_multi_delivery, allow_multi_delivery);
  }
  if (context->settings.funpay_auto_restore) {
    if (!allow_auto_restore) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.funpay_auto_restore), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.funpay_auto_restore, allow_auto_restore);
  }
  if (context->settings.funpay_auto_disable) {
    if (!allow_auto_disable) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.funpay_auto_disable), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.funpay_auto_disable, allow_auto_disable);
  }

  if (context->settings.greetings_cache_init) {
    if (!allow_auto_response) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.greetings_cache_init), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.greetings_cache_init, allow_auto_response);
  }
  if (context->settings.greetings_ignore_system) {
    if (!allow_auto_response) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.greetings_ignore_system), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.greetings_ignore_system, allow_auto_response);
  }
  if (context->settings.greetings_send) {
    if (!allow_auto_response) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.greetings_send), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.greetings_send, allow_auto_response);
  }
  if (context->settings.greetings_text) {
    gtk_widget_set_sensitive(context->settings.greetings_text, allow_auto_response);
  }
  if (context->settings.order_confirm_send) {
    if (!allow_auto_response) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.order_confirm_send), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.order_confirm_send, allow_auto_response);
  }
  if (context->settings.order_confirm_text) {
    gtk_widget_set_sensitive(context->settings.order_confirm_text, allow_auto_response);
  }

  if (context->settings.review_reply_enabled_all) {
    if (!allow_ai_reviews) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.review_reply_enabled_all), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.review_reply_enabled_all, allow_ai_reviews);
  }
  for (size_t i = 0; i < 5; i++) {
    if (context->settings.review_reply_enabled[i]) {
      if (!allow_ai_reviews) {
        gtk_switch_set_active(GTK_SWITCH(context->settings.review_reply_enabled[i]), FALSE);
      }
      gtk_widget_set_sensitive(context->settings.review_reply_enabled[i], allow_ai_reviews);
    }
    if (context->settings.review_reply_texts[i]) {
      gtk_widget_set_sensitive(context->settings.review_reply_texts[i], allow_ai_reviews);
    }
  }

  if (context->settings.review_auto_refund) {
    if (!allow_auto_refund) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.review_auto_refund), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.review_auto_refund, allow_auto_refund);
  }
  if (context->settings.review_auto_refund_max_stars) {
    gtk_widget_set_sensitive(
        context->settings.review_auto_refund_max_stars,
        allow_auto_refund);
  }

  if (context->settings.telegram_enabled) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.telegram_enabled), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.telegram_enabled, allow_telegram);
  }
  if (context->settings.telegram_token) {
    gtk_widget_set_sensitive(context->settings.telegram_token, allow_telegram);
  }
  if (context->settings.telegram_secret) {
    gtk_widget_set_sensitive(context->settings.telegram_secret, allow_telegram);
  }
  if (context->settings.include_my) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.include_my), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.include_my, allow_telegram);
  }
  if (context->settings.include_fp) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.include_fp), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.include_fp, allow_telegram);
  }
  if (context->settings.include_bot) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.include_bot), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.include_bot, allow_telegram);
  }
  if (context->settings.notify_only_my) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.notify_only_my), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.notify_only_my, allow_telegram);
  }
  if (context->settings.notify_only_fp) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.notify_only_fp), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.notify_only_fp, allow_telegram);
  }
  if (context->settings.notify_only_bot) {
    if (!allow_telegram) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.notify_only_bot), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.notify_only_bot, allow_telegram);
  }

  if (context->settings.block_delivery) {
    if (!allow_blacklist) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.block_delivery), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.block_delivery, allow_blacklist);
  }
  if (context->settings.block_response) {
    if (!allow_blacklist) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.block_response), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.block_response, allow_blacklist);
  }
  if (context->settings.block_new_message) {
    if (!allow_blacklist) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.block_new_message), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.block_new_message, allow_blacklist);
  }
  if (context->settings.block_new_order) {
    if (!allow_blacklist) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.block_new_order), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.block_new_order, allow_blacklist);
  }
  if (context->settings.block_command) {
    if (!allow_blacklist) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.block_command), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.block_command, allow_blacklist);
  }

  if (context->settings.proxy_enable) {
    if (!allow_proxy) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.proxy_enable), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.proxy_enable, allow_proxy);
  }
  if (context->settings.proxy_check) {
    if (!allow_proxy) {
      gtk_switch_set_active(GTK_SWITCH(context->settings.proxy_check), FALSE);
    }
    gtk_widget_set_sensitive(context->settings.proxy_check, allow_proxy);
  }
  if (context->settings.proxy_ip) {
    gtk_widget_set_sensitive(context->settings.proxy_ip, allow_proxy);
  }
  if (context->settings.proxy_port) {
    gtk_widget_set_sensitive(context->settings.proxy_port, allow_proxy);
  }
  if (context->settings.proxy_login) {
    gtk_widget_set_sensitive(context->settings.proxy_login, allow_proxy);
  }
  if (context->settings.proxy_password) {
    gtk_widget_set_sensitive(context->settings.proxy_password, allow_proxy);
  }

  if (context->settings.other_watermark) {
    gtk_widget_set_sensitive(context->settings.other_watermark, allow_watermark);
  }
}

void refresh_identity_labels(AppContext* context) {
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
    if (!app_has_feature(context, FPV_FEATURE_MANAGER_SYSTEM)) {
      can_invite = FALSE;
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
