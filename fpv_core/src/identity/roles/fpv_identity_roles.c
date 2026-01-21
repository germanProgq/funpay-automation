/* FunPay Vertex identity role and access operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_assign_role(
    fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role,
    fpv_user_role_t** out_role) {
  if (out_role) {
    *out_role = NULL;
  }
  if (!store || !store->db || !user_id || !organization_id ||
      !fpv_identity_role_valid(role)) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool user_exists = false;
  const char* user_params[] = {user_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
      user_params,
      1,
      &user_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!user_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
  result = fpv_identity_db_exists(
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
  result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
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
  bool exists = false;
  const char* role_params[] = {user_id, organization_id, team_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      role_params,
      3,
      &exists);
  if (result != FPV_OK) {
    return result;
  }
  if (exists) {
    return FPV_ERR_INVALID_STATE;
  }

  uint64_t now_ms = fpv_time_now_ms();
  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", role);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {
      user_id,
      organization_id,
      team_id,
      role_buf,
      assigned_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      params,
      5);
  if (result != FPV_OK) {
    return result;
  }
  if (out_role) {
    *out_role = fpv_user_role_create(
        user_id,
        organization_id,
        team_id,
        role,
        now_ms);
    if (!*out_role) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_update_role(
    fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role) {
  if (!store || !store->db || !user_id || !organization_id ||
      !fpv_identity_role_valid(role)) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {user_id, organization_id, team_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_identity_db_query_single(
      store,
      "SELECT role FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      params,
      3,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  char** row = rows->rows[0];
  int current_role = 0;
  bool role_ok = row && row[0] && fpv_identity_parse_int(row[0], &current_role);
  fpv_db_result_destroy(rows);
  if (!role_ok || !fpv_identity_role_valid((fpv_role_t)current_role)) {
    return FPV_ERR_INTERNAL;
  }
  if ((fpv_role_t)current_role == FPV_ROLE_OWNER && role != FPV_ROLE_OWNER) {
    bool has_owner = false;
    result = fpv_identity_has_other_owner(
        store,
        organization_id,
        user_id,
        team_id,
        &has_owner);
    if (result != FPV_OK) {
      return result;
    }
    if (!has_owner) {
      return FPV_ERR_INVALID_STATE;
    }
  }

  uint64_t now_ms = fpv_time_now_ms();
  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", role);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* update_params[] = {role_buf, assigned_buf, user_id, organization_id, team_id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_user_roles SET role = $1, assigned_at_ms = $2 "
      "WHERE user_id = $3 AND organization_id = $4 "
      "AND ((team_id IS NULL AND $5 IS NULL) OR team_id = $5);",
      update_params,
      5);
  return result;
}

fpv_result_t fpv_identity_remove_role(
    fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id) {
  if (!store || !store->db || !user_id || !organization_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {user_id, organization_id, team_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_identity_db_query_single(
      store,
      "SELECT role FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      params,
      3,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  char** row = rows->rows[0];
  int current_role = 0;
  bool role_ok = row && row[0] && fpv_identity_parse_int(row[0], &current_role);
  fpv_db_result_destroy(rows);
  if (!role_ok || !fpv_identity_role_valid((fpv_role_t)current_role)) {
    return FPV_ERR_INTERNAL;
  }
  if ((fpv_role_t)current_role == FPV_ROLE_OWNER) {
    bool has_owner = false;
    result = fpv_identity_has_other_owner(
        store,
        organization_id,
        user_id,
        team_id,
        &has_owner);
    if (result != FPV_OK) {
      return result;
    }
    if (!has_owner) {
      return FPV_ERR_INVALID_STATE;
    }
  }

  result = fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3);",
      params,
      3);
  return result;
}

fpv_result_t fpv_identity_list_access_members(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_access_member_t*** out_members,
    size_t* out_count) {
  if (out_members) {
    *out_members = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_members || !out_count) {
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
      "SELECT ur.user_id, u.email, u.display_name, u.active, ur.team_id, "
      "t.name, ur.role, ur.assigned_at_ms "
      "FROM fpv_user_roles ur "
      "JOIN fpv_users u ON ur.user_id = u.id "
      "LEFT JOIN fpv_teams t ON ur.team_id = t.id "
      "WHERE ur.organization_id = $1 "
      "ORDER BY ur.role ASC, u.email_normalized ASC;",
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
  fpv_access_member_t** list =
      (fpv_access_member_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_access_member_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_identity_access_member_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_members = list;
  *out_count = row_count;
  return FPV_OK;
}

bool fpv_identity_user_has_role(
    const fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t minimum_role) {
  if (!store || !store->db || !user_id || !organization_id) {
    return false;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = FPV_OK;
  if (team_id) {
    const char* params[] = {user_id, organization_id, team_id};
    result = fpv_db_query(
        store->db,
        "SELECT role, team_id FROM fpv_user_roles WHERE user_id = $1 "
        "AND organization_id = $2 AND (team_id = $3 OR team_id IS NULL);",
        params,
        3,
        &rows);
  } else {
    const char* params[] = {user_id, organization_id};
    result = fpv_db_query(
        store->db,
        "SELECT role, team_id FROM fpv_user_roles WHERE user_id = $1 "
        "AND organization_id = $2 AND team_id IS NULL;",
        params,
        2,
        &rows);
  }
  if (result != FPV_OK) {
    fpv_db_result_destroy(rows);
    return false;
  }
  fpv_role_t best = FPV_ROLE_UNKNOWN;
  for (size_t i = 0; rows && i < rows->row_count; i++) {
    char** row = rows->rows[i];
    int role_value = 0;
    if (!row || !row[0] || !fpv_identity_parse_int(row[0], &role_value)) {
      continue;
    }
    fpv_role_t current = (fpv_role_t)role_value;
    if (!fpv_identity_role_valid(current)) {
      continue;
    }
    if (best == FPV_ROLE_UNKNOWN || current < best) {
      best = current;
    }
  }
  fpv_db_result_destroy(rows);
  return fpv_identity_role_at_least(best, minimum_role);
}


