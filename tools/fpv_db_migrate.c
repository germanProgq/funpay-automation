/* FunPay Vertex database migration tool. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/data/fpv_db.h"


static void fpv_db_migrate_usage(const char* name) {
  fprintf(
      stderr,
      "Usage: %s [--db-url <url>] [--data-dir <path>]\n"
      "Environment:\n"
      "  FPV_DB_URL  Database URL (postgres:// or sqlite://).\n",
      name ? name : "fpv_db_migrate");
}

int main(int argc, char* argv[]) {
  const char* db_url = NULL;
  const char* data_dir = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--db-url") == 0 && i + 1 < argc) {
      db_url = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
      data_dir = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      fpv_db_migrate_usage(argv[0]);
      return 0;
    }
    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    fpv_db_migrate_usage(argv[0]);
    return 1;
  }

  if (!db_url || !db_url[0]) {
    db_url = getenv("FPV_DB_URL");
  }
  if ((!db_url || !db_url[0]) && (!data_dir || !data_dir[0])) {
    fpv_db_migrate_usage(argv[0]);
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

  result = fpv_db_migrate(db);
  if (result != FPV_OK) {
    fprintf(stderr, "Migration failed (code %d): %s\n", result, fpv_db_error(db));
    fpv_db_close(db);
    return 1;
  }

  fpv_db_close(db);
  printf("Migrations applied.\n");
  return 0;
}
