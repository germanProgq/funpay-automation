/* FunPay Vertex identity organization operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_create_organization(
    fpv_identity_store_t* store,
    const char* name,
    const char* timezone,
    const char* currency,
    const fpv_data_retention_policy_t* retention,
    const char* owner_user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_default_team,
    fpv_user_role_t** out_owner_role) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (out_default_team) {
    *out_default_team = NULL;
  }
  if (out_owner_role) {
    *out_owner_role = NULL;
  }
  if (!store || !store->db || !name || !owner_user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool owner_exists = false;
  const char* owner_params[] = {owner_user_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
      owner_params,
      1,
      &owner_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!owner_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  char* org_id = fpv_identity_random_id(16);
  if (!org_id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* team_id = fpv_identity_random_id(16);
  if (!team_id) {
    fpv_free(org_id);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  fpv_data_retention_policy_t policy =
      retention ? *retention : fpv_identity_default_retention();
  bool approval_required = false;
  fpv_product_tier_t tier = FPV_TIER_BASIC;

  char created_buf[32];
  char updated_buf[32];
  char audit_buf[16];
  char price_buf[16];
  char competitor_buf[16];
  char order_buf[16];
  char approval_buf[8];
  char tier_buf[8];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(audit_buf, sizeof(audit_buf), "%u", policy.audit_log_days);
  snprintf(price_buf, sizeof(price_buf), "%u", policy.price_history_days);
  snprintf(competitor_buf, sizeof(competitor_buf), "%u",
           policy.competitor_listing_days);
  snprintf(order_buf, sizeof(order_buf), "%u", policy.order_history_days);
  snprintf(approval_buf, sizeof(approval_buf), "%d", approval_required ? 1 : 0);
  snprintf(tier_buf, sizeof(tier_buf), "%d", (int)tier);

  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  const char* org_params[] = {
      org_id,
      name,
      timezone,
      currency,
      tier_buf,
      audit_buf,
      price_buf,
      competitor_buf,
      order_buf,
      approval_buf,
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_organizations (id, name, timezone, currency, tier, "
      "retention_audit_log_days, retention_price_history_days, "
      "retention_competitor_listing_days, retention_order_history_days, "
      "price_change_approval_required, created_at_ms, updated_at_ms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12);",
      org_params,
      12);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  const char* team_params[] = {
      team_id,
      org_id,
      "Default",
      "1",
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_teams (id, organization_id, name, active, "
      "created_at_ms, updated_at_ms) VALUES ($1, $2, $3, $4, $5, $6);",
      team_params,
      6);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", FPV_ROLE_OWNER);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* role_params[] = {
      owner_user_id,
      org_id,
      team_id,
      role_buf,
      assigned_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      role_params,
      5);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  result = fpv_db_commit(store->db);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  if (out_organization) {
    *out_organization = fpv_organization_create(
        org_id,
        name,
        timezone,
        currency,
        tier,
        &policy,
        approval_required,
        now_ms,
        now_ms);
    if (!*out_organization) {
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_default_team) {
    *out_default_team = fpv_team_create(
        team_id,
        org_id,
        "Default",
        true,
        now_ms,
        now_ms);
    if (!*out_default_team) {
      fpv_organization_destroy(*out_organization);
      if (out_organization) {
        *out_organization = NULL;
      }
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_owner_role) {
    *out_owner_role = fpv_user_role_create(
        owner_user_id,
        org_id,
        team_id,
        FPV_ROLE_OWNER,
        now_ms);
    if (!*out_owner_role) {
      fpv_team_destroy(*out_default_team);
      if (out_default_team) {
        *out_default_team = NULL;
      }
      fpv_organization_destroy(*out_organization);
      if (out_organization) {
        *out_organization = NULL;
      }
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  fpv_free(org_id);
  fpv_free(team_id);
  return FPV_OK;
}

fpv_result_t fpv_identity_update_organization(
    fpv_identity_store_t* store,
    const fpv_organization_t* organization) {
  if (!store || !store->db || !organization || !organization->id ||
      !organization->name) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool org_exists = false;
  const char* org_params[] = {organization->id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      org_params,
      1,
      &org_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!org_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char updated_buf[32];
  char audit_buf[16];
  char price_buf[16];
  char competitor_buf[16];
  char order_buf[16];
  char approval_buf[8];
  char tier_buf[8];
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(audit_buf, sizeof(audit_buf), "%u", organization->retention.audit_log_days);
  snprintf(price_buf, sizeof(price_buf), "%u", organization->retention.price_history_days);
  snprintf(competitor_buf, sizeof(competitor_buf), "%u",
           organization->retention.competitor_listing_days);
  snprintf(order_buf, sizeof(order_buf), "%u", organization->retention.order_history_days);
  snprintf(approval_buf, sizeof(approval_buf), "%d",
           organization->price_change_approval_required ? 1 : 0);
  snprintf(tier_buf, sizeof(tier_buf), "%d", (int)organization->tier);

  const char* params[] = {
      organization->name,
      organization->timezone,
      organization->currency,
      tier_buf,
      audit_buf,
      price_buf,
      competitor_buf,
      order_buf,
      approval_buf,
      updated_buf,
      organization->id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_organizations SET name = $1, timezone = $2, currency = $3, "
      "tier = $4, retention_audit_log_days = $5, retention_price_history_days = $6, "
      "retention_competitor_listing_days = $7, retention_order_history_days = $8, "
      "price_change_approval_required = $9, updated_at_ms = $10 WHERE id = $11;",
      params,
      11);
  return result;
}

fpv_result_t fpv_identity_get_organization(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_organization_t** out_organization) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (!store || !store->db || !organization_id || !out_organization) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, name, timezone, currency, tier, retention_audit_log_days, "
      "retention_price_history_days, retention_competitor_listing_days, "
      "retention_order_history_days, price_change_approval_required, "
      "created_at_ms, updated_at_ms "
      "FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_organization_t* org = fpv_identity_organization_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!org) {
    return FPV_ERR_INTERNAL;
  }
  *out_organization = org;
  return FPV_OK;
}

