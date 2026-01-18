/* FunPay Vertex audit log storage implementation. */

#include "fpv_core/fpv_audit.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_db.h"
#include "fpv_time.h"

struct fpv_audit_store {
  fpv_db_t* db;
  uint32_t retention_days;
};

fpv_audit_store_t* fpv_audit_store_open(
    const char* data_dir,
    const char* db_url,
    uint32_t retention_days,
    fpv_result_t* out_result) {
  if (out_result) {
    *out_result = FPV_OK;
  }

  const char* resolved = db_url && db_url[0] ? db_url : getenv("FPV_DB_URL");
  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = resolved;
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

  fpv_audit_store_t* store =
      (fpv_audit_store_t*)calloc(1, sizeof(*store));
  if (!store) {
    if (out_result) {
      *out_result = FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_db_close(db);
    return NULL;
  }
  store->db = db;
  store->retention_days = retention_days;
  return store;
}

void fpv_audit_store_destroy(fpv_audit_store_t* store) {
  if (!store) {
    return;
  }
  fpv_db_close(store->db);
  free(store);
}

fpv_result_t fpv_audit_store_append(
    const fpv_audit_store_t* store,
    const fpv_audit_log_entry_t* entry) {
  if (!store || !store->db || !entry || !entry->id || !entry->organization_id ||
      !entry->action || !entry->target_type || !entry->target_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  uint64_t timestamp = entry->created_at_ms;
  if (timestamp == 0) {
    timestamp = fpv_time_now_ms();
  }

  char role_buf[8];
  char created_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", entry->actor_role);
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)timestamp);

  const char* params[] = {
      entry->id,
      entry->organization_id,
      entry->team_id,
      entry->account_id,
      entry->actor_user_id,
      role_buf,
      entry->action,
      entry->target_type,
      entry->target_id,
      entry->summary,
      entry->metadata,
      entry->ip_address,
      entry->user_agent,
      created_buf};

  return fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_audit_logs (id, organization_id, team_id, account_id, "
      "actor_user_id, actor_role, action, target_type, target_id, summary, "
      "metadata, ip_address, user_agent, created_at_ms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14);",
      params,
      14);
}

fpv_result_t fpv_audit_store_prune(
    const fpv_audit_store_t* store,
    uint64_t now_ms) {
  if (!store || !store->db) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (store->retention_days == 0) {
    return FPV_OK;
  }
  uint64_t timestamp = now_ms == 0 ? fpv_time_now_ms() : now_ms;
  uint64_t retention_ms =
      (uint64_t)store->retention_days * 86400000ULL;
  if (retention_ms == 0) {
    return FPV_OK;
  }
  uint64_t cutoff = timestamp > retention_ms ? timestamp - retention_ms : 0;
  char cutoff_buf[32];
  snprintf(cutoff_buf, sizeof(cutoff_buf), "%llu",
           (unsigned long long)cutoff);
  const char* params[] = {cutoff_buf};
  return fpv_db_exec_params(
      store->db,
      "DELETE FROM fpv_audit_logs WHERE created_at_ms < $1;",
      params,
      1);
}
