#ifndef FPV_APP_SRC_APP_EVENTS_H_
#define FPV_APP_SRC_APP_EVENTS_H_

#include "core/app_context.h"

void update_status(
    AppContext* context,
    fpv_core_status_t status,
    const char* detail);

gboolean poll_events(gpointer user_data);
void start_core(GtkButton* button, gpointer user_data);
void stop_core(GtkButton* button, gpointer user_data);

void on_chat_selected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);
void on_send_message(GtkButton* button, gpointer user_data);
void on_message_entry_activate(GtkEntry* entry, gpointer user_data);

void start_lots_refresh(AppContext* context, gboolean check_running);
void on_refresh_lots(GtkButton* button, gpointer user_data);

#endif  // FPV_APP_SRC_APP_EVENTS_H_
