#ifndef FPV_APP_SRC_APP_UI_H_
#define FPV_APP_SRC_APP_UI_H_

#include "core/app_context.h"

GtkWidget* build_dashboard_page(AppContext* context);
GtkWidget* build_messages_page(AppContext* context);
GtkWidget* build_orders_page(AppContext* context);
GtkWidget* build_lots_page(AppContext* context);
GtkWidget* build_plugins_page(AppContext* context);
GtkWidget* build_auto_response_page(AppContext* context);
GtkWidget* build_auto_delivery_page(AppContext* context);
GtkWidget* build_settings_page(AppContext* context);
GtkWidget* build_admin_page(AppContext* context);
GtkWidget* build_main_layout(AppContext* context);

#endif  // FPV_APP_SRC_APP_UI_H_
