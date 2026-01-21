/* FunPay Vertex database seed tool. */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_core/fpv_identity.h"
#include "fpv_core/fpv_models.h"
#include "core/data/fpv_db.h"

#include "core/base/fpv_time.h"


typedef struct fpv_seed_item {
  const char* title;
  const char* normalized_title;
  const char* category;
  const char* subcategory;
  const char* description;
  const char* tags[4];
  size_t tag_count;
} fpv_seed_item_t;

typedef struct fpv_seed_listing {
  size_t item_index;
  const char* title;
  const char* category;
  const char* subcategory;
  const char* status;
  double price;
  const char* delivery_type;
  uint32_t quantity;
  const char* description;
  const char* tags[4];
  size_t tag_count;
} fpv_seed_listing_t;

static const fpv_seed_item_t fpv_seed_items[] = {
    {"CS2 Prime Accounts",
     "cs2 prime accounts",
     "CS2",
     "Accounts",
     "Prime accounts with email access and ranked-ready stats.",
     {"prime", "accounts", "ranked"},
     3},
    {"Valorant Points",
     "valorant points",
     "Valorant",
     "Points",
     "Instant VP top-ups delivered within minutes.",
     {"vp", "instant", "topup"},
     3},
    {"Steam Gift Cards",
     "steam gift cards",
     "Steam",
     "Gift Cards",
     "Global Steam wallet codes for all regions.",
     {"gift", "wallet", "steam"},
     3}};

static const fpv_seed_listing_t fpv_seed_listings[] = {
    {0,
     "Prime Account - Verified Email",
     "CS2",
     "Accounts",
     "active",
     8.99,
     "instant",
     15,
     "Full access prime accounts with email change.",
     {"prime", "email"},
     2},
    {1,
     "Valorant Points 1000 VP",
     "Valorant",
     "Points",
     "active",
     9.50,
     "instant",
     5000,
     "Fast VP delivery with order confirmation.",
     {"vp", "fast"},
     2},
    {2,
     "Steam Wallet 20 USD",
     "Steam",
     "Gift Cards",
     "active",
     18.00,
     "manual",
     250,
     "Digital wallet codes delivered after payment.",
     {"wallet", "gift"},
     2}};

static void fpv_db_seed_usage(const char* name) {
  fprintf(
      stderr,
      "Usage: %s [options]\n"
      "Options:\n"
      "  --db-url <url>          Database URL (postgres:// or sqlite://).\n"
      "  --data-dir <path>       Data directory (SQLite default location).\n"
      "  --admin-email <email>   Admin email (default: owner@local.dev).\n"
      "  --admin-password <pw>   Admin password (default: ChangeMe123!).\n"
      "  --admin-name <name>     Admin display name (default: Owner).\n"
      "  --org-name <name>       Organization name (default: Vertex Demo).\n"
      "  --team-name <name>      Team name (default: Default).\n"
      "  --account-id <id>       FunPay account id (default: seed_funpay_1).\n"
      "  --account-username <u>  FunPay username (default: seed_funpay).\n"
      "  --currency <code>       Currency code (default: USD).\n"
      "  --seed-catalog          Insert catalog, listings, and price data.\n"
      "  --force                 Seed even if data exists.\n"
      "Environment:\n"
      "  FPV_DB_URL  Database URL (postgres:// or sqlite://).\n",
      name ? name : "fpv_db_seed");
}

static void fpv_seed_set_env(const char* key, const char* value) {
  if (!key || !key[0]) {
    return;
  }
#ifdef _WIN32
  _putenv_s(key, value ? value : "");
#else
  if (value) {
    setenv(key, value, 1);
  } else {
    unsetenv(key);
  }
#endif
}

static char* fpv_seed_strdup(const char* value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char* copy = (char*)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len);
  copy[len] = '\0';
  return copy;
}

static char* fpv_seed_normalize_email(const char* email) {
  if (!email) {
    return NULL;
  }
  const char* start = email;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)*(end - 1))) {
    end--;
  }
  size_t len = (size_t)(end - start);
  char* normalized = (char*)malloc(len + 1);
  if (!normalized) {
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    normalized[i] = (char)tolower((unsigned char)start[i]);
  }
  normalized[len] = '\0';
  return normalized;
}

static bool fpv_seed_query_exists(fpv_db_t* db, const char* sql, bool* out_exists) {
  if (out_exists) {
    *out_exists = false;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(db, sql, NULL, 0, &rows);
  if (result != FPV_OK) {
    fprintf(stderr, "Query failed (code %d): %s\n", result, fpv_db_error(db));
    fpv_db_result_destroy(rows);
    return false;
  }
  bool exists = rows && rows->row_count > 0;
  fpv_db_result_destroy(rows);
  if (out_exists) {
    *out_exists = exists;
  }
  return true;
}

static fpv_result_t fpv_seed_fetch_id(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count,
    char** out_id) {
  if (out_id) {
    *out_id = NULL;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(db, sql, params, param_count, &rows);
  if (result != FPV_OK) {
    fpv_db_result_destroy(rows);
    return result;
  }
  if (rows && rows->row_count > 0 && rows->rows && rows->rows[0] &&
      rows->rows[0][0]) {
    if (out_id) {
      *out_id = fpv_seed_strdup(rows->rows[0][0]);
      if (!*out_id) {
        fpv_db_result_destroy(rows);
        return FPV_ERR_OUT_OF_MEMORY;
      }
    }
  }
  fpv_db_result_destroy(rows);
  return FPV_OK;
}

static bool fpv_seed_exec(
    fpv_db_t* db,
    const char* sql,
    const char* const* params,
    size_t param_count) {
  fpv_result_t result = fpv_db_exec_params(db, sql, params, param_count);
  if (result != FPV_OK) {
    fprintf(stderr, "Seed failed (code %d): %s\n", result, fpv_db_error(db));
    return false;
  }
  return true;
}

static fpv_result_t fpv_seed_insert_catalog(
    fpv_db_t* db,
    const char* org_id,
    const char* account_id,
    const char* currency) {
  if (!db || !org_id || !account_id || !currency) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_result_t result = fpv_db_begin(db);
  if (result != FPV_OK) {
    return result;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char now_buf[32];
  snprintf(now_buf, sizeof(now_buf), "%llu", (unsigned long long)now_ms);

  for (size_t i = 0; i < sizeof(fpv_seed_items) / sizeof(fpv_seed_items[0]); i++) {
    char item_id[128];
    snprintf(item_id, sizeof(item_id), "seed_item_%s_%zu", org_id, i + 1);
    const fpv_seed_item_t* item = &fpv_seed_items[i];
    const char* params[] = {
        item_id,
        org_id,
        item->title,
        item->normalized_title,
        item->category,
        item->subcategory,
        item->description,
        now_buf,
        now_buf};
    if (!fpv_seed_exec(
            db,
            "INSERT INTO fpv_items "
            "(id, organization_id, title, normalized_title, category, "
            "subcategory, description, created_at_ms, updated_at_ms) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
            "ON CONFLICT(id) DO NOTHING;",
            params,
            9)) {
      fpv_db_rollback(db);
      return FPV_ERR_IO;
    }
    for (size_t t = 0; t < item->tag_count; t++) {
      const char* tag_params[] = {item_id, item->tags[t]};
      if (!fpv_seed_exec(
              db,
              "INSERT INTO fpv_item_tags (item_id, tag) "
              "VALUES ($1, $2) ON CONFLICT(item_id, tag) DO NOTHING;",
              tag_params,
              2)) {
        fpv_db_rollback(db);
        return FPV_ERR_IO;
      }
    }
  }

  for (size_t i = 0; i < sizeof(fpv_seed_listings) / sizeof(fpv_seed_listings[0]); i++) {
    char listing_id[128];
    char item_id[128];
    snprintf(listing_id, sizeof(listing_id), "seed_listing_%s_%zu", org_id, i + 1);
    snprintf(item_id, sizeof(item_id), "seed_item_%s_%zu", org_id,
             fpv_seed_listings[i].item_index + 1);
    char price_buf[32];
    char quantity_buf[16];
    snprintf(price_buf, sizeof(price_buf), "%.2f", fpv_seed_listings[i].price);
    snprintf(quantity_buf, sizeof(quantity_buf), "%u", fpv_seed_listings[i].quantity);
    const char* listing_params[] = {
        listing_id,
        item_id,
        account_id,
        fpv_seed_listings[i].title,
        fpv_seed_listings[i].category,
        fpv_seed_listings[i].subcategory,
        fpv_seed_listings[i].status,
        price_buf,
        currency,
        quantity_buf,
        fpv_seed_listings[i].delivery_type,
        now_buf,
        fpv_seed_listings[i].description};
    if (!fpv_seed_exec(
            db,
            "INSERT INTO fpv_listings "
            "(id, item_id, account_id, title, category, subcategory, status, "
            "price, currency, quantity, delivery_type, last_updated_ms, description) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
            "ON CONFLICT(id) DO NOTHING;",
            listing_params,
            13)) {
      fpv_db_rollback(db);
      return FPV_ERR_IO;
    }
    for (size_t t = 0; t < fpv_seed_listings[i].tag_count; t++) {
      const char* tag_params[] = {listing_id, fpv_seed_listings[i].tags[t]};
      if (!fpv_seed_exec(
              db,
              "INSERT INTO fpv_listing_tags (listing_id, tag) "
              "VALUES ($1, $2) ON CONFLICT(listing_id, tag) DO NOTHING;",
              tag_params,
              2)) {
        fpv_db_rollback(db);
        return FPV_ERR_IO;
      }
    }

    for (size_t c = 0; c < 2; c++) {
      char competitor_id[128];
      char price_comp[32];
      char available_buf[8];
      char rating_buf[16];
      char last_seen_buf[32];
      double comp_price = fpv_seed_listings[i].price + (c == 0 ? -0.4 : 0.6);
      snprintf(competitor_id, sizeof(competitor_id),
               "seed_comp_%s_%zu_%zu", org_id, i + 1, c + 1);
      snprintf(price_comp, sizeof(price_comp), "%.2f", comp_price);
      snprintf(available_buf, sizeof(available_buf), "%d", 1);
      snprintf(rating_buf, sizeof(rating_buf), "%.2f", c == 0 ? 4.75 : 4.60);
      snprintf(last_seen_buf, sizeof(last_seen_buf), "%llu",
               (unsigned long long)(now_ms - (c + 1) * 60000ULL));
      const char* comp_params[] = {
          competitor_id,
          item_id,
          c == 0 ? "comp_seller_1" : "comp_seller_2",
          price_comp,
          currency,
          available_buf,
          fpv_seed_listings[i].delivery_type,
          rating_buf,
          last_seen_buf,
          c == 0 ? "https://funpay.com/lot/123" : "https://funpay.com/lot/456"};
      if (!fpv_seed_exec(
              db,
              "INSERT INTO fpv_competitor_listings "
              "(id, item_id, seller_id, price, currency, available, delivery_type, "
              "seller_rating, last_seen_ms, listing_url) "
              "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) "
              "ON CONFLICT(id) DO NOTHING;",
              comp_params,
              10)) {
        fpv_db_rollback(db);
        return FPV_ERR_IO;
      }
    }

    char rule_id[128];
    char scope_buf[8];
    char min_margin_buf[16];
    char min_price_buf[16];
    char undercut_type_buf[8];
    char undercut_value_buf[16];
    char enabled_buf[8];
    snprintf(rule_id, sizeof(rule_id), "seed_rule_listing_%s_%zu", org_id, i + 1);
    snprintf(scope_buf, sizeof(scope_buf), "%d", FPV_PRICE_SCOPE_LISTING);
    snprintf(min_margin_buf, sizeof(min_margin_buf), "%.2f", 0.10);
    snprintf(min_price_buf, sizeof(min_price_buf), "%.2f", 1.00);
    snprintf(undercut_type_buf, sizeof(undercut_type_buf), "%d", FPV_UNDERCUT_ABSOLUTE);
    snprintf(undercut_value_buf, sizeof(undercut_value_buf), "%.2f", 0.05);
    snprintf(enabled_buf, sizeof(enabled_buf), "%d", 1);
    const char* rule_params[] = {
        rule_id,
        scope_buf,
        org_id,
        NULL,
        NULL,
        listing_id,
        min_margin_buf,
        min_price_buf,
        undercut_type_buf,
        undercut_value_buf,
        enabled_buf,
        now_buf,
        now_buf};
    if (!fpv_seed_exec(
            db,
            "INSERT INTO fpv_price_rules "
            "(id, scope, organization_id, team_id, item_id, listing_id, "
            "min_margin, min_price, undercut_type, undercut_value, enabled, "
            "created_at_ms, updated_at_ms) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
            "ON CONFLICT(id) DO NOTHING;",
            rule_params,
            13)) {
      fpv_db_rollback(db);
      return FPV_ERR_IO;
    }

    char history_id[128];
    char median_buf[16];
    char recommended_buf[16];
    char applied_buf[16];
    snprintf(history_id, sizeof(history_id), "seed_history_%s_%zu", org_id, i + 1);
    snprintf(median_buf, sizeof(median_buf), "%.2f", fpv_seed_listings[i].price - 0.3);
    snprintf(recommended_buf, sizeof(recommended_buf), "%.2f", fpv_seed_listings[i].price - 0.2);
    snprintf(applied_buf, sizeof(applied_buf), "%.2f", fpv_seed_listings[i].price - 0.1);
    const char* history_params[] = {
        history_id,
        listing_id,
        rule_id,
        median_buf,
        recommended_buf,
        applied_buf,
        currency,
        "seed",
        now_buf};
    if (!fpv_seed_exec(
            db,
            "INSERT INTO fpv_price_history "
            "(id, listing_id, price_rule_id, competitor_median, "
            "recommended_price, applied_price, currency, reason, created_at_ms) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
            "ON CONFLICT(id) DO NOTHING;",
            history_params,
            9)) {
      fpv_db_rollback(db);
      return FPV_ERR_IO;
    }
  }

  char org_rule_id[128];
  char scope_buf[8];
  char min_margin_buf[16];
  char min_price_buf[16];
  char undercut_type_buf[8];
  char undercut_value_buf[16];
  char enabled_buf[8];
  snprintf(org_rule_id, sizeof(org_rule_id), "seed_rule_org_%s", org_id);
  snprintf(scope_buf, sizeof(scope_buf), "%d", FPV_PRICE_SCOPE_ORGANIZATION);
  snprintf(min_margin_buf, sizeof(min_margin_buf), "%.2f", 0.12);
  snprintf(min_price_buf, sizeof(min_price_buf), "%.2f", 2.00);
  snprintf(undercut_type_buf, sizeof(undercut_type_buf), "%d", FPV_UNDERCUT_PERCENT);
  snprintf(undercut_value_buf, sizeof(undercut_value_buf), "%.2f", 2.50);
  snprintf(enabled_buf, sizeof(enabled_buf), "%d", 1);
  const char* org_rule_params[] = {
      org_rule_id,
      scope_buf,
      org_id,
      NULL,
      NULL,
      NULL,
      min_margin_buf,
      min_price_buf,
      undercut_type_buf,
      undercut_value_buf,
      enabled_buf,
      now_buf,
      now_buf};
  if (!fpv_seed_exec(
          db,
          "INSERT INTO fpv_price_rules "
          "(id, scope, organization_id, team_id, item_id, listing_id, "
          "min_margin, min_price, undercut_type, undercut_value, enabled, "
          "created_at_ms, updated_at_ms) "
          "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
          "ON CONFLICT(id) DO NOTHING;",
          org_rule_params,
          13)) {
    fpv_db_rollback(db);
    return FPV_ERR_IO;
  }

  result = fpv_db_commit(db);
  if (result != FPV_OK) {
    fpv_db_rollback(db);
    return result;
  }
  return FPV_OK;
}

int main(int argc, char* argv[]) {
  const char* db_url = NULL;
  const char* data_dir = NULL;
  const char* admin_email = "owner@local.dev";
  const char* admin_password = "ChangeMe123!";
  const char* admin_name = "Owner";
  const char* org_name = "Vertex Demo";
  const char* team_name = "Default";
  const char* account_id = "seed_funpay_1";
  const char* account_username = "seed_funpay";
  const char* currency = "USD";
  bool seed_catalog = false;
  bool force = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--db-url") == 0 && i + 1 < argc) {
      db_url = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
      data_dir = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--admin-email") == 0 && i + 1 < argc) {
      admin_email = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--admin-password") == 0 && i + 1 < argc) {
      admin_password = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--admin-name") == 0 && i + 1 < argc) {
      admin_name = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--org-name") == 0 && i + 1 < argc) {
      org_name = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--team-name") == 0 && i + 1 < argc) {
      team_name = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--account-id") == 0 && i + 1 < argc) {
      account_id = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--account-username") == 0 && i + 1 < argc) {
      account_username = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--currency") == 0 && i + 1 < argc) {
      currency = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--seed-catalog") == 0) {
      seed_catalog = true;
      continue;
    }
    if (strcmp(argv[i], "--force") == 0) {
      force = true;
      continue;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      fpv_db_seed_usage(argv[0]);
      return 0;
    }
    fprintf(stderr, "Unknown argument: %s\n", argv[i]);
    fpv_db_seed_usage(argv[0]);
    return 1;
  }

  if (!db_url || !db_url[0]) {
    db_url = getenv("FPV_DB_URL");
  }
  if ((!db_url || !db_url[0]) && (!data_dir || !data_dir[0])) {
    fpv_db_seed_usage(argv[0]);
    return 1;
  }

  if (db_url && db_url[0]) {
    fpv_seed_set_env("FPV_DB_URL", db_url);
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

  if (!force) {
    bool has_users = false;
    bool has_orgs = false;
    if (!fpv_seed_query_exists(db, "SELECT 1 FROM fpv_users LIMIT 1;", &has_users) ||
        !fpv_seed_query_exists(db, "SELECT 1 FROM fpv_organizations LIMIT 1;", &has_orgs)) {
      fpv_db_close(db);
      return 1;
    }
    if (has_users || has_orgs) {
      fprintf(stderr, "Database already contains data. Use --force to seed.\n");
      fpv_db_close(db);
      return 1;
    }
  }

  fpv_identity_store_t* store = fpv_identity_store_open(data_dir, &result);
  if (!store || result != FPV_OK) {
    fprintf(stderr, "Identity store open failed (code %d).\n", result);
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }

  char* normalized_email = fpv_seed_normalize_email(admin_email);
  if (!normalized_email) {
    fprintf(stderr, "Failed to normalize email.\n");
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }

  char* user_id = NULL;
  const char* email_params[] = {normalized_email};
  result = fpv_seed_fetch_id(
      db,
      "SELECT id FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
      email_params,
      1,
      &user_id);
  if (result != FPV_OK) {
    fprintf(stderr, "User lookup failed (code %d): %s\n", result, fpv_db_error(db));
    free(normalized_email);
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }

  if (!user_id) {
    fpv_user_t* user = NULL;
    result = fpv_identity_create_user(
        store,
        admin_email,
        admin_name,
        admin_password,
        true,
        &user);
    if (result != FPV_OK && result != FPV_ERR_INVALID_STATE) {
      fprintf(stderr, "User creation failed (code %d).\n", result);
      fpv_user_destroy(user);
      free(normalized_email);
      fpv_identity_store_destroy(store);
      fpv_db_close(db);
      return 1;
    }
    if (result == FPV_OK) {
      user_id = fpv_seed_strdup(user->id);
    } else {
      result = fpv_seed_fetch_id(
          db,
          "SELECT id FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
          email_params,
          1,
          &user_id);
      if (result != FPV_OK || !user_id) {
        fprintf(stderr, "User lookup failed after creation.\n");
        fpv_user_destroy(user);
        free(normalized_email);
        fpv_identity_store_destroy(store);
        fpv_db_close(db);
        return 1;
      }
    }
    fpv_user_destroy(user);
  }

  char* org_id = NULL;
  const char* org_params[] = {org_name};
  result = fpv_seed_fetch_id(
      db,
      "SELECT id FROM fpv_organizations WHERE name = $1 LIMIT 1;",
      org_params,
      1,
      &org_id);
  if (result != FPV_OK) {
    fprintf(stderr, "Organization lookup failed (code %d): %s\n", result, fpv_db_error(db));
    free(org_id);
    free(user_id);
    free(normalized_email);
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }

  char* team_id = NULL;
  if (!org_id) {
    fpv_organization_t* org = NULL;
    fpv_team_t* team = NULL;
    fpv_user_role_t* role = NULL;
    result = fpv_identity_create_organization(
        store,
        org_name,
        NULL,
        currency,
        NULL,
        user_id,
        &org,
        &team,
        &role);
    if (result != FPV_OK || !org || !team) {
      fprintf(stderr, "Organization creation failed (code %d).\n", result);
      fpv_organization_destroy(org);
      fpv_team_destroy(team);
      fpv_user_role_destroy(role);
      free(org_id);
      free(team_id);
      free(user_id);
      free(normalized_email);
      fpv_identity_store_destroy(store);
      fpv_db_close(db);
      return 1;
    }
    org_id = fpv_seed_strdup(org->id);
    team_id = fpv_seed_strdup(team->id);
    fpv_organization_destroy(org);
    fpv_team_destroy(team);
    fpv_user_role_destroy(role);
  } else {
    const char* team_params[] = {org_id, team_name};
    result = fpv_seed_fetch_id(
        db,
        "SELECT id FROM fpv_teams WHERE organization_id = $1 AND name = $2 LIMIT 1;",
        team_params,
        2,
        &team_id);
    if (result != FPV_OK) {
      fprintf(stderr, "Team lookup failed (code %d): %s\n", result, fpv_db_error(db));
      free(org_id);
      free(user_id);
      free(normalized_email);
      fpv_identity_store_destroy(store);
      fpv_db_close(db);
      return 1;
    }
    if (!team_id) {
      fpv_team_t* team = NULL;
      result = fpv_identity_create_team(store, org_id, team_name, true, &team);
      if (result != FPV_OK || !team) {
        fprintf(stderr, "Team creation failed (code %d).\n", result);
        fpv_team_destroy(team);
        free(org_id);
        free(user_id);
        free(normalized_email);
        fpv_identity_store_destroy(store);
        fpv_db_close(db);
        return 1;
      }
      team_id = fpv_seed_strdup(team->id);
      fpv_team_destroy(team);
    }
  }

  fpv_user_role_t* role = NULL;
  result = fpv_identity_assign_role(
      store,
      user_id,
      org_id,
      team_id,
      FPV_ROLE_OWNER,
      &role);
  if (result != FPV_OK && result != FPV_ERR_INVALID_STATE) {
    fprintf(stderr, "Role assignment failed (code %d).\n", result);
    fpv_user_role_destroy(role);
    free(team_id);
    free(org_id);
    free(user_id);
    free(normalized_email);
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }
  fpv_user_role_destroy(role);

  fpv_account_t* account = NULL;
  result = fpv_identity_get_account_for_org(store, org_id, &account);
  if (result != FPV_OK && result != FPV_ERR_NOT_FOUND) {
    fprintf(stderr, "Account lookup failed (code %d).\n", result);
    fpv_account_destroy(account);
    free(team_id);
    free(org_id);
    free(user_id);
    free(normalized_email);
    fpv_identity_store_destroy(store);
    fpv_db_close(db);
    return 1;
  }

  char* account_db_id = NULL;
  if (result == FPV_OK) {
    account_db_id = fpv_seed_strdup(account->id);
  } else {
    result = fpv_identity_link_account(
        store,
        org_id,
        team_id,
        account_id,
        account_username,
        account_username,
        currency,
        &account);
    if (result != FPV_OK || !account) {
      fprintf(stderr, "Account link failed (code %d).\n", result);
      fpv_account_destroy(account);
      free(team_id);
      free(org_id);
      free(user_id);
      free(normalized_email);
      fpv_identity_store_destroy(store);
      fpv_db_close(db);
      return 1;
    }
    account_db_id = fpv_seed_strdup(account->id);
  }
  fpv_account_destroy(account);

  if (seed_catalog && account_db_id) {
    result = fpv_seed_insert_catalog(db, org_id, account_db_id, currency);
    if (result != FPV_OK) {
      fprintf(stderr, "Catalog seed failed (code %d): %s\n", result, fpv_db_error(db));
      free(account_db_id);
      free(team_id);
      free(org_id);
      free(user_id);
      free(normalized_email);
      fpv_identity_store_destroy(store);
      fpv_db_close(db);
      return 1;
    }
  }

  free(account_db_id);
  free(team_id);
  free(org_id);
  free(user_id);
  free(normalized_email);
  fpv_identity_store_destroy(store);
  fpv_db_close(db);
  printf("Seed complete.\n");
  return 0;
}
