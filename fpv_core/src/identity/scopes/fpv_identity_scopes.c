/* FunPay Vertex identity scope operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_list_team_category_scopes(
    const fpv_identity_store_t* store,
    const char* team_id,
    fpv_team_category_scope_t*** out_scopes,
    size_t* out_count) {
  if (out_scopes) {
    *out_scopes = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !team_id || !out_scopes || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_db_query(
      store->db,
      "SELECT category, subcategory FROM fpv_team_category_scopes "
      "WHERE team_id = $1 ORDER BY category ASC, subcategory ASC;",
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
  fpv_team_category_scope_t** list =
      (fpv_team_category_scope_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_category_scope_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_identity_team_category_scope_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_scopes = list;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_replace_team_category_scopes(
    fpv_identity_store_t* store,
    const char* team_id,
    const fpv_team_category_scope_t* const* scopes,
    size_t count) {
  if (!store || !store->db || !team_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* team_params[] = {team_id};
  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_team_category_scopes WHERE team_id = $1;",
      team_params,
      1);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    return result;
  }
  if (count == 0 || !scopes) {
    return fpv_db_commit(store->db);
  }

  char** seen_categories = (char**)calloc(count, sizeof(*seen_categories));
  char** seen_subcategories = (char**)calloc(count, sizeof(*seen_subcategories));
  if (!seen_categories || !seen_subcategories) {
    fpv_free(seen_categories);
    fpv_free(seen_subcategories);
    fpv_db_rollback(store->db);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t seen_count = 0;
  for (size_t i = 0; i < count; i++) {
    const fpv_team_category_scope_t* scope = scopes[i];
    if (!scope || !scope->category) {
      continue;
    }
    char* category = fpv_trim_copy(scope->category);
    if (!category || !category[0]) {
      fpv_free(category);
      continue;
    }
    char* subcategory = NULL;
    if (scope->subcategory && scope->subcategory[0]) {
      subcategory = fpv_trim_copy(scope->subcategory);
      if (!subcategory) {
        fpv_free(category);
        continue;
      }
    }
    if (!subcategory) {
      subcategory = fpv_strdup("");
      if (!subcategory) {
        fpv_free(category);
        continue;
      }
    }
    bool duplicate = false;
    for (size_t j = 0; j < seen_count; j++) {
      if (strcmp(seen_categories[j], category) == 0 &&
          strcmp(seen_subcategories[j], subcategory) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      fpv_free(category);
      fpv_free(subcategory);
      continue;
    }
    const char* insert_params[] = {team_id, category, subcategory};
    result = fpv_db_exec_params(
        store->db,
        "INSERT INTO fpv_team_category_scopes (team_id, category, subcategory) "
        "VALUES ($1, $2, $3);",
        insert_params,
        3);
    if (result != FPV_OK) {
      fpv_free(category);
      fpv_free(subcategory);
      for (size_t j = 0; j < seen_count; j++) {
        fpv_free(seen_categories[j]);
        fpv_free(seen_subcategories[j]);
      }
      fpv_free(seen_categories);
      fpv_free(seen_subcategories);
      fpv_db_rollback(store->db);
      return result;
    }
    seen_categories[seen_count] = category;
    seen_subcategories[seen_count] = subcategory;
    seen_count++;
  }

  for (size_t i = 0; i < seen_count; i++) {
    fpv_free(seen_categories[i]);
    fpv_free(seen_subcategories[i]);
  }
  fpv_free(seen_categories);
  fpv_free(seen_subcategories);
  return fpv_db_commit(store->db);
}

static bool fpv_identity_price_scope_valid(fpv_price_rule_scope_t scope) {
  return scope >= FPV_PRICE_SCOPE_ORGANIZATION && scope <= FPV_PRICE_SCOPE_LISTING;
}

fpv_result_t fpv_identity_list_team_price_scopes(
    const fpv_identity_store_t* store,
    const char* team_id,
    fpv_price_rule_scope_t** out_scopes,
    size_t* out_count) {
  if (out_scopes) {
    *out_scopes = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !team_id || !out_scopes || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_db_query(
      store->db,
      "SELECT scope FROM fpv_team_price_scopes WHERE team_id = $1 "
      "ORDER BY scope ASC;",
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
  fpv_price_rule_scope_t* scopes =
      (fpv_price_rule_scope_t*)calloc(rows->row_count, sizeof(*scopes));
  if (!scopes) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    int value = 0;
    if (!rows->rows[i] || !rows->rows[i][0] ||
        !fpv_identity_parse_int(rows->rows[i][0], &value) ||
        !fpv_identity_price_scope_valid((fpv_price_rule_scope_t)value)) {
      fpv_free(scopes);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
    scopes[i] = (fpv_price_rule_scope_t)value;
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_scopes = scopes;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_replace_team_price_scopes(
    fpv_identity_store_t* store,
    const char* team_id,
    const fpv_price_rule_scope_t* scopes,
    size_t count) {
  if (!store || !store->db || !team_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* team_params[] = {team_id};
  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_team_price_scopes WHERE team_id = $1;",
      team_params,
      1);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    return result;
  }
  if (count == 0 || !scopes) {
    return fpv_db_commit(store->db);
  }
  bool seen[5] = {false};
  for (size_t i = 0; i < count; i++) {
    fpv_price_rule_scope_t scope = scopes[i];
    if (!fpv_identity_price_scope_valid(scope)) {
      fpv_db_rollback(store->db);
      return FPV_ERR_INVALID_ARGUMENT;
    }
    if (seen[scope]) {
      continue;
    }
    seen[scope] = true;
    char scope_buf[8];
    snprintf(scope_buf, sizeof(scope_buf), "%d", scope);
    const char* insert_params[] = {team_id, scope_buf};
    result = fpv_db_exec_params(
        store->db,
        "INSERT INTO fpv_team_price_scopes (team_id, scope) VALUES ($1, $2);",
        insert_params,
        2);
    if (result != FPV_OK) {
      fpv_db_rollback(store->db);
      return result;
    }
  }
  return fpv_db_commit(store->db);
}

fpv_result_t fpv_identity_list_team_alert_scopes(
    const fpv_identity_store_t* store,
    const char* team_id,
    char*** out_alerts,
    size_t* out_count) {
  if (out_alerts) {
    *out_alerts = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !team_id || !out_alerts || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  result = fpv_db_query(
      store->db,
      "SELECT alert_type FROM fpv_team_alert_scopes WHERE team_id = $1 "
      "ORDER BY alert_type ASC;",
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
  char** alerts = (char**)calloc(rows->row_count, sizeof(*alerts));
  if (!alerts) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    if (!rows->rows[i] || !rows->rows[i][0]) {
      fpv_identity_string_list_destroy(alerts, i);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
    alerts[i] = fpv_strdup(rows->rows[i][0]);
    if (!alerts[i]) {
      fpv_identity_string_list_destroy(alerts, i);
      fpv_db_result_destroy(rows);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_alerts = alerts;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_replace_team_alert_scopes(
    fpv_identity_store_t* store,
    const char* team_id,
    const char* const* alerts,
    size_t count) {
  if (!store || !store->db || !team_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* org_id = NULL;
  fpv_result_t result =
      fpv_identity_get_team_org_id(store, team_id, &org_id);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_identity_require_feature(
      store,
      org_id,
      FPV_FEATURE_MANAGER_SYSTEM);
  fpv_free(org_id);
  if (result != FPV_OK) {
    return result;
  }
  const char* team_params[] = {team_id};
  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_team_alert_scopes WHERE team_id = $1;",
      team_params,
      1);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    return result;
  }
  if (count == 0 || !alerts) {
    return fpv_db_commit(store->db);
  }
  char** seen_alerts = (char**)calloc(count, sizeof(*seen_alerts));
  if (!seen_alerts) {
    fpv_db_rollback(store->db);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  size_t seen_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (!alerts[i]) {
      continue;
    }
    char* alert = fpv_trim_copy(alerts[i]);
    if (!alert || !alert[0]) {
      fpv_free(alert);
      continue;
    }
    bool duplicate = false;
    for (size_t j = 0; j < seen_count; j++) {
      if (strcmp(seen_alerts[j], alert) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      fpv_free(alert);
      continue;
    }
    const char* insert_params[] = {team_id, alert};
    result = fpv_db_exec_params(
        store->db,
        "INSERT INTO fpv_team_alert_scopes (team_id, alert_type) "
        "VALUES ($1, $2);",
        insert_params,
        2);
    if (result != FPV_OK) {
      fpv_free(alert);
      for (size_t j = 0; j < seen_count; j++) {
        fpv_free(seen_alerts[j]);
      }
      fpv_free(seen_alerts);
      fpv_db_rollback(store->db);
      return result;
    }
    seen_alerts[seen_count] = alert;
    seen_count++;
  }
  for (size_t i = 0; i < seen_count; i++) {
    fpv_free(seen_alerts[i]);
  }
  fpv_free(seen_alerts);
  return fpv_db_commit(store->db);
}

