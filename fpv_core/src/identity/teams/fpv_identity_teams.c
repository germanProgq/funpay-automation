/* FunPay Vertex identity team operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_create_team(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* name,
    bool active,
    fpv_team_t** out_team) {
  if (out_team) {
    *out_team = NULL;
  }
  if (!store || !store->db || !organization_id || !name) {
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
  result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char created_buf[32];
  char updated_buf[32];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {
      id,
      organization_id,
      name,
      active ? "1" : "0",
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_teams (id, organization_id, name, active, "
      "created_at_ms, updated_at_ms) VALUES ($1, $2, $3, $4, $5, $6);",
      params,
      6);
  if (result != FPV_OK) {
    fpv_free(id);
    return result;
  }
  if (out_team) {
    *out_team = fpv_team_create(
        id,
        organization_id,
        name,
        active,
        now_ms,
        now_ms);
    if (!*out_team) {
      fpv_free(id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(id);
  return FPV_OK;
}

fpv_result_t fpv_identity_update_team(
    fpv_identity_store_t* store,
    const fpv_team_t* team) {
  if (!store || !store->db || !team || !team->id || !team->organization_id ||
      !team->name) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool team_exists = false;
  const char* team_params[] = {team->id, team->organization_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 LIMIT 1;",
      team_params,
      2,
      &team_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!team_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  result = fpv_identity_require_feature(
      store,
      team->organization_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  if (result != FPV_OK) {
    return result;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char updated_buf[32];
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {
      team->name,
      team->active ? "1" : "0",
      updated_buf,
      team->id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_teams SET name = $1, active = $2, updated_at_ms = $3 "
      "WHERE id = $4;",
      params,
      4);
  return result;
}

fpv_result_t fpv_identity_get_team(
    const fpv_identity_store_t* store,
    const char* team_id,
    fpv_team_t** out_team) {
  if (out_team) {
    *out_team = NULL;
  }
  if (!store || !store->db || !team_id || !out_team) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
      "FROM fpv_teams WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_team_t* team = fpv_identity_team_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!team) {
    return FPV_ERR_INTERNAL;
  }
  *out_team = team;
  return FPV_OK;
}

fpv_result_t fpv_identity_list_teams(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_team_t*** out_teams,
    size_t* out_count) {
  if (out_teams) {
    *out_teams = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_teams || !out_count) {
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
      "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
      "FROM fpv_teams WHERE organization_id = $1 ORDER BY created_at_ms ASC;",
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
  fpv_team_t** list =
      (fpv_team_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_team_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_team_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_teams = list;
  *out_count = row_count;
  return FPV_OK;
}

