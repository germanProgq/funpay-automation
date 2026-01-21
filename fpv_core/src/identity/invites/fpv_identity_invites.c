/* FunPay Vertex identity invite operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_create_invite(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* email,
    fpv_role_t role,
    const char* created_by_user_id,
    uint64_t expires_at_ms,
    fpv_identity_invite_t** out_invite,
    char** out_token) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (out_token) {
    *out_token = NULL;
  }
  if (!store || !store->db || !organization_id || !fpv_identity_role_valid(role)) {
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
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  char* normalized_email = NULL;
  if (email && email[0]) {
    normalized_email = fpv_identity_normalize_email(email);
    if (!normalized_email || !fpv_identity_validate_email(normalized_email)) {
      fpv_free(normalized_email);
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    fpv_free(normalized_email);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* token = fpv_identity_random_id(24);
  if (!token) {
    fpv_free(id);
    fpv_free(normalized_email);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    fpv_free(id);
    fpv_free(normalized_email);
    fpv_free(token);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  uint64_t expiry = expires_at_ms;
  if (expiry == 0) {
    expiry = now_ms + 7ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
  }
  char role_buf[8];
  char created_buf[32];
  char expires_buf[32];
  char accepted_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", role);
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(expires_buf, sizeof(expires_buf), "%llu",
           (unsigned long long)expiry);
  snprintf(accepted_buf, sizeof(accepted_buf), "%llu",
           (unsigned long long)0ULL);
  const char* params[] = {
      id,
      organization_id,
      team_id,
      normalized_email,
      role_buf,
      created_buf,
      expires_buf,
      accepted_buf,
      NULL,
      created_by_user_id,
      token_hash};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_invites (id, organization_id, team_id, email, role, "
      "created_at_ms, expires_at_ms, accepted_at_ms, accepted_user_id, "
      "created_by_user_id, token_hash) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);",
      params,
      11);
  if (result != FPV_OK) {
    fpv_free(id);
    fpv_free(normalized_email);
    fpv_free(token);
    fpv_free(token_hash);
    return result;
  }

  if (out_invite) {
    fpv_identity_invite_t* invite =
        (fpv_identity_invite_t*)calloc(1, sizeof(*invite));
    if (!invite) {
      fpv_free(id);
      fpv_free(normalized_email);
      fpv_free(token);
      fpv_free(token_hash);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    invite->id = fpv_strdup(id);
    invite->organization_id = fpv_strdup(organization_id);
    invite->team_id = fpv_strdup(team_id);
    invite->email = normalized_email ? fpv_strdup(normalized_email) : NULL;
    invite->role = role;
    invite->created_at_ms = now_ms;
    invite->expires_at_ms = expiry;
    invite->accepted_at_ms = 0;
    invite->accepted_user_id = NULL;
    invite->created_by_user_id = fpv_strdup(created_by_user_id);
    if (!invite->id || !invite->organization_id ||
        (team_id && !invite->team_id) ||
        (normalized_email && !invite->email) ||
        (created_by_user_id && !invite->created_by_user_id)) {
      fpv_identity_invite_destroy(invite);
      fpv_free(id);
      fpv_free(normalized_email);
      fpv_free(token);
      fpv_free(token_hash);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    *out_invite = invite;
  }

  if (out_token) {
    *out_token = token;
  } else {
    fpv_free(token);
  }
  fpv_free(id);
  fpv_free(normalized_email);
  fpv_free(token_hash);
  return FPV_OK;
}

fpv_result_t fpv_identity_validate_invite(
    fpv_identity_store_t* store,
    const char* token,
    fpv_identity_invite_t** out_invite) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (!store || !store->db || !token) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_invite_t* invite = NULL;
  fpv_result_t result = fpv_identity_fetch_invite_by_hash(
      store,
      token_hash,
      &invite);
  fpv_free(token_hash);
  if (result != FPV_OK) {
    return result;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (invite->accepted_at_ms != 0 ||
      (invite->expires_at_ms != 0 && invite->expires_at_ms < now_ms)) {
    fpv_identity_invite_destroy(invite);
    return FPV_ERR_INVALID_STATE;
  }
  if (out_invite) {
    *out_invite = invite;
  } else {
    fpv_identity_invite_destroy(invite);
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_accept_invite(
    fpv_identity_store_t* store,
    const char* token,
    const char* user_id,
    fpv_identity_invite_t** out_invite,
    fpv_user_role_t** out_role) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (out_role) {
    *out_role = NULL;
  }
  if (!store || !store->db || !token || !user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_user_t* user = NULL;
  fpv_result_t result = fpv_identity_get_user(store, user_id, &user);
  if (result != FPV_OK || !user) {
    fpv_user_destroy(user);
    return result != FPV_OK ? result : FPV_ERR_NOT_FOUND;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    fpv_user_destroy(user);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_invite_t* invite = NULL;
  result = fpv_identity_fetch_invite_by_hash(store, token_hash, &invite);
  fpv_free(token_hash);
  if (result != FPV_OK || !invite) {
    fpv_user_destroy(user);
    return result != FPV_OK ? result : FPV_ERR_NOT_FOUND;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (invite->accepted_at_ms != 0 ||
      (invite->expires_at_ms != 0 && invite->expires_at_ms < now_ms)) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_STATE;
  }
  if (invite->email && user->email &&
      strcmp(invite->email, user->email) != 0) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  result = fpv_identity_require_feature(
      store,
      invite->organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  bool exists = false;
  const char* role_params[] = {user_id, invite->organization_id, invite->team_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      role_params,
      3,
      &exists);
  if (result != FPV_OK) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  if (exists) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_STATE;
  }

  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", invite->role);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* insert_params[] = {
      user_id,
      invite->organization_id,
      invite->team_id,
      role_buf,
      assigned_buf};
  const char* update_params[] = {assigned_buf, user_id, invite->id};

  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      insert_params,
      5);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_invites SET accepted_at_ms = $1, accepted_user_id = $2 "
      "WHERE id = $3;",
      update_params,
      3);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_commit(store->db);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }

  invite->accepted_at_ms = now_ms;
  fpv_free(invite->accepted_user_id);
  invite->accepted_user_id = fpv_strdup(user_id);
  if (!invite->accepted_user_id) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (out_role) {
    *out_role = fpv_user_role_create(
        user_id,
        invite->organization_id,
        invite->team_id,
        invite->role,
        now_ms);
    if (!*out_role) {
      fpv_identity_invite_destroy(invite);
      fpv_user_destroy(user);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_invite) {
    *out_invite = invite;
  } else {
    fpv_identity_invite_destroy(invite);
  }
  fpv_user_destroy(user);
  return FPV_OK;
}

fpv_result_t fpv_identity_list_invites(
    const fpv_identity_store_t* store,
    const char* organization_id,
    bool include_accepted,
    fpv_identity_invite_t*** out_invites,
    size_t* out_count) {
  if (out_invites) {
    *out_invites = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_invites || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_db_query(
      store->db,
      include_accepted
          ? "SELECT id, organization_id, team_id, email, role, created_at_ms, "
            "expires_at_ms, accepted_at_ms, accepted_user_id, created_by_user_id "
            "FROM fpv_invites WHERE organization_id = $1 "
            "ORDER BY created_at_ms DESC;"
          : "SELECT id, organization_id, team_id, email, role, created_at_ms, "
            "expires_at_ms, accepted_at_ms, accepted_user_id, created_by_user_id "
            "FROM fpv_invites WHERE organization_id = $1 AND accepted_at_ms = 0 "
            "ORDER BY created_at_ms DESC;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }
  fpv_identity_invite_t** list =
      (fpv_identity_invite_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_invite_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_identity_invite_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_invites = list;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_revoke_invite(
    fpv_identity_store_t* store,
    const char* invite_id) {
  if (!store || !store->db || !invite_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {invite_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT accepted_at_ms FROM fpv_invites WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  uint64_t accepted_at = 0;
  bool accepted_ok = rows && rows->rows && rows->rows[0] &&
      fpv_identity_parse_uint64(rows->rows[0][0], &accepted_at);
  fpv_db_result_destroy(rows);
  if (!accepted_ok) {
    return FPV_ERR_INTERNAL;
  }
  if (accepted_at != 0) {
    return FPV_ERR_INVALID_STATE;
  }
  result = fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_invites WHERE id = $1;",
      params,
      1);
  return result;
}

