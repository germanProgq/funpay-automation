#include "auth/app_auth.h"

#include "admin/app_admin.h"
#include "events/app_events.h"
#include "helpers/app_helpers.h"
#include "lists/app_lists.h"
#include "settings/app_settings.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <gtk/gtk.h>

const char* auth_error_message(fpv_result_t result) {
  switch (result) {
    case FPV_ERR_INVALID_ARGUMENT:
      return "Check the input values.";
    case FPV_ERR_NOT_FOUND:
      return "Account not found.";
    case FPV_ERR_INVALID_STATE:
      return "Credentials or invite are invalid.";
    case FPV_ERR_OUT_OF_MEMORY:
      return "Out of memory.";
    case FPV_ERR_IO:
      return "Storage error.";
    default:
      return "Sign-in failed.";
  }
}

const char* admin_error_message(fpv_result_t result) {
  switch (result) {
    case FPV_ERR_INVALID_ARGUMENT:
      return "Check the input values.";
    case FPV_ERR_NOT_FOUND:
      return "Record not found.";
    case FPV_ERR_INVALID_STATE:
      return "Operation not allowed.";
    case FPV_ERR_OUT_OF_MEMORY:
      return "Out of memory.";
    case FPV_ERR_IO:
      return "Storage error.";
    default:
      return "Operation failed.";
  }
}

void apply_authenticated_context(
    AppContext* context,
    fpv_user_t* user,
    fpv_organization_t* org,
    fpv_team_t* team,
    fpv_role_t role) {
  if (!context) {
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  fpv_user_destroy(context->current_user);
  fpv_organization_destroy(context->current_org);
  fpv_team_destroy(context->current_team);
  context->current_user = user;
  context->current_org = org;
  context->current_team = team;
  context->current_role = role;

  clear_chat_state(context);
  refresh_identity_labels(context);
  refresh_auto_response_list(context);
  refresh_auto_delivery_list(context);
  refresh_settings_from_file(context);
  refresh_admin_panel(context);
  load_cached_chats(context);

  gtk_window_present(GTK_WINDOW(context->window));
  const char* golden_key = gtk_editable_get_text(
      GTK_EDITABLE(context->settings.funpay_golden_key));
  if (golden_key && golden_key[0]) {
    start_core(NULL, context);
  }
}

void begin_auth_flow(AppContext* context) {
  if (!context || !context->identity) {
    return;
  }
  if (fpv_identity_store_has_users(context->identity)) {
    show_login_dialog(context);
  } else {
    show_onboarding_dialog(context);
  }
}

typedef struct AuthDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* password_entry;
  GtkWidget* status_label;
} AuthDialog;

void on_auth_sign_in(GtkButton* button, gpointer user_data) {
  AuthDialog* dialog = (AuthDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  if (!email || !email[0] || !password || !password[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Email and password are required.");
    return;
  }
  fpv_user_t* user = NULL;
  fpv_result_t result =
      fpv_identity_authenticate(dialog->context->identity, email, password, &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_role_t role = FPV_ROLE_UNKNOWN;
  result = fpv_identity_resolve_user_context(
      dialog->context->identity,
      user->id,
      &org,
      &team,
      &role);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Workspace not found.");
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
}

void on_auth_join(GtkButton* button, gpointer user_data) {
  AuthDialog* dialog = (AuthDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  show_join_dialog(context);
}

void show_login_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  AuthDialog* dialog = (AuthDialog*)g_new0(AuthDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Sign in");
  gtk_window_set_default_size(GTK_WINDOW(window), 420, 220);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Welcome back");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* login_button = gtk_button_new_with_label("Sign in");
  GtkWidget* join_button = gtk_button_new_with_label("Join with invite");
  gtk_box_append(GTK_BOX(actions), login_button);
  gtk_box_append(GTK_BOX(actions), join_button);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), actions);

  dialog->email_entry = email_entry;
  dialog->password_entry = password_entry;
  dialog->status_label = status;

  g_signal_connect(login_button, "clicked", G_CALLBACK(on_auth_sign_in), dialog);
  g_signal_connect(join_button, "clicked", G_CALLBACK(on_auth_join), dialog);

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-auth-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct JoinDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* name_entry;
  GtkWidget* password_entry;
  GtkWidget* confirm_entry;
  GtkWidget* token_entry;
  GtkWidget* status_label;
} JoinDialog;

void on_join_submit(GtkButton* button, gpointer user_data) {
  JoinDialog* dialog = (JoinDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* name = gtk_editable_get_text(GTK_EDITABLE(dialog->name_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  const char* confirm =
      gtk_editable_get_text(GTK_EDITABLE(dialog->confirm_entry));
  const char* token = gtk_editable_get_text(GTK_EDITABLE(dialog->token_entry));
  if (!email || !email[0] || !password || !password[0] || !token || !token[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Email, password, and invite token are required.");
    return;
  }
  if (strcmp(password, confirm ? confirm : "") != 0) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Passwords do not match.");
    return;
  }
  fpv_identity_invite_t* invite = NULL;
  fpv_result_t result =
      fpv_identity_validate_invite(dialog->context->identity, token, &invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    return;
  }
  const char* display_name = name && name[0] ? name : email;
  fpv_user_t* user = NULL;
  result = fpv_identity_create_user(
      dialog->context->identity,
      email,
      display_name,
      password,
      true,
      &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return;
  }
  fpv_identity_invite_t* accepted = NULL;
  fpv_user_role_t* assigned = NULL;
  result = fpv_identity_accept_invite(
      dialog->context->identity, token, user->id, &accepted, &assigned);
  fpv_user_role_destroy(assigned);
  fpv_identity_invite_destroy(accepted);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_role_t role = FPV_ROLE_UNKNOWN;
  result = fpv_identity_resolve_user_context(
      dialog->context->identity,
      user->id,
      &org,
      &team,
      &role);
  fpv_identity_invite_destroy(invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Workspace not found.");
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    return;
  }
  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
}

void show_join_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  JoinDialog* dialog = (JoinDialog*)g_new0(JoinDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Join workspace");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 320);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Accept invite");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* name_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
  GtkWidget* confirm_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_entry), "Confirm password");
  gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);
  GtkWidget* token_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(token_entry), "Invite token");

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* create_button = gtk_button_new_with_label("Join workspace");
  g_signal_connect(create_button, "clicked", G_CALLBACK(on_join_submit), dialog);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), name_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), confirm_entry);
  gtk_box_append(GTK_BOX(box), token_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), create_button);

  dialog->email_entry = email_entry;
  dialog->name_entry = name_entry;
  dialog->password_entry = password_entry;
  dialog->confirm_entry = confirm_entry;
  dialog->token_entry = token_entry;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-join-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct OnboardingDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* name_entry;
  GtkWidget* password_entry;
  GtkWidget* confirm_entry;
  GtkWidget* org_entry;
  GtkWidget* timezone_entry;
  GtkWidget* currency_entry;
  GtkWidget* status_label;
} OnboardingDialog;

void on_onboarding_submit(GtkButton* button, gpointer user_data) {
  OnboardingDialog* dialog = (OnboardingDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  const char* name = gtk_editable_get_text(GTK_EDITABLE(dialog->name_entry));
  const char* password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->password_entry));
  const char* confirm =
      gtk_editable_get_text(GTK_EDITABLE(dialog->confirm_entry));
  const char* org_name = gtk_editable_get_text(GTK_EDITABLE(dialog->org_entry));
  const char* timezone =
      gtk_editable_get_text(GTK_EDITABLE(dialog->timezone_entry));
  const char* currency =
      gtk_editable_get_text(GTK_EDITABLE(dialog->currency_entry));
  if (!email || !email[0] || !password || !password[0] ||
      !org_name || !org_name[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Email, password, and workspace name are required.");
    return;
  }
  if (strcmp(password, confirm ? confirm : "") != 0) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Passwords do not match.");
    return;
  }
  const char* display_name = name && name[0] ? name : email;
  fpv_user_t* user = NULL;
  fpv_result_t result = fpv_identity_create_user(
      dialog->context->identity,
      email,
      display_name,
      password,
      true,
      &user);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    return;
  }
  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_user_role_t* owner_role = NULL;
  const char* tz = timezone && timezone[0] ? timezone : "UTC";
  const char* cur = currency && currency[0] ? currency : "RUB";
  result = fpv_identity_create_organization(
      dialog->context->identity,
      org_name,
      tz,
      cur,
      NULL,
      user->id,
      &org,
      &team,
      &owner_role);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    fpv_user_destroy(user);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    fpv_user_role_destroy(owner_role);
    return;
  }
  fpv_role_t role = owner_role ? owner_role->role : FPV_ROLE_OWNER;
  fpv_user_role_destroy(owner_role);

  AppContext* context = dialog->context;
  gtk_window_close(GTK_WINDOW(dialog->window));
  apply_authenticated_context(context, user, org, team, role);
  show_link_dialog(context);
}

void show_onboarding_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  OnboardingDialog* dialog = (OnboardingDialog*)g_new0(OnboardingDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Create workspace");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 420);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* title = gtk_label_new("Welcome to FunPay Vertex");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
  gtk_widget_add_css_class(title, "page-title");

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");
  GtkWidget* name_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Display name");
  GtkWidget* password_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(password_entry), "Password");
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), FALSE);
  GtkWidget* confirm_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_entry), "Confirm password");
  gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);

  GtkWidget* org_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(org_entry), "Workspace name");
  GtkWidget* timezone_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(timezone_entry), "Timezone (e.g., UTC)");
  GtkWidget* currency_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(currency_entry), "Currency (e.g., RUB)");

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* create_button = gtk_button_new_with_label("Create workspace");
  g_signal_connect(create_button, "clicked", G_CALLBACK(on_onboarding_submit), dialog);

  gtk_box_append(GTK_BOX(box), title);
  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), name_entry);
  gtk_box_append(GTK_BOX(box), password_entry);
  gtk_box_append(GTK_BOX(box), confirm_entry);
  gtk_box_append(GTK_BOX(box), org_entry);
  gtk_box_append(GTK_BOX(box), timezone_entry);
  gtk_box_append(GTK_BOX(box), currency_entry);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), create_button);

  dialog->email_entry = email_entry;
  dialog->name_entry = name_entry;
  dialog->password_entry = password_entry;
  dialog->confirm_entry = confirm_entry;
  dialog->org_entry = org_entry;
  dialog->timezone_entry = timezone_entry;
  dialog->currency_entry = currency_entry;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-onboarding-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

typedef struct InviteDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* email_entry;
  GtkWidget* role_combo;
  GtkWidget* team_combo;
  GtkWidget* token_entry;
  GtkWidget* copy_button;
  GtkWidget* status_label;
} InviteDialog;

fpv_role_t role_from_id(const char* role_id) {
  if (!role_id) {
    return FPV_ROLE_UNKNOWN;
  }
  if (strcmp(role_id, "owner") == 0) {
    return FPV_ROLE_OWNER;
  }
  if (strcmp(role_id, "admin") == 0) {
    return FPV_ROLE_ADMIN;
  }
  if (strcmp(role_id, "manager") == 0) {
    return FPV_ROLE_MANAGER;
  }
  if (strcmp(role_id, "analyst") == 0) {
    return FPV_ROLE_ANALYST;
  }
  if (strcmp(role_id, "viewer") == 0) {
    return FPV_ROLE_VIEWER;
  }
  return FPV_ROLE_UNKNOWN;
}

gboolean copy_invite_token(InviteDialog* dialog, gboolean show_status) {
  if (!dialog || !dialog->token_entry) {
    return FALSE;
  }
  const char* token = gtk_editable_get_text(GTK_EDITABLE(dialog->token_entry));
  if (!token || !token[0]) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Create an invite first.");
    }
    return FALSE;
  }
#if defined(__APPLE__)
  GError* error = NULL;
  GSubprocess* process =
      g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE, &error, "pbcopy", NULL);
  if (process) {
    gboolean ok =
        g_subprocess_communicate_utf8(process, token, NULL, NULL, NULL, &error);
    g_object_unref(process);
    if (ok) {
      if (show_status && dialog->status_label) {
        gtk_label_set_text(GTK_LABEL(dialog->status_label), "Invite token copied.");
      }
      return TRUE;
    }
    if (error) {
      g_error_free(error);
      error = NULL;
    }
  } else if (error) {
    g_error_free(error);
    error = NULL;
  }
#endif
  GdkDisplay* display = gtk_widget_get_display(dialog->token_entry);
  if (!display) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Clipboard unavailable.");
    }
    return FALSE;
  }
  GdkClipboard* clipboard = gdk_display_get_clipboard(display);
  if (!clipboard) {
    if (show_status && dialog->status_label) {
      gtk_label_set_text(GTK_LABEL(dialog->status_label), "Clipboard unavailable.");
    }
    return FALSE;
  }
  gdk_clipboard_set_text(clipboard, token);
  if (show_status && dialog->status_label) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Invite token copied.");
  }
  return TRUE;
}

void on_invite_copy(GtkButton* button, gpointer user_data) {
  (void)button;
  InviteDialog* dialog = (InviteDialog*)user_data;
  copy_invite_token(dialog, TRUE);
}

gboolean on_invite_token_key(
    GtkEventControllerKey* controller,
    guint keyval,
    guint keycode,
    GdkModifierType state,
    gpointer user_data) {
  (void)controller;
  (void)keycode;
  InviteDialog* dialog = (InviteDialog*)user_data;
  if (!dialog) {
    return FALSE;
  }
  if ((state & (GDK_CONTROL_MASK | GDK_META_MASK | GDK_SUPER_MASK)) == 0) {
    return FALSE;
  }
  if (gdk_keyval_to_lower(keyval) != GDK_KEY_c) {
    return FALSE;
  }
  copy_invite_token(dialog, TRUE);
  return TRUE;
}

void on_invite_create(GtkButton* button, gpointer user_data) {
  InviteDialog* dialog = (InviteDialog*)user_data;
  if (!dialog || !dialog->context || !dialog->context->current_org) {
    return;
  }
  const char* email = gtk_editable_get_text(GTK_EDITABLE(dialog->email_entry));
  if (!email || !email[0]) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Email is required.");
    return;
  }
  const char* role_id = fpv_dropdown_get_active_id(dialog->role_combo);
  fpv_role_t role = role_from_id(role_id);
  if (role == FPV_ROLE_UNKNOWN) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), "Select a role.");
    return;
  }
  const char* team_id = fpv_dropdown_get_active_id(dialog->team_combo);
  if (team_id && !team_id[0]) {
    team_id = NULL;
  }
  if (dialog->context->current_role == FPV_ROLE_MANAGER && !team_id) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       "Managers can only invite to their team.");
    return;
  }
  fpv_identity_invite_t* invite = NULL;
  char* token = NULL;
  fpv_result_t result = fpv_identity_create_invite(
      dialog->context->identity,
      dialog->context->current_org->id,
      team_id,
      email,
      role,
      dialog->context->current_user ? dialog->context->current_user->id : NULL,
      0,
      &invite,
      &token);
  fpv_identity_invite_destroy(invite);
  if (result != FPV_OK) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label), auth_error_message(result));
    if (dialog->token_entry) {
      gtk_editable_set_text(GTK_EDITABLE(dialog->token_entry), "");
    }
    if (dialog->copy_button) {
      gtk_widget_set_sensitive(dialog->copy_button, FALSE);
    }
    free(token);
    return;
  }
  const char* token_text = token ? token : "";
  gtk_editable_set_text(GTK_EDITABLE(dialog->token_entry), token_text);
  if (dialog->copy_button) {
    gtk_widget_set_sensitive(dialog->copy_button, token_text[0] != '\0');
  }
  gboolean copied = copy_invite_token(dialog, FALSE);
  gtk_label_set_text(
      GTK_LABEL(dialog->status_label),
      (token_text[0] != '\0' && copied)
          ? "Invite created. Token copied to clipboard."
          : "Invite created.");
  free(token);
}

void show_invite_dialog(AppContext* context) {
  if (!context || !context->window || !context->current_org) {
    return;
  }
  InviteDialog* dialog = (InviteDialog*)g_new0(InviteDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Invite team member");
  gtk_window_set_default_size(GTK_WINDOW(window), 480, 280);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* email_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(email_entry), "Email");

  GtkWidget* role_combo = fpv_dropdown_new();
  fpv_role_t current_role = context->current_role;
  if (current_role == FPV_ROLE_OWNER) {
    fpv_dropdown_append(role_combo, "admin", "Admin");
  }
  if (current_role == FPV_ROLE_OWNER || current_role == FPV_ROLE_ADMIN) {
    fpv_dropdown_append(role_combo, "manager", "Manager");
  }
  fpv_dropdown_append(role_combo, "analyst", "Analyst");
  fpv_dropdown_append(role_combo, "viewer", "Viewer");
  fpv_dropdown_set_active_index(role_combo, 0);

  GtkWidget* team_combo = fpv_dropdown_new();
  fpv_dropdown_append(team_combo, "", "Organization");
  fpv_team_t** teams = NULL;
  size_t team_count = 0;
  if (fpv_identity_list_teams(
          context->identity,
          context->current_org->id,
          &teams,
          &team_count) == FPV_OK) {
    for (size_t i = 0; i < team_count; i++) {
      const char* name = teams[i] && teams[i]->name ? teams[i]->name : "Team";
      const char* id = teams[i] && teams[i]->id ? teams[i]->id : "";
      fpv_dropdown_append(team_combo, id, name);
    }
  }
  for (size_t i = 0; i < team_count; i++) {
    fpv_team_destroy(teams[i]);
  }
  free(teams);
  fpv_dropdown_set_active_index(team_combo, 0);
  if (current_role == FPV_ROLE_MANAGER &&
      context->current_team &&
      context->current_team->id) {
    fpv_dropdown_set_active_id(team_combo, context->current_team->id);
    gtk_widget_set_sensitive(team_combo, FALSE);
  }

  GtkWidget* token_entry = gtk_entry_new();
  gtk_editable_set_editable(GTK_EDITABLE(token_entry), FALSE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(token_entry), "Invite token");
  gtk_widget_set_hexpand(token_entry, TRUE);

  GtkEventController* token_controller = gtk_event_controller_key_new();
  g_signal_connect(
      token_controller,
      "key-pressed",
      G_CALLBACK(on_invite_token_key),
      dialog);
  gtk_widget_add_controller(token_entry, token_controller);

  GtkWidget* copy_button = gtk_button_new_with_label("Copy");
  gtk_widget_set_sensitive(copy_button, FALSE);
  g_signal_connect(copy_button, "clicked", G_CALLBACK(on_invite_copy), dialog);

  GtkWidget* token_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(token_row), token_entry);
  gtk_box_append(GTK_BOX(token_row), copy_button);

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* invite_button = gtk_button_new_with_label("Create invite");
  g_signal_connect(invite_button, "clicked", G_CALLBACK(on_invite_create), dialog);

  gtk_box_append(GTK_BOX(box), email_entry);
  gtk_box_append(GTK_BOX(box), role_combo);
  gtk_box_append(GTK_BOX(box), team_combo);
  gtk_box_append(GTK_BOX(box), token_row);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), invite_button);

  dialog->email_entry = email_entry;
  dialog->role_combo = role_combo;
  dialog->team_combo = team_combo;
  dialog->token_entry = token_entry;
  dialog->copy_button = copy_button;
  dialog->status_label = status;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-invite-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

void on_invite_open(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_invite_dialog(context);
}

void on_link_open(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  show_link_dialog(context);
}

typedef struct LinkDialog {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* golden_entry;
  GtkWidget* agent_entry;
  GtkWidget* proxy_enable;
  GtkWidget* proxy_ip;
  GtkWidget* proxy_port;
  GtkWidget* proxy_login;
  GtkWidget* proxy_password;
  GtkWidget* status_label;
  GtkWidget* link_button;
} LinkDialog;

typedef struct LinkTask {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* status_label;
  GtkWidget* link_button;
  gchar* golden_key;
  gchar* user_agent;
  gboolean proxy_enabled;
  gchar* proxy_ip;
  guint proxy_port;
  gchar* proxy_login;
  gchar* proxy_password;
} LinkTask;

typedef struct LinkResult {
  AppContext* context;
  GtkWidget* window;
  GtkWidget* status_label;
  GtkWidget* link_button;
  gboolean success;
  gchar* message;
} LinkResult;

gboolean save_funpay_settings(
    AppContext* context,
    const char* golden_key,
    const char* user_agent,
    gboolean proxy_enabled,
    const char* proxy_ip,
    guint proxy_port,
    const char* proxy_login,
    const char* proxy_password,
    gchar** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!context || !context->config_dir || !golden_key || !golden_key[0]) {
    if (out_error) {
      *out_error = g_strdup("Golden key is required.");
    }
    return FALSE;
  }
  gchar* path = g_build_filename(context->config_dir, "_main.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    if (out_error) {
      *out_error = g_strdup("Failed to load settings file.");
    }
    g_free(path);
    return FALSE;
  }

  fpv_result_t result = fpv_ini_set(ini, "FunPay", "golden_key", golden_key);
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "FunPay", "user_agent", user_agent ? user_agent : "");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "enable", proxy_enabled ? "1" : "0");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(ini, "Proxy", "ip", proxy_ip ? proxy_ip : "");
  }
  gchar* port_value = g_strdup_printf("%u", proxy_port);
  if (result == FPV_OK) {
    result = fpv_ini_set(ini, "Proxy", "port", port_value);
  }
  g_free(port_value);
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "login", proxy_login ? proxy_login : "");
  }
  if (result == FPV_OK) {
    result = fpv_ini_set(
        ini, "Proxy", "password", proxy_password ? proxy_password : "");
  }

  if (result == FPV_OK) {
    result = fpv_ini_save(ini, path);
  }
  fpv_ini_destroy(ini);
  g_free(path);
  if (result != FPV_OK) {
    if (out_error) {
      *out_error = g_strdup("Failed to save settings file.");
    }
    return FALSE;
  }
  return TRUE;
}

gboolean link_task_finish(gpointer data) {
  LinkResult* result = (LinkResult*)data;
  if (!result) {
    return G_SOURCE_REMOVE;
  }
  if (result->success) {
    gtk_label_set_text(
        GTK_LABEL(result->status_label),
        result->message ? result->message : "Linked.");
    refresh_settings_from_file(result->context);
    refresh_identity_labels(result->context);
    const char* key = gtk_editable_get_text(
        GTK_EDITABLE(result->context->settings.funpay_golden_key));
    if (key && key[0] && !result->context->running) {
      start_core(NULL, result->context);
    }
    gtk_window_close(GTK_WINDOW(result->window));
  } else {
    gtk_label_set_text(
        GTK_LABEL(result->status_label),
        result->message ? result->message : "Linking failed.");
    if (result->link_button) {
      gtk_widget_set_sensitive(result->link_button, TRUE);
    }
    if (result->window) {
      gtk_window_set_deletable(GTK_WINDOW(result->window), TRUE);
    }
  }
  g_free(result->message);
  g_free(result);
  return G_SOURCE_REMOVE;
}

gpointer link_task_run(gpointer data) {
  LinkTask* task = (LinkTask*)data;
  if (!task) {
    return NULL;
  }
  LinkResult* result = (LinkResult*)g_new0(LinkResult, 1);
  result->context = task->context;
  result->window = task->window;
  result->status_label = task->status_label;
  result->link_button = task->link_button;

  fpv_funpay_account_t* account = NULL;
  fpv_result_t refresh_result = FPV_OK;
  fpv_result_t link_result = FPV_OK;
  const char* username = NULL;
  const char* currency = NULL;
  char id_buf[32] = {0};

  fpv_funpay_account_config_t config;
  memset(&config, 0, sizeof(config));
  config.golden_key = task->golden_key;
  config.user_agent = task->user_agent;
  config.timeout_ms = 15000;
  config.data_dir = task->context->data_dir;
  config.proxy.enabled = task->proxy_enabled;
  config.proxy.host = task->proxy_ip;
  config.proxy.port = (uint16_t)task->proxy_port;
  config.proxy.username = task->proxy_login;
  config.proxy.password = task->proxy_password;

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  account = fpv_funpay_account_create(&config, &error);
  if (!account) {
    result->message = g_strdup(error.message ? error.message : "Authorization failed.");
    goto done;
  }
  refresh_result = fpv_funpay_account_refresh(account, &error);
  if (refresh_result != FPV_OK || !fpv_funpay_account_is_initiated(account)) {
    result->message = g_strdup(error.message ? error.message : "FunPay refresh failed.");
    goto done;
  }
  snprintf(id_buf, sizeof(id_buf), "%" PRIu64, fpv_funpay_account_id(account));
  username = fpv_funpay_account_username(account);
  currency = fpv_funpay_account_currency(account);
  link_result = fpv_identity_link_account(
      task->context->identity,
      task->context->current_org ? task->context->current_org->id : NULL,
      task->context->current_team ? task->context->current_team->id : NULL,
      id_buf,
      username,
      username,
      currency,
      NULL);
  if (link_result != FPV_OK) {
    result->message = g_strdup(auth_error_message(link_result));
    goto done;
  }
  result->success = TRUE;
  result->message = g_strdup("FunPay account linked.");

done:
  if (account) {
    fpv_funpay_account_destroy(account);
  }
  fpv_funpay_error_clear(&error);
  g_idle_add(link_task_finish, result);
  g_free(task->golden_key);
  g_free(task->user_agent);
  g_free(task->proxy_ip);
  g_free(task->proxy_login);
  g_free(task->proxy_password);
  g_free(task);
  return NULL;
}

void on_link_submit(GtkButton* button, gpointer user_data) {
  LinkDialog* dialog = (LinkDialog*)user_data;
  if (!dialog || !dialog->context) {
    return;
  }
  const char* golden_key =
      gtk_editable_get_text(GTK_EDITABLE(dialog->golden_entry));
  const char* user_agent =
      gtk_editable_get_text(GTK_EDITABLE(dialog->agent_entry));
  gboolean proxy_enabled =
      gtk_switch_get_active(GTK_SWITCH(dialog->proxy_enable));
  const char* proxy_ip =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_ip));
  guint proxy_port =
      (guint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(dialog->proxy_port));
  const char* proxy_login =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_login));
  const char* proxy_password =
      gtk_editable_get_text(GTK_EDITABLE(dialog->proxy_password));

  gchar* error = NULL;
  if (!save_funpay_settings(
          dialog->context,
          golden_key,
          user_agent,
          proxy_enabled,
          proxy_ip,
          proxy_port,
          proxy_login,
          proxy_password,
          &error)) {
    gtk_label_set_text(GTK_LABEL(dialog->status_label),
                       error ? error : "Failed to save settings.");
    g_free(error);
    return;
  }
  g_free(error);

  gtk_widget_set_sensitive(dialog->link_button, FALSE);
  gtk_label_set_text(GTK_LABEL(dialog->status_label), "Linking...");
  gtk_window_set_deletable(GTK_WINDOW(dialog->window), FALSE);

  LinkTask* task = (LinkTask*)g_new0(LinkTask, 1);
  task->context = dialog->context;
  task->window = dialog->window;
  task->status_label = dialog->status_label;
  task->link_button = dialog->link_button;
  task->golden_key = g_strdup(golden_key);
  task->user_agent = g_strdup(user_agent ? user_agent : "");
  task->proxy_enabled = proxy_enabled;
  task->proxy_ip = g_strdup(proxy_ip ? proxy_ip : "");
  task->proxy_port = proxy_port;
  task->proxy_login = g_strdup(proxy_login ? proxy_login : "");
  task->proxy_password = g_strdup(proxy_password ? proxy_password : "");

  GThread* thread = g_thread_new("fpv-link", link_task_run, task);
  if (thread) {
    g_thread_unref(thread);
  }
}

void show_link_dialog(AppContext* context) {
  if (!context || !context->window) {
    return;
  }
  refresh_settings_from_file(context);
  LinkDialog* dialog = (LinkDialog*)g_new0(LinkDialog, 1);
  dialog->context = context;

  GtkWidget* window = gtk_window_new();
  dialog->window = window;
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(context->window));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_title(GTK_WINDOW(window), "Link FunPay account");
  gtk_window_set_default_size(GTK_WINDOW(window), 520, 360);

  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);

  GtkWidget* golden_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(golden_entry), "Golden key");
  GtkWidget* agent_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(agent_entry), "User agent");

  GtkWidget* proxy_enable = gtk_switch_new();
  GtkWidget* proxy_ip = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_ip), "Proxy IP");
  GtkWidget* proxy_port = gtk_spin_button_new_with_range(0, 65535, 1);
  GtkWidget* proxy_login = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_login), "Proxy login");
  GtkWidget* proxy_password = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(proxy_password), "Proxy password");
  gtk_entry_set_visibility(GTK_ENTRY(proxy_password), FALSE);

  const char* current_key =
      gtk_editable_get_text(GTK_EDITABLE(context->settings.funpay_golden_key));
  const char* current_agent =
      gtk_editable_get_text(GTK_EDITABLE(context->settings.funpay_user_agent));
  gtk_editable_set_text(GTK_EDITABLE(golden_entry), current_key ? current_key : "");
  gtk_editable_set_text(GTK_EDITABLE(agent_entry), current_agent ? current_agent : "");
  gtk_switch_set_active(
      GTK_SWITCH(proxy_enable),
      gtk_switch_get_active(GTK_SWITCH(context->settings.proxy_enable)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_ip),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_ip)));
  gtk_spin_button_set_value(
      GTK_SPIN_BUTTON(proxy_port),
      gtk_spin_button_get_value(GTK_SPIN_BUTTON(context->settings.proxy_port)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_login),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_login)));
  gtk_editable_set_text(
      GTK_EDITABLE(proxy_password),
      gtk_editable_get_text(GTK_EDITABLE(context->settings.proxy_password)));

  GtkWidget* status = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(status), 0.0f);
  gtk_widget_add_css_class(status, "muted");

  GtkWidget* link_button = gtk_button_new_with_label("Link account");
  g_signal_connect(link_button, "clicked", G_CALLBACK(on_link_submit), dialog);

  gtk_box_append(GTK_BOX(box), golden_entry);
  gtk_box_append(GTK_BOX(box), agent_entry);
  GtkWidget* proxy_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* proxy_label = gtk_label_new("Enable proxy");
  gtk_label_set_xalign(GTK_LABEL(proxy_label), 0.0f);
  gtk_widget_set_hexpand(proxy_label, TRUE);
  gtk_box_append(GTK_BOX(proxy_row), proxy_label);
  gtk_box_append(GTK_BOX(proxy_row), proxy_enable);
  gtk_box_append(GTK_BOX(box), proxy_row);
  gtk_box_append(GTK_BOX(box), proxy_ip);
  gtk_box_append(GTK_BOX(box), proxy_port);
  gtk_box_append(GTK_BOX(box), proxy_login);
  gtk_box_append(GTK_BOX(box), proxy_password);
  gtk_box_append(GTK_BOX(box), status);
  gtk_box_append(GTK_BOX(box), link_button);

  dialog->golden_entry = golden_entry;
  dialog->agent_entry = agent_entry;
  dialog->proxy_enable = proxy_enable;
  dialog->proxy_ip = proxy_ip;
  dialog->proxy_port = proxy_port;
  dialog->proxy_login = proxy_login;
  dialog->proxy_password = proxy_password;
  dialog->status_label = status;
  dialog->link_button = link_button;

  gtk_window_set_child(GTK_WINDOW(window), box);
  g_object_set_data_full(G_OBJECT(window), "fpv-link-dialog", dialog, g_free);
  gtk_window_present(GTK_WINDOW(window));
}

void update_status(
    AppContext* context,
    fpv_core_status_t status,
    const char* detail) {
  if (!context || !context->status_label) {
    return;
  }

  gchar* text = NULL;
  gchar* safe_detail = NULL;
  gboolean running = status == FPV_CORE_RUNNING;

  if (detail && detail[0] != '\0') {
    safe_detail = sanitize_utf8(detail);
    text = g_strdup_printf("%s - %s", status_label(status), safe_detail);
  } else {
    text = g_strdup(status_label(status));
  }

  gtk_label_set_text(GTK_LABEL(context->status_label), text);
  g_free(safe_detail);
  g_free(text);

  context->running = running;
  gtk_widget_set_sensitive(context->start_button, !running);
  gtk_widget_set_sensitive(context->stop_button, running);
  gtk_widget_set_sensitive(
      context->message_send_button,
      running && context->active_chat_id != NULL);
}
