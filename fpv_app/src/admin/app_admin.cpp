#include "admin/app_admin.h"

#include "auth/app_auth.h"
#include "helpers/app_helpers.h"
#include "settings/app_settings.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

gboolean admin_can_manage(const AppContext* context) {
  return context && context->current_role <= FPV_ROLE_ADMIN;
}

void admin_set_status(AppContext* context, const char* message) {
  if (!context || !context->admin.status_label) {
    return;
  }
  gtk_label_set_text(
      GTK_LABEL(context->admin.status_label),
      message ? message : "");
}

bool update_config_tier(AppContext* context, fpv_product_tier_t tier) {
  if (!context || !context->config_dir) {
    return false;
  }
  gchar* path = g_build_filename(context->config_dir, "_main.cfg", NULL);
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  if (!ini) {
    g_free(path);
    return false;
  }
  const char* tier_value = fpv_tier_to_string(tier);
  bool ok = fpv_ini_set(ini, "Product", "tier", tier_value) == FPV_OK &&
      fpv_ini_save(ini, path) == FPV_OK;
  fpv_ini_destroy(ini);
  g_free(path);
  return ok;
}

GPtrArray* split_csv_list(const char* text) {
  GPtrArray* items = g_ptr_array_new_with_free_func(g_free);
  if (!text) {
    return items;
  }
  gchar** parts = g_strsplit(text, ",", -1);
  for (size_t i = 0; parts && parts[i]; i++) {
    gchar* part = g_strstrip(parts[i]);
    if (part && part[0]) {
      g_ptr_array_add(items, g_strdup(part));
    }
  }
  g_strfreev(parts);
  return items;
}

gchar* join_string_list(char** items, size_t count) {
  if (!items || count == 0) {
    return g_strdup("");
  }
  GString* out = g_string_new(NULL);
  for (size_t i = 0; i < count; i++) {
    if (!items[i] || !items[i][0]) {
      continue;
    }
    if (out->len > 0) {
      g_string_append(out, ", ");
    }
    g_string_append(out, items[i]);
  }
  return g_string_free(out, FALSE);
}

gchar* join_category_scopes(
    fpv_team_category_scope_t** scopes,
    size_t count) {
  GString* out = g_string_new(NULL);
  for (size_t i = 0; i < count; i++) {
    fpv_team_category_scope_t* scope = scopes[i];
    if (!scope || !scope->category || !scope->category[0]) {
      continue;
    }
    if (out->len > 0) {
      g_string_append(out, ", ");
    }
    g_string_append(out, scope->category);
    if (scope->subcategory && scope->subcategory[0]) {
      g_string_append(out, "/");
      g_string_append(out, scope->subcategory);
    }
  }
  return g_string_free(out, FALSE);
}

void refresh_admin_team_list(AppContext* context) {
  if (!context || context->closing || !context->admin.team_combo) {
    return;
  }
  const char* active_id = fpv_dropdown_get_active_id(context->admin.team_combo);
  gchar* previous = active_id ? g_strdup(active_id) : NULL;
  fpv_dropdown_clear(context->admin.team_combo);
  if (!context->identity || !context->current_org) {
    g_free(previous);
    return;
  }
  if (!app_has_feature(context, FPV_FEATURE_MANAGER_SYSTEM)) {
    g_free(previous);
    return;
  }

  fpv_team_t** teams = NULL;
  size_t team_count = 0;
  fpv_result_t result = fpv_identity_list_teams(
      context->identity,
      context->current_org->id,
      &teams,
      &team_count);
  if (result == FPV_OK) {
    for (size_t i = 0; i < team_count; i++) {
      const char* name = teams[i] && teams[i]->name ? teams[i]->name : "Team";
      const char* id = teams[i] && teams[i]->id ? teams[i]->id : "";
      fpv_dropdown_append(context->admin.team_combo, id, name);
    }
  }
  for (size_t i = 0; i < team_count; i++) {
    fpv_team_destroy(teams[i]);
  }
  free(teams);
  if (previous && previous[0]) {
    fpv_dropdown_set_active_id(context->admin.team_combo, previous);
  }
  if (!fpv_dropdown_get_active_id(context->admin.team_combo) && team_count > 0) {
    fpv_dropdown_set_active_index(context->admin.team_combo, 0);
  }
  g_free(previous);
}

void refresh_admin_team_scopes(AppContext* context) {
  if (!context || context->closing || !context->admin.team_combo) {
    return;
  }
  if (context->admin.team_categories) {
    gtk_editable_set_text(GTK_EDITABLE(context->admin.team_categories), "");
  }
  if (context->admin.team_alerts) {
    gtk_editable_set_text(GTK_EDITABLE(context->admin.team_alerts), "");
  }
  if (context->admin.scope_org) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_org), FALSE);
  }
  if (context->admin.scope_team) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_team), FALSE);
  }
  if (context->admin.scope_item) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_item), FALSE);
  }
  if (context->admin.scope_listing) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_listing), FALSE);
  }
  if (!app_has_feature(context, FPV_FEATURE_MANAGER_SYSTEM)) {
    return;
  }
  const char* team_id = fpv_dropdown_get_active_id(context->admin.team_combo);
  if (!context->identity || !team_id || !team_id[0]) {
    return;
  }

  fpv_team_category_scope_t** scopes = NULL;
  size_t scope_count = 0;
  if (fpv_identity_list_team_category_scopes(
          context->identity,
          team_id,
          &scopes,
          &scope_count) == FPV_OK) {
    gchar* joined = join_category_scopes(scopes, scope_count);
    if (context->admin.team_categories) {
      gtk_editable_set_text(GTK_EDITABLE(context->admin.team_categories), joined);
    }
    g_free(joined);
    fpv_identity_team_category_scope_list_destroy(scopes, scope_count);
  }

  char** alerts = NULL;
  size_t alert_count = 0;
  if (fpv_identity_list_team_alert_scopes(
          context->identity,
          team_id,
          &alerts,
          &alert_count) == FPV_OK) {
    gchar* joined = join_string_list(alerts, alert_count);
    if (context->admin.team_alerts) {
      gtk_editable_set_text(GTK_EDITABLE(context->admin.team_alerts), joined);
    }
    g_free(joined);
    fpv_identity_string_list_destroy(alerts, alert_count);
  }

  fpv_price_rule_scope_t* price_scopes = NULL;
  size_t price_scope_count = 0;
  if (fpv_identity_list_team_price_scopes(
          context->identity,
          team_id,
          &price_scopes,
          &price_scope_count) == FPV_OK) {
    gboolean org_enabled = FALSE;
    gboolean team_enabled = FALSE;
    gboolean item_enabled = FALSE;
    gboolean listing_enabled = FALSE;
    for (size_t i = 0; i < price_scope_count; i++) {
      switch (price_scopes[i]) {
        case FPV_PRICE_SCOPE_ORGANIZATION:
          org_enabled = TRUE;
          break;
        case FPV_PRICE_SCOPE_TEAM:
          team_enabled = TRUE;
          break;
        case FPV_PRICE_SCOPE_ITEM:
          item_enabled = TRUE;
          break;
        case FPV_PRICE_SCOPE_LISTING:
          listing_enabled = TRUE;
          break;
        default:
          break;
      }
    }
    if (context->admin.scope_org) {
      gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_org), org_enabled);
    }
    if (context->admin.scope_team) {
      gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_team), team_enabled);
    }
    if (context->admin.scope_item) {
      gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_item), item_enabled);
    }
    if (context->admin.scope_listing) {
      gtk_check_button_set_active(GTK_CHECK_BUTTON(context->admin.scope_listing), listing_enabled);
    }
    free(price_scopes);
  }
}

void refresh_admin_access_lists(AppContext* context) {
  if (!context) {
    return;
  }
  if (context->admin.members_list) {
    clear_list_box(context->admin.members_list);
  }
  if (context->admin.invites_list) {
    clear_list_box(context->admin.invites_list);
  }
  if (!app_has_feature(context, FPV_FEATURE_MANAGER_SYSTEM)) {
    append_list_message(context->admin.members_list,
                        "Manager system is not available in this tier.");
    append_list_message(context->admin.invites_list,
                        "Manager system is not available in this tier.");
    return;
  }
  if (!context->identity || !context->current_org) {
    return;
  }

  fpv_access_member_t** members = NULL;
  size_t member_count = 0;
  if (fpv_identity_list_access_members(
          context->identity,
          context->current_org->id,
          &members,
          &member_count) == FPV_OK &&
      context->admin.members_list) {
    for (size_t i = 0; i < member_count; i++) {
      fpv_access_member_t* member = members[i];
      if (!member) {
        continue;
      }
      GtkWidget* row = gtk_list_box_row_new();
      GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
      GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

      const char* display = member->display_name && member->display_name[0]
          ? member->display_name
          : (member->email ? member->email : "");
      gchar* title_text = sanitize_utf8(display);
      GtkWidget* title = gtk_label_new(title_text);
      gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

      const char* team_name =
          member->team_name && member->team_name[0]
              ? member->team_name
              : "Organization";
      gchar* meta = g_strdup_printf("Team: %s", team_name);
      GtkWidget* meta_label = gtk_label_new(meta);
      gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
      gtk_widget_add_css_class(meta_label, "muted");

      gtk_box_append(GTK_BOX(info), title);
      gtk_box_append(GTK_BOX(info), meta_label);

      GtkWidget* role_combo = fpv_dropdown_new();
      fpv_dropdown_append(role_combo, "owner", "Owner");
      fpv_dropdown_append(role_combo, "admin", "Admin");
      fpv_dropdown_append(role_combo, "manager", "Manager");
      fpv_dropdown_append(role_combo, "analyst", "Analyst");
      fpv_dropdown_append(role_combo, "viewer", "Viewer");
      fpv_dropdown_set_active_id(role_combo, role_id(member->role));
      g_object_set_data_full(
          G_OBJECT(role_combo),
          "user-id",
          member->user_id ? g_strdup(member->user_id) : NULL,
          g_free);
      g_object_set_data_full(
          G_OBJECT(role_combo),
          "team-id",
          member->team_id ? g_strdup(member->team_id) : NULL,
          g_free);
      g_object_set_data(
          G_OBJECT(role_combo),
          "current-role",
          GINT_TO_POINTER(member->role));
      g_signal_connect(
          role_combo,
          "notify::selected",
          G_CALLBACK(on_admin_role_changed),
          context);

      GtkWidget* remove_button = gtk_button_new_with_label("Remove");
      g_object_set_data_full(
          G_OBJECT(remove_button),
          "user-id",
          member->user_id ? g_strdup(member->user_id) : NULL,
          g_free);
      g_object_set_data_full(
          G_OBJECT(remove_button),
          "team-id",
          member->team_id ? g_strdup(member->team_id) : NULL,
          g_free);
      g_signal_connect(remove_button, "clicked", G_CALLBACK(on_admin_remove_role), context);

      gboolean can_manage = admin_can_manage(context);
      gboolean is_self = context->current_user && member->user_id &&
          strcmp(member->user_id, context->current_user->id) == 0;
      gboolean is_owner = member->role == FPV_ROLE_OWNER;
      gboolean editable = can_manage && !is_self &&
          (context->current_role == FPV_ROLE_OWNER || !is_owner);
      gtk_widget_set_sensitive(role_combo, editable);
      gtk_widget_set_sensitive(remove_button, editable);

      GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_box_append(GTK_BOX(actions), role_combo);
      gtk_box_append(GTK_BOX(actions), remove_button);

      gtk_widget_set_hexpand(info, TRUE);
      gtk_box_append(GTK_BOX(box), info);
      gtk_box_append(GTK_BOX(box), actions);
      gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
      gtk_list_box_append(GTK_LIST_BOX(context->admin.members_list), row);

      g_free(meta);
      g_free(title_text);
    }
    fpv_identity_access_member_list_destroy(members, member_count);
  }

  fpv_identity_invite_t** invites = NULL;
  size_t invite_count = 0;
  if (fpv_identity_list_invites(
          context->identity,
          context->current_org->id,
          false,
          &invites,
          &invite_count) == FPV_OK &&
      context->admin.invites_list) {
    for (size_t i = 0; i < invite_count; i++) {
      fpv_identity_invite_t* invite = invites[i];
      if (!invite) {
        continue;
      }
      GtkWidget* row = gtk_list_box_row_new();
      GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
      GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

      const char* email = invite->email ? invite->email : "Invite";
      gchar* title_text = sanitize_utf8(email);
      GtkWidget* title = gtk_label_new(title_text);
      gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

      const char* team_label = "Organization";
      fpv_team_t* team = NULL;
      if (invite->team_id) {
        if (fpv_identity_get_team(
                context->identity,
                invite->team_id,
                &team) == FPV_OK &&
            team && team->name && team->name[0]) {
          team_label = team->name;
        }
      }
      gchar* meta = g_strdup_printf(
          "%s · %s",
          role_label(invite->role),
          team_label);
      GtkWidget* meta_label = gtk_label_new(meta);
      gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
      gtk_widget_add_css_class(meta_label, "muted");

      gtk_box_append(GTK_BOX(info), title);
      gtk_box_append(GTK_BOX(info), meta_label);

      GtkWidget* revoke_button = gtk_button_new_with_label("Revoke");
      g_object_set_data_full(
          G_OBJECT(revoke_button),
          "invite-id",
          invite->id ? g_strdup(invite->id) : NULL,
          g_free);
      g_signal_connect(revoke_button, "clicked", G_CALLBACK(on_admin_revoke_invite), context);
      gtk_widget_set_sensitive(revoke_button, admin_can_manage(context));

      gtk_widget_set_hexpand(info, TRUE);
      gtk_box_append(GTK_BOX(box), info);
      gtk_box_append(GTK_BOX(box), revoke_button);
      gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
      gtk_list_box_append(GTK_LIST_BOX(context->admin.invites_list), row);

      fpv_team_destroy(team);
      g_free(meta);
      g_free(title_text);
    }
    fpv_identity_invite_list_destroy(invites, invite_count);
  }
}

void refresh_admin_access_review(AppContext* context) {
  if (!context || !context->admin.access_review_label) {
    return;
  }
  gtk_label_set_text(GTK_LABEL(context->admin.access_review_label),
                     "No access reviews yet.");
  if (!context->identity || !context->current_org) {
    return;
  }
  fpv_access_review_t* review = NULL;
  fpv_result_t result = fpv_identity_get_latest_access_review(
      context->identity,
      context->current_org->id,
      &review);
  if (result != FPV_OK || !review) {
    fpv_access_review_destroy(review);
    return;
  }
  gchar* timestamp = format_timestamp(review->reviewed_at_ms);
  gchar* label_text = NULL;
  if (review->reviewer_user_id) {
    fpv_user_t* reviewer = NULL;
    if (fpv_identity_get_user(
            context->identity,
            review->reviewer_user_id,
            &reviewer) == FPV_OK &&
        reviewer) {
      const char* name =
          reviewer->display_name && reviewer->display_name[0]
              ? reviewer->display_name
              : reviewer->email;
      label_text = g_strdup_printf("Last review: %s · %s",
                                   timestamp ? timestamp : "",
                                   name ? name : "");
    }
    fpv_user_destroy(reviewer);
  }
  if (!label_text) {
    label_text = g_strdup_printf("Last review: %s",
                                 timestamp ? timestamp : "");
  }
  gtk_label_set_text(GTK_LABEL(context->admin.access_review_label), label_text);
  g_free(label_text);
  g_free(timestamp);
  fpv_access_review_destroy(review);
}

void refresh_admin_price_changes(AppContext* context) {
  if (!context || !context->admin.price_change_list) {
    return;
  }
  clear_list_box(context->admin.price_change_list);
  if (!app_has_feature(context, FPV_FEATURE_MASS_PRICE_EDITOR)) {
    append_list_message(context->admin.price_change_list,
                        "Price approvals require Advanced tier.");
    return;
  }
  if (!context->identity || !context->current_org) {
    return;
  }
  fpv_price_change_request_t** requests = NULL;
  size_t request_count = 0;
  fpv_result_t result = fpv_identity_list_price_change_requests(
          context->identity,
          context->current_org->id,
          FPV_PRICE_CHANGE_PENDING,
          &requests,
          &request_count);
  if (result != FPV_OK) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("Failed to load price changes.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->admin.price_change_list), row);
    fpv_identity_price_change_request_list_destroy(requests, request_count);
    return;
  }
  if (!requests || request_count == 0) {
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* label = gtk_label_new("No pending price changes.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
    gtk_list_box_append(GTK_LIST_BOX(context->admin.price_change_list), row);
    fpv_identity_price_change_request_list_destroy(requests, request_count);
    return;
  }

  for (size_t i = 0; i < request_count; i++) {
    fpv_price_change_request_t* request = requests[i];
    if (!request) {
      continue;
    }
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gchar* title_text = g_strdup_printf(
        "Listing %s",
        request->listing_id ? request->listing_id : "");
    GtkWidget* title = gtk_label_new(title_text);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);

    gchar* meta = g_strdup_printf(
        "%.2f -> %.2f %s",
        request->current_price,
        request->requested_price,
        request->currency ? request->currency : "");
    GtkWidget* meta_label = gtk_label_new(meta);
    gtk_label_set_xalign(GTK_LABEL(meta_label), 0.0f);
    gtk_widget_add_css_class(meta_label, "muted");

    gtk_box_append(GTK_BOX(info), title);
    gtk_box_append(GTK_BOX(info), meta_label);

    GtkWidget* approve_button = gtk_button_new_with_label("Approve");
    GtkWidget* reject_button = gtk_button_new_with_label("Reject");
    g_object_set_data_full(
        G_OBJECT(approve_button),
        "request-id",
        request->id ? g_strdup(request->id) : NULL,
        g_free);
    g_object_set_data_full(
        G_OBJECT(reject_button),
        "request-id",
        request->id ? g_strdup(request->id) : NULL,
        g_free);
    g_signal_connect(approve_button, "clicked",
                     G_CALLBACK(on_admin_price_change_approve), context);
    g_signal_connect(reject_button, "clicked",
                     G_CALLBACK(on_admin_price_change_reject), context);
    gboolean can_manage =
        admin_can_manage(context) &&
        app_has_feature(context, FPV_FEATURE_MASS_PRICE_EDITOR);
    gtk_widget_set_sensitive(approve_button, can_manage);
    gtk_widget_set_sensitive(reject_button, can_manage);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(actions), approve_button);
    gtk_box_append(GTK_BOX(actions), reject_button);

    gtk_widget_set_hexpand(info, TRUE);
    gtk_box_append(GTK_BOX(box), info);
    gtk_box_append(GTK_BOX(box), actions);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_list_box_append(GTK_LIST_BOX(context->admin.price_change_list), row);

    g_free(title_text);
    g_free(meta);
  }
  fpv_identity_price_change_request_list_destroy(requests, request_count);
}

void refresh_admin_panel(AppContext* context) {
  if (!context || context->closing) {
    return;
  }
  admin_set_status(context, "");
  gboolean can_manage = admin_can_manage(context);
  gboolean allow_manager = app_has_feature(context, FPV_FEATURE_MANAGER_SYSTEM);
  gboolean allow_price = app_has_feature(context, FPV_FEATURE_MASS_PRICE_EDITOR);
  gboolean can_manage_team = can_manage && allow_manager;

  if (context->admin.org_name) {
    const char* name =
        context->current_org && context->current_org->name
            ? context->current_org->name
            : "";
    gtk_editable_set_text(GTK_EDITABLE(context->admin.org_name), name);
    gtk_widget_set_sensitive(context->admin.org_name, can_manage);
  }
  if (context->admin.org_timezone) {
    const char* value =
        context->current_org && context->current_org->timezone
            ? context->current_org->timezone
            : "";
    gtk_editable_set_text(GTK_EDITABLE(context->admin.org_timezone), value);
    gtk_widget_set_sensitive(context->admin.org_timezone, can_manage);
  }
  if (context->admin.org_currency) {
    const char* value =
        context->current_org && context->current_org->currency
            ? context->current_org->currency
            : "";
    gtk_editable_set_text(GTK_EDITABLE(context->admin.org_currency), value);
    gtk_widget_set_sensitive(context->admin.org_currency, can_manage);
  }
  if (context->admin.org_tier) {
    const char* tier_value =
        context->current_org ? fpv_tier_to_string(context->current_org->tier) : "basic";
    fpv_dropdown_set_active_id(context->admin.org_tier, tier_value);
    gtk_widget_set_sensitive(context->admin.org_tier, can_manage);
  }
  if (context->admin.retention_audit && context->current_org) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->admin.retention_audit),
        (double)context->current_org->retention.audit_log_days);
    gtk_widget_set_sensitive(context->admin.retention_audit, can_manage);
  }
  if (context->admin.retention_price && context->current_org) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->admin.retention_price),
        (double)context->current_org->retention.price_history_days);
    gtk_widget_set_sensitive(context->admin.retention_price, can_manage);
  }
  if (context->admin.retention_competitor && context->current_org) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->admin.retention_competitor),
        (double)context->current_org->retention.competitor_listing_days);
    gtk_widget_set_sensitive(context->admin.retention_competitor, can_manage);
  }
  if (context->admin.retention_order && context->current_org) {
    gtk_spin_button_set_value(
        GTK_SPIN_BUTTON(context->admin.retention_order),
        (double)context->current_org->retention.order_history_days);
    gtk_widget_set_sensitive(context->admin.retention_order, can_manage);
  }
  if (context->admin.approval_required && context->current_org) {
    gtk_switch_set_active(
        GTK_SWITCH(context->admin.approval_required),
        context->current_org->price_change_approval_required ? TRUE : FALSE);
    gtk_widget_set_sensitive(context->admin.approval_required, can_manage && allow_price);
  }
  if (context->admin.org_save_button) {
    gtk_widget_set_sensitive(context->admin.org_save_button, can_manage);
  }
  if (context->admin.team_create_button) {
    gtk_widget_set_sensitive(context->admin.team_create_button, can_manage_team);
  }
  if (context->admin.team_create_entry) {
    gtk_widget_set_sensitive(context->admin.team_create_entry, can_manage_team);
  }
  if (context->admin.team_categories) {
    gtk_widget_set_sensitive(context->admin.team_categories, can_manage_team);
  }
  if (context->admin.team_alerts) {
    gtk_widget_set_sensitive(context->admin.team_alerts, can_manage_team);
  }
  if (context->admin.scope_org) {
    gtk_widget_set_sensitive(context->admin.scope_org, can_manage_team);
  }
  if (context->admin.scope_team) {
    gtk_widget_set_sensitive(context->admin.scope_team, can_manage_team);
  }
  if (context->admin.scope_item) {
    gtk_widget_set_sensitive(context->admin.scope_item, can_manage_team);
  }
  if (context->admin.scope_listing) {
    gtk_widget_set_sensitive(context->admin.scope_listing, can_manage_team);
  }
  if (context->admin.team_save_button) {
    gtk_widget_set_sensitive(context->admin.team_save_button, can_manage_team);
  }
  if (context->admin.access_review_button) {
    gtk_widget_set_sensitive(context->admin.access_review_button, can_manage_team);
  }
  if (context->admin.access_review_note) {
    gtk_widget_set_sensitive(context->admin.access_review_note, can_manage_team);
  }
  if (context->admin.team_combo) {
    gtk_widget_set_sensitive(context->admin.team_combo, can_manage_team);
  }
  if (context->admin.members_list) {
    gtk_widget_set_sensitive(context->admin.members_list, can_manage_team);
  }
  if (context->admin.invites_list) {
    gtk_widget_set_sensitive(context->admin.invites_list, can_manage_team);
  }
  if (context->admin.price_change_list) {
    gtk_widget_set_sensitive(context->admin.price_change_list, can_manage && allow_price);
  }

  refresh_admin_team_list(context);
  refresh_admin_team_scopes(context);
  refresh_admin_access_lists(context);
  refresh_admin_access_review(context);
  refresh_admin_price_changes(context);
}

void on_admin_refresh(GtkButton* button, gpointer user_data) {
  (void)button;
  AppContext* context = (AppContext*)user_data;
  refresh_admin_panel(context);
}

void on_admin_save_org(GtkButton* button, gpointer user_data) {
  (void)button;
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity || !context->current_org) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* name = gtk_editable_get_text(GTK_EDITABLE(context->admin.org_name));
  if (!name || !name[0]) {
    admin_set_status(context, "Organization name is required.");
    return;
  }
  fpv_organization_t updated;
  memset(&updated, 0, sizeof(updated));
  updated.id = context->current_org->id;
  updated.name = (char*)name;
  updated.timezone =
      (char*)gtk_editable_get_text(GTK_EDITABLE(context->admin.org_timezone));
  updated.currency =
      (char*)gtk_editable_get_text(GTK_EDITABLE(context->admin.org_currency));
  updated.retention.audit_log_days =
      (uint32_t)gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->admin.retention_audit));
  updated.retention.price_history_days =
      (uint32_t)gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->admin.retention_price));
  updated.retention.competitor_listing_days =
      (uint32_t)gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->admin.retention_competitor));
  updated.retention.order_history_days =
      (uint32_t)gtk_spin_button_get_value_as_int(
          GTK_SPIN_BUTTON(context->admin.retention_order));
  updated.price_change_approval_required =
      gtk_switch_get_active(GTK_SWITCH(context->admin.approval_required));
  fpv_product_tier_t tier = context->current_org
      ? context->current_org->tier
      : FPV_TIER_BASIC;
  if (context->admin.org_tier) {
    const char* tier_id = fpv_dropdown_get_active_id(context->admin.org_tier);
    if (tier_id && tier_id[0]) {
      tier = fpv_tier_from_string(tier_id);
    }
  }
  updated.tier = tier;

  fpv_result_t result = fpv_identity_update_organization(
      context->identity,
      &updated);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }

  fpv_organization_t* refreshed = NULL;
  if (fpv_identity_get_organization(
          context->identity,
          context->current_org->id,
          &refreshed) == FPV_OK &&
      refreshed) {
    fpv_organization_destroy(context->current_org);
    context->current_org = refreshed;
  }
  refresh_identity_labels(context);
  refresh_admin_panel(context);
  bool tier_ok = update_config_tier(context, updated.tier);
  refresh_settings_from_file(context);
  if (!tier_ok) {
    admin_set_status(context, "Organization saved, tier sync failed.");
    return;
  }
  admin_set_status(context, "Organization settings saved.");
}

void on_admin_team_changed(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)object;
  (void)pspec;
  AppContext* context = (AppContext*)user_data;
  if (!context || context->closing) {
    return;
  }
  refresh_admin_team_scopes(context);
}

void on_admin_save_team_scopes(GtkButton* button, gpointer user_data) {
  (void)button;
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity || !context->admin.team_combo) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* team_id = fpv_dropdown_get_active_id(context->admin.team_combo);
  if (!team_id || !team_id[0]) {
    admin_set_status(context, "Select a team.");
    return;
  }

  GPtrArray* categories = split_csv_list(
      gtk_editable_get_text(GTK_EDITABLE(context->admin.team_categories)));
  fpv_team_category_scope_t** scopes = NULL;
  if (categories->len > 0) {
    scopes = (fpv_team_category_scope_t**)calloc(categories->len, sizeof(*scopes));
    if (!scopes) {
      g_ptr_array_free(categories, TRUE);
      admin_set_status(context, "Out of memory.");
      return;
    }
  }
  size_t scope_count = 0;
  for (guint i = 0; i < categories->len; i++) {
    const char* token = (const char*)g_ptr_array_index(categories, i);
    if (!token || !token[0]) {
      continue;
    }
    const char* slash = strchr(token, '/');
    gchar* category = NULL;
    gchar* subcategory = NULL;
    if (slash) {
      category = g_strndup(token, (gsize)(slash - token));
      subcategory = g_strdup(slash + 1);
    } else {
      category = g_strdup(token);
      subcategory = g_strdup("");
    }
    if (!category || !subcategory) {
      g_free(category);
      g_free(subcategory);
      continue;
    }
    g_strstrip(category);
    g_strstrip(subcategory);
    if (!category[0]) {
      g_free(category);
      g_free(subcategory);
      continue;
    }
    fpv_team_category_scope_t* scope =
        (fpv_team_category_scope_t*)calloc(1, sizeof(*scope));
    if (!scope) {
      g_free(category);
      g_free(subcategory);
      continue;
    }
    scope->category = app_strdup(category);
    scope->subcategory = app_strdup(subcategory);
    g_free(category);
    g_free(subcategory);
    if (!scope->category || !scope->subcategory) {
      free(scope->category);
      free(scope->subcategory);
      free(scope);
      continue;
    }
    scopes[scope_count++] = scope;
  }
  g_ptr_array_free(categories, TRUE);
  fpv_result_t result = fpv_identity_replace_team_category_scopes(
      context->identity,
      team_id,
      (const fpv_team_category_scope_t* const*)scopes,
      scope_count);
  fpv_identity_team_category_scope_list_destroy(scopes, scope_count);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }

  fpv_price_rule_scope_t scope_values[4];
  size_t price_scope_count = 0;
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(context->admin.scope_org))) {
    scope_values[price_scope_count++] = FPV_PRICE_SCOPE_ORGANIZATION;
  }
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(context->admin.scope_team))) {
    scope_values[price_scope_count++] = FPV_PRICE_SCOPE_TEAM;
  }
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(context->admin.scope_item))) {
    scope_values[price_scope_count++] = FPV_PRICE_SCOPE_ITEM;
  }
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(context->admin.scope_listing))) {
    scope_values[price_scope_count++] = FPV_PRICE_SCOPE_LISTING;
  }
  fpv_price_rule_scope_t* scopes_copy = NULL;
  if (price_scope_count > 0) {
    scopes_copy = (fpv_price_rule_scope_t*)calloc(
        price_scope_count,
        sizeof(*scopes_copy));
    if (!scopes_copy) {
      admin_set_status(context, "Out of memory.");
      return;
    }
    memcpy(scopes_copy, scope_values,
           price_scope_count * sizeof(*scopes_copy));
  }
  result = fpv_identity_replace_team_price_scopes(
      context->identity,
      team_id,
      scopes_copy,
      price_scope_count);
  free(scopes_copy);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }

  GPtrArray* alerts = split_csv_list(
      gtk_editable_get_text(GTK_EDITABLE(context->admin.team_alerts)));
  char** alert_items = NULL;
  if (alerts->len > 0) {
    alert_items = (char**)calloc(alerts->len, sizeof(*alert_items));
    if (!alert_items) {
      g_ptr_array_free(alerts, TRUE);
      admin_set_status(context, "Out of memory.");
      return;
    }
  }
  size_t alert_count = 0;
  for (guint i = 0; i < alerts->len; i++) {
    const char* token = (const char*)g_ptr_array_index(alerts, i);
    if (!token || !token[0]) {
      continue;
    }
    alert_items[alert_count] = app_strdup(token);
    if (!alert_items[alert_count]) {
      continue;
    }
    alert_count++;
  }
  g_ptr_array_free(alerts, TRUE);
  result = fpv_identity_replace_team_alert_scopes(
      context->identity,
      team_id,
      (const char* const*)alert_items,
      alert_count);
  fpv_identity_string_list_destroy(alert_items, alert_count);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }

  refresh_admin_team_scopes(context);
  admin_set_status(context, "Team scopes saved.");
}

void on_admin_create_team(GtkButton* button, gpointer user_data) {
  (void)button;
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity || !context->current_org ||
      !context->admin.team_create_entry) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* name = gtk_editable_get_text(
      GTK_EDITABLE(context->admin.team_create_entry));
  if (!name || !name[0]) {
    admin_set_status(context, "Team name is required.");
    return;
  }
  fpv_team_t* team = NULL;
  fpv_result_t result = fpv_identity_create_team(
      context->identity,
      context->current_org->id,
      name,
      true,
      &team);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    fpv_team_destroy(team);
    return;
  }
  fpv_team_destroy(team);
  gtk_editable_set_text(GTK_EDITABLE(context->admin.team_create_entry), "");
  refresh_admin_team_list(context);
  refresh_admin_team_scopes(context);
  admin_set_status(context, "Team created.");
}

void on_admin_role_changed(
    GObject* object,
    GParamSpec* pspec,
    gpointer user_data) {
  (void)pspec;
  GtkWidget* dropdown = GTK_WIDGET(object);
  AppContext* context = (AppContext*)user_data;
  if (!context || context->closing || !context->identity || !context->current_org) {
    return;
  }
  if (!admin_can_manage(context)) {
    return;
  }
  const char* user_id =
      (const char*)g_object_get_data(G_OBJECT(dropdown), "user-id");
  const char* team_id =
      (const char*)g_object_get_data(G_OBJECT(dropdown), "team-id");
  if (!user_id || !user_id[0]) {
    return;
  }
  const char* role_text = fpv_dropdown_get_active_id(dropdown);
  fpv_role_t new_role = role_from_id(role_text);
  fpv_role_t current_role = (fpv_role_t)GPOINTER_TO_INT(
      g_object_get_data(G_OBJECT(dropdown), "current-role"));
  if (new_role == FPV_ROLE_UNKNOWN || new_role == current_role) {
    return;
  }
  if (context->current_role != FPV_ROLE_OWNER &&
      new_role == FPV_ROLE_OWNER) {
    fpv_dropdown_set_active_id(dropdown, role_id(current_role));
    admin_set_status(context, "Only owners can assign owner roles.");
    return;
  }
  fpv_result_t result = fpv_identity_update_role(
      context->identity,
      user_id,
      context->current_org->id,
      team_id,
      new_role);
  if (result != FPV_OK) {
    fpv_dropdown_set_active_id(dropdown, role_id(current_role));
    admin_set_status(context, admin_error_message(result));
    return;
  }
  g_object_set_data(G_OBJECT(dropdown), "current-role", GINT_TO_POINTER(new_role));
  admin_set_status(context, "Role updated.");
}

void on_admin_remove_role(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity || !context->current_org) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* user_id =
      (const char*)g_object_get_data(G_OBJECT(button), "user-id");
  const char* team_id =
      (const char*)g_object_get_data(G_OBJECT(button), "team-id");
  if (!user_id || !user_id[0]) {
    return;
  }
  fpv_result_t result = fpv_identity_remove_role(
      context->identity,
      user_id,
      context->current_org->id,
      team_id);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }
  refresh_admin_access_lists(context);
  admin_set_status(context, "Role removed.");
}

void on_admin_revoke_invite(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* invite_id =
      (const char*)g_object_get_data(G_OBJECT(button), "invite-id");
  if (!invite_id || !invite_id[0]) {
    return;
  }
  fpv_result_t result = fpv_identity_revoke_invite(
      context->identity,
      invite_id);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }
  refresh_admin_access_lists(context);
  admin_set_status(context, "Invite revoked.");
}

void on_admin_mark_access_review(GtkButton* button, gpointer user_data) {
  (void)button;
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity || !context->current_org) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* note = NULL;
  if (context->admin.access_review_note) {
    const char* text = gtk_editable_get_text(
        GTK_EDITABLE(context->admin.access_review_note));
    if (text && text[0]) {
      note = text;
    }
  }
  fpv_access_review_t* review = NULL;
  fpv_result_t result = fpv_identity_record_access_review(
      context->identity,
      context->current_org->id,
      context->current_user ? context->current_user->id : NULL,
      note,
      &review);
  fpv_access_review_destroy(review);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }
  if (context->admin.access_review_note) {
    gtk_editable_set_text(GTK_EDITABLE(context->admin.access_review_note), "");
  }
  refresh_admin_access_review(context);
  admin_set_status(context, "Access review recorded.");
}

void on_admin_price_change_approve(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* request_id =
      (const char*)g_object_get_data(G_OBJECT(button), "request-id");
  if (!request_id || !request_id[0]) {
    return;
  }
  fpv_price_change_request_t* request = NULL;
  fpv_result_t result = fpv_identity_review_price_change_request(
      context->identity,
      request_id,
      FPV_PRICE_CHANGE_APPROVED,
      context->current_user ? context->current_user->id : NULL,
      NULL,
      &request);
  fpv_price_change_request_destroy(request);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }
  refresh_admin_price_changes(context);
  admin_set_status(context, "Price change approved.");
}

void on_admin_price_change_reject(GtkButton* button, gpointer user_data) {
  AppContext* context = (AppContext*)user_data;
  if (!context || !context->identity) {
    return;
  }
  if (!admin_can_manage(context)) {
    admin_set_status(context, "Insufficient permissions.");
    return;
  }
  const char* request_id =
      (const char*)g_object_get_data(G_OBJECT(button), "request-id");
  if (!request_id || !request_id[0]) {
    return;
  }
  fpv_price_change_request_t* request = NULL;
  fpv_result_t result = fpv_identity_review_price_change_request(
      context->identity,
      request_id,
      FPV_PRICE_CHANGE_REJECTED,
      context->current_user ? context->current_user->id : NULL,
      NULL,
      &request);
  fpv_price_change_request_destroy(request);
  if (result != FPV_OK) {
    admin_set_status(context, admin_error_message(result));
    return;
  }
  refresh_admin_price_changes(context);
  admin_set_status(context, "Price change rejected.");
}
