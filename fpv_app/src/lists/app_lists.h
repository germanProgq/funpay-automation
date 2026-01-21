#ifndef FPV_APP_SRC_APP_LISTS_H_
#define FPV_APP_SRC_APP_LISTS_H_

#include "core/app_context.h"

void refresh_metrics(AppContext* context);
void refresh_chat_list(AppContext* context);
void refresh_message_list(AppContext* context);
void clear_chat_state(AppContext* context);
void load_cached_chats(AppContext* context);
void refresh_order_list(AppContext* context);
void refresh_lot_list(AppContext* context);
void refresh_plugin_list(AppContext* context);

gint compare_lot_items(gconstpointer a, gconstpointer b);

void on_lot_clone_clicked(GtkButton* button, gpointer user_data);
void on_lot_clone_link_clicked(GtkButton* button, gpointer user_data);
void on_lot_auto_delivery_clicked(GtkButton* button, gpointer user_data);

#endif  // FPV_APP_SRC_APP_LISTS_H_
