#ifndef FPV_APP_SRC_APP_ADMIN_H_
#define FPV_APP_SRC_APP_ADMIN_H_

#include "core/app_context.h"

void refresh_admin_panel(AppContext* context);

void on_admin_refresh(GtkButton* button, gpointer user_data);
void on_admin_save_org(GtkButton* button, gpointer user_data);
void on_admin_team_changed(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data);
void on_admin_save_team_scopes(GtkButton* button, gpointer user_data);
void on_admin_create_team(GtkButton* button, gpointer user_data);
void on_admin_role_changed(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data);
void on_admin_remove_role(GtkButton* button, gpointer user_data);
void on_admin_revoke_invite(GtkButton* button, gpointer user_data);
void on_admin_mark_access_review(GtkButton* button, gpointer user_data);
void on_admin_price_change_approve(GtkButton* button, gpointer user_data);
void on_admin_price_change_reject(GtkButton* button, gpointer user_data);

#endif  // FPV_APP_SRC_APP_ADMIN_H_
