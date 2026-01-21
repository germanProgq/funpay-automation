/* FunPay Vertex identity user operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_create_user(
    fpv_identity_store_t* store,
    const char* email,
    const char* display_name,
    const char* password,
    bool email_verified,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !email || !password) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* normalized = fpv_identity_normalize_email(email);
  if (!normalized || !fpv_identity_validate_email(normalized)) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!fpv_identity_validate_password(password)) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool exists = false;
  const char* exists_params[] = {normalized};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
      exists_params,
      1,
      &exists);
  if (result != FPV_OK) {
    fpv_free(normalized);
    return result;
  }
  if (exists) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_STATE;
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint8_t salt[16];
  if (!fpv_random_bytes(salt, sizeof(salt))) {
    fpv_free(id);
    fpv_free(normalized);
    return FPV_ERR_INTERNAL;
  }
  const uint32_t iterations = 200000;
  char* salt_hex = fpv_hex_encode(salt, sizeof(salt));
  char* hash_hex = fpv_identity_hash_password(
      password, salt, sizeof(salt), iterations);
  fpv_secure_zero(salt, sizeof(salt));
  if (!salt_hex || !hash_hex) {
    fpv_free(id);
    fpv_free(normalized);
    fpv_free(salt_hex);
    fpv_free(hash_hex);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  uint64_t now_ms = fpv_time_now_ms();
  fpv_user_t* user = fpv_user_create(
      id,
      normalized,
      display_name,
      email_verified,
      true,
      now_ms,
      0);
  if (!user) {
    fpv_free(id);
    fpv_free(normalized);
    fpv_free(salt_hex);
    fpv_free(hash_hex);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char created_buf[32];
  char last_login_buf[32];
  char iterations_buf[32];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(last_login_buf, sizeof(last_login_buf), "%llu",
           (unsigned long long)0ULL);
  snprintf(iterations_buf, sizeof(iterations_buf), "%u", iterations);
  const char* params[] = {
      id,
      normalized,
      normalized,
      display_name,
      email_verified ? "1" : "0",
      "1",
      created_buf,
      last_login_buf,
      salt_hex,
      hash_hex,
      iterations_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_users (id, email, email_normalized, display_name, "
      "email_verified, active, created_at_ms, last_login_ms, password_salt, "
      "password_hash, password_iterations) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);",
      params,
      11);
  fpv_free(id);
  fpv_free(normalized);
  fpv_free(salt_hex);
  fpv_free(hash_hex);
  if (result != FPV_OK) {
    fpv_user_destroy(user);
    return result;
  }
  if (out_user) {
    *out_user = user;
  } else {
    fpv_user_destroy(user);
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_authenticate(
    fpv_identity_store_t* store,
    const char* email,
    const char* password,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !email || !password) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* normalized = fpv_identity_normalize_email(email);
  if (!normalized) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_user_auth_t auth;
  fpv_result_t result = fpv_identity_fetch_user_auth_by_email(
      store, normalized, &auth);
  fpv_free(normalized);
  if (result != FPV_OK) {
    return result;
  }
  if (!auth.user || !auth.user->active) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_NOT_FOUND;
  }
  uint8_t salt[16];
  if (!fpv_hex_decode(auth.password_salt, salt, sizeof(salt))) {
    fpv_secure_zero(salt, sizeof(salt));
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_INTERNAL;
  }
  char* hash_hex = fpv_identity_hash_password(
      password,
      salt,
      sizeof(salt),
      auth.password_iterations);
  fpv_secure_zero(salt, sizeof(salt));
  if (!hash_hex) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  bool match = strlen(hash_hex) == strlen(auth.password_hash) &&
               fpv_secure_equal(
                   (const uint8_t*)hash_hex,
                   (const uint8_t*)auth.password_hash,
                   strlen(auth.password_hash));
  fpv_free(hash_hex);
  if (!match) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_INVALID_STATE;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char now_buf[32];
  snprintf(now_buf, sizeof(now_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {now_buf, auth.user->id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_users SET last_login_ms = $1 WHERE id = $2;",
      params,
      2);
  if (result != FPV_OK) {
    fpv_identity_user_auth_clear(&auth);
    return result;
  }
  auth.user->last_login_ms = now_ms;
  if (out_user) {
    *out_user = fpv_user_clone(auth.user);
    if (!*out_user) {
      fpv_identity_user_auth_clear(&auth);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_identity_user_auth_clear(&auth);
  return FPV_OK;
}

fpv_result_t fpv_identity_get_user(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !user_id || !out_user) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {user_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, email, display_name, email_verified, active, created_at_ms, "
      "last_login_ms FROM fpv_users WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_user_t* user = fpv_identity_user_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!user) {
    return FPV_ERR_INTERNAL;
  }
  *out_user = user;
  return FPV_OK;
}

fpv_result_t fpv_identity_resolve_user_context(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_team,
    fpv_role_t* out_role) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (out_team) {
    *out_team = NULL;
  }
  if (out_role) {
    *out_role = FPV_ROLE_UNKNOWN;
  }
  if (!store || !store->db || !user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {user_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(
      store->db,
      "SELECT organization_id, team_id, role FROM fpv_user_roles "
      "WHERE user_id = $1 ORDER BY role ASC, assigned_at_ms ASC;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_NOT_FOUND;
  }
  char** row = rows->rows[0];
  char* org_id = row[0] ? fpv_strdup(row[0]) : NULL;
  char* team_id = row[1] ? fpv_strdup(row[1]) : NULL;
  int role_value = 0;
  bool role_ok = row[2] && fpv_identity_parse_int(row[2], &role_value);
  fpv_db_result_destroy(rows);
  if (!org_id || !role_ok || !fpv_identity_role_valid((fpv_role_t)role_value)) {
    fpv_free(org_id);
    fpv_free(team_id);
    return FPV_ERR_INTERNAL;
  }

  fpv_organization_t* org = NULL;
  result = fpv_identity_get_organization(store, org_id, &org);
  if (result != FPV_OK) {
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  fpv_team_t* team = NULL;
  if (team_id) {
    result = fpv_identity_get_team(store, team_id, &team);
    if (result != FPV_OK && result != FPV_ERR_NOT_FOUND) {
      fpv_organization_destroy(org);
      fpv_free(org_id);
      fpv_free(team_id);
      return result;
    }
  }
  if (!team) {
    const char* team_params[] = {org_id};
    fpv_db_result_t* team_rows = NULL;
    result = fpv_identity_db_query_single(
        store,
        "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
        "FROM fpv_teams WHERE organization_id = $1 ORDER BY created_at_ms ASC "
        "LIMIT 1;",
        team_params,
        1,
        &team_rows);
    if (result == FPV_OK) {
      team = fpv_identity_team_from_row(team_rows->rows[0]);
      fpv_db_result_destroy(team_rows);
    } else if (result != FPV_ERR_NOT_FOUND) {
      fpv_organization_destroy(org);
      fpv_free(org_id);
      fpv_free(team_id);
      return result;
    }
  }

  if (out_organization) {
    *out_organization = org;
  } else {
    fpv_organization_destroy(org);
  }
  if (out_team) {
    *out_team = team;
  } else {
    fpv_team_destroy(team);
  }
  if (out_role) {
    *out_role = (fpv_role_t)role_value;
  }

  fpv_free(org_id);
  fpv_free(team_id);
  return FPV_OK;
}

