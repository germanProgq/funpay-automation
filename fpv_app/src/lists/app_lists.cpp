#include "lists/app_lists.h"

#include "helpers/app_helpers.h"
#include "settings/app_settings.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

void refresh_metrics(AppContext* context) {
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

gint compare_chat_items(gconstpointer a, gconstpointer b) {
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

void refresh_chat_list(AppContext* context) {
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

void refresh_message_list(AppContext* context) {
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

void clear_chat_state(AppContext* context) {
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

void load_cached_chats(AppContext* context) {
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

gint compare_order_items(gconstpointer a, gconstpointer b) {
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

void refresh_order_list(AppContext* context) {
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

gint compare_lot_items(gconstpointer a, gconstpointer b) {
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

typedef struct LotCloneTask {
  AppContext* context;
  guint64 lot_id;
  char* title;
  char* original_title;
  GtkWidget* button;
  fpv_result_t result;
} LotCloneTask;

typedef struct LotCloneDialog {
  AppContext* context;
  GtkWidget* dialog;
  GtkWidget* title_entry;
  GtkWidget* source_button;
  guint64 lot_id;
  char* original_title;
} LotCloneDialog;

typedef struct LotCloneLinkTask {
  AppContext* context;
  char* link;
  char* title;
  fpv_result_t result;
} LotCloneLinkTask;

typedef struct LotCloneLinkDialog {
  AppContext* context;
  GtkWidget* dialog;
  GtkWidget* link_entry;
  GtkWidget* title_entry;
} LotCloneLinkDialog;

gboolean lot_toggle_complete(gpointer data) {
  LotToggleTask* task = (LotToggleTask*)data;
  if (!task || !task->context) {
    return G_SOURCE_REMOVE;
  }
  if (task->context->closing) {
    g_free(task);
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

gpointer lot_toggle_thread(gpointer data) {
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

void on_lot_toggle_changed(
    GObject* object,
    GParamSpec* spec,
    gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  GtkWidget* toggle = GTK_WIDGET(object);
  if (!context || context->closing || !toggle) {
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

void lot_clone_dialog_destroy(GtkWidget* widget, gpointer user_data) {
  LotCloneDialog* dialog = (LotCloneDialog*)user_data;
  (void)widget;
  if (!dialog) {
    return;
  }
  g_free(dialog->original_title);
  g_free(dialog);
}

gboolean lot_clone_complete(gpointer data) {
  LotCloneTask* task = (LotCloneTask*)data;
  if (!task) {
    return G_SOURCE_REMOVE;
  }
  if (task->button) {
    g_object_set_data(G_OBJECT(task->button), "busy", NULL);
    if (!task->context || !task->context->closing) {
      gtk_widget_set_sensitive(task->button, TRUE);
    }
    g_object_unref(task->button);
  }
  if (task->context && !task->context->closing) {
    if (task->result != FPV_OK) {
      schedule_dialog(task->context, "Lots", "Lot clone failed.");
    } else {
      schedule_dialog(task->context, "Lots", "Lot cloned.");
    }
  }
  g_free(task->title);
  g_free(task->original_title);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gpointer lot_clone_thread(gpointer data) {
  LotCloneTask* task = (LotCloneTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task) {
      task->result = FPV_ERR_INVALID_STATE;
      g_main_context_invoke(NULL, lot_clone_complete, task);
    }
    return NULL;
  }
  task->result = fpv_core_clone_lot(
      task->context->core,
      task->lot_id,
      task->title,
      task->original_title,
      NULL);
  if (task->result == FPV_OK) {
    fpv_core_refresh_lots(task->context->core);
  }
  g_main_context_invoke(NULL, lot_clone_complete, task);
  return NULL;
}

void lot_clone_dialog_submit(GtkButton* button, gpointer user_data) {
  LotCloneDialog* dialog = (LotCloneDialog*)user_data;
  (void)button;
  if (!dialog || !dialog->context) {
    return;
  }
  if (!dialog->context->running) {
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Start the core before cloning lots.");
    return;
  }

  const char* title_text =
      gtk_editable_get_text(GTK_EDITABLE(dialog->title_entry));
  const char* final_title = (title_text && title_text[0]) ? title_text : NULL;

  LotCloneTask* task = (LotCloneTask*)g_new0(LotCloneTask, 1);
  if (!task) {
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Failed to start lot clone.");
    return;
  }
  task->context = dialog->context;
  task->lot_id = dialog->lot_id;
  if (final_title) {
    task->title = g_strdup(final_title);
    if (!task->title) {
      g_free(task);
      show_message_dialog(
          GTK_WINDOW(dialog->dialog),
          "Lots",
          "Failed to start lot clone.");
      return;
    }
  }
  if (dialog->original_title && dialog->original_title[0]) {
    task->original_title = g_strdup(dialog->original_title);
    if (!task->original_title) {
      g_free(task->title);
      g_free(task);
      show_message_dialog(
          GTK_WINDOW(dialog->dialog),
          "Lots",
          "Failed to start lot clone.");
      return;
    }
  }
  task->button = dialog->source_button;

  if (task->button) {
    g_object_ref(task->button);
    g_object_set_data(G_OBJECT(task->button), "busy", task);
    gtk_widget_set_sensitive(task->button, FALSE);
  }

  gtk_window_close(GTK_WINDOW(dialog->dialog));
  g_thread_new("fpv-lot-clone", lot_clone_thread, task);
}

void show_lot_clone_dialog(
    AppContext* context,
    guint64 lot_id,
    const char* title,
    GtkWidget* source_button) {
  if (!context || !context->window || lot_id == 0) {
    return;
  }

  GtkWidget* dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Clone Lot");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 180);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);

  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

  GtkWidget* title_entry = gtk_entry_new();
  add_setting_row(grid, 0, "Title", title_entry);

  GtkWidget* note = gtk_label_new("Leave empty to keep the original title.");
  gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
  gtk_widget_add_css_class(note, "muted");

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget* clone_button = gtk_button_new_with_label("Clone");

  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), clone_button);

  gtk_box_append(GTK_BOX(root), grid);
  gtk_box_append(GTK_BOX(root), note);
  gtk_box_append(GTK_BOX(root), actions);
  gtk_window_set_child(GTK_WINDOW(dialog), root);

  if (title && title[0]) {
    gchar* safe_title = sanitize_utf8(title);
    if (safe_title && safe_title[0]) {
      gchar* default_title = g_strdup_printf("%s (copy)", safe_title);
      gtk_editable_set_text(GTK_EDITABLE(title_entry), default_title);
      g_free(default_title);
    }
    g_free(safe_title);
  }

  LotCloneDialog* dialog_data = (LotCloneDialog*)g_new0(LotCloneDialog, 1);
  if (!dialog_data) {
    gtk_window_close(GTK_WINDOW(dialog));
    return;
  }
  dialog_data->context = context;
  dialog_data->dialog = dialog;
  dialog_data->title_entry = title_entry;
  dialog_data->source_button = source_button;
  dialog_data->lot_id = lot_id;
  dialog_data->original_title = title ? g_strdup(title) : NULL;

  g_signal_connect(clone_button, "clicked", G_CALLBACK(lot_clone_dialog_submit), dialog_data);
  g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_close), dialog);
  g_signal_connect(dialog, "destroy", G_CALLBACK(lot_clone_dialog_destroy), dialog_data);

  gtk_window_present(GTK_WINDOW(dialog));
}

void on_lot_clone_clicked(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !button) {
    return;
  }
  gpointer busy = g_object_get_data(G_OBJECT(button), "busy");
  if (busy) {
    return;
  }
  const gchar* id_text = (const gchar*)g_object_get_data(
      G_OBJECT(button),
      "lot-id");
  if (!id_text || !id_text[0]) {
    return;
  }
  guint64 lot_id = g_ascii_strtoull(id_text, NULL, 10);
  if (lot_id == 0) {
    return;
  }
  if (!context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Lots",
        "Start the core before cloning lots.");
    return;
  }
  const gchar* title = (const gchar*)g_object_get_data(
      G_OBJECT(button),
      "lot-title");
  show_lot_clone_dialog(context, lot_id, title ? title : "", GTK_WIDGET(button));
}

void lot_clone_link_dialog_destroy(GtkWidget* widget, gpointer user_data) {
  LotCloneLinkDialog* dialog = (LotCloneLinkDialog*)user_data;
  (void)widget;
  if (!dialog) {
    return;
  }
  g_free(dialog);
}

gboolean lot_clone_link_complete(gpointer data) {
  LotCloneLinkTask* task = (LotCloneLinkTask*)data;
  if (!task) {
    return G_SOURCE_REMOVE;
  }
  if (task->context && !task->context->closing) {
    if (task->result != FPV_OK) {
      schedule_dialog(task->context, "Lots", "Lot clone from link failed.");
    } else {
      schedule_dialog(task->context, "Lots", "Lot cloned from link.");
    }
  }
  g_free(task->link);
  g_free(task->title);
  g_free(task);
  return G_SOURCE_REMOVE;
}

gpointer lot_clone_link_thread(gpointer data) {
  LotCloneLinkTask* task = (LotCloneLinkTask*)data;
  if (!task || !task->context || !task->context->core) {
    if (task) {
      task->result = FPV_ERR_INVALID_STATE;
      g_main_context_invoke(NULL, lot_clone_link_complete, task);
    }
    return NULL;
  }
  task->result = fpv_core_clone_lot_from_url(
      task->context->core,
      task->link,
      task->title,
      NULL);
  if (task->result == FPV_OK) {
    fpv_core_refresh_lots(task->context->core);
  }
  g_main_context_invoke(NULL, lot_clone_link_complete, task);
  return NULL;
}

void lot_clone_link_dialog_submit(GtkButton* button, gpointer user_data) {
  LotCloneLinkDialog* dialog = (LotCloneLinkDialog*)user_data;
  (void)button;
  if (!dialog || !dialog->context) {
    return;
  }
  if (!dialog->context->running) {
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Start the core before cloning lots.");
    return;
  }
  const char* link_text =
      gtk_editable_get_text(GTK_EDITABLE(dialog->link_entry));
  const char* title_text =
      gtk_editable_get_text(GTK_EDITABLE(dialog->title_entry));
  gchar* link = g_strdup(link_text ? link_text : "");
  if (!link) {
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Failed to start lot clone.");
    return;
  }
  g_strstrip(link);
  if (!link[0]) {
    g_free(link);
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Lot link is required.");
    return;
  }
  gchar* title = NULL;
  if (title_text && title_text[0]) {
    title = g_strdup(title_text);
    if (!title) {
      g_free(link);
      show_message_dialog(
          GTK_WINDOW(dialog->dialog),
          "Lots",
          "Failed to start lot clone.");
      return;
    }
  }

  LotCloneLinkTask* task = (LotCloneLinkTask*)g_new0(LotCloneLinkTask, 1);
  if (!task) {
    g_free(link);
    g_free(title);
    show_message_dialog(
        GTK_WINDOW(dialog->dialog),
        "Lots",
        "Failed to start lot clone.");
    return;
  }
  task->context = dialog->context;
  task->link = link;
  task->title = title;

  gtk_window_close(GTK_WINDOW(dialog->dialog));
  g_thread_new("fpv-lot-clone-link", lot_clone_link_thread, task);
}

void show_lot_clone_link_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }

  GtkWidget* dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Clone Lot From Link");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 200);

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);

  GtkWidget* grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

  GtkWidget* link_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(link_entry), "FunPay lot link or ID");
  GtkWidget* title_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Optional title override");

  add_setting_row(grid, 0, "Link", link_entry);
  add_setting_row(grid, 1, "Title", title_entry);

  GtkWidget* note = gtk_label_new("Cloned lots start disabled.");
  gtk_label_set_xalign(GTK_LABEL(note), 0.0f);
  gtk_widget_add_css_class(note, "muted");

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget* cancel_button = gtk_button_new_with_label("Cancel");
  GtkWidget* clone_button = gtk_button_new_with_label("Clone");

  gtk_box_append(GTK_BOX(actions), cancel_button);
  gtk_box_append(GTK_BOX(actions), clone_button);

  gtk_box_append(GTK_BOX(root), grid);
  gtk_box_append(GTK_BOX(root), note);
  gtk_box_append(GTK_BOX(root), actions);
  gtk_window_set_child(GTK_WINDOW(dialog), root);

  LotCloneLinkDialog* dialog_data =
      (LotCloneLinkDialog*)g_new0(LotCloneLinkDialog, 1);
  if (!dialog_data) {
    gtk_window_close(GTK_WINDOW(dialog));
    return;
  }
  dialog_data->context = context;
  dialog_data->dialog = dialog;
  dialog_data->link_entry = link_entry;
  dialog_data->title_entry = title_entry;

  g_signal_connect(clone_button, "clicked", G_CALLBACK(lot_clone_link_dialog_submit), dialog_data);
  g_signal_connect_swapped(cancel_button, "clicked", G_CALLBACK(gtk_window_close), dialog);
  g_signal_connect(dialog, "destroy", G_CALLBACK(lot_clone_link_dialog_destroy), dialog_data);

  gtk_window_present(GTK_WINDOW(dialog));
}

void on_lot_clone_link_clicked(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  (void)button;
  if (!context) {
    return;
  }
  if (!context->running) {
    show_message_dialog(
        GTK_WINDOW(context->window),
        "Lots",
        "Start the core before cloning lots.");
    return;
  }
  show_lot_clone_link_dialog(context);
}

void on_lot_auto_delivery_clicked(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context) {
    return;
  }
  const gchar* title = (const gchar*)g_object_get_data(
      G_OBJECT(button), "lot-title");
  const gchar* fallback_id = (const gchar*)g_object_get_data(
      G_OBJECT(button), "lot-id");
  if (title && title[0]) {
    show_auto_delivery_dialog(context, title);
    return;
  }
  if (fallback_id && fallback_id[0]) {
    show_auto_delivery_dialog(context, fallback_id);
    return;
  }
  show_auto_delivery_dialog(context, NULL);
}

void refresh_lot_list(AppContext* context) {
  if (!context || !context->lot_list || !context->lots) {
    return;
  }

  clear_list_box(context->lot_list);
  gboolean allow_status_manager =
      app_has_feature(context, FPV_FEATURE_STATUS_MANAGER);
  gboolean allow_auto_delivery =
      app_has_feature(context, FPV_FEATURE_AUTO_DELIVERY);
  gboolean allow_lot_cloner =
      app_has_feature(context, FPV_FEATURE_LOT_CLONER);

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
    gtk_widget_set_sensitive(toggle, allow_status_manager);
    if (lot->id) {
      g_object_set_data_full(
          G_OBJECT(toggle),
          "lot-id",
          g_strdup(lot->id),
          g_free);
    }
    g_signal_connect(toggle, "notify::active", G_CALLBACK(on_lot_toggle_changed), context);

    GtkWidget* delivery_button = NULL;
    if (allow_auto_delivery) {
      delivery_button = gtk_button_new_with_label("Auto Delivery");
      if (lot->id) {
        g_object_set_data_full(
            G_OBJECT(delivery_button),
            "lot-id",
            g_strdup(lot->id),
            g_free);
      }
      if (lot->title) {
        g_object_set_data_full(
            G_OBJECT(delivery_button),
            "lot-title",
            g_strdup(lot->title),
            g_free);
      }
      g_signal_connect(
          delivery_button,
          "clicked",
          G_CALLBACK(on_lot_auto_delivery_clicked),
          context);
    }

    GtkWidget* clone_button = NULL;
    if (allow_lot_cloner) {
      clone_button = gtk_button_new_with_label("Clone");
      if (lot->id) {
        g_object_set_data_full(
            G_OBJECT(clone_button),
            "lot-id",
            g_strdup(lot->id),
            g_free);
      }
      if (lot->title) {
        g_object_set_data_full(
            G_OBJECT(clone_button),
            "lot-title",
            g_strdup(lot->title),
            g_free);
      }
      g_signal_connect(clone_button, "clicked", G_CALLBACK(on_lot_clone_clicked), context);
    }

    gtk_box_append(GTK_BOX(info), title);
    gtk_box_append(GTK_BOX(info), meta_label);

    gtk_widget_set_hexpand(info, TRUE);
    gtk_box_append(GTK_BOX(box), info);
    if (delivery_button) {
      gtk_box_append(GTK_BOX(box), delivery_button);
    }
    if (clone_button) {
      gtk_box_append(GTK_BOX(box), clone_button);
    }
    gtk_box_append(GTK_BOX(box), toggle);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->lot_list), row);

    g_free(meta);
    g_free(title_text);
  }

  g_ptr_array_free(lots, TRUE);
}

gint compare_plugin_items(gconstpointer a, gconstpointer b) {
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

void refresh_plugin_list(AppContext* context) {
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
