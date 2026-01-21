/* FunPay Vertex identity and access management implementation. */

#include "identity/core/fpv_identity_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


fpv_identity_store_t* fpv_identity_store_open(
    const char* data_dir,
    fpv_result_t* out_result) {
  if (out_result) {
    *out_result = FPV_OK;
  }

  const char* db_url = getenv("FPV_DB_URL");
  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = db_url;
  config.data_dir = data_dir;

  fpv_db_t* db = NULL;
  fpv_result_t result = fpv_db_open(&config, &db);
  if (result != FPV_OK) {
    if (out_result) {
      *out_result = result;
    }
    return NULL;
  }

  result = fpv_db_migrate(db);
  if (result != FPV_OK) {
    if (out_result) {
      *out_result = result;
    }
    fpv_db_close(db);
    return NULL;
  }

  fpv_identity_store_t* store =
      (fpv_identity_store_t*)calloc(1, sizeof(*store));
  if (!store) {
    if (out_result) {
      *out_result = FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_db_close(db);
    return NULL;
  }
  store->db = db;
  return store;
}

void fpv_identity_store_destroy(fpv_identity_store_t* store) {
  if (!store) {
    return;
  }
  fpv_db_close(store->db);
  free(store);
}

bool fpv_identity_store_has_users(const fpv_identity_store_t* store) {
  if (!store || !store->db) {
    return false;
  }
  bool exists = false;
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users LIMIT 1;",
      NULL,
      0,
      &exists);
  return result == FPV_OK && exists;
}

void fpv_identity_invite_destroy(fpv_identity_invite_t* invite) {
  if (!invite) {
    return;
  }
  fpv_free(invite->id);
  fpv_free(invite->organization_id);
  fpv_free(invite->team_id);
  fpv_free(invite->email);
  fpv_free(invite->accepted_user_id);
  fpv_free(invite->created_by_user_id);
  free(invite);
}

void fpv_identity_invite_list_destroy(
    fpv_identity_invite_t** invites,
    size_t count) {
  if (!invites) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_identity_invite_destroy(invites[i]);
  }
  fpv_free(invites);
}

void fpv_identity_access_member_list_destroy(
    fpv_access_member_t** members,
    size_t count) {
  if (!members) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_identity_access_member_destroy(members[i]);
  }
  fpv_free(members);
}

void fpv_identity_team_category_scope_list_destroy(
    fpv_team_category_scope_t** scopes,
    size_t count) {
  if (!scopes) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_identity_team_category_scope_destroy(scopes[i]);
  }
  fpv_free(scopes);
}

void fpv_identity_string_list_destroy(char** items, size_t count) {
  if (!items) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(items[i]);
  }
  fpv_free(items);
}

void fpv_identity_price_change_request_list_destroy(
    fpv_price_change_request_t** requests,
    size_t count) {
  if (!requests) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_price_change_request_destroy(requests[i]);
  }
  fpv_free(requests);
}
