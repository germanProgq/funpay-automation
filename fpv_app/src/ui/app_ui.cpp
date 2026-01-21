#include "ui/app_ui.h"

#include "admin/app_admin.h"
#include "auth/app_auth.h"
#include "events/app_events.h"
#include "helpers/app_helpers.h"
#include "lists/app_lists.h"
#include "settings/app_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

void save_settings_to_file(GtkButton* button, gpointer user_data) {
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
  fpv_ini_set(ini, "FunPay", "autoRefund",
              gtk_switch_get_active(
                  GTK_SWITCH(context->settings.review_auto_refund)) ? "1" : "0");
  char refund_stars_buf[16];
  snprintf(refund_stars_buf, sizeof(refund_stars_buf), "%d",
           gtk_spin_button_get_value_as_int(
               GTK_SPIN_BUTTON(context->settings.review_auto_refund_max_stars)));
  fpv_ini_set(ini, "FunPay", "autoRefundMaxStars", refund_stars_buf);
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

GtkWidget* build_metric_card(const char* title, GtkWidget** value_out) {
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

GtkWidget* build_dashboard_page(AppContext* context) {
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

GtkWidget* build_messages_page(AppContext* context) {
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

GtkWidget* build_orders_page(AppContext* context) {
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

GtkWidget* build_lots_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Lots");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget* clone_link_button = gtk_button_new_with_label("Clone link");
  g_signal_connect(
      clone_link_button,
      "clicked",
      G_CALLBACK(on_lot_clone_link_clicked),
      context);

  context->lot_refresh_button = gtk_button_new_with_label("Refresh");
  g_signal_connect(
      context->lot_refresh_button,
      "clicked",
      G_CALLBACK(on_refresh_lots),
      context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), clone_link_button);
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

GtkWidget* build_plugins_page(AppContext* context) {
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

GtkWidget* build_auto_response_page(AppContext* context) {
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

GtkWidget* build_auto_delivery_page(AppContext* context) {
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
GtkWidget* build_star_label_widget(size_t count, const char* suffix) {
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

void set_settings_grid_margins(GtkWidget* grid) {
  if (!grid) {
    return;
  }
  gtk_widget_set_margin_top(grid, 10);
  gtk_widget_set_margin_bottom(grid, 10);
  gtk_widget_set_margin_start(grid, 12);
  gtk_widget_set_margin_end(grid, 12);
}

GtkWidget* build_settings_page(AppContext* context) {
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

  context->settings.review_auto_refund = gtk_switch_new();
  context->settings.review_auto_refund_max_stars =
      gtk_spin_button_new_with_range(1, 5, 1);
  add_setting_row(review_grid, 1, "Auto refund", context->settings.review_auto_refund);
  add_setting_row(review_grid, 2, "Refund max stars",
                  context->settings.review_auto_refund_max_stars);

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

GtkWidget* build_admin_page(AppContext* context) {
  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* title = gtk_label_new("Admin");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");
  gtk_widget_set_hexpand(title, TRUE);

  GtkWidget* refresh_button = gtk_button_new_with_label("Refresh");
  g_signal_connect(refresh_button, "clicked", G_CALLBACK(on_admin_refresh), context);

  gtk_box_append(GTK_BOX(header), title);
  gtk_box_append(GTK_BOX(header), refresh_button);

  context->admin.status_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(context->admin.status_label), 0.0f);
  gtk_widget_add_css_class(context->admin.status_label, "muted");

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

  context->admin.org_name = gtk_entry_new();
  context->admin.org_timezone = gtk_entry_new();
  context->admin.org_currency = gtk_entry_new();
  context->admin.org_tier = fpv_dropdown_new();
  fpv_dropdown_append(context->admin.org_tier, "basic", "Basic (Starter)");
  fpv_dropdown_append(context->admin.org_tier, "advanced", "Advanced");
  fpv_dropdown_append(context->admin.org_tier, "ultimate", "Ultimate (Team)");
  context->admin.retention_audit = gtk_spin_button_new_with_range(0, 3650, 1);
  context->admin.retention_price = gtk_spin_button_new_with_range(0, 3650, 1);
  context->admin.retention_competitor = gtk_spin_button_new_with_range(0, 3650, 1);
  context->admin.retention_order = gtk_spin_button_new_with_range(0, 3650, 1);
  context->admin.approval_required = gtk_switch_new();
  context->admin.org_save_button = gtk_button_new_with_label("Save organization");
  g_signal_connect(
      context->admin.org_save_button,
      "clicked",
      G_CALLBACK(on_admin_save_org),
      context);

  add_setting_row(org_grid, 0, "Name", context->admin.org_name);
  add_setting_row(org_grid, 1, "Timezone", context->admin.org_timezone);
  add_setting_row(org_grid, 2, "Currency", context->admin.org_currency);
  add_setting_row(org_grid, 3, "Tier", context->admin.org_tier);
  add_setting_row(org_grid, 4, "Audit retention (days)", context->admin.retention_audit);
  add_setting_row(org_grid, 5, "Price history retention (days)", context->admin.retention_price);
  add_setting_row(org_grid, 6, "Competitor retention (days)", context->admin.retention_competitor);
  add_setting_row(org_grid, 7, "Order retention (days)", context->admin.retention_order);
  add_setting_row(org_grid, 8, "Price approval required", context->admin.approval_required);
  add_setting_row(org_grid, 9, "Save", context->admin.org_save_button);

  GtkWidget* team_frame = gtk_frame_new("Team scopes");
  GtkWidget* team_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* team_grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(team_grid), 6);
  gtk_grid_set_column_spacing(GTK_GRID(team_grid), 12);
  set_settings_grid_margins(team_grid);
  gtk_frame_set_child(GTK_FRAME(team_frame), team_box);
  gtk_box_append(GTK_BOX(team_box), team_grid);

  context->admin.team_combo = fpv_dropdown_new();
  g_signal_connect(
      context->admin.team_combo,
      "notify::selected",
      G_CALLBACK(on_admin_team_changed),
      context);

  GtkWidget* team_create_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  context->admin.team_create_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(context->admin.team_create_entry), "Team name");
  context->admin.team_create_button = gtk_button_new_with_label("Create");
  g_signal_connect(
      context->admin.team_create_button,
      "clicked",
      G_CALLBACK(on_admin_create_team),
      context);
  gtk_box_append(GTK_BOX(team_create_box), context->admin.team_create_entry);
  gtk_box_append(GTK_BOX(team_create_box), context->admin.team_create_button);

  context->admin.team_categories = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(context->admin.team_categories),
      "Category/Subcategory, Category");
  context->admin.team_alerts = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(context->admin.team_alerts),
      "alerts, price_conflict, margin_risk");

  GtkWidget* scope_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  context->admin.scope_org = gtk_check_button_new_with_label("Org");
  context->admin.scope_team = gtk_check_button_new_with_label("Team");
  context->admin.scope_item = gtk_check_button_new_with_label("Item");
  context->admin.scope_listing = gtk_check_button_new_with_label("Listing");
  gtk_box_append(GTK_BOX(scope_box), context->admin.scope_org);
  gtk_box_append(GTK_BOX(scope_box), context->admin.scope_team);
  gtk_box_append(GTK_BOX(scope_box), context->admin.scope_item);
  gtk_box_append(GTK_BOX(scope_box), context->admin.scope_listing);

  add_setting_row(team_grid, 0, "Team", context->admin.team_combo);
  add_setting_row(team_grid, 1, "New team", team_create_box);
  add_setting_row(team_grid, 2, "Allowed categories", context->admin.team_categories);
  add_setting_row(team_grid, 3, "Alert types", context->admin.team_alerts);
  add_setting_row(team_grid, 4, "Price scopes", scope_box);

  context->admin.team_save_button = gtk_button_new_with_label("Save scopes");
  g_signal_connect(
      context->admin.team_save_button,
      "clicked",
      G_CALLBACK(on_admin_save_team_scopes),
      context);
  gtk_box_append(GTK_BOX(team_box), context->admin.team_save_button);

  GtkWidget* access_frame = gtk_frame_new("Roles and access");
  GtkWidget* access_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_frame_set_child(GTK_FRAME(access_frame), access_box);

  GtkWidget* member_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* member_title = gtk_label_new("Members");
  gtk_label_set_xalign(GTK_LABEL(member_title), 0.0f);
  gtk_widget_set_hexpand(member_title, TRUE);
  gtk_box_append(GTK_BOX(member_header), member_title);

  context->admin.members_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->admin.members_list),
      GTK_SELECTION_NONE);

  GtkWidget* invite_title = gtk_label_new("Pending invites");
  gtk_label_set_xalign(GTK_LABEL(invite_title), 0.0f);
  gtk_widget_add_css_class(invite_title, "muted");

  context->admin.invites_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->admin.invites_list),
      GTK_SELECTION_NONE);

  GtkWidget* review_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  context->admin.access_review_label = gtk_label_new("No access reviews yet.");
  gtk_label_set_xalign(GTK_LABEL(context->admin.access_review_label), 0.0f);
  gtk_widget_set_hexpand(context->admin.access_review_label, TRUE);
  context->admin.access_review_note = gtk_entry_new();
  gtk_entry_set_placeholder_text(
      GTK_ENTRY(context->admin.access_review_note),
      "Review note");
  context->admin.access_review_button = gtk_button_new_with_label("Mark review");
  g_signal_connect(
      context->admin.access_review_button,
      "clicked",
      G_CALLBACK(on_admin_mark_access_review),
      context);
  gtk_box_append(GTK_BOX(review_box), context->admin.access_review_label);
  gtk_box_append(GTK_BOX(review_box), context->admin.access_review_note);
  gtk_box_append(GTK_BOX(review_box), context->admin.access_review_button);

  gtk_box_append(GTK_BOX(access_box), member_header);
  gtk_box_append(GTK_BOX(access_box), context->admin.members_list);
  gtk_box_append(GTK_BOX(access_box), invite_title);
  gtk_box_append(GTK_BOX(access_box), context->admin.invites_list);
  gtk_box_append(GTK_BOX(access_box), review_box);

  GtkWidget* price_frame = gtk_frame_new("Price approvals");
  GtkWidget* price_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_frame_set_child(GTK_FRAME(price_frame), price_box);

  context->admin.price_change_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(
      GTK_LIST_BOX(context->admin.price_change_list),
      GTK_SELECTION_NONE);
  gtk_box_append(GTK_BOX(price_box), context->admin.price_change_list);

  gtk_box_append(GTK_BOX(content), org_frame);
  gtk_box_append(GTK_BOX(content), team_frame);
  gtk_box_append(GTK_BOX(content), access_frame);
  gtk_box_append(GTK_BOX(content), price_frame);

  gtk_box_append(GTK_BOX(root), header);
  gtk_box_append(GTK_BOX(root), context->admin.status_label);
  gtk_box_append(GTK_BOX(root), scroller);

  return root;
}

GtkWidget* build_main_layout(AppContext* context) {
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
      build_admin_page(context),
      "admin",
      "Admin");
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
