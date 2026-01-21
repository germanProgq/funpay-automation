#ifndef FPV_APP_SRC_APP_SETTINGS_H_
#define FPV_APP_SRC_APP_SETTINGS_H_

#include "core/app_context.h"

void refresh_auto_response_list(AppContext* context);
void refresh_auto_delivery_list(AppContext* context);
void refresh_settings_from_file(AppContext* context);
void refresh_identity_labels(AppContext* context);

void show_auto_delivery_dialog(AppContext* context, const char* section_name);

void on_auto_response_add(GtkButton* button, gpointer user_data);
void on_auto_response_edit(GtkButton* button, gpointer user_data);
void on_auto_response_delete(GtkButton* button, gpointer user_data);
void on_auto_response_reload(GtkButton* button, gpointer user_data);

void on_auto_delivery_add(GtkButton* button, gpointer user_data);
void on_auto_delivery_edit(GtkButton* button, gpointer user_data);
void on_auto_delivery_delete(GtkButton* button, gpointer user_data);
void on_auto_delivery_reload(GtkButton* button, gpointer user_data);

#endif  // FPV_APP_SRC_APP_SETTINGS_H_
