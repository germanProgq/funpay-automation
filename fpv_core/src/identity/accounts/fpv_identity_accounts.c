/* FunPay Vertex identity account operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_link_account(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* funpay_user_id,
    const char* funpay_username,
    const char* display_name,
    const char* currency,
    fpv_account_t** out_account) {
  if (out_account) {
    *out_account = NULL;
  }
  if (!store || !store->db || !organization_id || !funpay_user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
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
  if (team_id) {
    bool team_ok = false;
    const char* team_params[] = {team_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 "
        "LIMIT 1;",
        team_params,
        2,
        &team_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!team_ok) {
      return FPV_ERR_NOT_FOUND;
    }
  }

  char* account_id = NULL;
  result = fpv_identity_fetch_account_id_for_org(
      store,
      organization_id,
      &account_id);
  bool exists = result == FPV_OK;
  if (result != FPV_OK && result != FPV_ERR_NOT_FOUND) {
    return result;
  }
  if (!exists) {
    account_id = fpv_identity_random_id(16);
    if (!account_id) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  uint64_t now_ms = fpv_time_now_ms();
  char linked_buf[32];
  snprintf(linked_buf, sizeof(linked_buf), "%llu",
           (unsigned long long)now_ms);

  if (exists) {
    const char* params[] = {
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        linked_buf,
        account_id};
    result = fpv_db_exec_params(
        store->db,
        "UPDATE fpv_accounts SET team_id = $1, funpay_user_id = $2, "
        "funpay_username = $3, display_name = $4, currency = $5, active = 1, "
        "linked_at_ms = $6, last_sync_at_ms = $6 WHERE id = $7;",
        params,
        7);
  } else {
    const char* params[] = {
        account_id,
        organization_id,
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        "1",
        linked_buf,
        linked_buf};
    result = fpv_db_exec_params(
        store->db,
        "INSERT INTO fpv_accounts (id, organization_id, team_id, funpay_user_id, "
        "funpay_username, display_name, currency, active, linked_at_ms, "
        "last_sync_at_ms) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10);",
        params,
        10);
  }
  if (result != FPV_OK) {
    fpv_free(account_id);
    return result;
  }

  if (out_account) {
    *out_account = fpv_account_create(
        account_id,
        organization_id,
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        true,
        now_ms,
        now_ms);
    if (!*out_account) {
      fpv_free(account_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(account_id);
  return FPV_OK;
}

fpv_result_t fpv_identity_get_account_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_account_t** out_account) {
  if (out_account) {
    *out_account = NULL;
  }
  if (!store || !store->db || !organization_id || !out_account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, funpay_user_id, funpay_username, "
      "display_name, currency, active, linked_at_ms, last_sync_at_ms "
      "FROM fpv_accounts WHERE organization_id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_account_t* account = fpv_identity_account_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!account) {
    return FPV_ERR_INTERNAL;
  }
  *out_account = account;
  return FPV_OK;
}

