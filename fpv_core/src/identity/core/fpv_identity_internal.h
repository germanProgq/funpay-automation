/* FunPay Vertex identity internal helpers. */

#ifndef FPV_IDENTITY_INTERNAL_H
#define FPV_IDENTITY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_core/fpv_identity.h"
#include "core/data/fpv_db.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


struct fpv_identity_store {
  fpv_db_t* db;
};

typedef struct fpv_identity_user_auth {
  fpv_user_t* user;
  char* password_salt;
  char* password_hash;
  uint32_t password_iterations;
} fpv_identity_user_auth_t;

void fpv_secure_zero(void* data, size_t len);
bool fpv_random_bytes(void* data, size_t len);
char* fpv_hex_encode(const uint8_t* data, size_t len);
bool fpv_hex_decode(const char* hex, uint8_t* out, size_t out_len);
bool fpv_secure_equal(const uint8_t* left, const uint8_t* right, size_t len);
char* fpv_trim_copy(const char* value);

char* fpv_identity_normalize_email(const char* email);
bool fpv_identity_validate_email(const char* email);
bool fpv_identity_validate_password(const char* password);
bool fpv_identity_role_valid(fpv_role_t role);
bool fpv_identity_role_at_least(fpv_role_t have, fpv_role_t need);
fpv_data_retention_policy_t fpv_identity_default_retention(void);
char* fpv_identity_random_id(size_t bytes);
char* fpv_identity_hash_password(
    const char* password,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations);
char* fpv_identity_hash_token(const char* token);
void fpv_identity_user_auth_clear(fpv_identity_user_auth_t* auth);
bool fpv_identity_parse_bool(const char* text, bool* out);
bool fpv_identity_parse_uint64(const char* text, uint64_t* out);
bool fpv_identity_parse_uint32(const char* text, uint32_t* out);
bool fpv_identity_parse_int(const char* text, int* out);
bool fpv_identity_parse_double(const char* text, double* out);

fpv_result_t fpv_identity_require_feature(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_feature_flag_t feature);
fpv_result_t fpv_identity_get_team_org_id(
    const fpv_identity_store_t* store,
    const char* team_id,
    char** out_org_id);
fpv_result_t fpv_identity_db_query_single(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    fpv_db_result_t** out_rows);
fpv_result_t fpv_identity_db_exists(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    bool* out_exists);
fpv_user_t* fpv_identity_user_from_row(char** row);
fpv_organization_t* fpv_identity_organization_from_row(char** row);
fpv_team_t* fpv_identity_team_from_row(char** row);
fpv_account_t* fpv_identity_account_from_row(char** row);
fpv_identity_invite_t* fpv_identity_invite_from_row(char** row);
fpv_access_member_t* fpv_identity_access_member_from_row(char** row);
fpv_team_category_scope_t* fpv_identity_category_scope_from_row(char** row);
fpv_access_review_t* fpv_identity_access_review_from_row(char** row);
fpv_price_change_request_t* fpv_identity_price_change_request_from_row(char** row);
void fpv_identity_access_member_destroy(fpv_access_member_t* member);
void fpv_identity_team_category_scope_destroy(fpv_team_category_scope_t* scope);
fpv_result_t fpv_identity_has_other_owner(
    const fpv_identity_store_t* store,
    const char* organization_id,
    const char* user_id,
    const char* team_id,
    bool* out_exists);
fpv_result_t fpv_identity_fetch_user_auth_by_email(
    const fpv_identity_store_t* store,
    const char* normalized_email,
    fpv_identity_user_auth_t* out_auth);
fpv_result_t fpv_identity_fetch_account_id_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    char** out_id);
fpv_result_t fpv_identity_fetch_invite_by_hash(
    const fpv_identity_store_t* store,
    const char* token_hash,
    fpv_identity_invite_t** out_invite);

#endif
