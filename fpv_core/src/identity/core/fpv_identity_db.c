/* FunPay Vertex identity database helpers. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_require_feature(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_feature_flag_t feature) {
  if (!store || !store->db || !organization_id || !organization_id[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_organization_t* org = NULL;
  fpv_result_t result =
      fpv_identity_get_organization(store, organization_id, &org);
  if (result != FPV_OK) {
    return result;
  }
  fpv_feature_mask_t mask = fpv_feature_mask_for_tier(org->tier);
  fpv_organization_destroy(org);
  if (!fpv_feature_mask_has(mask, feature)) {
    return FPV_ERR_UNSUPPORTED;
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_get_team_org_id(
    const fpv_identity_store_t* store,
    const char* team_id,
    char** out_org_id) {
  if (out_org_id) {
    *out_org_id = NULL;
  }
  if (!store || !store->db || !team_id || !team_id[0] || !out_org_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT organization_id FROM fpv_teams WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  char** row = rows->rows[0];
  char* org_id = row && row[0] ? fpv_strdup(row[0]) : NULL;
  fpv_db_result_destroy(rows);
  if (!org_id) {
    return FPV_ERR_INTERNAL;
  }
  *out_org_id = org_id;
  return FPV_OK;
}
fpv_result_t fpv_identity_db_query_single(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    fpv_db_result_t** out_rows) {
  if (out_rows) {
    *out_rows = NULL;
  }
  if (!store || !store->db || !sql || !out_rows) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result =
      fpv_db_query(store->db, sql, params, param_count, &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_NOT_FOUND;
  }
  *out_rows = rows;
  return FPV_OK;
}

fpv_result_t fpv_identity_db_exists(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    bool* out_exists) {
  if (out_exists) {
    *out_exists = false;
  }
  if (!store || !store->db || !sql || !out_exists) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result =
      fpv_db_query(store->db, sql, params, param_count, &rows);
  if (result != FPV_OK) {
    return result;
  }
  *out_exists = rows && rows->row_count > 0;
  fpv_db_result_destroy(rows);
  return FPV_OK;
}

fpv_user_t* fpv_identity_user_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  bool email_verified = false;
  bool active = false;
  uint64_t created_at = 0;
  uint64_t last_login = 0;
  if (!fpv_identity_parse_bool(row[3], &email_verified) ||
      !fpv_identity_parse_bool(row[4], &active) ||
      !fpv_identity_parse_uint64(row[5], &created_at) ||
      !fpv_identity_parse_uint64(row[6], &last_login)) {
    return NULL;
  }
  return fpv_user_create(
      row[0],
      row[1],
      row[2],
      email_verified,
      active,
      created_at,
      last_login);
}

fpv_organization_t* fpv_identity_organization_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  fpv_data_retention_policy_t retention;
  int tier_value = 0;
  if (!fpv_identity_parse_int(row[4], &tier_value)) {
    return NULL;
  }
  fpv_product_tier_t tier = FPV_TIER_BASIC;
  if (tier_value == (int)FPV_TIER_ADVANCED ||
      tier_value == (int)FPV_TIER_ULTIMATE ||
      tier_value == (int)FPV_TIER_BASIC) {
    tier = (fpv_product_tier_t)tier_value;
  }
  if (!fpv_identity_parse_uint32(row[5], &retention.audit_log_days) ||
      !fpv_identity_parse_uint32(row[6], &retention.price_history_days) ||
      !fpv_identity_parse_uint32(row[7], &retention.competitor_listing_days) ||
      !fpv_identity_parse_uint32(row[8], &retention.order_history_days)) {
    return NULL;
  }
  bool approval_required = false;
  if (!fpv_identity_parse_bool(row[9], &approval_required)) {
    return NULL;
  }
  uint64_t created_at = 0;
  uint64_t updated_at = 0;
  if (!fpv_identity_parse_uint64(row[10], &created_at) ||
      !fpv_identity_parse_uint64(row[11], &updated_at)) {
    return NULL;
  }
  return fpv_organization_create(
      row[0],
      row[1],
      row[2],
      row[3],
      tier,
      &retention,
      approval_required,
      created_at,
      updated_at);
}

fpv_team_t* fpv_identity_team_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  bool active = false;
  uint64_t created_at = 0;
  uint64_t updated_at = 0;
  if (!fpv_identity_parse_bool(row[3], &active) ||
      !fpv_identity_parse_uint64(row[4], &created_at) ||
      !fpv_identity_parse_uint64(row[5], &updated_at)) {
    return NULL;
  }
  return fpv_team_create(
      row[0],
      row[1],
      row[2],
      active,
      created_at,
      updated_at);
}

fpv_account_t* fpv_identity_account_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[3]) {
    return NULL;
  }
  bool active = false;
  uint64_t linked_at = 0;
  uint64_t last_sync = 0;
  if (!fpv_identity_parse_bool(row[7], &active) ||
      !fpv_identity_parse_uint64(row[8], &linked_at) ||
      !fpv_identity_parse_uint64(row[9], &last_sync)) {
    return NULL;
  }
  return fpv_account_create(
      row[0],
      row[1],
      row[2],
      row[3],
      row[4],
      row[5],
      row[6],
      active,
      linked_at,
      last_sync);
}

fpv_identity_invite_t* fpv_identity_invite_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[4]) {
    return NULL;
  }
  int role_value = 0;
  if (!fpv_identity_parse_int(row[4], &role_value) ||
      !fpv_identity_role_valid((fpv_role_t)role_value)) {
    return NULL;
  }
  uint64_t created_at = 0;
  uint64_t expires_at = 0;
  uint64_t accepted_at = 0;
  if (!fpv_identity_parse_uint64(row[5], &created_at) ||
      !fpv_identity_parse_uint64(row[6], &expires_at) ||
      !fpv_identity_parse_uint64(row[7], &accepted_at)) {
    return NULL;
  }
  fpv_identity_invite_t* invite =
      (fpv_identity_invite_t*)calloc(1, sizeof(*invite));
  if (!invite) {
    return NULL;
  }
  invite->id = fpv_strdup(row[0]);
  invite->organization_id = fpv_strdup(row[1]);
  invite->team_id = fpv_strdup(row[2]);
  invite->email = fpv_strdup(row[3]);
  invite->role = (fpv_role_t)role_value;
  invite->created_at_ms = created_at;
  invite->expires_at_ms = expires_at;
  invite->accepted_at_ms = accepted_at;
  invite->accepted_user_id = fpv_strdup(row[8]);
  invite->created_by_user_id = fpv_strdup(row[9]);
  if (!invite->id || !invite->organization_id) {
    fpv_identity_invite_destroy(invite);
    return NULL;
  }
  return invite;
}

void fpv_identity_access_member_destroy(fpv_access_member_t* member) {
  if (!member) {
    return;
  }
  fpv_free(member->user_id);
  fpv_free(member->email);
  fpv_free(member->display_name);
  fpv_free(member->team_id);
  fpv_free(member->team_name);
  free(member);
}

fpv_access_member_t* fpv_identity_access_member_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[6] || !row[7]) {
    return NULL;
  }
  bool active = false;
  if (!fpv_identity_parse_bool(row[3], &active)) {
    return NULL;
  }
  int role_value = 0;
  if (!fpv_identity_parse_int(row[6], &role_value) ||
      !fpv_identity_role_valid((fpv_role_t)role_value)) {
    return NULL;
  }
  uint64_t assigned_at = 0;
  if (!fpv_identity_parse_uint64(row[7], &assigned_at)) {
    return NULL;
  }
  fpv_access_member_t* member =
      (fpv_access_member_t*)calloc(1, sizeof(*member));
  if (!member) {
    return NULL;
  }
  member->user_id = fpv_strdup(row[0]);
  member->email = fpv_strdup(row[1]);
  member->display_name = fpv_strdup(row[2]);
  member->active = active;
  member->role = (fpv_role_t)role_value;
  member->team_id = fpv_strdup(row[4]);
  member->team_name = fpv_strdup(row[5]);
  member->assigned_at_ms = assigned_at;
  if (!member->user_id || !member->email ||
      (row[2] && !member->display_name) ||
      (row[4] && !member->team_id) ||
      (row[5] && !member->team_name)) {
    fpv_identity_access_member_destroy(member);
    return NULL;
  }
  return member;
}

void fpv_identity_team_category_scope_destroy(
    fpv_team_category_scope_t* scope) {
  if (!scope) {
    return;
  }
  fpv_free(scope->category);
  fpv_free(scope->subcategory);
  free(scope);
}

fpv_team_category_scope_t* fpv_identity_category_scope_from_row(
    char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  fpv_team_category_scope_t* scope =
      (fpv_team_category_scope_t*)calloc(1, sizeof(*scope));
  if (!scope) {
    return NULL;
  }
  scope->category = fpv_strdup(row[0]);
  scope->subcategory = fpv_strdup(row[1]);
  if (!scope->category || !scope->subcategory) {
    fpv_identity_team_category_scope_destroy(scope);
    return NULL;
  }
  return scope;
}

fpv_access_review_t* fpv_identity_access_review_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[4]) {
    return NULL;
  }
  uint64_t reviewed_at = 0;
  if (!fpv_identity_parse_uint64(row[4], &reviewed_at)) {
    return NULL;
  }
  fpv_access_review_t* review =
      (fpv_access_review_t*)calloc(1, sizeof(*review));
  if (!review) {
    return NULL;
  }
  review->id = fpv_strdup(row[0]);
  review->organization_id = fpv_strdup(row[1]);
  review->reviewer_user_id = fpv_strdup(row[2]);
  review->note = fpv_strdup(row[3]);
  review->reviewed_at_ms = reviewed_at;
  if (!review->id || !review->organization_id ||
      (row[2] && !review->reviewer_user_id) ||
      (row[3] && !review->note)) {
    fpv_access_review_destroy(review);
    return NULL;
  }
  return review;
}

fpv_price_change_request_t* fpv_identity_price_change_request_from_row(
    char** row) {
  if (!row || !row[0] || !row[1] || !row[3] || !row[8] || !row[10] ||
      !row[13] || !row[14] || !row[15]) {
    return NULL;
  }
  double current_price = 0.0;
  double requested_price = 0.0;
  if (!fpv_identity_parse_double(row[6], &current_price) ||
      !fpv_identity_parse_double(row[7], &requested_price)) {
    return NULL;
  }
  int status_value = 0;
  if (!fpv_identity_parse_int(row[10], &status_value)) {
    return NULL;
  }
  if (status_value < FPV_PRICE_CHANGE_PENDING ||
      status_value > FPV_PRICE_CHANGE_APPLIED) {
    return NULL;
  }
  uint64_t requested_at = 0;
  uint64_t reviewed_at = 0;
  uint64_t applied_at = 0;
  if (!fpv_identity_parse_uint64(row[13], &requested_at) ||
      !fpv_identity_parse_uint64(row[14], &reviewed_at) ||
      !fpv_identity_parse_uint64(row[15], &applied_at)) {
    return NULL;
  }
  fpv_price_change_request_t* request =
      (fpv_price_change_request_t*)calloc(1, sizeof(*request));
  if (!request) {
    return NULL;
  }
  request->id = fpv_strdup(row[0]);
  request->organization_id = fpv_strdup(row[1]);
  request->team_id = fpv_strdup(row[2]);
  request->listing_id = fpv_strdup(row[3]);
  request->price_rule_id = fpv_strdup(row[4]);
  request->requested_by_user_id = fpv_strdup(row[5]);
  request->current_price = current_price;
  request->requested_price = requested_price;
  request->currency = fpv_strdup(row[8]);
  request->reason = fpv_strdup(row[9]);
  request->status = (fpv_price_change_status_t)status_value;
  request->reviewed_by_user_id = fpv_strdup(row[11]);
  request->review_note = fpv_strdup(row[12]);
  request->requested_at_ms = requested_at;
  request->reviewed_at_ms = reviewed_at;
  request->applied_at_ms = applied_at;
  if (!request->id || !request->organization_id || !request->listing_id ||
      !request->currency ||
      (row[2] && !request->team_id) ||
      (row[4] && !request->price_rule_id) ||
      (row[5] && !request->requested_by_user_id) ||
      (row[9] && !request->reason) ||
      (row[11] && !request->reviewed_by_user_id) ||
      (row[12] && !request->review_note)) {
    fpv_price_change_request_destroy(request);
    return NULL;
  }
  return request;
}

fpv_result_t fpv_identity_has_other_owner(
    const fpv_identity_store_t* store,
    const char* organization_id,
    const char* user_id,
    const char* team_id,
    bool* out_exists) {
  if (out_exists) {
    *out_exists = false;
  }
  if (!store || !store->db || !organization_id || !user_id || !out_exists) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char role_buf[8];
  snprintf(role_buf, sizeof(role_buf), "%d", FPV_ROLE_OWNER);
  const char* params[] = {organization_id, role_buf, user_id, team_id};
  return fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_user_roles WHERE organization_id = $1 AND role = $2 "
      "AND NOT (user_id = $3 AND ((team_id IS NULL AND $4 IS NULL) OR team_id = $4)) "
      "LIMIT 1;",
      params,
      4,
      out_exists);
}

fpv_result_t fpv_identity_fetch_user_auth_by_email(
    const fpv_identity_store_t* store,
    const char* normalized_email,
    fpv_identity_user_auth_t* out_auth) {
  if (!out_auth) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  memset(out_auth, 0, sizeof(*out_auth));
  const char* params[] = {normalized_email};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, email, display_name, email_verified, active, created_at_ms, "
      "last_login_ms, password_salt, password_hash, password_iterations "
      "FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  char** row = rows->rows[0];
  fpv_user_t* user = fpv_identity_user_from_row(row);
  if (!user || !row[7] || !row[8] || !row[9]) {
    fpv_user_destroy(user);
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  uint32_t iterations = 0;
  if (!fpv_identity_parse_uint32(row[9], &iterations)) {
    fpv_user_destroy(user);
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  out_auth->user = user;
  out_auth->password_salt = fpv_strdup(row[7]);
  out_auth->password_hash = fpv_strdup(row[8]);
  out_auth->password_iterations = iterations;
  fpv_db_result_destroy(rows);
  if (!out_auth->password_salt || !out_auth->password_hash) {
    fpv_identity_user_auth_clear(out_auth);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_fetch_account_id_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    char** out_id) {
  if (out_id) {
    *out_id = NULL;
  }
  if (!out_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id FROM fpv_accounts WHERE organization_id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows->rows[0] || !rows->rows[0][0]) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  *out_id = fpv_strdup(rows->rows[0][0]);
  fpv_db_result_destroy(rows);
  if (!*out_id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_fetch_invite_by_hash(
    const fpv_identity_store_t* store,
    const char* token_hash,
    fpv_identity_invite_t** out_invite) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (!out_invite) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {token_hash};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, email, role, created_at_ms, "
      "expires_at_ms, accepted_at_ms, accepted_user_id, created_by_user_id "
      "FROM fpv_invites WHERE token_hash = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_identity_invite_t* invite = fpv_identity_invite_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!invite) {
    return FPV_ERR_INTERNAL;
  }
  *out_invite = invite;
  return FPV_OK;
}
