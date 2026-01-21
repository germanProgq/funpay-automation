#ifndef FPV_APP_SRC_APP_AUTH_H_
#define FPV_APP_SRC_APP_AUTH_H_

#include "core/app_context.h"

void begin_auth_flow(AppContext* context);
void show_login_dialog(AppContext* context);
void show_join_dialog(AppContext* context);
void show_onboarding_dialog(AppContext* context);
void show_invite_dialog(AppContext* context);
void show_link_dialog(AppContext* context);

void on_invite_open(GtkButton* button, gpointer user_data);
void on_link_open(GtkButton* button, gpointer user_data);

// Used by admin flows to translate results.
const char* admin_error_message(fpv_result_t result);
fpv_role_t role_from_id(const char* role_id);

#endif  // FPV_APP_SRC_APP_AUTH_H_
