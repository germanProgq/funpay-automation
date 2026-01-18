/* FunPay Vertex database access utilities implementation. */

#include "fpv_db.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_fs.h"
#include "fpv_string.h"
#include "fpv_time.h"

#if defined(FPV_HAVE_POSTGRES)
#include <libpq-fe.h>
#endif

#if defined(FPV_HAVE_SQLITE)
#include <sqlite3.h>
#endif

struct fpv_db {
  fpv_db_backend_t backend;
  char* error;
#if defined(FPV_HAVE_POSTGRES)
  PGconn* pg;
#endif
#if defined(FPV_HAVE_SQLITE)
  sqlite3* sqlite;
#endif
};

typedef struct fpv_db_migration {
  int version;
  const char* name;
  const char* sql;
} fpv_db_migration_t;

static void fpv_db_set_error(fpv_db_t* db, const char* message) {
  if (!db) {
    return;
  }
  fpv_free(db->error);
  db->error = message ? fpv_strdup(message) : NULL;
}

static bool fpv_db_has_prefix(const char* value, const char* prefix) {
  size_t len = 0;
  if (!value || !prefix) {
    return false;
  }
  len = strlen(prefix);
  return strncmp(value, prefix, len) == 0;
}

static bool fpv_db_has_scheme(const char* value) {
  if (!value) {
    return false;
  }
  return strstr(value, "://") != NULL;
}

static bool fpv_db_is_absolute_path(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
  if (path[0] == '/' || path[0] == '\\') {
    return true;
  }
  if (path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  return false;
}

static char* fpv_db_strdup_range(const char* start, const char* end) {
  if (!start || !end || end < start) {
    return NULL;
  }
  size_t len = (size_t)(end - start);
  char* copy = (char*)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, start, len);
  copy[len] = '\0';
  return copy;
}

static char* fpv_db_extract_sqlite_path(const char* url, const char* data_dir) {
  const char* start = url;
  if (!start || !start[0]) {
    return NULL;
  }
  if (fpv_db_has_prefix(start, "sqlite://")) {
    start += strlen("sqlite://");
  } else if (fpv_db_has_prefix(start, "file:")) {
    start += strlen("file:");
  }
  const char* end = strchr(start, '?');
  char* raw = end ? fpv_db_strdup_range(start, end) : fpv_strdup(start);
  if (!raw) {
    return NULL;
  }
  if (strcmp(raw, ":memory:") == 0) {
    return raw;
  }
  if (data_dir && data_dir[0] && !fpv_db_is_absolute_path(raw)) {
    char* joined = fpv_path_join(data_dir, raw);
    fpv_free(raw);
    return joined;
  }
  return raw;
}

#if defined(FPV_HAVE_SQLITE)
static fpv_result_t fpv_db_sqlite_apply_pragmas(fpv_db_t* db) {
  char* error = NULL;
  if (sqlite3_exec(db->sqlite, "PRAGMA foreign_keys = ON;", NULL, NULL, &error) !=
      SQLITE_OK) {
    fpv_db_set_error(db, error ? error : "SQLite pragma failed.");
    sqlite3_free(error);
    return FPV_ERR_IO;
  }
  sqlite3_free(error);
  sqlite3_exec(db->sqlite, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
  sqlite3_exec(db->sqlite, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL);
  return FPV_OK;
}
#endif

fpv_result_t fpv_db_open(const fpv_db_config_t* config, fpv_db_t** out_db) {
  if (!config || !out_db) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_db = NULL;

  const char* url = config->url && config->url[0] ? config->url : NULL;
  fpv_db_backend_t backend = FPV_DB_BACKEND_UNKNOWN;
  char* sqlite_path = NULL;

  if (url) {
    if (fpv_db_has_prefix(url, "postgres://") ||
        fpv_db_has_prefix(url, "postgresql://")) {
      backend = FPV_DB_BACKEND_POSTGRES;
    } else if (fpv_db_has_scheme(url) &&
               !fpv_db_has_prefix(url, "sqlite://") &&
               !fpv_db_has_prefix(url, "file:")) {
      return FPV_ERR_UNSUPPORTED;
    } else {
      backend = FPV_DB_BACKEND_SQLITE;
      sqlite_path = fpv_db_extract_sqlite_path(url, config->data_dir);
    }
  } else if (config->sqlite_path && config->sqlite_path[0]) {
    backend = FPV_DB_BACKEND_SQLITE;
    sqlite_path = fpv_db_extract_sqlite_path(
        config->sqlite_path,
        config->data_dir);
  } else if (config->data_dir && config->data_dir[0]) {
    backend = FPV_DB_BACKEND_SQLITE;
    sqlite_path = fpv_path_join(config->data_dir, "fpv.db");
  }

  if (backend == FPV_DB_BACKEND_UNKNOWN) {
    fpv_free(sqlite_path);
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_db_t* db = (fpv_db_t*)calloc(1, sizeof(*db));
  if (!db) {
    fpv_free(sqlite_path);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  db->backend = backend;

  if (backend == FPV_DB_BACKEND_POSTGRES) {
#if defined(FPV_HAVE_POSTGRES)
    PGconn* conn = PQconnectdb(url);
    if (!conn || PQstatus(conn) != CONNECTION_OK) {
      fpv_db_set_error(db, conn ? PQerrorMessage(conn) : "PostgreSQL connect failed.");
      if (conn) {
        PQfinish(conn);
      }
      fpv_db_close(db);
      return FPV_ERR_IO;
    }
    db->pg = conn;
#else
    fpv_db_set_error(db, "PostgreSQL backend not enabled.");
    fpv_db_close(db);
    return FPV_ERR_UNSUPPORTED;
#endif
  } else if (backend == FPV_DB_BACKEND_SQLITE) {
#if defined(FPV_HAVE_SQLITE)
    if (!sqlite_path || !sqlite_path[0]) {
      fpv_db_close(db);
      fpv_free(sqlite_path);
      return FPV_ERR_INVALID_ARGUMENT;
    }
    if (sqlite3_open_v2(
            sqlite_path,
            &db->sqlite,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            NULL) != SQLITE_OK) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      fpv_db_close(db);
      fpv_free(sqlite_path);
      return FPV_ERR_IO;
    }
    sqlite3_busy_timeout(db->sqlite, 5000);
    fpv_result_t result = fpv_db_sqlite_apply_pragmas(db);
    fpv_free(sqlite_path);
    if (result != FPV_OK) {
      fpv_db_close(db);
      return result;
    }
#else
    fpv_db_set_error(db, "SQLite backend not enabled.");
    fpv_db_close(db);
    fpv_free(sqlite_path);
    return FPV_ERR_UNSUPPORTED;
#endif
  }

  *out_db = db;
  return FPV_OK;
}

void fpv_db_close(fpv_db_t* db) {
  if (!db) {
    return;
  }
#if defined(FPV_HAVE_POSTGRES)
  if (db->pg) {
    PQfinish(db->pg);
  }
#endif
#if defined(FPV_HAVE_SQLITE)
  if (db->sqlite) {
    sqlite3_close(db->sqlite);
  }
#endif
  fpv_free(db->error);
  free(db);
}

fpv_db_backend_t fpv_db_backend(const fpv_db_t* db) {
  return db ? db->backend : FPV_DB_BACKEND_UNKNOWN;
}

const char* fpv_db_error(const fpv_db_t* db) {
  return db && db->error ? db->error : "";
}

fpv_result_t fpv_db_exec(fpv_db_t* db, const char* sql) {
  if (!db || !sql) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_set_error(db, NULL);
  if (db->backend == FPV_DB_BACKEND_POSTGRES) {
#if defined(FPV_HAVE_POSTGRES)
    PGresult* res = PQexec(db->pg, sql);
    if (!res) {
      fpv_db_set_error(db, PQerrorMessage(db->pg));
      return FPV_ERR_IO;
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      fpv_db_set_error(db, PQresultErrorMessage(res));
      PQclear(res);
      return FPV_ERR_IO;
    }
    PQclear(res);
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  if (db->backend == FPV_DB_BACKEND_SQLITE) {
#if defined(FPV_HAVE_SQLITE)
    char* error = NULL;
    if (sqlite3_exec(db->sqlite, sql, NULL, NULL, &error) != SQLITE_OK) {
      fpv_db_set_error(db, error ? error : sqlite3_errmsg(db->sqlite));
      sqlite3_free(error);
      return FPV_ERR_IO;
    }
    sqlite3_free(error);
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  return FPV_ERR_INVALID_STATE;
}

fpv_result_t fpv_db_exec_params(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count) {
  if (!db || !sql) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_set_error(db, NULL);
  if (db->backend == FPV_DB_BACKEND_POSTGRES) {
#if defined(FPV_HAVE_POSTGRES)
    PGresult* res = PQexecParams(
        db->pg,
        sql,
        (int)param_count,
        NULL,
        params,
        NULL,
        NULL,
        0);
    if (!res) {
      fpv_db_set_error(db, PQerrorMessage(db->pg));
      return FPV_ERR_IO;
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
      fpv_db_set_error(db, PQresultErrorMessage(res));
      PQclear(res);
      return FPV_ERR_IO;
    }
    PQclear(res);
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  if (db->backend == FPV_DB_BACKEND_SQLITE) {
#if defined(FPV_HAVE_SQLITE)
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      return FPV_ERR_IO;
    }
    for (size_t i = 0; i < param_count; i++) {
      const char* value = params ? params[i] : NULL;
      if (!value) {
        sqlite3_bind_null(stmt, (int)i + 1);
      } else if (sqlite3_bind_text(stmt, (int)i + 1, value, -1, SQLITE_TRANSIENT) !=
                 SQLITE_OK) {
        fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
        sqlite3_finalize(stmt);
        return FPV_ERR_IO;
      }
    }
    int rc = sqlite3_step(stmt);
    while (rc == SQLITE_ROW) {
      rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      sqlite3_finalize(stmt);
      return FPV_ERR_IO;
    }
    sqlite3_finalize(stmt);
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  return FPV_ERR_INVALID_STATE;
}

fpv_result_t fpv_db_query(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count,
    fpv_db_result_t** out_result) {
  if (out_result) {
    *out_result = NULL;
  }
  if (!db || !sql || !out_result) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_set_error(db, NULL);
  if (db->backend == FPV_DB_BACKEND_POSTGRES) {
#if defined(FPV_HAVE_POSTGRES)
    PGresult* res = PQexecParams(
        db->pg,
        sql,
        (int)param_count,
        NULL,
        params,
        NULL,
        NULL,
        0);
    if (!res) {
      fpv_db_set_error(db, PQerrorMessage(db->pg));
      return FPV_ERR_IO;
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK) {
      fpv_db_set_error(db, PQresultErrorMessage(res));
      PQclear(res);
      return FPV_ERR_IO;
    }
    int rows = PQntuples(res);
    int cols = PQnfields(res);
    fpv_db_result_t* result = (fpv_db_result_t*)calloc(1, sizeof(*result));
    if (!result) {
      PQclear(res);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    result->row_count = (size_t)rows;
    result->column_count = (size_t)cols;
    if (rows > 0 && cols > 0) {
      result->rows = (char***)calloc((size_t)rows, sizeof(*result->rows));
      if (!result->rows) {
        PQclear(res);
        fpv_db_result_destroy(result);
        return FPV_ERR_OUT_OF_MEMORY;
      }
      for (int r = 0; r < rows; r++) {
        result->rows[r] = (char**)calloc((size_t)cols, sizeof(**result->rows));
        if (!result->rows[r]) {
          PQclear(res);
          fpv_db_result_destroy(result);
          return FPV_ERR_OUT_OF_MEMORY;
        }
        for (int c = 0; c < cols; c++) {
          if (PQgetisnull(res, r, c)) {
            result->rows[r][c] = NULL;
            continue;
          }
          const char* value = PQgetvalue(res, r, c);
          result->rows[r][c] = value ? fpv_strdup(value) : NULL;
          if (value && !result->rows[r][c]) {
            PQclear(res);
            fpv_db_result_destroy(result);
            return FPV_ERR_OUT_OF_MEMORY;
          }
        }
      }
    }
    PQclear(res);
    *out_result = result;
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  if (db->backend == FPV_DB_BACKEND_SQLITE) {
#if defined(FPV_HAVE_SQLITE)
    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db->sqlite, sql, -1, &stmt, NULL) != SQLITE_OK) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      return FPV_ERR_IO;
    }
    for (size_t i = 0; i < param_count; i++) {
      const char* value = params ? params[i] : NULL;
      if (!value) {
        sqlite3_bind_null(stmt, (int)i + 1);
      } else if (sqlite3_bind_text(stmt, (int)i + 1, value, -1, SQLITE_TRANSIENT) !=
                 SQLITE_OK) {
        fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
        sqlite3_finalize(stmt);
        return FPV_ERR_IO;
      }
    }
    int cols = sqlite3_column_count(stmt);
    size_t capacity = 0;
    fpv_db_result_t* result = (fpv_db_result_t*)calloc(1, sizeof(*result));
    if (!result) {
      sqlite3_finalize(stmt);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    result->column_count = (size_t)cols;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      if (result->row_count == capacity) {
        size_t next = capacity == 0 ? 8 : capacity * 2;
        char*** grown = (char***)realloc(result->rows, next * sizeof(*grown));
        if (!grown) {
          sqlite3_finalize(stmt);
          fpv_db_result_destroy(result);
          return FPV_ERR_OUT_OF_MEMORY;
        }
        result->rows = grown;
        capacity = next;
      }
      result->rows[result->row_count] =
          (char**)calloc(result->column_count, sizeof(**result->rows));
      if (!result->rows[result->row_count]) {
        sqlite3_finalize(stmt);
        fpv_db_result_destroy(result);
        return FPV_ERR_OUT_OF_MEMORY;
      }
      for (int c = 0; c < cols; c++) {
        if (sqlite3_column_type(stmt, c) == SQLITE_NULL) {
          result->rows[result->row_count][c] = NULL;
          continue;
        }
        const unsigned char* text = sqlite3_column_text(stmt, c);
        const char* value = text ? (const char*)text : "";
        result->rows[result->row_count][c] = fpv_strdup(value);
        if (!result->rows[result->row_count][c]) {
          sqlite3_finalize(stmt);
          fpv_db_result_destroy(result);
          return FPV_ERR_OUT_OF_MEMORY;
        }
      }
      result->row_count++;
    }
    if (rc != SQLITE_DONE) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      sqlite3_finalize(stmt);
      fpv_db_result_destroy(result);
      return FPV_ERR_IO;
    }
    sqlite3_finalize(stmt);
    *out_result = result;
    return FPV_OK;
#else
    return FPV_ERR_UNSUPPORTED;
#endif
  }
  return FPV_ERR_INVALID_STATE;
}

void fpv_db_result_destroy(fpv_db_result_t* result) {
  if (!result) {
    return;
  }
  if (result->rows) {
    for (size_t r = 0; r < result->row_count; r++) {
      if (!result->rows[r]) {
        continue;
      }
      for (size_t c = 0; c < result->column_count; c++) {
        fpv_free(result->rows[r][c]);
      }
      fpv_free(result->rows[r]);
    }
    fpv_free(result->rows);
  }
  free(result);
}

fpv_result_t fpv_db_begin(fpv_db_t* db) {
  return fpv_db_exec(db, "BEGIN;");
}

fpv_result_t fpv_db_commit(fpv_db_t* db) {
  return fpv_db_exec(db, "COMMIT;");
}

fpv_result_t fpv_db_rollback(fpv_db_t* db) {
  return fpv_db_exec(db, "ROLLBACK;");
}

static bool fpv_db_parse_int(const char* text, int* out) {
  if (!text || !out) {
    return false;
  }
  char* end = NULL;
  long value = strtol(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (int)value;
  return true;
}

static const char fpv_db_migration_1_sql[] =
    "CREATE TABLE IF NOT EXISTS fpv_users ("
    "  id TEXT PRIMARY KEY,"
    "  email TEXT NOT NULL,"
    "  email_normalized TEXT NOT NULL UNIQUE,"
    "  display_name TEXT,"
    "  email_verified BOOLEAN NOT NULL,"
    "  active BOOLEAN NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  last_login_ms BIGINT NOT NULL,"
    "  password_salt TEXT NOT NULL,"
    "  password_hash TEXT NOT NULL,"
    "  password_iterations INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS fpv_organizations ("
    "  id TEXT PRIMARY KEY,"
    "  name TEXT NOT NULL,"
    "  timezone TEXT,"
    "  currency TEXT,"
    "  retention_audit_log_days INTEGER NOT NULL,"
    "  retention_price_history_days INTEGER NOT NULL,"
    "  retention_competitor_listing_days INTEGER NOT NULL,"
    "  retention_order_history_days INTEGER NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  updated_at_ms BIGINT NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS fpv_teams ("
    "  id TEXT PRIMARY KEY,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  name TEXT NOT NULL,"
    "  active BOOLEAN NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  updated_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_teams_org_idx ON fpv_teams(organization_id);"
    "CREATE TABLE IF NOT EXISTS fpv_user_roles ("
    "  user_id TEXT NOT NULL REFERENCES fpv_users(id) ON DELETE CASCADE,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  team_id TEXT REFERENCES fpv_teams(id) ON DELETE SET NULL,"
    "  role INTEGER NOT NULL,"
    "  assigned_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_user_roles_org_idx"
    "  ON fpv_user_roles(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_user_roles_team_idx"
    "  ON fpv_user_roles(team_id);"
    "CREATE UNIQUE INDEX IF NOT EXISTS fpv_user_roles_unique_org"
    "  ON fpv_user_roles(user_id, organization_id)"
    "  WHERE team_id IS NULL;"
    "CREATE UNIQUE INDEX IF NOT EXISTS fpv_user_roles_unique_team"
    "  ON fpv_user_roles(user_id, organization_id, team_id)"
    "  WHERE team_id IS NOT NULL;"
    "CREATE TABLE IF NOT EXISTS fpv_accounts ("
    "  id TEXT PRIMARY KEY,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  team_id TEXT REFERENCES fpv_teams(id) ON DELETE SET NULL,"
    "  funpay_user_id TEXT NOT NULL,"
    "  funpay_username TEXT,"
    "  display_name TEXT,"
    "  currency TEXT,"
    "  active BOOLEAN NOT NULL,"
    "  linked_at_ms BIGINT NOT NULL,"
    "  last_sync_at_ms BIGINT NOT NULL"
    ");"
    "CREATE UNIQUE INDEX IF NOT EXISTS fpv_accounts_org_idx"
    "  ON fpv_accounts(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_accounts_team_idx ON fpv_accounts(team_id);"
    "CREATE TABLE IF NOT EXISTS fpv_invites ("
    "  id TEXT PRIMARY KEY,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  team_id TEXT REFERENCES fpv_teams(id) ON DELETE SET NULL,"
    "  email TEXT,"
    "  role INTEGER NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  expires_at_ms BIGINT NOT NULL,"
    "  accepted_at_ms BIGINT NOT NULL,"
    "  accepted_user_id TEXT REFERENCES fpv_users(id) ON DELETE SET NULL,"
    "  created_by_user_id TEXT REFERENCES fpv_users(id) ON DELETE SET NULL,"
    "  token_hash TEXT NOT NULL UNIQUE"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_invites_org_idx ON fpv_invites(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_invites_team_idx ON fpv_invites(team_id);"
    "CREATE INDEX IF NOT EXISTS fpv_invites_email_idx ON fpv_invites(email);"
    "CREATE TABLE IF NOT EXISTS fpv_audit_logs ("
    "  id TEXT PRIMARY KEY,"
    "  organization_id TEXT NOT NULL,"
    "  team_id TEXT,"
    "  account_id TEXT,"
    "  actor_user_id TEXT,"
    "  actor_role INTEGER NOT NULL,"
    "  action TEXT NOT NULL,"
    "  target_type TEXT NOT NULL,"
    "  target_id TEXT NOT NULL,"
    "  summary TEXT,"
    "  metadata TEXT,"
    "  ip_address TEXT,"
    "  user_agent TEXT,"
    "  created_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_audit_org_time_idx"
    "  ON fpv_audit_logs(organization_id, created_at_ms);"
    "CREATE INDEX IF NOT EXISTS fpv_audit_team_time_idx"
    "  ON fpv_audit_logs(team_id, created_at_ms);"
    "CREATE TABLE IF NOT EXISTS fpv_items ("
    "  id TEXT PRIMARY KEY,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  title TEXT NOT NULL,"
    "  normalized_title TEXT NOT NULL,"
    "  category TEXT NOT NULL,"
    "  subcategory TEXT NOT NULL,"
    "  description TEXT NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  updated_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_items_org_idx ON fpv_items(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_items_lookup_idx"
    "  ON fpv_items(organization_id, normalized_title, category, subcategory);"
    "CREATE TABLE IF NOT EXISTS fpv_item_tags ("
    "  item_id TEXT NOT NULL REFERENCES fpv_items(id) ON DELETE CASCADE,"
    "  tag TEXT NOT NULL,"
    "  PRIMARY KEY (item_id, tag)"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_item_tags_tag_idx ON fpv_item_tags(tag);"
    "CREATE TABLE IF NOT EXISTS fpv_listings ("
    "  id TEXT PRIMARY KEY,"
    "  item_id TEXT NOT NULL REFERENCES fpv_items(id) ON DELETE CASCADE,"
    "  account_id TEXT NOT NULL REFERENCES fpv_accounts(id) ON DELETE CASCADE,"
    "  title TEXT NOT NULL,"
    "  category TEXT NOT NULL,"
    "  subcategory TEXT NOT NULL,"
    "  status TEXT NOT NULL,"
    "  price REAL NOT NULL,"
    "  currency TEXT NOT NULL,"
    "  quantity INTEGER NOT NULL,"
    "  delivery_type TEXT NOT NULL,"
    "  last_updated_ms BIGINT NOT NULL,"
    "  description TEXT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_listings_item_idx ON fpv_listings(item_id);"
    "CREATE INDEX IF NOT EXISTS fpv_listings_account_idx ON fpv_listings(account_id);"
    "CREATE INDEX IF NOT EXISTS fpv_listings_status_idx ON fpv_listings(status);"
    "CREATE TABLE IF NOT EXISTS fpv_listing_tags ("
    "  listing_id TEXT NOT NULL REFERENCES fpv_listings(id) ON DELETE CASCADE,"
    "  tag TEXT NOT NULL,"
    "  PRIMARY KEY (listing_id, tag)"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_listing_tags_tag_idx ON fpv_listing_tags(tag);"
    "CREATE TABLE IF NOT EXISTS fpv_competitor_listings ("
    "  id TEXT PRIMARY KEY,"
    "  item_id TEXT NOT NULL REFERENCES fpv_items(id) ON DELETE CASCADE,"
    "  seller_id TEXT NOT NULL,"
    "  price REAL NOT NULL,"
    "  currency TEXT NOT NULL,"
    "  available BOOLEAN NOT NULL,"
    "  delivery_type TEXT NOT NULL,"
    "  seller_rating REAL NOT NULL,"
    "  last_seen_ms BIGINT NOT NULL,"
    "  listing_url TEXT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_competitor_item_idx"
    "  ON fpv_competitor_listings(item_id);"
    "CREATE INDEX IF NOT EXISTS fpv_competitor_seen_idx"
    "  ON fpv_competitor_listings(last_seen_ms);"
    "CREATE TABLE IF NOT EXISTS fpv_price_rules ("
    "  id TEXT PRIMARY KEY,"
    "  scope INTEGER NOT NULL,"
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  team_id TEXT REFERENCES fpv_teams(id) ON DELETE SET NULL,"
    "  item_id TEXT REFERENCES fpv_items(id) ON DELETE SET NULL,"
    "  listing_id TEXT REFERENCES fpv_listings(id) ON DELETE SET NULL,"
    "  min_margin REAL NOT NULL,"
    "  min_price REAL NOT NULL,"
    "  undercut_type INTEGER NOT NULL,"
    "  undercut_value REAL NOT NULL,"
    "  enabled BOOLEAN NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  updated_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_price_rules_org_idx"
    "  ON fpv_price_rules(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_price_rules_team_idx"
    "  ON fpv_price_rules(team_id);"
    "CREATE INDEX IF NOT EXISTS fpv_price_rules_item_idx"
    "  ON fpv_price_rules(item_id);"
    "CREATE INDEX IF NOT EXISTS fpv_price_rules_listing_idx"
    "  ON fpv_price_rules(listing_id);"
    "CREATE TABLE IF NOT EXISTS fpv_price_history ("
    "  id TEXT PRIMARY KEY,"
    "  listing_id TEXT NOT NULL REFERENCES fpv_listings(id) ON DELETE CASCADE,"
    "  price_rule_id TEXT REFERENCES fpv_price_rules(id) ON DELETE SET NULL,"
    "  competitor_median REAL NOT NULL,"
    "  recommended_price REAL NOT NULL,"
    "  applied_price REAL NOT NULL,"
    "  currency TEXT NOT NULL,"
    "  reason TEXT NOT NULL,"
    "  created_at_ms BIGINT NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_price_history_listing_idx"
    "  ON fpv_price_history(listing_id, created_at_ms);"
    "CREATE INDEX IF NOT EXISTS fpv_price_history_rule_idx"
    "  ON fpv_price_history(price_rule_id);";

static const char fpv_db_migration_2_sql[] =
    "CREATE TABLE IF NOT EXISTS fpv_chats ("
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  chat_id TEXT NOT NULL,"
    "  title TEXT,"
    "  last_message_text TEXT,"
    "  last_message_time TEXT,"
    "  last_message_id TEXT,"
    "  unread_count INTEGER NOT NULL,"
    "  updated_at_ms BIGINT NOT NULL,"
    "  PRIMARY KEY (organization_id, chat_id)"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_chats_org_idx"
    "  ON fpv_chats(organization_id);"
    "CREATE INDEX IF NOT EXISTS fpv_chats_updated_idx"
    "  ON fpv_chats(organization_id, updated_at_ms);"
    "CREATE TABLE IF NOT EXISTS fpv_messages ("
    "  organization_id TEXT NOT NULL REFERENCES fpv_organizations(id)"
    "    ON DELETE CASCADE,"
    "  chat_id TEXT NOT NULL,"
    "  message_id TEXT NOT NULL,"
    "  sender_id TEXT,"
    "  sender_name TEXT,"
    "  message_text TEXT,"
    "  image_url TEXT,"
    "  badge TEXT,"
    "  direction INTEGER NOT NULL,"
    "  by_bot BOOLEAN NOT NULL,"
    "  created_at_ms BIGINT NOT NULL,"
    "  PRIMARY KEY (organization_id, chat_id, message_id)"
    ");"
    "CREATE INDEX IF NOT EXISTS fpv_messages_chat_time_idx"
    "  ON fpv_messages(organization_id, chat_id, created_at_ms);"
    "CREATE INDEX IF NOT EXISTS fpv_messages_time_idx"
    "  ON fpv_messages(organization_id, created_at_ms);";

static const fpv_db_migration_t fpv_db_migrations[] = {
    {1, "initial_schema", fpv_db_migration_1_sql},
    {2, "chat_cache", fpv_db_migration_2_sql},
};

fpv_result_t fpv_db_migrate(fpv_db_t* db) {
  if (!db) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_db_exec(
      db,
      "CREATE TABLE IF NOT EXISTS fpv_schema_migrations ("
      "  version INTEGER PRIMARY KEY,"
      "  name TEXT NOT NULL,"
      "  applied_at_ms BIGINT NOT NULL"
      ");");
  if (result != FPV_OK) {
    return result;
  }
  fpv_db_result_t* rows = NULL;
  result = fpv_db_query(
      db,
      "SELECT COALESCE(MAX(version), 0) FROM fpv_schema_migrations;",
      NULL,
      0,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  int current = 0;
  if (rows && rows->row_count > 0 && rows->column_count > 0 &&
      rows->rows && rows->rows[0] && rows->rows[0][0]) {
    fpv_db_parse_int(rows->rows[0][0], &current);
  }
  fpv_db_result_destroy(rows);

  for (size_t i = 0; i < sizeof(fpv_db_migrations) / sizeof(fpv_db_migrations[0]); i++) {
    const fpv_db_migration_t* migration = &fpv_db_migrations[i];
    if (!migration->sql || migration->version <= current) {
      continue;
    }
    result = fpv_db_begin(db);
    if (result != FPV_OK) {
      return result;
    }
    result = fpv_db_exec(db, migration->sql);
    if (result != FPV_OK) {
      fpv_db_rollback(db);
      return result;
    }
    char version_buf[32];
    char applied_buf[32];
    snprintf(version_buf, sizeof(version_buf), "%d", migration->version);
    snprintf(applied_buf, sizeof(applied_buf), "%" PRIu64, fpv_time_now_ms());
    const char* params[] = {version_buf, migration->name, applied_buf};
    result = fpv_db_exec_params(
        db,
        "INSERT INTO fpv_schema_migrations (version, name, applied_at_ms)"
        " VALUES ($1, $2, $3);",
        params,
        3);
    if (result != FPV_OK) {
      fpv_db_rollback(db);
      return result;
    }
    result = fpv_db_commit(db);
    if (result != FPV_OK) {
      fpv_db_rollback(db);
      return result;
    }
  }
  return FPV_OK;
}

fpv_result_t fpv_db_backup(fpv_db_t* db, const char* path) {
  if (!db || !path || !path[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_set_error(db, NULL);
#if defined(FPV_HAVE_SQLITE)
  if (db->backend == FPV_DB_BACKEND_SQLITE) {
    if (!db->sqlite) {
      fpv_db_set_error(db, "SQLite connection not available.");
      return FPV_ERR_INVALID_STATE;
    }
    sqlite3_exec(db->sqlite, "PRAGMA wal_checkpoint(FULL);", NULL, NULL, NULL);
    sqlite3* dest = NULL;
    if (sqlite3_open_v2(
            path,
            &dest,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            NULL) != SQLITE_OK) {
      const char* error = dest ? sqlite3_errmsg(dest) : "SQLite backup open failed.";
      fpv_db_set_error(db, error);
      if (dest) {
        sqlite3_close(dest);
      }
      return FPV_ERR_IO;
    }
    sqlite3_backup* backup = sqlite3_backup_init(dest, "main", db->sqlite, "main");
    if (!backup) {
      fpv_db_set_error(db, sqlite3_errmsg(dest));
      sqlite3_close(dest);
      return FPV_ERR_IO;
    }
    int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) {
      fpv_db_set_error(db, sqlite3_errmsg(dest));
      sqlite3_close(dest);
      return FPV_ERR_IO;
    }
    if (sqlite3_close(dest) != SQLITE_OK) {
      fpv_db_set_error(db, "SQLite backup close failed.");
      return FPV_ERR_IO;
    }
    return FPV_OK;
  }
#endif
  fpv_db_set_error(db, "Backup not supported for this backend.");
  return FPV_ERR_UNSUPPORTED;
}

fpv_result_t fpv_db_restore(fpv_db_t* db, const char* path) {
  if (!db || !path || !path[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_set_error(db, NULL);
#if defined(FPV_HAVE_SQLITE)
  if (db->backend == FPV_DB_BACKEND_SQLITE) {
    if (!db->sqlite) {
      fpv_db_set_error(db, "SQLite connection not available.");
      return FPV_ERR_INVALID_STATE;
    }
    sqlite3* src = NULL;
    if (sqlite3_open_v2(path, &src, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
      const char* error = src ? sqlite3_errmsg(src) : "SQLite restore open failed.";
      fpv_db_set_error(db, error);
      if (src) {
        sqlite3_close(src);
      }
      return FPV_ERR_IO;
    }
    sqlite3_backup* backup = sqlite3_backup_init(db->sqlite, "main", src, "main");
    if (!backup) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      sqlite3_close(src);
      return FPV_ERR_IO;
    }
    int rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) {
      fpv_db_set_error(db, sqlite3_errmsg(db->sqlite));
      sqlite3_close(src);
      return FPV_ERR_IO;
    }
    sqlite3_exec(db->sqlite, "PRAGMA wal_checkpoint(FULL);", NULL, NULL, NULL);
    if (sqlite3_close(src) != SQLITE_OK) {
      fpv_db_set_error(db, "SQLite restore close failed.");
      return FPV_ERR_IO;
    }
    return FPV_OK;
  }
#endif
  fpv_db_set_error(db, "Restore not supported for this backend.");
  return FPV_ERR_UNSUPPORTED;
}
