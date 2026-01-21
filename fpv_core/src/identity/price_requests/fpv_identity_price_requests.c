/* FunPay Vertex identity price change operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_create_price_change_request(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* listing_id,
    const char* price_rule_id,
    const char* requested_by_user_id,
    double current_price,
    double requested_price,
    const char* currency,
    const char* reason,
    fpv_price_change_request_t** out_request) {
  if (out_request) {
    *out_request = NULL;
  }
  if (!store || !store->db || !organization_id || !listing_id ||
      !currency || !currency[0]) {
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
      FPV_FEATURE_MASS_PRICE_EDITOR);
  if (result != FPV_OK) {
    return result;
  }
  bool listing_exists = false;
  const char* listing_params[] = {listing_id, organization_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_listings l JOIN fpv_items i ON l.item_id = i.id "
      "WHERE l.id = $1 AND i.organization_id = $2 LIMIT 1;",
      listing_params,
      2,
      &listing_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!listing_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  if (team_id) {
    bool team_ok = false;
    const char* team_params[] = {team_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 LIMIT 1;",
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
  if (price_rule_id) {
    bool rule_ok = false;
    const char* rule_params[] = {price_rule_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_price_rules WHERE id = $1 AND organization_id = $2 "
        "LIMIT 1;",
        rule_params,
        2,
        &rule_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!rule_ok) {
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  if (requested_by_user_id) {
    bool user_ok = false;
    const char* user_params[] = {requested_by_user_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
        user_params,
        1,
        &user_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!user_ok) {
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char current_buf[64];
  char requested_buf[64];
  char status_buf[8];
  char requested_at_buf[32];
  char reviewed_at_buf[32];
  char applied_at_buf[32];
  snprintf(current_buf, sizeof(current_buf), "%.6f", current_price);
  snprintf(requested_buf, sizeof(requested_buf), "%.6f", requested_price);
  snprintf(status_buf, sizeof(status_buf), "%d", FPV_PRICE_CHANGE_PENDING);
  snprintf(requested_at_buf, sizeof(requested_at_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(reviewed_at_buf, sizeof(reviewed_at_buf), "%llu",
           (unsigned long long)0ULL);
  snprintf(applied_at_buf, sizeof(applied_at_buf), "%llu",
           (unsigned long long)0ULL);
  const char* params[] = {
      id,
      organization_id,
      team_id,
      listing_id,
      price_rule_id,
      requested_by_user_id,
      current_buf,
      requested_buf,
      currency,
      reason,
      status_buf,
      NULL,
      NULL,
      requested_at_buf,
      reviewed_at_buf,
      applied_at_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_price_change_requests (id, organization_id, team_id, "
      "listing_id, price_rule_id, requested_by_user_id, current_price, "
      "requested_price, currency, reason, status, reviewed_by_user_id, "
      "review_note, requested_at_ms, reviewed_at_ms, applied_at_ms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, "
      "$14, $15, $16);",
      params,
      16);
  if (result != FPV_OK) {
    fpv_free(id);
    return result;
  }
  if (out_request) {
    *out_request = fpv_price_change_request_create(
        id,
        organization_id,
        team_id,
        listing_id,
        price_rule_id,
        requested_by_user_id,
        current_price,
        requested_price,
        currency,
        reason,
        FPV_PRICE_CHANGE_PENDING,
        NULL,
        NULL,
        now_ms,
        0,
        0);
    if (!*out_request) {
      fpv_free(id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(id);
  return FPV_OK;
}

fpv_result_t fpv_identity_list_price_change_requests(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_price_change_status_t status_filter,
    fpv_price_change_request_t*** out_requests,
    size_t* out_count) {
  if (out_requests) {
    *out_requests = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_requests || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_identity_require_feature(
      store,
      organization_id,
      FPV_FEATURE_MASS_PRICE_EDITOR);
  if (result != FPV_OK) {
    return result;
  }
  fpv_db_result_t* rows = NULL;
  if (status_filter == FPV_PRICE_CHANGE_ANY) {
    const char* params[] = {organization_id};
    result = fpv_db_query(
        store->db,
        "SELECT id, organization_id, team_id, listing_id, price_rule_id, "
        "requested_by_user_id, current_price, requested_price, currency, "
        "reason, status, reviewed_by_user_id, review_note, requested_at_ms, "
        "reviewed_at_ms, applied_at_ms "
        "FROM fpv_price_change_requests WHERE organization_id = $1 "
        "ORDER BY requested_at_ms DESC;",
        params,
        1,
        &rows);
  } else {
    char status_buf[8];
    snprintf(status_buf, sizeof(status_buf), "%d", status_filter);
    const char* params[] = {organization_id, status_buf};
    result = fpv_db_query(
        store->db,
        "SELECT id, organization_id, team_id, listing_id, price_rule_id, "
        "requested_by_user_id, current_price, requested_price, currency, "
        "reason, status, reviewed_by_user_id, review_note, requested_at_ms, "
        "reviewed_at_ms, applied_at_ms "
        "FROM fpv_price_change_requests WHERE organization_id = $1 AND status = $2 "
        "ORDER BY requested_at_ms DESC;",
        params,
        2,
        &rows);
  }
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }
  fpv_price_change_request_t** list =
      (fpv_price_change_request_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_price_change_request_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_price_change_request_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_requests = list;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_review_price_change_request(
    fpv_identity_store_t* store,
    const char* request_id,
    fpv_price_change_status_t decision,
    const char* reviewer_user_id,
    const char* review_note,
    fpv_price_change_request_t** out_request) {
  if (out_request) {
    *out_request = NULL;
  }
  if (!store || !store->db || !request_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (decision != FPV_PRICE_CHANGE_APPROVED &&
      decision != FPV_PRICE_CHANGE_REJECTED) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_price_change_request_t* current = NULL;
  const char* params[] = {request_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, listing_id, price_rule_id, "
      "requested_by_user_id, current_price, requested_price, currency, "
      "reason, status, reviewed_by_user_id, review_note, requested_at_ms, "
      "reviewed_at_ms, applied_at_ms "
      "FROM fpv_price_change_requests WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  current = fpv_identity_price_change_request_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!current) {
    return FPV_ERR_INTERNAL;
  }
  result = fpv_identity_require_feature(
      store,
      current->organization_id,
      FPV_FEATURE_MASS_PRICE_EDITOR);
  if (result != FPV_OK) {
    fpv_price_change_request_destroy(current);
    return result;
  }
  if (current->status != FPV_PRICE_CHANGE_PENDING) {
    fpv_price_change_request_destroy(current);
    return FPV_ERR_INVALID_STATE;
  }
  if (reviewer_user_id) {
    bool user_ok = false;
    const char* user_params[] = {reviewer_user_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
        user_params,
        1,
        &user_ok);
    if (result != FPV_OK) {
      fpv_price_change_request_destroy(current);
      return result;
    }
    if (!user_ok) {
      fpv_price_change_request_destroy(current);
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  uint64_t now_ms = fpv_time_now_ms();
  char status_buf[8];
  char reviewed_at_buf[32];
  snprintf(status_buf, sizeof(status_buf), "%d", decision);
  snprintf(reviewed_at_buf, sizeof(reviewed_at_buf), "%llu",
           (unsigned long long)now_ms);
  const char* update_params[] = {
      status_buf,
      reviewer_user_id,
      review_note,
      reviewed_at_buf,
      request_id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_price_change_requests SET status = $1, reviewed_by_user_id = $2, "
      "review_note = $3, reviewed_at_ms = $4 WHERE id = $5;",
      update_params,
      5);
  if (result != FPV_OK) {
    fpv_price_change_request_destroy(current);
    return result;
  }
  current->status = decision;
  current->reviewed_at_ms = now_ms;
  fpv_free(current->reviewed_by_user_id);
  current->reviewed_by_user_id = reviewer_user_id ? fpv_strdup(reviewer_user_id) : NULL;
  fpv_free(current->review_note);
  current->review_note = review_note ? fpv_strdup(review_note) : NULL;
  if ((reviewer_user_id && !current->reviewed_by_user_id) ||
      (review_note && !current->review_note)) {
    fpv_price_change_request_destroy(current);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (out_request) {
    *out_request = current;
  } else {
    fpv_price_change_request_destroy(current);
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_mark_price_change_applied(
    fpv_identity_store_t* store,
    const char* request_id,
    fpv_price_change_request_t** out_request) {
  if (out_request) {
    *out_request = NULL;
  }
  if (!store || !store->db || !request_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_price_change_request_t* current = NULL;
  const char* params[] = {request_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, listing_id, price_rule_id, "
      "requested_by_user_id, current_price, requested_price, currency, "
      "reason, status, reviewed_by_user_id, review_note, requested_at_ms, "
      "reviewed_at_ms, applied_at_ms "
      "FROM fpv_price_change_requests WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  current = fpv_identity_price_change_request_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!current) {
    return FPV_ERR_INTERNAL;
  }
  result = fpv_identity_require_feature(
      store,
      current->organization_id,
      FPV_FEATURE_MASS_PRICE_EDITOR);
  if (result != FPV_OK) {
    fpv_price_change_request_destroy(current);
    return result;
  }
  if (current->status != FPV_PRICE_CHANGE_APPROVED) {
    fpv_price_change_request_destroy(current);
    return FPV_ERR_INVALID_STATE;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char status_buf[8];
  char applied_at_buf[32];
  snprintf(status_buf, sizeof(status_buf), "%d", FPV_PRICE_CHANGE_APPLIED);
  snprintf(applied_at_buf, sizeof(applied_at_buf), "%llu",
           (unsigned long long)now_ms);
  const char* update_params[] = {status_buf, applied_at_buf, request_id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_price_change_requests SET status = $1, applied_at_ms = $2 "
      "WHERE id = $3;",
      update_params,
      3);
  if (result != FPV_OK) {
    fpv_price_change_request_destroy(current);
    return result;
  }
  current->status = FPV_PRICE_CHANGE_APPLIED;
  current->applied_at_ms = now_ms;
  if (out_request) {
    *out_request = current;
  } else {
    fpv_price_change_request_destroy(current);
  }
  return FPV_OK;
}

