/* FunPay Vertex database restore tool. */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/data/fpv_db.h"


#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

static bool fpv_has_prefix(const char* value, const char* prefix) {
  size_t len = 0;
  if (!value || !prefix) {
    return false;
  }
  len = strlen(prefix);
  return strncmp(value, prefix, len) == 0;
}

static bool fpv_has_scheme(const char* value) {
  if (!value) {
    return false;
  }
  return strstr(value, "://") != NULL;
}

static bool fpv_is_postgres_url(const char* url) {
  return fpv_has_prefix(url, "postgres://") ||
         fpv_has_prefix(url, "postgresql://");
}

static int fpv_spawn(const char* file, char* const argv[]) {
#ifdef _WIN32
  int result = _spawnvp(_P_WAIT, file, (const char* const*)argv);
  return result == -1 ? errno : result;
#else
  pid_t pid;
  int status = 0;
  if (posix_spawnp(&pid, file, NULL, NULL, argv, environ) != 0) {
    return errno ? errno : 1;
  }
  if (waitpid(pid, &status, 0) < 0) {
    return errno ? errno : 1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 1;
#endif
}

static void fpv_db_restore_usage(const char* name) {
  fprintf(
      stderr,
      "Usage: %s --input <path> [--db-url <url>] [--data-dir <path>]\n"
      "Environment:\n"
      "  FPV_DB_URL  Database URL (postgres:// or sqlite://).\n",
      name ? name : "fpv_db_restore");
}

int main(int argc, char* argv[]) {
  const char* db_url = NULL;
  const char* data_dir = NULL;
  const char* input = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--db-url") == 0 && i + 1 < argc) {
      db_url = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
      data_dir = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      fpv_db_restore_usage(argv[0]);
      return 0;
    }
    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    fpv_db_restore_usage(argv[0]);
    return 1;
  }

  if (!input || !input[0]) {
    fpv_db_restore_usage(argv[0]);
    return 1;
  }

  if (!db_url || !db_url[0]) {
    db_url = getenv("FPV_DB_URL");
  }

  if (db_url && fpv_has_scheme(db_url) &&
      !fpv_is_postgres_url(db_url) &&
      !fpv_has_prefix(db_url, "sqlite://") &&
      !fpv_has_prefix(db_url, "file:")) {
    fprintf(stderr, "Unsupported database URL scheme.\n");
    return 1;
  }

  if (db_url && fpv_is_postgres_url(db_url)) {
    char* args[] = {
        (char*)"pg_restore",
        (char*)"--clean",
        (char*)"--if-exists",
        (char*)"--no-owner",
        (char*)"--no-acl",
        (char*)"--exit-on-error",
        (char*)"--dbname",
        (char*)db_url,
        (char*)input,
        NULL};
    int rc = fpv_spawn("pg_restore", args);
    if (rc != 0) {
      fprintf(stderr, "pg_restore failed with code %d.\n", rc);
      return 1;
    }
  } else {
    if ((!db_url || !db_url[0]) && (!data_dir || !data_dir[0])) {
      fpv_db_restore_usage(argv[0]);
      return 1;
    }
    fpv_db_config_t config;
    memset(&config, 0, sizeof(config));
    config.url = db_url;
    config.data_dir = data_dir;

    fpv_db_t* db = NULL;
    fpv_result_t result = fpv_db_open(&config, &db);
    if (result != FPV_OK) {
      fprintf(stderr, "Database open failed (code %d).\n", result);
      fpv_db_close(db);
      return 1;
    }
    result = fpv_db_restore(db, input);
    if (result != FPV_OK) {
      fprintf(stderr, "Restore failed (code %d): %s\n", result, fpv_db_error(db));
      fpv_db_close(db);
      return 1;
    }
    result = fpv_db_migrate(db);
    if (result != FPV_OK) {
      fprintf(stderr, "Migration failed (code %d): %s\n", result, fpv_db_error(db));
      fpv_db_close(db);
      return 1;
    }
    fpv_db_close(db);
  }

  if (db_url && fpv_is_postgres_url(db_url)) {
    fpv_db_config_t config;
    memset(&config, 0, sizeof(config));
    config.url = db_url;
    fpv_db_t* db = NULL;
    fpv_result_t result = fpv_db_open(&config, &db);
    if (result == FPV_OK) {
      result = fpv_db_migrate(db);
      if (result != FPV_OK) {
        fprintf(stderr, "Migration failed (code %d): %s\n", result, fpv_db_error(db));
        fpv_db_close(db);
        return 1;
      }
      fpv_db_close(db);
    } else {
      fprintf(stderr, "Migration skipped (code %d).\n", result);
    }
  }

  printf("Restore complete.\n");
  return 0;
}
