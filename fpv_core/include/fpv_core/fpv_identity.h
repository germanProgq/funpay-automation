/* FunPay Vertex identity and access management API. */

#ifndef FPV_IDENTITY_H
#define FPV_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_identity_store fpv_identity_store_t;

typedef struct fpv_identity_invite {
  char* id;
  char* organization_id;
  char* team_id;
  char* email;
  fpv_role_t role;
  uint64_t created_at_ms;
  uint64_t expires_at_ms;
  uint64_t accepted_at_ms;
  char* accepted_user_id;
  char* created_by_user_id;
} fpv_identity_invite_t;

FPV_CORE_API fpv_identity_store_t* fpv_identity_store_open(
    const char* data_dir,
    fpv_result_t* out_result);
FPV_CORE_API void fpv_identity_store_destroy(fpv_identity_store_t* store);
FPV_CORE_API bool fpv_identity_store_has_users(const fpv_identity_store_t* store);

FPV_CORE_API fpv_result_t fpv_identity_create_user(
    fpv_identity_store_t* store,
    const char* email,
    const char* display_name,
    const char* password,
    bool email_verified,
    fpv_user_t** out_user);
FPV_CORE_API fpv_result_t fpv_identity_authenticate(
    fpv_identity_store_t* store,
    const char* email,
    const char* password,
    fpv_user_t** out_user);

FPV_CORE_API fpv_result_t fpv_identity_create_organization(
    fpv_identity_store_t* store,
    const char* name,
    const char* timezone,
    const char* currency,
    const fpv_data_retention_policy_t* retention,
    const char* owner_user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_default_team,
    fpv_user_role_t** out_owner_role);
FPV_CORE_API fpv_result_t fpv_identity_create_team(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* name,
    bool active,
    fpv_team_t** out_team);
FPV_CORE_API fpv_result_t fpv_identity_assign_role(
    fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role,
    fpv_user_role_t** out_role);

FPV_CORE_API fpv_result_t fpv_identity_create_invite(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* email,
    fpv_role_t role,
    const char* created_by_user_id,
    uint64_t expires_at_ms,
    fpv_identity_invite_t** out_invite,
    char** out_token);
FPV_CORE_API fpv_result_t fpv_identity_validate_invite(
    fpv_identity_store_t* store,
    const char* token,
    fpv_identity_invite_t** out_invite);
FPV_CORE_API fpv_result_t fpv_identity_accept_invite(
    fpv_identity_store_t* store,
    const char* token,
    const char* user_id,
    fpv_identity_invite_t** out_invite,
    fpv_user_role_t** out_role);

FPV_CORE_API fpv_result_t fpv_identity_link_account(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* funpay_user_id,
    const char* funpay_username,
    const char* display_name,
    const char* currency,
    fpv_account_t** out_account);

FPV_CORE_API fpv_result_t fpv_identity_get_user(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_user_t** out_user);
FPV_CORE_API fpv_result_t fpv_identity_get_organization(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_organization_t** out_organization);
FPV_CORE_API fpv_result_t fpv_identity_get_team(
    const fpv_identity_store_t* store,
    const char* team_id,
    fpv_team_t** out_team);
FPV_CORE_API fpv_result_t fpv_identity_get_account_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_account_t** out_account);
FPV_CORE_API fpv_result_t fpv_identity_list_teams(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_team_t*** out_teams,
    size_t* out_count);

FPV_CORE_API fpv_result_t fpv_identity_resolve_user_context(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_team,
    fpv_role_t* out_role);
FPV_CORE_API bool fpv_identity_user_has_role(
    const fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t minimum_role);

FPV_CORE_API void fpv_identity_invite_destroy(fpv_identity_invite_t* invite);
FPV_CORE_API void fpv_identity_invite_list_destroy(
    fpv_identity_invite_t** invites,
    size_t count);

#ifdef __cplusplus
}
#endif

#endif
