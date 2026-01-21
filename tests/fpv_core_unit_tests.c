/* FunPay Vertex core unit tests. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "fpv_core/fpv_audit.h"
#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_identity.h"
#include "fpv_core/fpv_ini.h"
#include "fpv_core/fpv_types.h"
#include "core/data/fpv_db.h"

#include "core/data/fpv_json.h"

#include "core/app/fpv_localization.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


typedef struct fpv_test_case {
  const char* name;
  bool (*fn)(void);
} fpv_test_case_t;

static bool fpv_test_expect(bool condition, const char* message) {
  if (condition) {
    return true;
  }
  if (message) {
    fprintf(stderr, "%s\n", message);
  }
  return false;
}

static char* fpv_test_join_path(const char* left, const char* right) {
  if (!left || !right) {
    return NULL;
  }
#if defined(_WIN32)
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  size_t len = strlen(left) + strlen(right) + 2;
  char* path = (char*)malloc(len);
  if (!path) {
    return NULL;
  }
  snprintf(path, len, "%s%c%s", left, sep, right);
  return path;
}

static char* fpv_test_create_temp_file(const char* prefix, const char* content) {
  if (!prefix) {
    return NULL;
  }
#if defined(_WIN32)
  char temp_path[MAX_PATH];
  DWORD len = GetTempPathA(MAX_PATH, temp_path);
  if (len == 0 || len > MAX_PATH) {
    return NULL;
  }
  char temp_file[MAX_PATH];
  if (GetTempFileNameA(temp_path, prefix, 0, temp_file) == 0) {
    return NULL;
  }
  FILE* file = fopen(temp_file, "wb");
  if (!file) {
    return NULL;
  }
  const char* data = content ? content : "";
  size_t length = strlen(data);
  if (length > 0 && fwrite(data, 1, length, file) != length) {
    fclose(file);
    return NULL;
  }
  fclose(file);
  return fpv_strdup(temp_file);
#else
  char tmpl[256];
  snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
  int fd = mkstemp(tmpl);
  if (fd < 0) {
    return NULL;
  }
  FILE* file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
    return NULL;
  }
  const char* data = content ? content : "";
  size_t length = strlen(data);
  if (length > 0 && fwrite(data, 1, length, file) != length) {
    fclose(file);
    return NULL;
  }
  fclose(file);
  return fpv_strdup(tmpl);
#endif
}

static char* fpv_test_create_temp_dir(const char* prefix) {
  if (!prefix) {
    return NULL;
  }
#if defined(_WIN32)
  char temp_path[MAX_PATH];
  DWORD len = GetTempPathA(MAX_PATH, temp_path);
  if (len == 0 || len > MAX_PATH) {
    return NULL;
  }
  char temp_dir[MAX_PATH];
  if (GetTempFileNameA(temp_path, prefix, 0, temp_dir) == 0) {
    return NULL;
  }
  DeleteFileA(temp_dir);
  if (!CreateDirectoryA(temp_dir, NULL)) {
    return NULL;
  }
  return fpv_strdup(temp_dir);
#else
  char tmpl[256];
  snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
  if (!mkdtemp(tmpl)) {
    return NULL;
  }
  return fpv_strdup(tmpl);
#endif
}

static void fpv_test_remove_file(const char* path) {
  if (!path) {
    return;
  }
#if defined(_WIN32)
  DeleteFileA(path);
#else
  remove(path);
#endif
}

static void fpv_test_remove_dir(const char* path) {
  if (!path) {
    return;
  }
#if defined(_WIN32)
  RemoveDirectoryA(path);
#else
  rmdir(path);
#endif
}

static size_t fpv_test_count_lines_in_file(const char* path) {
  if (!path) {
    return 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  int ch = 0;
  int last = 0;
  while ((ch = fgetc(file)) != EOF) {
    if (ch == '\n') {
      count++;
    }
    last = ch;
  }
  fclose(file);
  if (last != 0 && last != '\n') {
    count++;
  }
  return count;
}

static bool test_ini_parse(void) {
  const char* content =
      "; comment\n"
      "[main]\n"
      "greeting = hello\n"
      "multi = first\n"
      " second\n"
      " third\n"
      "\n";
  char* path = fpv_test_create_temp_file("fpvini", content);
  if (!fpv_test_expect(path != NULL, "temp file creation failed")) {
    return false;
  }
  fpv_ini_error_t error;
  fpv_ini_t* ini = fpv_ini_load(path, &error);
  fpv_test_remove_file(path);
  fpv_free(path);
  if (!fpv_test_expect(ini != NULL, "ini load failed")) {
    return false;
  }
  const char* greeting = fpv_ini_get(ini, "main", "greeting");
  bool ok = fpv_test_expect(greeting && strcmp(greeting, "hello") == 0,
                            "ini greeting mismatch");
  const char* multi = fpv_ini_get(ini, "main", "multi");
  ok = ok && fpv_test_expect(multi && strcmp(multi, "first\nsecond\nthird") == 0,
                             "ini multiline mismatch");
  fpv_ini_destroy(ini);
  return ok;
}

static bool test_json_parse(void) {
  const char* input =
      "{\"name\":\"fpv\",\"items\":[1,2,3],\"ok\":true,\"text\":\"line\\nnext\"}";
  fpv_json_value_t* root = NULL;
  fpv_json_error_t error;
  fpv_result_t result = fpv_json_parse(input, strlen(input), &root, &error);
  if (!fpv_test_expect(result == FPV_OK && root != NULL, "json parse failed")) {
    fpv_json_destroy(root);
    return false;
  }
  bool ok = fpv_test_expect(fpv_json_is_type(root, FPV_JSON_OBJECT),
                            "json root type mismatch");
  const fpv_json_value_t* name_val = fpv_json_object_get(root, "name");
  ok = ok && fpv_test_expect(name_val && fpv_json_string(name_val) &&
                                 strcmp(fpv_json_string(name_val), "fpv") == 0,
                             "json name mismatch");
  const fpv_json_value_t* items_val = fpv_json_object_get(root, "items");
  ok = ok && fpv_test_expect(fpv_json_is_type(items_val, FPV_JSON_ARRAY),
                             "json items type mismatch");
  ok = ok && fpv_test_expect(fpv_json_array_size(items_val) == 3,
                             "json array size mismatch");
  const fpv_json_value_t* num_val = fpv_json_array_get(items_val, 1);
  int64_t num = 0;
  ok = ok && fpv_test_expect(fpv_json_number_to_int64(num_val, &num) && num == 2,
                             "json number mismatch");
  fpv_json_destroy(root);
  return ok;
}

static bool test_localizer_format(void) {
  char* dir = fpv_test_create_temp_dir("fpvloc");
  if (!fpv_test_expect(dir != NULL, "temp dir creation failed")) {
    return false;
  }
  char* file_path = fpv_test_join_path(dir, "en.py");
  if (!fpv_test_expect(file_path != NULL, "path join failed")) {
    fpv_test_remove_dir(dir);
    fpv_free(dir);
    return false;
  }
  const char* content = "greet = \"Hello {}\"\npair = \"{}:{}\"\n";
  FILE* file = fopen(file_path, "wb");
  if (!file) {
    fpv_free(file_path);
    fpv_test_remove_dir(dir);
    fpv_free(dir);
    return false;
  }
  fwrite(content, 1, strlen(content), file);
  fclose(file);

  fpv_localizer_t* localizer = fpv_localizer_create(dir, "en", "en");
  fpv_test_remove_file(file_path);
  fpv_free(file_path);
  fpv_test_remove_dir(dir);
  fpv_free(dir);

  if (!fpv_test_expect(localizer != NULL, "localizer create failed")) {
    return false;
  }
  char* result = fpv_localizer_format(localizer, "greet",
                                      (const char*[]){"world"}, 1);
  bool ok = fpv_test_expect(result && strcmp(result, "Hello world") == 0,
                            "localizer format mismatch");
  fpv_free(result);
  result = fpv_localizer_format(localizer, "pair",
                                (const char*[]){"a", "b"}, 2);
  ok = ok && fpv_test_expect(result && strcmp(result, "a:b") == 0,
                             "localizer multi format mismatch");
  fpv_free(result);
  fpv_localizer_destroy(localizer);
  return ok;
}

static bool test_event_bus(void) {
  fpv_event_bus_t* bus = fpv_event_bus_create();
  if (!fpv_test_expect(bus != NULL, "event bus create failed")) {
    return false;
  }
  fpv_event_t* event = fpv_event_create_core_status(
      FPV_CORE_RUNNING, "ok", 42);
  if (!event) {
    fpv_event_bus_destroy(bus);
    return false;
  }
  fpv_result_t result = fpv_event_bus_publish(bus, event);
  if (!fpv_test_expect(result == FPV_OK, "event publish failed")) {
    fpv_event_bus_destroy(bus);
    return false;
  }
  fpv_event_batch_t batch = fpv_event_bus_drain(bus);
  bool ok = fpv_test_expect(batch.count == 1, "event batch size mismatch");
  if (ok && batch.events && batch.events[0]) {
    fpv_event_t* drained = batch.events[0];
    ok = ok && fpv_test_expect(drained->type == FPV_EVENT_CORE_STATUS,
                               "event type mismatch");
    fpv_core_status_event_t* payload =
        (fpv_core_status_event_t*)drained->payload;
    ok = ok && fpv_test_expect(payload && payload->status == FPV_CORE_RUNNING,
                               "event payload status mismatch");
    ok = ok && fpv_test_expect(payload && payload->detail &&
                                   strcmp(payload->detail, "ok") == 0,
                               "event payload detail mismatch");
  }
  fpv_event_batch_destroy(&batch);
  fpv_event_bus_destroy(bus);
  return ok;
}

static bool test_identity_flow(void) {
  bool ok = true;
  char* dir = fpv_test_create_temp_dir("fpvid");
  ok = ok && fpv_test_expect(dir != NULL, "temp dir creation failed");
  if (!dir) {
    return false;
  }

  fpv_result_t result = FPV_OK;
  fpv_identity_store_t* store = fpv_identity_store_open(dir, &result);
  ok = ok && fpv_test_expect(store != NULL, "identity store open failed");
  if (!store) {
    fpv_test_remove_dir(dir);
    fpv_free(dir);
    return false;
  }

  ok = ok && fpv_test_expect(!fpv_identity_store_has_users(store),
                             "expected no users");

  fpv_user_t* owner = NULL;
  result = fpv_identity_create_user(
      store,
      "owner@example.com",
      "Owner",
      "Password123",
      true,
      &owner);
  ok = ok && fpv_test_expect(result == FPV_OK && owner != NULL,
                             "owner create failed");
  ok = ok && fpv_test_expect(fpv_identity_store_has_users(store),
                             "expected users");

  fpv_user_t* auth = NULL;
  result = fpv_identity_authenticate(
      store,
      "owner@example.com",
      "Password123",
      &auth);
  ok = ok && fpv_test_expect(result == FPV_OK && auth != NULL,
                             "auth failed");
  fpv_user_destroy(auth);

  fpv_user_t* bad = NULL;
  result = fpv_identity_authenticate(
      store,
      "owner@example.com",
      "WrongPassword",
      &bad);
  ok = ok && fpv_test_expect(result != FPV_OK, "expected auth failure");
  fpv_user_destroy(bad);

  fpv_organization_t* org = NULL;
  fpv_team_t* team = NULL;
  fpv_user_role_t* role = NULL;
  result = fpv_identity_create_organization(
      store,
      "Acme",
      "UTC",
      "USD",
      NULL,
      owner->id,
      &org,
      &team,
      &role);
  ok = ok && fpv_test_expect(result == FPV_OK && org && team && role,
                             "organization create failed");

  if (org) {
    fpv_organization_t tier_update;
    memset(&tier_update, 0, sizeof(tier_update));
    tier_update.id = org->id;
    tier_update.name = org->name;
    tier_update.timezone = org->timezone;
    tier_update.currency = org->currency;
    tier_update.retention = org->retention;
    tier_update.price_change_approval_required =
        org->price_change_approval_required;
    tier_update.tier = FPV_TIER_ULTIMATE;
    result = fpv_identity_update_organization(store, &tier_update);
    ok = ok && fpv_test_expect(result == FPV_OK, "org tier update failed");
  }

  fpv_identity_invite_t* invite = NULL;
  char* token = NULL;
  result = fpv_identity_create_invite(
      store,
      org->id,
      team->id,
      "user@example.com",
      FPV_ROLE_ANALYST,
      owner->id,
      0,
      &invite,
      &token);
  ok = ok && fpv_test_expect(result == FPV_OK && token,
                             "invite create failed");
  fpv_identity_invite_destroy(invite);

  fpv_identity_invite_t* validated = NULL;
  result = fpv_identity_validate_invite(store, token, &validated);
  ok = ok && fpv_test_expect(result == FPV_OK && validated,
                             "invite validate failed");
  fpv_identity_invite_destroy(validated);

  fpv_user_t* member = NULL;
  result = fpv_identity_create_user(
      store,
      "user@example.com",
      "Member",
      "Password123",
      true,
      &member);
  ok = ok && fpv_test_expect(result == FPV_OK && member,
                             "member create failed");

  fpv_identity_invite_t* accepted = NULL;
  fpv_user_role_t* assigned = NULL;
  result = fpv_identity_accept_invite(
      store,
      token,
      member->id,
      &accepted,
      &assigned);
  ok = ok && fpv_test_expect(result == FPV_OK && accepted && assigned,
                             "invite accept failed");
  fpv_identity_invite_destroy(accepted);
  fpv_user_role_destroy(assigned);

  fpv_organization_t* resolved_org = NULL;
  fpv_team_t* resolved_team = NULL;
  fpv_role_t resolved_role = FPV_ROLE_UNKNOWN;
  result = fpv_identity_resolve_user_context(
      store,
      member->id,
      &resolved_org,
      &resolved_team,
      &resolved_role);
  ok = ok && fpv_test_expect(result == FPV_OK && resolved_role == FPV_ROLE_ANALYST,
                             "resolve context failed");
  fpv_organization_destroy(resolved_org);
  fpv_team_destroy(resolved_team);

  fpv_free(token);
  fpv_user_destroy(owner);
  fpv_user_destroy(member);
  fpv_organization_destroy(org);
  fpv_team_destroy(team);
  fpv_user_role_destroy(role);

  fpv_identity_store_destroy(store);
  char* path = fpv_test_join_path(dir, "fpv.db");
  if (path) {
    fpv_test_remove_file(path);
    fpv_free(path);
  }
  char* wal_path = fpv_test_join_path(dir, "fpv.db-wal");
  if (wal_path) {
    fpv_test_remove_file(wal_path);
    fpv_free(wal_path);
  }
  char* shm_path = fpv_test_join_path(dir, "fpv.db-shm");
  if (shm_path) {
    fpv_test_remove_file(shm_path);
    fpv_free(shm_path);
  }
  fpv_test_remove_dir(dir);
  fpv_free(dir);
  return ok;
}

static bool test_phase1_models_audit(void) {
  fpv_data_retention_policy_t retention;
  retention.audit_log_days = 30;
  retention.price_history_days = 365;
  retention.competitor_listing_days = 30;
  retention.order_history_days = 365;

  fpv_organization_t* organization = fpv_organization_create(
      "org-1",
      "Acme",
      "UTC",
      "USD",
      FPV_TIER_BASIC,
      &retention,
      false,
      100,
      200);
  if (!fpv_test_expect(organization != NULL, "organization create failed")) {
    return false;
  }
  bool ok = fpv_test_expect(
      organization->retention.audit_log_days == retention.audit_log_days,
      "organization retention mismatch");
  ok = ok && fpv_test_expect(
      organization->currency && strcmp(organization->currency, "USD") == 0,
      "organization currency mismatch");

  const char* tags[] = {"fast", "safe"};
  fpv_item_t* item = fpv_item_create(
      "item-1",
      "org-1",
      "Title",
      "title",
      "Category",
      "Sub",
      "Desc",
      tags,
      2,
      1,
      2);
  if (!fpv_test_expect(item != NULL, "item create failed")) {
    fpv_organization_destroy(organization);
    return false;
  }
  fpv_item_t* item_clone = fpv_item_clone(item);
  ok = ok && fpv_test_expect(item_clone != NULL, "item clone failed");
  ok = ok && fpv_test_expect(item_clone && item_clone->tag_count == 2,
                             "item tag count mismatch");
  ok = ok && fpv_test_expect(item_clone && item_clone->tags &&
                                 strcmp(item_clone->tags[0], "fast") == 0,
                             "item tag 0 mismatch");
  ok = ok && fpv_test_expect(item_clone && item_clone->tags &&
                                 strcmp(item_clone->tags[1], "safe") == 0,
                             "item tag 1 mismatch");

  fpv_item_destroy(item);
  fpv_item_destroy(item_clone);
  fpv_organization_destroy(organization);

  char* dir = fpv_test_create_temp_dir("fpvaudit");
  if (!fpv_test_expect(dir != NULL, "temp dir creation failed")) {
    return false;
  }

  fpv_result_t result = FPV_OK;
  fpv_audit_store_t* store = fpv_audit_store_open(dir, NULL, 1, &result);
  if (!fpv_test_expect(store != NULL, "audit store open failed")) {
    fpv_test_remove_dir(dir);
    fpv_free(dir);
    return false;
  }

  uint64_t now = fpv_time_now_ms();
  fpv_audit_log_entry_t* old_entry = fpv_audit_log_entry_create(
      "entry-old",
      "org-1",
      "team-1",
      "acct-1",
      "user-1",
      FPV_ROLE_ADMIN,
      "update",
      "listing",
      "listing-1",
      "old",
      "{}",
      "127.0.0.1",
      "test",
      now - 172800000ULL);
  fpv_audit_log_entry_t* new_entry = fpv_audit_log_entry_create(
      "entry-new",
      "org-1",
      "team-1",
      "acct-1",
      "user-1",
      FPV_ROLE_ADMIN,
      "update",
      "listing",
      "listing-1",
      "new",
      "{}",
      "127.0.0.1",
      "test",
      now);
  ok = ok && fpv_test_expect(old_entry != NULL, "audit entry create failed");
  ok = ok && fpv_test_expect(new_entry != NULL, "audit entry create failed");

  if (old_entry) {
    result = fpv_audit_store_append(store, old_entry);
    ok = ok && fpv_test_expect(result == FPV_OK, "audit append failed");
  }
  if (new_entry) {
    result = fpv_audit_store_append(store, new_entry);
    ok = ok && fpv_test_expect(result == FPV_OK, "audit append failed");
  }

  result = fpv_audit_store_prune(store, now);
  ok = ok && fpv_test_expect(result == FPV_OK, "audit prune failed");

  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.data_dir = dir;
  fpv_db_t* db = NULL;
  result = fpv_db_open(&config, &db);
  ok = ok && fpv_test_expect(result == FPV_OK && db, "audit db open failed");
  fpv_db_result_t* rows = NULL;
  size_t row_count = 0;
  if (result == FPV_OK) {
    result = fpv_db_query(db, "SELECT COUNT(*) FROM fpv_audit_logs;", NULL, 0, &rows);
    if (result == FPV_OK && rows && rows->row_count > 0 && rows->rows &&
        rows->rows[0] && rows->rows[0][0]) {
      row_count = (size_t)strtoull(rows->rows[0][0], NULL, 10);
    }
  }
  ok = ok && fpv_test_expect(row_count == 1, "audit retention mismatch");
  fpv_db_result_destroy(rows);
  fpv_db_close(db);

  fpv_audit_log_entry_destroy(old_entry);
  fpv_audit_log_entry_destroy(new_entry);

  fpv_audit_store_destroy(store);
  char* path = fpv_test_join_path(dir, "fpv.db");
  if (path) {
    fpv_test_remove_file(path);
    fpv_free(path);
  }
  char* wal_path = fpv_test_join_path(dir, "fpv.db-wal");
  if (wal_path) {
    fpv_test_remove_file(wal_path);
    fpv_free(wal_path);
  }
  char* shm_path = fpv_test_join_path(dir, "fpv.db-shm");
  if (shm_path) {
    fpv_test_remove_file(shm_path);
    fpv_free(shm_path);
  }
  fpv_test_remove_dir(dir);
  fpv_free(dir);
  return ok;
}

static bool test_phase3_admin_controls(void) {
  bool ok = true;
  char* dir = fpv_test_create_temp_dir("fpvphase3");
  ok = ok && fpv_test_expect(dir != NULL, "temp dir creation failed");
  if (!dir) {
    return false;
  }

  fpv_result_t result = FPV_OK;
  fpv_identity_store_t* store = fpv_identity_store_open(dir, &result);
  ok = ok && fpv_test_expect(store != NULL, "identity store open failed");
  if (!store) {
    fpv_test_remove_dir(dir);
    fpv_free(dir);
    return false;
  }

  fpv_user_t* owner = NULL;
  result = fpv_identity_create_user(
      store,
      "owner2@example.com",
      "Owner Two",
      "Password123",
      true,
      &owner);
  ok = ok && fpv_test_expect(result == FPV_OK && owner, "owner create failed");

  fpv_organization_t* org = NULL;
  fpv_team_t* default_team = NULL;
  fpv_user_role_t* owner_role = NULL;
  result = fpv_identity_create_organization(
      store,
      "Acme Two",
      "UTC",
      "USD",
      NULL,
      owner ? owner->id : NULL,
      &org,
      &default_team,
      &owner_role);
  ok = ok && fpv_test_expect(result == FPV_OK && org, "org create failed");

  fpv_organization_t updated;
  memset(&updated, 0, sizeof(updated));
  updated.id = org ? org->id : NULL;
  updated.name = "Acme Updated";
  updated.timezone = "Europe/Moscow";
  updated.currency = "EUR";
  updated.retention.audit_log_days = 90;
  updated.retention.price_history_days = 180;
  updated.retention.competitor_listing_days = 60;
  updated.retention.order_history_days = 365;
  updated.price_change_approval_required = true;
  updated.tier = FPV_TIER_ULTIMATE;
  result = fpv_identity_update_organization(store, &updated);
  ok = ok && fpv_test_expect(result == FPV_OK, "org update failed");

  fpv_organization_t* org_check = NULL;
  result = fpv_identity_get_organization(store, org ? org->id : NULL, &org_check);
  ok = ok && fpv_test_expect(result == FPV_OK && org_check, "org get failed");
  ok = ok && fpv_test_expect(org_check && org_check->price_change_approval_required,
                             "org approval flag mismatch");
  ok = ok && fpv_test_expect(org_check && org_check->retention.audit_log_days == 90,
                             "org retention update mismatch");
  fpv_organization_destroy(org_check);

  fpv_team_t* team = NULL;
  result = fpv_identity_create_team(
      store,
      org ? org->id : NULL,
      "Ops",
      true,
      &team);
  ok = ok && fpv_test_expect(result == FPV_OK && team, "team create failed");

  fpv_team_t team_update;
  memset(&team_update, 0, sizeof(team_update));
  team_update.id = team ? team->id : NULL;
  team_update.organization_id = org ? org->id : NULL;
  team_update.name = "Ops North";
  team_update.active = false;
  result = fpv_identity_update_team(store, &team_update);
  ok = ok && fpv_test_expect(result == FPV_OK, "team update failed");

  fpv_team_t* team_check = NULL;
  result = fpv_identity_get_team(store, team ? team->id : NULL, &team_check);
  ok = ok && fpv_test_expect(result == FPV_OK && team_check, "team get failed");
  ok = ok && fpv_test_expect(team_check && !team_check->active, "team active mismatch");
  fpv_team_destroy(team_check);

  fpv_user_t* member = NULL;
  result = fpv_identity_create_user(
      store,
      "member@example.com",
      "Member",
      "Password123",
      true,
      &member);
  ok = ok && fpv_test_expect(result == FPV_OK && member, "member create failed");

  fpv_user_role_t* role = NULL;
  result = fpv_identity_assign_role(
      store,
      member ? member->id : NULL,
      org ? org->id : NULL,
      team ? team->id : NULL,
      FPV_ROLE_MANAGER,
      &role);
  ok = ok && fpv_test_expect(result == FPV_OK && role, "role assign failed");

  result = fpv_identity_update_role(
      store,
      member ? member->id : NULL,
      org ? org->id : NULL,
      team ? team->id : NULL,
      FPV_ROLE_ANALYST);
  ok = ok && fpv_test_expect(result == FPV_OK, "role update failed");

  fpv_access_member_t** members = NULL;
  size_t member_count = 0;
  result = fpv_identity_list_access_members(
      store,
      org ? org->id : NULL,
      &members,
      &member_count);
  ok = ok && fpv_test_expect(result == FPV_OK && member_count >= 2,
                             "access members list failed");
  fpv_identity_access_member_list_destroy(members, member_count);

  result = fpv_identity_remove_role(
      store,
      member ? member->id : NULL,
      org ? org->id : NULL,
      team ? team->id : NULL);
  ok = ok && fpv_test_expect(result == FPV_OK, "role remove failed");

  fpv_team_category_scope_t scope1 = {"Games", "Skins"};
  fpv_team_category_scope_t scope2 = {"Services", NULL};
  fpv_team_category_scope_t* scopes[] = {&scope1, &scope2};
  result = fpv_identity_replace_team_category_scopes(
      store,
      team ? team->id : NULL,
      (const fpv_team_category_scope_t* const*)scopes,
      2);
  ok = ok && fpv_test_expect(result == FPV_OK, "category scopes replace failed");

  fpv_team_category_scope_t** loaded_scopes = NULL;
  size_t loaded_scope_count = 0;
  result = fpv_identity_list_team_category_scopes(
      store,
      team ? team->id : NULL,
      &loaded_scopes,
      &loaded_scope_count);
  ok = ok && fpv_test_expect(result == FPV_OK && loaded_scope_count == 2,
                             "category scopes list failed");
  fpv_identity_team_category_scope_list_destroy(loaded_scopes, loaded_scope_count);

  fpv_price_rule_scope_t price_scopes[] = {
      FPV_PRICE_SCOPE_ORGANIZATION,
      FPV_PRICE_SCOPE_LISTING};
  result = fpv_identity_replace_team_price_scopes(
      store,
      team ? team->id : NULL,
      price_scopes,
      2);
  ok = ok && fpv_test_expect(result == FPV_OK, "price scopes replace failed");

  fpv_price_rule_scope_t* price_scopes_out = NULL;
  size_t price_scope_count = 0;
  result = fpv_identity_list_team_price_scopes(
      store,
      team ? team->id : NULL,
      &price_scopes_out,
      &price_scope_count);
  ok = ok && fpv_test_expect(result == FPV_OK && price_scope_count == 2,
                             "price scopes list failed");
  fpv_free(price_scopes_out);

  const char* alert_scopes[] = {"price_conflict", "margin_risk"};
  result = fpv_identity_replace_team_alert_scopes(
      store,
      team ? team->id : NULL,
      alert_scopes,
      2);
  ok = ok && fpv_test_expect(result == FPV_OK, "alert scopes replace failed");

  char** alert_scopes_out = NULL;
  size_t alert_scope_count = 0;
  result = fpv_identity_list_team_alert_scopes(
      store,
      team ? team->id : NULL,
      &alert_scopes_out,
      &alert_scope_count);
  ok = ok && fpv_test_expect(result == FPV_OK && alert_scope_count == 2,
                             "alert scopes list failed");
  fpv_identity_string_list_destroy(alert_scopes_out, alert_scope_count);

  fpv_access_review_t* review = NULL;
  result = fpv_identity_record_access_review(
      store,
      org ? org->id : NULL,
      owner ? owner->id : NULL,
      "Quarterly review",
      &review);
  ok = ok && fpv_test_expect(result == FPV_OK && review, "access review record failed");
  fpv_access_review_destroy(review);

  fpv_access_review_t* latest = NULL;
  result = fpv_identity_get_latest_access_review(
      store,
      org ? org->id : NULL,
      &latest);
  ok = ok && fpv_test_expect(result == FPV_OK && latest, "access review fetch failed");
  fpv_access_review_destroy(latest);

  fpv_account_t* account = NULL;
  result = fpv_identity_link_account(
      store,
      org ? org->id : NULL,
      team ? team->id : NULL,
      "fpv-user",
      "fpv_user",
      "FPV User",
      "USD",
      &account);
  ok = ok && fpv_test_expect(result == FPV_OK && account, "account link failed");

  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.data_dir = dir;
  fpv_db_t* db = NULL;
  result = fpv_db_open(&config, &db);
  ok = ok && fpv_test_expect(result == FPV_OK && db, "db open failed");
  if (result == FPV_OK && db) {
    fpv_db_migrate(db);
    uint64_t now_ms = fpv_time_now_ms();
    char now_buf[32];
    snprintf(now_buf, sizeof(now_buf), "%llu", (unsigned long long)now_ms);
    const char* item_params[] = {
        "item-1",
        org ? org->id : NULL,
        "Item",
        "item",
        "Category",
        "Sub",
        "Desc",
        now_buf,
        now_buf};
    fpv_db_exec_params(
        db,
        "INSERT INTO fpv_items (id, organization_id, title, normalized_title, "
        "category, subcategory, description, created_at_ms, updated_at_ms) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9);",
        item_params,
        9);
    const char* listing_params[] = {
        "listing-1",
        "item-1",
        account ? account->id : NULL,
        "Listing",
        "Category",
        "Sub",
        "active",
        "10.00",
        "USD",
        "5",
        "digital",
        now_buf,
        "Desc"};
    fpv_db_exec_params(
        db,
        "INSERT INTO fpv_listings (id, item_id, account_id, title, category, "
        "subcategory, status, price, currency, quantity, delivery_type, "
        "last_updated_ms, description) VALUES "
        "($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13);",
        listing_params,
        13);
    fpv_db_close(db);
  }

  fpv_price_change_request_t* request = NULL;
  result = fpv_identity_create_price_change_request(
      store,
      org ? org->id : NULL,
      team ? team->id : NULL,
      "listing-1",
      NULL,
      owner ? owner->id : NULL,
      10.0,
      9.0,
      "USD",
      "undercut",
      &request);
  ok = ok && fpv_test_expect(result == FPV_OK && request,
                             "price change request create failed");
  char* request_id = request && request->id ? fpv_strdup(request->id) : NULL;
  fpv_price_change_request_destroy(request);

  fpv_price_change_request_t** pending = NULL;
  size_t pending_count = 0;
  result = fpv_identity_list_price_change_requests(
      store,
      org ? org->id : NULL,
      FPV_PRICE_CHANGE_PENDING,
      &pending,
      &pending_count);
  ok = ok && fpv_test_expect(result == FPV_OK && pending_count == 1,
                             "pending price change list failed");
  fpv_identity_price_change_request_list_destroy(pending, pending_count);

  fpv_price_change_request_t* approved = NULL;
  result = fpv_identity_review_price_change_request(
      store,
      request_id,
      FPV_PRICE_CHANGE_APPROVED,
      owner ? owner->id : NULL,
      NULL,
      &approved);
  ok = ok && fpv_test_expect(result == FPV_OK && approved &&
                             approved->status == FPV_PRICE_CHANGE_APPROVED,
                             "price change approve failed");
  fpv_price_change_request_destroy(approved);

  fpv_price_change_request_t* applied = NULL;
  result = fpv_identity_mark_price_change_applied(
      store,
      request_id,
      &applied);
  ok = ok && fpv_test_expect(result == FPV_OK && applied &&
                             applied->status == FPV_PRICE_CHANGE_APPLIED,
                             "price change apply failed");
  fpv_price_change_request_destroy(applied);

  fpv_free(request_id);
  fpv_account_destroy(account);
  fpv_user_role_destroy(role);
  fpv_user_destroy(member);
  fpv_user_destroy(owner);
  fpv_organization_destroy(org);
  fpv_team_destroy(team);
  fpv_team_destroy(default_team);
  fpv_user_role_destroy(owner_role);

  fpv_identity_store_destroy(store);
  char* path = fpv_test_join_path(dir, "fpv.db");
  if (path) {
    fpv_test_remove_file(path);
    fpv_free(path);
  }
  char* wal_path = fpv_test_join_path(dir, "fpv.db-wal");
  if (wal_path) {
    fpv_test_remove_file(wal_path);
    fpv_free(wal_path);
  }
  char* shm_path = fpv_test_join_path(dir, "fpv.db-shm");
  if (shm_path) {
    fpv_test_remove_file(shm_path);
    fpv_free(shm_path);
  }
  fpv_test_remove_dir(dir);
  fpv_free(dir);
  return ok;
}

static int fpv_run_tests(const fpv_test_case_t* tests, size_t count) {
  int failed = 0;
  for (size_t i = 0; i < count; i++) {
    bool ok = tests[i].fn();
    if (!ok) {
      fprintf(stderr, "FAIL: %s\n", tests[i].name);
      failed++;
    } else {
      printf("PASS: %s\n", tests[i].name);
    }
  }
  return failed == 0 ? 0 : 1;
}

int main(void) {
  const fpv_test_case_t tests[] = {
      {"ini_parse", test_ini_parse},
      {"json_parse", test_json_parse},
      {"localizer_format", test_localizer_format},
      {"event_bus", test_event_bus},
      {"identity_flow", test_identity_flow},
      {"phase1_models_audit", test_phase1_models_audit},
      {"phase3_admin_controls", test_phase3_admin_controls},
  };
  return fpv_run_tests(tests, sizeof(tests) / sizeof(tests[0]));
}
