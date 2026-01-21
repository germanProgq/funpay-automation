/* FunPay Vertex identity access review operations. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_result_t fpv_identity_get_latest_access_review(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_access_review_t** out_review) {
  if (out_review) {
    *out_review = NULL;
  }
  if (!store || !store->db || !organization_id || !out_review) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, reviewer_user_id, note, reviewed_at_ms "
      "FROM fpv_access_reviews WHERE organization_id = $1 "
      "ORDER BY reviewed_at_ms DESC LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_access_review_t* review =
      fpv_identity_access_review_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!review) {
    return FPV_ERR_INTERNAL;
  }
  *out_review = review;
  return FPV_OK;
}

fpv_result_t fpv_identity_record_access_review(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* reviewer_user_id,
    const char* note,
    fpv_access_review_t** out_review) {
  if (out_review) {
    *out_review = NULL;
  }
  if (!store || !store->db || !organization_id) {
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
  char* id = fpv_identity_random_id(16);
  if (!id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char reviewed_buf[32];
  snprintf(reviewed_buf, sizeof(reviewed_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {id, organization_id, reviewer_user_id, note, reviewed_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_access_reviews (id, organization_id, reviewer_user_id, "
      "note, reviewed_at_ms) VALUES ($1, $2, $3, $4, $5);",
      params,
      5);
  if (result != FPV_OK) {
    fpv_free(id);
    return result;
  }
  if (out_review) {
    *out_review = fpv_access_review_create(
        id,
        organization_id,
        reviewer_user_id,
        note,
        now_ms);
    if (!*out_review) {
      fpv_free(id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(id);
  return FPV_OK;
}

