#include "funpay/core/fpv_funpay_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static fpv_result_t fpv_funpay_open_session_db(
    fpv_funpay_account_t* account,
    const fpv_funpay_account_config_t* config) {
  if (!account || !config) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* db_url = getenv("FPV_DB_URL");
  fpv_db_config_t db_config;
  memset(&db_config, 0, sizeof(db_config));
  db_config.url = db_url;
  db_config.data_dir = config->data_dir;

  fpv_db_t* db = NULL;
  fpv_result_t result = fpv_db_open(&db_config, &db);
  if (result != FPV_OK) {
    return result;
  }
  result = fpv_db_migrate(db);
  if (result != FPV_OK) {
    fpv_db_close(db);
    return result;
  }
  account->session_db = db;
  return FPV_OK;
}


static void fpv_funpay_session_load(fpv_funpay_account_t* account) {
  if (!account || !account->session_db || !account->golden_key ||
      !account->golden_key[0]) {
    return;
  }
  const char* params[] = {account->golden_key};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(
      account->session_db,
      "SELECT phpsessid FROM fpv_funpay_sessions "
      "WHERE golden_key = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK || !rows) {
    fpv_db_result_destroy(rows);
    return;
  }
  if (rows->row_count > 0 && rows->column_count > 0 &&
      rows->rows && rows->rows[0]) {
    const char* value = rows->rows[0][0];
    fpv_free(account->phpsessid);
    account->phpsessid = (value && value[0]) ? fpv_strdup(value) : NULL;
  }
  fpv_db_result_destroy(rows);
}


static void fpv_funpay_session_store(fpv_funpay_account_t* account) {
  if (!account || !account->session_db || !account->golden_key ||
      !account->golden_key[0]) {
    return;
  }
  if (!account->phpsessid || !account->phpsessid[0]) {
    const char* params[] = {account->golden_key};
    fpv_db_exec_params(
        account->session_db,
        "DELETE FROM fpv_funpay_sessions WHERE golden_key = $1;",
        params,
        1);
    return;
  }
  char updated_buf[32];
  snprintf(updated_buf, sizeof(updated_buf), "%" PRIu64, fpv_time_now_ms());
  const char* params[] = {account->golden_key, account->phpsessid, updated_buf};
  fpv_db_exec_params(
      account->session_db,
      "INSERT INTO fpv_funpay_sessions (golden_key, phpsessid, updated_at_ms) "
      "VALUES ($1, $2, $3) "
      "ON CONFLICT (golden_key) DO UPDATE SET "
      "phpsessid = excluded.phpsessid, updated_at_ms = excluded.updated_at_ms;",
      params,
      3);
}


fpv_result_t fpv_funpay_account_request(
    fpv_funpay_account_t* account,
    const char* method,
    const char* api_method,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const char* body,
    bool exclude_phpsessid,
    bool raise_not_200,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error) {
  if (!account || !method || !api_method || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char* url = fpv_funpay_build_url(api_method);
  if (!url) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_string_builder_t cookie_builder;
  fpv_funpay_sb_reset(&cookie_builder);
  fpv_funpay_sb_append(&cookie_builder, "golden_key=");
  fpv_funpay_sb_append(&cookie_builder, account->golden_key);
  if (account->phpsessid && !exclude_phpsessid) {
    fpv_funpay_sb_append(&cookie_builder, "; PHPSESSID=");
    fpv_funpay_sb_append(&cookie_builder, account->phpsessid);
  }
  char* cookie = fpv_funpay_sb_detach(&cookie_builder);
  if (!cookie) {
    fpv_free(url);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_result_t result = FPV_ERR_IO;
  const int max_429_retries = 10;
  int attempts = 0;

  while (true) {
    fpv_funpay_http_response_t local_response;
    local_response.status = 0;
    local_response.body = NULL;
    local_response.body_size = 0;
    local_response.phpsessid = NULL;
    fpv_mutex_lock(&account->request_mutex);
    result = fpv_funpay_http_request(
        account->http,
        method,
        url,
        cookie,
        headers,
        header_count,
        body,
        &local_response,
        error);
    fpv_mutex_unlock(&account->request_mutex);
    if (result != FPV_OK) {
      fpv_funpay_http_response_clear(&local_response);
      break;
    }

    if (local_response.status == 429 && attempts < max_429_retries) {
      fpv_funpay_http_response_clear(&local_response);
#if defined(_WIN32)
      Sleep(400);
#else
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 400000000L;
      nanosleep(&ts, NULL);
#endif
      attempts++;
      continue;
    }
    if (local_response.status == 429) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_RATE_LIMIT,
          "Rate limited",
          url,
          method,
          local_response.status);
      fpv_funpay_http_response_clear(&local_response);
      result = FPV_ERR_IO;
      break;
    }

    if (local_response.phpsessid) {
      fpv_free(account->phpsessid);
      account->phpsessid = fpv_strdup(local_response.phpsessid);
      fpv_funpay_session_store(account);
    }

    if (local_response.status == 403) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_UNAUTHORIZED,
          "Unauthorized",
          url,
          method,
          local_response.status);
      fpv_funpay_http_response_clear(&local_response);
      result = FPV_ERR_INVALID_STATE;
      break;
    }

    if (raise_not_200 && local_response.status != 200) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Request failed",
          url,
          method,
          local_response.status);
      fpv_funpay_http_response_clear(&local_response);
      result = FPV_ERR_IO;
      break;
    }

    *response = local_response;
    result = FPV_OK;
    break;
  }

  fpv_free(cookie);
  fpv_free(url);
  return result;
}


static fpv_result_t fpv_funpay_account_fetch_lot_form(
    fpv_funpay_account_t* account,
    const char* url,
    bool check_missing,
    fpv_funpay_form_t* out_form,
    fpv_funpay_error_t* error) {
  if (!account || !url || !out_form) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  bool html_has_secrets = response.body &&
      strstr(response.body, "textarea-lot-secrets") != NULL;
  bool html_has_auto_delivery = response.body &&
      strstr(response.body, "auto_delivery") != NULL;
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot secrets fetch: status=%ld html_secrets=%d html_auto_delivery=%d",
      response.status,
      html_has_secrets ? 1 : 0,
      html_has_auto_delivery ? 1 : 0);

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        url,
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, NULL, "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  if (check_missing) {
    xmlNode* header_node =
        fpv_html_find_first_by_class(root, "h1", "page-header");
    if (header_node) {
      char* header_text = fpv_html_node_text(header_node);
      if (header_text &&
          strcmp(header_text,
                 "\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB4\xD0\xBB\xD0\xBE\xD0\xB6"
                 "\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBD\xD0\xB5 "
                 "\xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xBE") == 0) {
        fpv_free(header_text);
        fpv_funpay_http_response_clear(&response);
        fpv_html_destroy(doc);
        fpv_funpay_error_set(
            error,
            FPV_FUNPAY_ERR_NOT_FOUND,
            "Lot not found",
            url,
            "GET",
            response.status);
        return FPV_ERR_NOT_FOUND;
      }
      fpv_free(header_text);
    }
  }

  fpv_funpay_form_t form;
  memset(&form, 0, sizeof(form));
  bool ok = fpv_funpay_form_parse_from_html(root, &form);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  if (!ok) {
    fpv_funpay_form_clear(&form);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  *out_form = form;
  return FPV_OK;
}

static bool fpv_funpay_parse_href_id(
    const char* href,
    uint64_t* out_id);
static bool fpv_funpay_parse_lot_section_id(
    const char* href,
    uint64_t* out_id);


static void fpv_funpay_format_price_value(
    double price,
    char* buffer,
    size_t size) {
  if (!buffer || size == 0) {
    return;
  }
  snprintf(buffer, size, "%.2f", price);
  size_t len = strlen(buffer);
  while (len > 0 && buffer[len - 1] == '0') {
    buffer[--len] = '\0';
  }
  if (len > 0 && buffer[len - 1] == '.') {
    buffer[--len] = '\0';
  }
  if (len == 0) {
    snprintf(buffer, size, "0");
  }
}


static char* fpv_funpay_normalize_lot_url(const char* input) {
  if (!input || !input[0]) {
    return NULL;
  }
  char* trimmed = fpv_strdup(input);
  if (!trimmed) {
    return NULL;
  }
  fpv_funpay_trim(trimmed);
  if (!trimmed[0]) {
    fpv_free(trimmed);
    return NULL;
  }
  if (strncmp(trimmed, "http://", 7) == 0 ||
      strncmp(trimmed, "https://", 8) == 0) {
    return trimmed;
  }

  bool all_digits = true;
  for (const char* ptr = trimmed; *ptr; ptr++) {
    if (!isdigit((unsigned char)*ptr)) {
      all_digits = false;
      break;
    }
  }
  if (all_digits) {
    char buffer[192];
    snprintf(
        buffer,
        sizeof(buffer),
        "https://funpay.com/lots/offer?id=%s",
        trimmed);
    fpv_free(trimmed);
    return fpv_strdup(buffer);
  }

  if (strncmp(trimmed, "funpay.com", 10) == 0) {
    size_t size = strlen(trimmed) + 9;
    char* url = (char*)malloc(size);
    if (!url) {
      fpv_free(trimmed);
      return NULL;
    }
    snprintf(url, size, "https://%s", trimmed);
    fpv_free(trimmed);
    return url;
  }

  char* url = fpv_funpay_build_url(trimmed);
  fpv_free(trimmed);
  return url;
}


static bool fpv_funpay_is_user_profile_url(const char* url) {
  if (!url || !url[0]) {
    return false;
  }
  if (strstr(url, "/users/")) {
    return true;
  }
  return strncmp(url, "users/", 6) == 0 || strncmp(url, "/users/", 7) == 0;
}


static fpv_result_t fpv_funpay_resolve_profile_lot_url(
    fpv_funpay_account_t* account,
    const char* profile_url,
    char** out_lot_url,
    fpv_funpay_error_t* error) {
  if (out_lot_url) {
    *out_lot_url = NULL;
  }
  if (!account || !profile_url || !out_lot_url) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      profile_url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  long status = response.status;
  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse profile HTML",
        profile_url,
        "GET",
        status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  fpv_html_node_list_t links = fpv_funpay_find_all_tag(root, "a");
  for (size_t i = 0; i < links.count; i++) {
    xmlNode* link = links.nodes[i];
    char* href = fpv_html_node_attr(link, "href");
    if (!href) {
      continue;
    }
    char* class_attr = fpv_html_node_attr(link, "class");
    bool class_match = class_attr && strstr(class_attr, "tc-item") != NULL;
    bool offer_match =
        strstr(href, "/lots/offer") || strstr(href, "lots/offer") ||
        strstr(href, "/chips/offer") || strstr(href, "chips/offer") ||
        strstr(href, "offer?id=") || strstr(href, "offer=");
    if (class_match || offer_match) {
      char* built = fpv_funpay_build_url(href);
      if (!built) {
        fpv_free(href);
        fpv_free(class_attr);
        fpv_html_node_list_destroy(&links);
        fpv_funpay_http_response_clear(&response);
        fpv_html_destroy(doc);
        return FPV_ERR_OUT_OF_MEMORY;
      }
      *out_lot_url = built;
      fpv_free(href);
      fpv_free(class_attr);
      break;
    }
    fpv_free(href);
    fpv_free(class_attr);
  }

  fpv_html_node_list_destroy(&links);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  if (!*out_lot_url) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_NOT_FOUND,
        "No lots found on profile",
        profile_url,
        "GET",
        status);
    return FPV_ERR_NOT_FOUND;
  }

  return FPV_OK;
}

typedef struct fpv_funpay_public_lot {
  char* title;
  char* description;
  double price;
  bool has_price;
  uint64_t subcategory_id;
} fpv_funpay_public_lot_t;


static void fpv_funpay_public_lot_clear(fpv_funpay_public_lot_t* lot) {
  if (!lot) {
    return;
  }
  fpv_free(lot->title);
  fpv_free(lot->description);
  lot->title = NULL;
  lot->description = NULL;
  lot->price = 0.0;
  lot->has_price = false;
  lot->subcategory_id = 0;
}


static char* fpv_funpay_clone_extract_text(xmlNode* root) {
  if (!root) {
    return NULL;
  }
  char* text = fpv_html_node_text(root);
  if (text) {
    fpv_funpay_trim(text);
    if (!text[0]) {
      fpv_free(text);
      text = NULL;
    }
  }
  return text;
}


static char* fpv_funpay_clone_extract_title(xmlNode* root) {
  xmlNode* node =
      fpv_html_find_first_by_class(root, "h1", "offer-title");
  if (!node) {
    node = fpv_html_find_first_by_class(root, "h1", "lot-title");
  }
  if (!node) {
    node = fpv_html_find_first_by_class(root, "h1", "page-title");
  }
  if (!node) {
    node = fpv_html_find_first_by_class(root, "h1", "page-header");
  }
  if (!node) {
    node = fpv_funpay_find_first_by_class_substr(root, NULL, "offer-title");
  }
  if (!node) {
    node = fpv_funpay_find_first_by_class_substr(root, NULL, "lot-title");
  }
  if (!node) {
    node = fpv_funpay_find_first_by_class_substr(root, NULL, "page-title");
  }
  if (!node) {
    node = fpv_funpay_find_first_by_class_substr(root, NULL, "page-header");
  }
  if (!node) {
    node = fpv_funpay_find_first_tag(root, "h1");
  }
  return node ? fpv_funpay_clone_extract_text(node) : NULL;
}


static char* fpv_funpay_clone_extract_description(xmlNode* root) {
  const char* classes[] = {
      "offer-description",
      "lot-description",
      "offer-desc",
      "lot-text",
      "tc-desc",
      "description"};
  for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
    xmlNode* node = fpv_html_find_first_by_class(root, "div", classes[i]);
    if (node) {
      char* text = fpv_funpay_clone_extract_text(node);
      if (text) {
        return text;
      }
    }
  }
  xmlNode* node = fpv_html_find_first_by_class(root, "p", "offer-desc");
  return node ? fpv_funpay_clone_extract_text(node) : NULL;
}


static bool fpv_funpay_clone_extract_price_from_node(
    xmlNode* node,
    double* out_price) {
  if (!node || !out_price) {
    return false;
  }
  char* data = fpv_html_node_attr(node, "data-price");
  if (data && data[0]) {
    double parsed = 0.0;
    const char* currency = NULL;
    bool ok = fpv_funpay_parse_price(data, &parsed, &currency);
    fpv_free(data);
    if (ok) {
      *out_price = parsed;
      return true;
    }
  }
  fpv_free(data);

  char* text = fpv_html_node_text(node);
  if (!text) {
    return false;
  }
  fpv_funpay_trim(text);
  double parsed = 0.0;
  const char* currency = NULL;
  bool ok = fpv_funpay_parse_price(text, &parsed, &currency);
  fpv_free(text);
  if (!ok) {
    return false;
  }
  *out_price = parsed;
  return true;
}


static bool fpv_funpay_parse_first_factor(const char* data, double* out_value) {
  if (!data || !out_value) {
    return false;
  }
  char* end = NULL;
  double parsed = strtod(data, &end);
  if (!end || end == data) {
    return false;
  }
  *out_value = parsed;
  return true;
}


static bool fpv_funpay_clone_extract_price_from_payment_options(
    xmlNode* root,
    const char* preferred_currency,
    double* out_price) {
  if (!root || !out_price) {
    return false;
  }
  fpv_html_node_list_t options = fpv_funpay_find_all_tag(root, "option");
  bool has_fallback = false;
  double fallback_price = 0.0;
  for (size_t i = 0; i < options.count; i++) {
    xmlNode* node = options.nodes[i];
    if (!node) {
      continue;
    }
    char* factors = fpv_html_node_attr(node, "data-factors");
    if (!factors || !factors[0]) {
      fpv_free(factors);
      continue;
    }
    fpv_funpay_trim(factors);
    double parsed = 0.0;
    bool ok = fpv_funpay_parse_first_factor(factors, &parsed);
    fpv_free(factors);
    if (!ok || parsed <= 0.0) {
      continue;
    }
    char* cy = fpv_html_node_attr(node, "data-cy");
    const char* currency = fpv_funpay_currency_from_cy(cy);
    fpv_free(cy);
    if (!currency) {
      char* unit = fpv_html_node_attr(node, "data-unit");
      currency = fpv_funpay_currency_from_symbol(unit);
      fpv_free(unit);
    }
    if (!has_fallback) {
      fallback_price = parsed;
      has_fallback = true;
    }
    if (preferred_currency && currency &&
        strcmp(currency, preferred_currency) == 0) {
      *out_price = parsed;
      fpv_html_node_list_destroy(&options);
      return true;
    }
    if (!preferred_currency && currency) {
      *out_price = parsed;
      fpv_html_node_list_destroy(&options);
      return true;
    }
  }
  fpv_html_node_list_destroy(&options);
  if (has_fallback) {
    *out_price = fallback_price;
    return true;
  }
  return false;
}


static bool fpv_funpay_clone_extract_price(
    xmlNode* root,
    const char* preferred_currency,
    double* out_price) {
  if (!root || !out_price) {
    return false;
  }
  xmlNode* node = fpv_html_find_first_by_class(root, "div", "tc-price");
  if (!node) {
    node = fpv_html_find_first_by_class(root, "span", "offer-price");
  }
  if (!node) {
    node = fpv_html_find_first_by_class(root, "div", "offer-price");
  }
  if (!node) {
    node = fpv_html_find_first_by_class(root, "span", "price");
  }
  if (node && fpv_funpay_clone_extract_price_from_node(node, out_price)) {
    return true;
  }

  node = fpv_funpay_find_first_by_class_substr(root, "span", "price");
  if (!node) {
    node = fpv_funpay_find_first_by_class_substr(root, "div", "price");
  }
  if (node && fpv_funpay_clone_extract_price_from_node(node, out_price)) {
    return true;
  }

  node = fpv_funpay_find_first_by_attr(root, NULL, "itemprop", "price");
  if (node) {
    char* content = fpv_html_node_attr(node, "content");
    if (content && content[0]) {
      double parsed = 0.0;
      const char* currency = NULL;
      bool ok = fpv_funpay_parse_price(content, &parsed, &currency);
      fpv_free(content);
      if (ok) {
        *out_price = parsed;
        return true;
      }
    }
    fpv_free(content);
    if (fpv_funpay_clone_extract_price_from_node(node, out_price)) {
      return true;
    }
  }

  const char* meta_props[] = {"product:price:amount", "og:price:amount"};
  for (size_t i = 0; i < sizeof(meta_props) / sizeof(meta_props[0]); i++) {
    node = fpv_funpay_find_first_by_attr(root, "meta", "property", meta_props[i]);
    if (!node) {
      continue;
    }
    char* content = fpv_html_node_attr(node, "content");
    if (content && content[0]) {
      double parsed = 0.0;
      const char* currency = NULL;
      bool ok = fpv_funpay_parse_price(content, &parsed, &currency);
      fpv_free(content);
      if (ok) {
        *out_price = parsed;
        return true;
      }
    }
    fpv_free(content);
  }

  node = fpv_funpay_find_first_by_attr(root, "meta", "name", "price");
  if (node) {
    char* content = fpv_html_node_attr(node, "content");
    if (content && content[0]) {
      double parsed = 0.0;
      const char* currency = NULL;
      bool ok = fpv_funpay_parse_price(content, &parsed, &currency);
      fpv_free(content);
      if (ok) {
        *out_price = parsed;
        return true;
      }
    }
    fpv_free(content);
  }

  const char* price_attrs[] = {
      "data-price",
      "data-price-rub",
      "data-price-usd",
      "data-price-eur",
      "data-amount",
      "data-offer-price",
      "data-price-value",
      "data-cost"};
  fpv_html_node_list_t price_nodes =
      fpv_funpay_find_all_by_attrs(
          root,
          NULL,
          price_attrs,
          sizeof(price_attrs) / sizeof(price_attrs[0]));
  for (size_t i = 0; i < price_nodes.count; i++) {
    xmlNode* price_node = price_nodes.nodes[i];
    if (!price_node) {
      continue;
    }
    for (size_t j = 0; j < sizeof(price_attrs) / sizeof(price_attrs[0]); j++) {
      char* value = fpv_html_node_attr(price_node, price_attrs[j]);
      if (!value || !value[0]) {
        fpv_free(value);
        continue;
      }
      double parsed = 0.0;
      const char* currency = NULL;
      bool ok = fpv_funpay_parse_price(value, &parsed, &currency);
      fpv_free(value);
      if (ok) {
        fpv_html_node_list_destroy(&price_nodes);
        *out_price = parsed;
        return true;
      }
    }
  }
  fpv_html_node_list_destroy(&price_nodes);

  if (fpv_funpay_clone_extract_price_from_payment_options(
          root,
          preferred_currency,
          out_price)) {
    return true;
  }

  return false;
}


static bool fpv_funpay_is_json_ld_type(const char* type) {
  if (!type || !type[0]) {
    return false;
  }
  const char* needle = "application/ld+json";
  if (!fpv_funpay_is_ascii(type)) {
    return strstr(type, needle) != NULL;
  }
  char* lowered = fpv_funpay_ascii_lower(type);
  if (!lowered) {
    return false;
  }
  bool ok = strstr(lowered, needle) != NULL;
  fpv_free(lowered);
  return ok;
}


static bool fpv_funpay_public_lot_parse_price_json(
    const fpv_json_value_t* value,
    double* out_price) {
  if (!value || !out_price) {
    return false;
  }
  double parsed = 0.0;
  if (fpv_json_number_to_double(value, &parsed)) {
    *out_price = parsed;
    return true;
  }
  if (fpv_json_is_type(value, FPV_JSON_STRING)) {
    const char* raw = fpv_json_string(value);
    const char* currency = NULL;
    if (raw && fpv_funpay_parse_price(raw, &parsed, &currency)) {
      *out_price = parsed;
      return true;
    }
    return false;
  }
  if (fpv_json_is_type(value, FPV_JSON_OBJECT)) {
    const char* keys[] = {
        "price",
        "lowPrice",
        "highPrice",
        "minPrice",
        "maxPrice"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
      if (fpv_funpay_public_lot_parse_price_json(
              fpv_json_object_get(value, keys[i]),
              out_price)) {
        return true;
      }
    }
    if (fpv_funpay_public_lot_parse_price_json(
            fpv_json_object_get(value, "priceSpecification"),
            out_price)) {
      return true;
    }
    return false;
  }
  if (fpv_json_is_type(value, FPV_JSON_ARRAY)) {
    size_t count = fpv_json_array_size(value);
    for (size_t i = 0; i < count; i++) {
      if (fpv_funpay_public_lot_parse_price_json(
              fpv_json_array_get(value, i),
              out_price)) {
        return true;
      }
    }
    return false;
  }
  return false;
}


static void fpv_funpay_public_lot_apply_offer_json(
    const fpv_json_value_t* value,
    fpv_funpay_public_lot_t* lot) {
  if (!value || !lot) {
    return;
  }
  if (!fpv_json_is_type(value, FPV_JSON_OBJECT)) {
    return;
  }
  if (!lot->title) {
    const char* name = fpv_json_string(fpv_json_object_get(value, "name"));
    if (name && name[0]) {
      lot->title = fpv_strdup(name);
    }
  }
  if (!lot->description) {
    const char* desc = fpv_json_string(fpv_json_object_get(value, "description"));
    if (desc && desc[0]) {
      lot->description = fpv_strdup(desc);
    }
  }
  if (!lot->has_price) {
    const fpv_json_value_t* offers = fpv_json_object_get(value, "offers");
    double parsed = 0.0;
    if (fpv_funpay_public_lot_parse_price_json(offers, &parsed) ||
        fpv_funpay_public_lot_parse_price_json(
            fpv_json_object_get(value, "price"),
            &parsed) ||
        fpv_funpay_public_lot_parse_price_json(
            fpv_json_object_get(value, "priceSpecification"),
            &parsed)) {
      lot->price = parsed;
      lot->has_price = true;
    }
  }
}


static void fpv_funpay_public_lot_fill_from_json_ld(
    xmlNode* root,
    fpv_funpay_public_lot_t* lot) {
  if (!root || !lot) {
    return;
  }
  fpv_html_node_list_t scripts = fpv_funpay_find_all_tag(root, "script");
  for (size_t i = 0; i < scripts.count; i++) {
    xmlNode* node = scripts.nodes[i];
    if (!node) {
      continue;
    }
    char* type = fpv_html_node_attr(node, "type");
    bool json_ld = fpv_funpay_is_json_ld_type(type);
    fpv_free(type);
    if (!json_ld) {
      continue;
    }
    char* text = fpv_html_node_text(node);
    if (!text) {
      continue;
    }
    fpv_funpay_trim(text);
    if (!text[0]) {
      fpv_free(text);
      continue;
    }
    fpv_json_value_t* json = NULL;
    fpv_json_error_t json_error;
    if (fpv_json_parse(text, strlen(text), &json, &json_error) == FPV_OK && json) {
      if (fpv_json_is_type(json, FPV_JSON_ARRAY)) {
        size_t count = fpv_json_array_size(json);
        for (size_t j = 0; j < count; j++) {
          fpv_funpay_public_lot_apply_offer_json(
              fpv_json_array_get(json, j),
              lot);
        }
      } else if (fpv_json_is_type(json, FPV_JSON_OBJECT)) {
        fpv_funpay_public_lot_apply_offer_json(json, lot);
        const fpv_json_value_t* graph = fpv_json_object_get(json, "@graph");
        if (fpv_json_is_type(graph, FPV_JSON_ARRAY)) {
          size_t count = fpv_json_array_size(graph);
          for (size_t j = 0; j < count; j++) {
            fpv_funpay_public_lot_apply_offer_json(
                fpv_json_array_get(graph, j),
                lot);
          }
        } else if (fpv_json_is_type(graph, FPV_JSON_OBJECT)) {
          fpv_funpay_public_lot_apply_offer_json(graph, lot);
        }
      } else {
        fpv_funpay_public_lot_apply_offer_json(json, lot);
      }
    }
    fpv_json_destroy(json);
    fpv_free(text);
  }
  fpv_html_node_list_destroy(&scripts);
}


static bool fpv_funpay_json_value_to_uint64(
    const fpv_json_value_t* value,
    uint64_t* out_value) {
  if (!value || !out_value) {
    return false;
  }
  if (fpv_json_number_to_uint64(value, out_value)) {
    return true;
  }
  const char* text = fpv_json_string(value);
  if (!text || !text[0]) {
    return false;
  }
  char* copy = fpv_strdup(text);
  if (!copy) {
    return false;
  }
  fpv_funpay_trim(copy);
  if (!copy[0]) {
    fpv_free(copy);
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(copy, &end, 10);
  bool ok = end && *end == '\0';
  fpv_free(copy);
  if (!ok) {
    return false;
  }
  *out_value = (uint64_t)parsed;
  return true;
}


static bool fpv_funpay_public_lot_extract_subcategory_from_json_value(
    const fpv_json_value_t* value,
    const uint64_t* allowed_ids,
    size_t allowed_count,
    uint64_t* out_id) {
  if (!value || !out_id) {
    return false;
  }
  if (fpv_json_is_type(value, FPV_JSON_STRING)) {
    const char* text = fpv_json_string(value);
    uint64_t parsed = 0;
    if (text && fpv_funpay_parse_lot_section_id(text, &parsed)) {
      if (parsed != 0 &&
          (allowed_count == 0 ||
           fpv_funpay_list_contains_u64(allowed_ids, allowed_count, parsed))) {
        *out_id = parsed;
        return true;
      }
    }
    return false;
  }
  if (fpv_json_is_type(value, FPV_JSON_ARRAY)) {
    size_t count = fpv_json_array_size(value);
    for (size_t i = 0; i < count; i++) {
      if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
              fpv_json_array_get(value, i),
              allowed_ids,
              allowed_count,
              out_id)) {
        return true;
      }
    }
    return false;
  }
  if (fpv_json_is_type(value, FPV_JSON_OBJECT)) {
    const char* keys[] = {
        "subcategory",
        "subcategoryId",
        "subCategoryId",
        "subcategory_id",
        "category",
        "categoryId",
        "category_id",
        "nodeId",
        "node_id",
        "gameId",
        "game_id"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
      const fpv_json_value_t* field = fpv_json_object_get(value, keys[i]);
      uint64_t parsed = 0;
      if (fpv_funpay_json_value_to_uint64(field, &parsed)) {
        if (parsed != 0 &&
            (allowed_count == 0 ||
             fpv_funpay_list_contains_u64(allowed_ids, allowed_count, parsed))) {
          *out_id = parsed;
          return true;
        }
      }
    }
    size_t count = fpv_json_object_size(value);
    for (size_t i = 0; i < count; i++) {
      if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
              fpv_json_object_value(value, i),
              allowed_ids,
              allowed_count,
              out_id)) {
        return true;
      }
    }
  }
  return false;
}


static bool fpv_funpay_public_lot_extract_subcategory_id_from_json_ld(
    xmlNode* root,
    const uint64_t* allowed_ids,
    size_t allowed_count,
    uint64_t* out_id) {
  if (!root || !out_id) {
    return false;
  }
  fpv_html_node_list_t scripts = fpv_funpay_find_all_tag(root, "script");
  for (size_t i = 0; i < scripts.count; i++) {
    xmlNode* node = scripts.nodes[i];
    if (!node) {
      continue;
    }
    char* type = fpv_html_node_attr(node, "type");
    bool json_ld = fpv_funpay_is_json_ld_type(type);
    fpv_free(type);
    if (!json_ld) {
      continue;
    }
    char* text = fpv_html_node_text(node);
    if (!text) {
      continue;
    }
    fpv_funpay_trim(text);
    if (!text[0]) {
      fpv_free(text);
      continue;
    }
    fpv_json_value_t* json = NULL;
    fpv_json_error_t json_error;
    bool found = false;
    if (fpv_json_parse(text, strlen(text), &json, &json_error) == FPV_OK && json) {
      if (fpv_json_is_type(json, FPV_JSON_ARRAY)) {
        size_t count = fpv_json_array_size(json);
        for (size_t j = 0; j < count; j++) {
          if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
                  fpv_json_array_get(json, j),
                  allowed_ids,
                  allowed_count,
                  out_id)) {
            found = true;
            break;
          }
        }
      } else if (fpv_json_is_type(json, FPV_JSON_OBJECT)) {
        if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
                json,
                allowed_ids,
                allowed_count,
                out_id)) {
          found = true;
        } else {
          const fpv_json_value_t* graph = fpv_json_object_get(json, "@graph");
          if (fpv_json_is_type(graph, FPV_JSON_ARRAY)) {
            size_t count = fpv_json_array_size(graph);
            for (size_t j = 0; j < count; j++) {
              if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
                      fpv_json_array_get(graph, j),
                      allowed_ids,
                      allowed_count,
                      out_id)) {
                found = true;
                break;
              }
            }
          } else if (fpv_json_is_type(graph, FPV_JSON_OBJECT)) {
            if (fpv_funpay_public_lot_extract_subcategory_from_json_value(
                    graph,
                    allowed_ids,
                    allowed_count,
                    out_id)) {
              found = true;
            }
          }
        }
      } else {
        found = fpv_funpay_public_lot_extract_subcategory_from_json_value(
            json,
            allowed_ids,
            allowed_count,
            out_id);
      }
    }
    fpv_json_destroy(json);
    fpv_free(text);
    if (found) {
      fpv_html_node_list_destroy(&scripts);
      return true;
    }
  }
  fpv_html_node_list_destroy(&scripts);
  return false;
}


static bool fpv_funpay_public_lot_extract_subcategory_id(
    xmlNode* root,
    uint64_t lot_id,
    const uint64_t* allowed_ids,
    size_t allowed_count,
    uint64_t* out_id) {
  if (!root || !out_id) {
    return false;
  }
  const char* attr_keys[] = {
      "data-subcategory-id",
      "data-subcategory",
      "data-subcat",
      "data-category-id",
      "data-category",
      "data-subcategoryId",
      "data-subcategory_id",
      "data-subcategoryid",
      "data-categoryId",
      "data-category_id",
      "data-node-id",
      "data-node_id",
      "data-nodeid",
      "data-game-id",
      "data-game_id",
      "data-gameid"};
  for (size_t i = 0; i < sizeof(attr_keys) / sizeof(attr_keys[0]); i++) {
    xmlNode* node = fpv_funpay_find_first_by_attr(root, NULL, attr_keys[i], NULL);
    if (!node) {
      continue;
    }
    uint64_t parsed = 0;
    if (fpv_funpay_parse_uint64_attr(node, attr_keys[i], &parsed)) {
      if (parsed == lot_id) {
        continue;
      }
      if (allowed_count == 0 || fpv_funpay_list_contains_u64(allowed_ids, allowed_count, parsed)) {
        *out_id = parsed;
        return true;
      }
    }
  }

  fpv_html_node_list_t links = fpv_funpay_find_all_tag(root, "a");
  for (size_t i = 0; i < links.count; i++) {
    xmlNode* node = links.nodes[i];
    if (!node) {
      continue;
    }
    char* href = fpv_html_node_attr(node, "href");
    if (!href || !href[0]) {
      fpv_free(href);
      continue;
    }
    if (strstr(href, "offer") || strstr(href, "orders") || strstr(href, "users")) {
      fpv_free(href);
      continue;
    }
    if (!strstr(href, "/lots/") && !strstr(href, "/chips/")) {
      fpv_free(href);
      continue;
    }
    uint64_t parsed = 0;
    bool ok = fpv_funpay_parse_lot_section_id(href, &parsed);
    fpv_free(href);
    if (!ok || parsed == 0 || parsed == lot_id) {
      continue;
    }
    if (allowed_count == 0 || fpv_funpay_list_contains_u64(allowed_ids, allowed_count, parsed)) {
      fpv_html_node_list_destroy(&links);
      *out_id = parsed;
      return true;
    }
  }
  fpv_html_node_list_destroy(&links);
  return fpv_funpay_public_lot_extract_subcategory_id_from_json_ld(
      root,
      allowed_ids,
      allowed_count,
      out_id);
}


static void fpv_funpay_account_clear_chats(fpv_funpay_account_t* account) {
  if (!account) {
    return;
  }
  for (size_t i = 0; i < account->chats.count; i++) {
    fpv_chat_destroy(account->chats.chats[i]);
  }
  fpv_free(account->chats.chats);
  account->chats.chats = NULL;
  account->chats.count = 0;
}


static void fpv_funpay_catalog_clear(fpv_funpay_account_t* account) {
  if (!account) {
    return;
  }
  for (size_t i = 0; i < account->catalog.category_count; i++) {
    fpv_free(account->catalog.categories[i].name);
  }
  for (size_t i = 0; i < account->catalog.subcategory_count; i++) {
    fpv_free(account->catalog.subcategories[i].name);
  }
  fpv_free(account->catalog.categories);
  fpv_free(account->catalog.subcategories);
  account->catalog.categories = NULL;
  account->catalog.subcategories = NULL;
  account->catalog.category_count = 0;
  account->catalog.subcategory_count = 0;
}


static fpv_funpay_category_t* fpv_funpay_catalog_find_category(
    fpv_funpay_account_t* account,
    uint64_t category_id) {
  if (!account) {
    return NULL;
  }
  for (size_t i = 0; i < account->catalog.category_count; i++) {
    if (account->catalog.categories[i].id == category_id) {
      return &account->catalog.categories[i];
    }
  }
  return NULL;
}


static fpv_funpay_subcategory_t* fpv_funpay_catalog_find_subcategory(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id) {
  if (!account) {
    return NULL;
  }
  for (size_t i = 0; i < account->catalog.subcategory_count; i++) {
    if (account->catalog.subcategories[i].id == subcategory_id) {
      return &account->catalog.subcategories[i];
    }
  }
  return NULL;
}


static bool fpv_funpay_catalog_add_category(
    fpv_funpay_account_t* account,
    uint64_t category_id,
    const char* name) {
  if (!account || !name || !name[0]) {
    return false;
  }
  if (fpv_funpay_catalog_find_category(account, category_id)) {
    return true;
  }
  fpv_funpay_category_t* grown = (fpv_funpay_category_t*)realloc(
      account->catalog.categories,
      (account->catalog.category_count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  account->catalog.categories = grown;
  fpv_funpay_category_t* entry =
      &account->catalog.categories[account->catalog.category_count++];
  entry->id = category_id;
  entry->name = fpv_strdup(name);
  return entry->name != NULL;
}


static bool fpv_funpay_catalog_add_subcategory(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id,
    fpv_funpay_subcategory_type_t type,
    uint64_t category_id,
    const char* name) {
  if (!account || !name || !name[0]) {
    return false;
  }
  if (fpv_funpay_catalog_find_subcategory(account, subcategory_id)) {
    return true;
  }
  fpv_funpay_subcategory_t* grown = (fpv_funpay_subcategory_t*)realloc(
      account->catalog.subcategories,
      (account->catalog.subcategory_count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  account->catalog.subcategories = grown;
  fpv_funpay_subcategory_t* entry =
      &account->catalog.subcategories[account->catalog.subcategory_count++];
  entry->id = subcategory_id;
  entry->type = type;
  entry->category_id = category_id;
  entry->name = fpv_strdup(name);
  return entry->name != NULL;
}


fpv_result_t fpv_funpay_account_store_chat(
    fpv_funpay_account_t* account,
    const fpv_chat_t* chat) {
  if (!account || !chat) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  for (size_t i = 0; i < account->chats.count; i++) {
    if (account->chats.chats[i] &&
        account->chats.chats[i]->id &&
        chat->id &&
        strcmp(account->chats.chats[i]->id, chat->id) == 0) {
      fpv_chat_destroy(account->chats.chats[i]);
      account->chats.chats[i] = fpv_chat_clone(chat);
      return account->chats.chats[i] ? FPV_OK : FPV_ERR_OUT_OF_MEMORY;
    }
  }

  fpv_chat_t** grown = (fpv_chat_t**)realloc(
      account->chats.chats,
      (account->chats.count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  account->chats.chats = grown;
  account->chats.chats[account->chats.count] = fpv_chat_clone(chat);
  if (!account->chats.chats[account->chats.count]) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  account->chats.count++;
  return FPV_OK;
}


fpv_chat_t** fpv_funpay_account_clone_chats(
    const fpv_funpay_account_t* account,
    size_t* count) {
  if (!account || !count || account->chats.count == 0) {
    if (count) {
      *count = 0;
    }
    return NULL;
  }
  fpv_chat_t** list =
      (fpv_chat_t**)calloc(account->chats.count, sizeof(*list));
  if (!list) {
    *count = 0;
    return NULL;
  }
  for (size_t i = 0; i < account->chats.count; i++) {
    list[i] = fpv_chat_clone(account->chats.chats[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_chat_destroy(list[j]);
      }
      free(list);
      *count = 0;
      return NULL;
    }
  }
  *count = account->chats.count;
  return list;
}


static bool fpv_funpay_parse_href_id(
    const char* href,
    uint64_t* out_id) {
  if (!href || !out_id) {
    return false;
  }
  const char* end = href + strlen(href);
  const char* suffix = strpbrk(href, "?#");
  if (suffix && suffix < end) {
    end = suffix;
  }
  while (end > href &&
         (end[-1] == '/' || isspace((unsigned char)end[-1]))) {
    end--;
  }
  const char* start = end;
  while (start > href && fpv_funpay_is_digit_ascii((unsigned char)start[-1])) {
    start--;
  }
  if (start == end) {
    return false;
  }
  char* temp = fpv_strdup_n(start, (size_t)(end - start));
  if (!temp) {
    return false;
  }
  char* parse_end = NULL;
  unsigned long long parsed = strtoull(temp, &parse_end, 10);
  bool ok = parse_end && *parse_end == '\0';
  fpv_free(temp);
  if (!ok) {
    return false;
  }
  *out_id = (uint64_t)parsed;
  return true;
}


static bool fpv_funpay_parse_last_uint64(
    const char* text,
    uint64_t* out_id) {
  if (!text || !out_id) {
    return false;
  }
  const char* end = text + strlen(text);
  const char* suffix = strpbrk(text, "?#");
  if (suffix && suffix < end) {
    end = suffix;
  }
  const char* cursor = end;
  while (cursor > text &&
         !fpv_funpay_is_digit_ascii((unsigned char)cursor[-1])) {
    cursor--;
  }
  const char* digit_end = cursor;
  while (cursor > text &&
         fpv_funpay_is_digit_ascii((unsigned char)cursor[-1])) {
    cursor--;
  }
  if (digit_end == cursor) {
    return false;
  }
  char* temp = fpv_strdup_n(cursor, (size_t)(digit_end - cursor));
  if (!temp) {
    return false;
  }
  char* parse_end = NULL;
  unsigned long long parsed = strtoull(temp, &parse_end, 10);
  bool ok = parse_end && *parse_end == '\0';
  fpv_free(temp);
  if (!ok) {
    return false;
  }
  *out_id = (uint64_t)parsed;
  return true;
}


static void fpv_funpay_log_href_debug(
    fpv_funpay_account_t* account,
    const char* context,
    const char* href) {
  if (!account || !href || !context) {
    return;
  }
  size_t len = strlen(href);
  char hex_buf[256];
  char ascii_buf[80];
  size_t max_bytes = len < 48 ? len : 48;
  size_t hex_pos = 0;
  size_t ascii_pos = 0;
  for (size_t i = 0; i < max_bytes && hex_pos + 4 < sizeof(hex_buf); i++) {
    unsigned char ch = (unsigned char)href[i];
    hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02X ", ch);
    if (ascii_pos + 1 < sizeof(ascii_buf)) {
      ascii_buf[ascii_pos++] = isprint(ch) ? (char)ch : '.';
    }
  }
  hex_buf[hex_pos] = '\0';
  ascii_buf[ascii_pos] = '\0';
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "%s href_len=%zu bytes=%s ascii=%s",
      context,
      len,
      hex_buf,
      ascii_buf);
}


static bool fpv_funpay_parse_query_param_u64(
    const char* href,
    const char* key,
    uint64_t* out_id) {
  if (!href || !key || !out_id) {
    return false;
  }
  const char* query = strchr(href, '?');
  if (!query) {
    return false;
  }
  const char* query_end = strchr(query + 1, '#');
  if (!query_end) {
    query_end = href + strlen(href);
  }
  size_t key_len = strlen(key);
  const char* cur = query + 1;
  while (cur < query_end) {
    const char* amp = strchr(cur, '&');
    if (!amp || amp > query_end) {
      amp = query_end;
    }
    if ((size_t)(amp - cur) > key_len &&
        strncmp(cur, key, key_len) == 0 &&
        cur[key_len] == '=') {
      const char* value = cur + key_len + 1;
      if (fpv_funpay_is_digit_ascii((unsigned char)*value)) {
        char* end = NULL;
        unsigned long long parsed = strtoull(value, &end, 10);
        if (end && end > value) {
          *out_id = (uint64_t)parsed;
          return true;
        }
      }
    }
    if (amp >= query_end) {
      break;
    }
    cur = amp + 1;
  }
  return false;
}


static bool fpv_funpay_parse_lot_section_id(
    const char* href,
    uint64_t* out_id) {
  if (!href || !out_id) {
    return false;
  }
  bool has_lots = strstr(href, "lots") != NULL;
  bool has_chips = strstr(href, "chips") != NULL;
  bool allow_query = (has_lots || has_chips) && !strstr(href, "offer");
  if (fpv_funpay_parse_href_id(href, out_id)) {
    return true;
  }
  const char* marker = strstr(href, "/lots/");
  size_t marker_len = 0;
  if (marker) {
    marker_len = strlen("/lots/");
  } else {
    marker = strstr(href, "/chips/");
    if (marker) {
      marker_len = strlen("/chips/");
    }
  }
  if (!marker) {
    if (strncmp(href, "lots/", strlen("lots/")) == 0) {
      marker = href;
      marker_len = strlen("lots/");
    } else if (strncmp(href, "chips/", strlen("chips/")) == 0) {
      marker = href;
      marker_len = strlen("chips/");
    }
  }
  if (!marker) {
    marker = strstr(href, "/lots");
    if (marker) {
      const char* tail = marker + strlen("/lots");
      if (*tail == '/' || *tail == '?' || *tail == '#' || *tail == '\0') {
        marker_len = strlen("/lots");
      } else {
        marker = NULL;
      }
    }
  }
  if (!marker) {
    marker = strstr(href, "/chips");
    if (marker) {
      const char* tail = marker + strlen("/chips");
      if (*tail == '/' || *tail == '?' || *tail == '#' || *tail == '\0') {
        marker_len = strlen("/chips");
      } else {
        marker = NULL;
      }
    }
  }
  if (!marker && strncmp(href, "lots", strlen("lots")) == 0) {
    const char* tail = href + strlen("lots");
    if (*tail == '/' || *tail == '?' || *tail == '#' || *tail == '\0') {
      marker = href;
      marker_len = strlen("lots");
    }
  }
  if (!marker && strncmp(href, "chips", strlen("chips")) == 0) {
    const char* tail = href + strlen("chips");
    if (*tail == '/' || *tail == '?' || *tail == '#' || *tail == '\0') {
      marker = href;
      marker_len = strlen("chips");
    }
  }
  if (!marker || marker_len == 0) {
    if (allow_query &&
        (fpv_funpay_parse_query_param_u64(href, "id", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "sub", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcat", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcategory", out_id))) {
      return true;
    }
    if (allow_query && fpv_funpay_parse_last_uint64(href, out_id)) {
      return true;
    }
    return false;
  }
  const char* cursor = marker + marker_len;
  if (*cursor == '/') {
    cursor++;
  }
  const char* limit = cursor;
  while (*limit && *limit != '?' && *limit != '#') {
    limit++;
  }
  const char* start = cursor;
  while (start < limit &&
         !fpv_funpay_is_digit_ascii((unsigned char)*start)) {
    start++;
  }
  if (start >= limit) {
    if (allow_query && fpv_funpay_parse_last_uint64(href, out_id)) {
      return true;
    }
    if (allow_query &&
        (fpv_funpay_parse_query_param_u64(href, "id", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "sub", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcat", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcategory", out_id))) {
      return true;
    }
    return false;
  }
  const char* end = start;
  while (end < limit &&
         fpv_funpay_is_digit_ascii((unsigned char)*end)) {
    end++;
  }
  char* temp = fpv_strdup_n(start, (size_t)(end - start));
  if (!temp) {
    return false;
  }
  char* parse_end = NULL;
  unsigned long long parsed = strtoull(temp, &parse_end, 10);
  bool ok = parse_end && *parse_end == '\0';
  fpv_free(temp);
  if (!ok) {
    if (allow_query && fpv_funpay_parse_last_uint64(href, out_id)) {
      return true;
    }
    if (allow_query &&
        (fpv_funpay_parse_query_param_u64(href, "id", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "sub", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcat", out_id) ||
         fpv_funpay_parse_query_param_u64(href, "subcategory", out_id))) {
      return true;
    }
    return false;
  }
  *out_id = (uint64_t)parsed;
  return true;
}

static bool fpv_funpay_parse_lot_id_from_text(
    const char* text,
    uint64_t* out_id) {
  if (!text || !out_id) {
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (end && *end == '\0') {
    *out_id = (uint64_t)parsed;
    return true;
  }
  return fpv_funpay_parse_last_uint64(text, out_id);
}


static bool fpv_funpay_parse_lot_id_from_href(
    const char* href,
    uint64_t* out_id) {
  if (!href || !out_id) {
    return false;
  }
  if (fpv_funpay_parse_query_param_u64(href, "id", out_id) ||
      fpv_funpay_parse_query_param_u64(href, "offer", out_id) ||
      fpv_funpay_parse_query_param_u64(href, "lot", out_id)) {
    return true;
  }
  if (fpv_funpay_parse_href_id(href, out_id)) {
    return true;
  }
  return fpv_funpay_parse_last_uint64(href, out_id);
}


static bool fpv_funpay_parse_lot_id_from_attr(
    xmlNode* node,
    const char* attr,
    uint64_t* out_id) {
  if (!node || !attr || !out_id) {
    return false;
  }
  char* value = fpv_html_node_attr(node, attr);
  if (!value) {
    return false;
  }
  bool ok = fpv_funpay_parse_lot_id_from_text(value, out_id);
  fpv_free(value);
  return ok;
}


static bool fpv_funpay_parse_lot_id(
    xmlNode* node,
    uint64_t* out_id) {
  if (!node || !out_id) {
    return false;
  }
  const char* attrs[] = {
      "data-offer",
      "data-offer-id",
      "data-offerid",
      "data-id",
      "data-lot-id",
      "data-lotid",
      "data-lot"};
  for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
    if (fpv_funpay_parse_lot_id_from_attr(node, attrs[i], out_id)) {
      return true;
    }
  }

  char* href = fpv_html_node_attr(node, "href");
  if (href) {
    bool ok = fpv_funpay_parse_lot_id_from_href(href, out_id);
    fpv_free(href);
    if (ok) {
      return true;
    }
  }
  char* data_href = fpv_html_node_attr(node, "data-href");
  if (data_href) {
    bool ok = fpv_funpay_parse_lot_id_from_href(data_href, out_id);
    fpv_free(data_href);
    if (ok) {
      return true;
    }
  }

  for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
    xmlNode* found =
        fpv_funpay_find_first_by_attr(node->children, NULL, attrs[i], NULL);
    if (found && fpv_funpay_parse_lot_id_from_attr(found, attrs[i], out_id)) {
      return true;
    }
  }

  xmlNode* link = fpv_funpay_find_first_tag(node->children, "a");
  if (link) {
    for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++) {
      if (fpv_funpay_parse_lot_id_from_attr(link, attrs[i], out_id)) {
        return true;
      }
    }
    href = fpv_html_node_attr(link, "href");
    if (href) {
      bool ok = fpv_funpay_parse_lot_id_from_href(href, out_id);
      fpv_free(href);
      if (ok) {
        return true;
      }
    }
    data_href = fpv_html_node_attr(link, "data-href");
    if (data_href) {
      bool ok = fpv_funpay_parse_lot_id_from_href(data_href, out_id);
      fpv_free(data_href);
      if (ok) {
        return true;
      }
    }
  }

  return false;
}


static xmlNode* fpv_funpay_find_lot_title_node(xmlNode* node) {
  if (!node) {
    return NULL;
  }
  const char* classes[] = {
      "tc-item-title",
      "offer-title",
      "lot-title",
      "offer-list-title",
      "lot-name",
      "tc-item-name",
      "item-title",
      "title",
      "name"};
  for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
    xmlNode* found = fpv_html_find_first_by_class(node, NULL, classes[i]);
    if (found) {
      return found;
    }
  }
  xmlNode* found = fpv_funpay_find_first_by_class_substr(node, NULL, "title");
  if (found) {
    return found;
  }
  found = fpv_funpay_find_first_by_class_substr(node, NULL, "name");
  if (found) {
    return found;
  }
  found = fpv_funpay_find_first_tag(node, "h3");
  if (!found) {
    found = fpv_funpay_find_first_tag(node, "h2");
  }
  if (!found) {
    found = fpv_funpay_find_first_tag(node, "h1");
  }
  if (found) {
    return found;
  }
  found = fpv_funpay_find_first_tag(node, "a");
  if (found) {
    return found;
  }
  found = fpv_funpay_find_first_tag(node, "div");
  if (found) {
    return found;
  }
  return fpv_funpay_find_first_tag(node, "span");
}


static void fpv_funpay_account_parse_catalog(
    fpv_funpay_account_t* account,
    xmlNode* root) {
  if (!account || !root) {
    return;
  }
  fpv_funpay_catalog_clear(account);

  fpv_html_node_list_t lists =
      fpv_html_find_all_by_class(root, "div", "promo-game-list");
  if (lists.count == 0) {
    fpv_html_node_list_destroy(&lists);
    return;
  }
  xmlNode* list_node =
      lists.count > 1 ? lists.nodes[1] : lists.nodes[0];
  fpv_html_node_list_destroy(&lists);
  if (!list_node) {
    return;
  }

  fpv_html_node_list_t items =
      fpv_html_find_all_by_class(list_node, "div", "promo-game-item");
  for (size_t i = 0; i < items.count; i++) {
    xmlNode* item = items.nodes[i];
    if (!item) {
      continue;
    }
    xmlNode* title_node =
        fpv_html_find_first_by_class(item->children, "div", "game-title");
    uint64_t base_id = 0;
    if (!fpv_funpay_parse_uint64_attr(title_node, "data-id", &base_id)) {
      continue;
    }
    xmlNode* link_node = NULL;
    if (title_node) {
      link_node = fpv_funpay_find_first_tag(title_node->children, "a");
    }
    if (!link_node) {
      link_node = fpv_funpay_find_first_tag(item->children, "a");
    }
    char* base_name = link_node ? fpv_html_node_text(link_node) : NULL;
    if (base_name) {
      fpv_funpay_trim(base_name);
      fpv_funpay_catalog_add_category(account, base_id, base_name);
    }

    xmlNode* region_node =
        fpv_funpay_find_first_by_attr(item->children, NULL, "role", "group");
    if (region_node && base_name) {
      fpv_html_node_list_t buttons =
          fpv_funpay_find_all_tag(region_node->children, "button");
      for (size_t b = 0; b < buttons.count; b++) {
        xmlNode* button = buttons.nodes[b];
        uint64_t region_id = 0;
        if (!fpv_funpay_parse_uint64_attr(button, "data-id", &region_id)) {
          continue;
        }
        char* region_text = fpv_html_node_text(button);
        if (region_text) {
          fpv_funpay_trim(region_text);
          fpv_string_builder_t builder;
          fpv_funpay_sb_reset(&builder);
          fpv_funpay_sb_append(&builder, base_name);
          fpv_funpay_sb_append(&builder, " (");
          fpv_funpay_sb_append(&builder, region_text);
          fpv_funpay_sb_append(&builder, ")");
          char* combined = fpv_funpay_sb_detach(&builder);
          if (combined) {
            fpv_funpay_catalog_add_category(account, region_id, combined);
          }
          fpv_free(combined);
          fpv_free(region_text);
        }
      }
      fpv_html_node_list_destroy(&buttons);
    }
    fpv_free(base_name);

    fpv_html_node_list_t sublists =
        fpv_html_find_all_by_class(item->children, "ul", "list-inline");
    for (size_t s = 0; s < sublists.count; s++) {
      xmlNode* list = sublists.nodes[s];
      uint64_t category_id = 0;
      if (!fpv_funpay_parse_uint64_attr(list, "data-id", &category_id)) {
        continue;
      }
      fpv_html_node_list_t items_li =
          fpv_funpay_find_all_tag(list->children, "li");
      for (size_t l = 0; l < items_li.count; l++) {
        xmlNode* li = items_li.nodes[l];
        xmlNode* link = fpv_funpay_find_first_tag(li->children, "a");
        if (!link) {
          continue;
        }
        char* href = fpv_html_node_attr(link, "href");
        char* name = fpv_html_node_text(link);
        if (name) {
          fpv_funpay_trim(name);
        }
        if (!href || !name) {
          fpv_free(href);
          fpv_free(name);
          continue;
        }
        fpv_funpay_subcategory_type_t type =
            strstr(href, "chips") ? FPV_FUNPAY_SUBCATEGORY_CURRENCY
                                  : FPV_FUNPAY_SUBCATEGORY_COMMON;
        uint64_t sub_id = 0;
        if (fpv_funpay_parse_lot_section_id(href, &sub_id)) {
          fpv_funpay_catalog_add_subcategory(
              account,
              sub_id,
              type,
              category_id,
              name);
        }
        fpv_free(href);
        fpv_free(name);
      }
      fpv_html_node_list_destroy(&items_li);
    }
    fpv_html_node_list_destroy(&sublists);
  }
  fpv_html_node_list_destroy(&items);
}


fpv_funpay_account_t* fpv_funpay_account_create(
    const fpv_funpay_account_config_t* config,
    fpv_funpay_error_t* error) {
  if (!config || !config->golden_key || !config->golden_key[0]) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        "Missing golden_key",
        NULL,
        NULL,
        0);
    return NULL;
  }

  fpv_funpay_account_t* account =
      (fpv_funpay_account_t*)calloc(1, sizeof(*account));
  if (!account) {
    return NULL;
  }

  account->golden_key = fpv_strdup(config->golden_key);
  account->user_agent = fpv_strdup(config->user_agent);
  account->http = fpv_funpay_http_client_create(
      config->user_agent,
      config->timeout_ms,
      &config->proxy,
      error);
  if (!account->golden_key || !account->http) {
    fpv_funpay_account_destroy(account);
    return NULL;
  }

  account->id = 0;
  account->last_update_ms = 0;
  account->initiated = false;
  account->chats.chats = NULL;
  account->chats.count = 0;
  account->catalog.categories = NULL;
  account->catalog.category_count = 0;
  account->catalog.subcategories = NULL;
  account->catalog.subcategory_count = 0;
  if (!fpv_mutex_init(&account->request_mutex)) {
    fpv_funpay_account_destroy(account);
    return NULL;
  }
  if (fpv_funpay_open_session_db(account, config) == FPV_OK) {
    fpv_funpay_session_load(account);
  }
  return account;
}


void fpv_funpay_account_destroy(fpv_funpay_account_t* account) {
  if (!account) {
    return;
  }
  fpv_free(account->golden_key);
  fpv_free(account->user_agent);
  fpv_funpay_http_client_destroy(account->http);
  fpv_db_close(account->session_db);
  fpv_free(account->csrf_token);
  fpv_free(account->phpsessid);
  fpv_free(account->username);
  fpv_free(account->currency);
  fpv_funpay_account_clear_chats(account);
  fpv_funpay_catalog_clear(account);
  fpv_mutex_destroy(&account->request_mutex);
  free(account);
}


void fpv_funpay_account_set_logger(
    fpv_funpay_account_t* account,
    fpv_logger_t* logger,
    bool debug_messages,
    fpv_event_bus_t* bus) {
  if (!account) {
    return;
  }
  account->logger = logger;
  account->debug_log_messages = debug_messages;
  account->bus = bus;
}


fpv_result_t fpv_funpay_account_refresh(
    fpv_funpay_account_t* account,
    fpv_funpay_error_t* error) {
  if (!account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      "https://funpay.com",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        "https://funpay.com",
        "GET",
        response.status);
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, NULL, "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        "https://funpay.com",
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  char* username = fpv_html_node_text(username_node);
  if (username) {
    fpv_funpay_trim(username);
  }

  uint32_t active_sales = 0;
  uint32_t active_purchases = 0;
  xmlNode* sales_node =
      fpv_html_find_first_by_class(root, "span", "badge-trade");
  if (sales_node) {
    char* sales_text = fpv_html_node_text(sales_node);
    if (sales_text) {
      fpv_funpay_trim(sales_text);
      active_sales = (uint32_t)strtoul(sales_text, NULL, 10);
      fpv_free(sales_text);
    }
  }
  xmlNode* purchases_node =
      fpv_html_find_first_by_class(root, "span", "badge-orders");
  if (purchases_node) {
    char* purchases_text = fpv_html_node_text(purchases_node);
    if (purchases_text) {
      fpv_funpay_trim(purchases_text);
      active_purchases = (uint32_t)strtoul(purchases_text, NULL, 10);
      fpv_free(purchases_text);
    }
  }

  xmlNode* body_node = fpv_html_find_first_by_class(root, "body", NULL);
  char* app_data = body_node ? fpv_html_node_attr(body_node, "data-app-data")
                             : NULL;
  fpv_json_value_t* app_json = NULL;
  fpv_json_error_t json_error;
  if (app_data) {
    fpv_result_t json_result =
        fpv_json_parse(app_data, strlen(app_data), &app_json, &json_error);
    if (json_result != FPV_OK) {
      fpv_free(app_data);
      fpv_free(username);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_PARSE,
          "Failed to parse account metadata",
          "https://funpay.com",
          "GET",
          response.status);
      return FPV_ERR_PARSE;
    }
  } else {
    fpv_free(username);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Missing account metadata",
        "https://funpay.com",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  uint64_t user_id = 0;
  const char* csrf = NULL;
  const fpv_json_value_t* id_value =
      fpv_json_object_get(app_json, "userId");
  if (!fpv_json_number_to_uint64(id_value, &user_id) || user_id == 0) {
    fpv_free(app_data);
    fpv_free(username);
    fpv_json_destroy(app_json);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Account ID missing",
        "https://funpay.com",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }
  const fpv_json_value_t* csrf_value =
      fpv_json_object_get(app_json, "csrf-token");
  csrf = fpv_json_string(csrf_value);
  if (!csrf || !csrf[0]) {
    fpv_free(app_data);
    fpv_free(username);
    fpv_json_destroy(app_json);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "CSRF token missing",
        "https://funpay.com",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  const char* currency = NULL;
  bool seen_rub = false;
  bool seen_usd = false;
  bool seen_eur = false;
  fpv_html_node_list_t currencies =
      fpv_html_find_all_by_class(root, "a", "user-cy-switcher");
  for (size_t i = 0; i < currencies.count; i++) {
    xmlNode* node = currencies.nodes[i];
    if (!fpv_html_node_has_class(node, "menu-item-currency")) {
      continue;
    }
    char* data = fpv_html_node_attr(node, "data-cy");
    if (!data) {
      continue;
    }
    if (strcmp(data, "rub") == 0) {
      seen_rub = true;
    } else if (strcmp(data, "usd") == 0) {
      seen_usd = true;
    } else if (strcmp(data, "eur") == 0) {
      seen_eur = true;
    }
    fpv_free(data);
  }
  fpv_html_node_list_destroy(&currencies);
  if (!seen_rub && seen_usd && seen_eur) {
    currency = "RUB";
  } else if (!seen_usd && seen_rub && seen_eur) {
    currency = "USD";
  } else if (!seen_eur && seen_rub && seen_usd) {
    currency = "EUR";
  }

  fpv_free(account->username);
  account->username = username;
  account->id = user_id;
  account->active_sales = active_sales;
  account->active_purchases = active_purchases;
  fpv_free(account->csrf_token);
  account->csrf_token = csrf ? fpv_strdup(csrf) : NULL;
  fpv_free(account->currency);
  account->currency = currency ? fpv_strdup(currency) : NULL;
  account->last_update_ms = fpv_time_now_ms();
  account->initiated = true;
  fpv_funpay_account_parse_catalog(account, root);

  fpv_free(app_data);
  fpv_json_destroy(app_json);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  return FPV_OK;
}


bool fpv_funpay_account_is_initiated(const fpv_funpay_account_t* account) {
  return account && account->initiated;
}


uint64_t fpv_funpay_account_id(const fpv_funpay_account_t* account) {
  return account ? account->id : 0;
}


const char* fpv_funpay_account_username(const fpv_funpay_account_t* account) {
  return account ? account->username : NULL;
}


const char* fpv_funpay_account_currency(const fpv_funpay_account_t* account) {
  return account ? account->currency : NULL;
}


uint32_t fpv_funpay_account_active_sales(const fpv_funpay_account_t* account) {
  return account ? account->active_sales : 0;
}


uint32_t fpv_funpay_account_active_purchases(const fpv_funpay_account_t* account) {
  return account ? account->active_purchases : 0;
}


uint64_t fpv_funpay_account_last_update_ms(const fpv_funpay_account_t* account) {
  return account ? account->last_update_ms : 0;
}


const char* fpv_funpay_account_csrf_token(const fpv_funpay_account_t* account) {
  return account ? account->csrf_token : NULL;
}


fpv_result_t fpv_funpay_account_set_golden_key(
    fpv_funpay_account_t* account,
    const char* golden_key) {
  if (!account || !golden_key || !golden_key[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* copy = fpv_strdup(golden_key);
  if (!copy) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_funpay_session_store(account);
  fpv_free(account->golden_key);
  account->golden_key = copy;
  account->initiated = false;
  account->id = 0;
  account->last_update_ms = 0;
  account->active_sales = 0;
  account->active_purchases = 0;
  fpv_free(account->csrf_token);
  account->csrf_token = NULL;
  fpv_free(account->phpsessid);
  account->phpsessid = NULL;
  fpv_free(account->username);
  account->username = NULL;
  fpv_free(account->currency);
  account->currency = NULL;
  fpv_funpay_account_clear_chats(account);
  fpv_funpay_catalog_clear(account);
  fpv_funpay_session_load(account);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_balance(
    fpv_funpay_account_t* account,
    fpv_funpay_balance_t* balance,
    fpv_funpay_error_t* error) {
  if (!account || !balance) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_funpay_balance_clear(balance);
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      "https://funpay.com/lots/offer?id=0",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse balance HTML",
        "https://funpay.com/lots/offer?id=0",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        "https://funpay.com/lots/offer?id=0",
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  xmlNode* select_node =
      fpv_funpay_find_first_by_attr(root, "select", "name", "method");
  if (!select_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Balance selector missing",
        "https://funpay.com/lots/offer?id=0",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-total-rub",
      &balance->total_rub);
  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-rub",
      &balance->available_rub);
  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-total-usd",
      &balance->total_usd);
  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-usd",
      &balance->available_usd);
  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-total-eur",
      &balance->total_eur);
  fpv_funpay_parse_double_attr(
      select_node,
      "data-balance-eur",
      &balance->available_eur);

  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_order_detail(
    fpv_funpay_account_t* account,
    const char* order_id,
    fpv_funpay_order_detail_t* detail,
    fpv_funpay_error_t* error) {
  if (!account || !order_id || !detail) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_funpay_order_detail_clear(detail);
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  size_t order_len = strlen(order_id);
  const char* prefix = "https://funpay.com/orders/";
  size_t prefix_len = strlen(prefix);
  char* url = (char*)malloc(prefix_len + order_len + 2);
  if (!url) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  memcpy(url, prefix, prefix_len);
  memcpy(url + prefix_len, order_id, order_len);
  url[prefix_len + order_len] = '/';
  url[prefix_len + order_len + 1] = '\0';

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  fpv_free(url);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse order HTML",
        "https://funpay.com/orders/",
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        "https://funpay.com/orders/",
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  detail->id = fpv_strdup(order_id);
  if (!detail->id) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  detail->status = FPV_ORDER_PAID;
  xmlNode* warning = fpv_html_find_first_by_class(root, "span", "text-warning");
  if (warning) {
    char* text = fpv_html_node_text(warning);
    if (text) {
      if (strstr(text, "Возврат") || strstr(text, "Refund")) {
        detail->status = FPV_ORDER_REFUNDED;
      }
      fpv_free(text);
    }
  }
  if (detail->status == FPV_ORDER_PAID) {
    xmlNode* success =
        fpv_html_find_first_by_class(root, "span", "text-success");
    if (success) {
      char* text = fpv_html_node_text(success);
      if (text) {
        if (strstr(text, "Закрыт") || strstr(text, "Closed")) {
          detail->status = FPV_ORDER_DELIVERED;
        }
        fpv_free(text);
      }
    }
  }

  xmlNode* chat_header =
      fpv_html_find_first_by_class(root, "div", "chat-header");
  if (chat_header) {
    xmlNode* name_node =
        fpv_html_find_first_by_class(chat_header->children, "div", "media-user-name");
    if (name_node) {
      xmlNode* link = fpv_funpay_find_first_tag(name_node->children, "a");
      if (link) {
        char* name = fpv_html_node_text(link);
        char* href = fpv_html_node_attr(link, "href");
        if (name) {
          fpv_funpay_trim(name);
          detail->buyer_username = name;
        }
        if (href) {
          fpv_funpay_parse_user_id_from_href(href, &detail->buyer_id);
          fpv_free(href);
        }
      }
    }
  }

  if (account->username) {
    detail->seller_username = fpv_strdup(account->username);
  }
  detail->seller_id = account->id;

  fpv_html_node_list_t params =
      fpv_html_find_all_by_class(root, "div", "param-item");
  for (size_t i = 0; i < params.count; i++) {
    xmlNode* node = params.nodes[i];
    xmlNode* header = fpv_funpay_find_first_tag(node->children, "h5");
    if (!header) {
      continue;
    }
    char* label = fpv_html_node_text(header);
    if (!label) {
      continue;
    }
    fpv_funpay_trim(label);
    if (strcmp(label, "Сумма") == 0 || strcmp(label, "Amount") == 0) {
      xmlNode* span = fpv_funpay_find_first_tag(node->children, "span");
      if (span) {
        char* price_text = fpv_html_node_text(span);
        if (price_text) {
          fpv_funpay_trim(price_text);
          const char* currency = NULL;
          double amount = 0.0;
          if (fpv_funpay_parse_price(price_text, &amount, &currency)) {
            detail->amount = amount;
            detail->currency = currency ? fpv_strdup(currency) : NULL;
          }
          fpv_free(price_text);
        }
      }
    } else if (strcmp(label, "Краткое описание") == 0 ||
               strcmp(label, "Short description") == 0) {
      xmlNode* value_node = fpv_funpay_find_first_tag(node->children, "div");
      if (value_node) {
        char* text = fpv_html_node_text(value_node);
        if (text) {
          fpv_funpay_trim(text);
          detail->title = text;
        }
      }
    } else if (strcmp(label, "Подробное описание") == 0 ||
               strcmp(label, "Description") == 0) {
      xmlNode* value_node = fpv_funpay_find_first_tag(node->children, "div");
      if (value_node) {
        char* text = fpv_html_node_text(value_node);
        if (text) {
          fpv_funpay_trim(text);
          detail->description = text;
        }
      }
    } else if (strcmp(label, "Количество") == 0 ||
               strcmp(label, "Quantity") == 0) {
      xmlNode* value_node = fpv_funpay_find_first_tag(node->children, "div");
      if (value_node) {
        char* text = fpv_html_node_text(value_node);
        if (text) {
          fpv_funpay_trim(text);
          char* end = NULL;
          unsigned long qty = strtoul(text, &end, 10);
          if (end && qty > 0) {
            detail->quantity = (uint32_t)qty;
          }
          fpv_free(text);
        }
      }
    }
    fpv_free(label);
  }
  fpv_html_node_list_destroy(&params);

  xmlNode* review_node =
      fpv_html_find_first_by_class(root, "div", "order-review");
  if (review_node) {
    xmlNode* rating_node =
        fpv_html_find_first_by_class(review_node->children, "div", "rating");
    if (rating_node) {
      char* class_value = fpv_html_node_attr(rating_node, "class");
      if (class_value) {
        detail->review.stars = fpv_funpay_parse_rating_from_class(class_value);
        fpv_free(class_value);
      }
    }
    xmlNode* text_node =
        fpv_html_find_first_by_class(review_node->children, "div", "review-item-text");
    if (text_node) {
      char* text = fpv_html_node_text(text_node);
      if (text) {
        fpv_funpay_trim(text);
        if (text[0] != '\0') {
          detail->review.text = text;
          detail->review.has_review = true;
        } else {
          fpv_free(text);
        }
      }
    }
    fpv_html_node_list_t replies =
        fpv_html_find_all_by_class(review_node->children, "div", "review-item-answer");
    for (size_t i = 0; i < replies.count; i++) {
      if (!fpv_html_node_has_class(replies.nodes[i], "review-compiled-reply")) {
        continue;
      }
      char* reply_text = fpv_html_node_text(replies.nodes[i]);
      if (reply_text) {
        fpv_funpay_trim(reply_text);
        if (reply_text[0] != '\0') {
          detail->review.reply = reply_text;
          detail->review.has_reply = true;
        } else {
          fpv_free(reply_text);
        }
      }
      break;
    }
    fpv_html_node_list_destroy(&replies);
    if (detail->review.stars > 0) {
      detail->review.has_review = true;
    }
  }

  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_send_review(
    fpv_funpay_account_t* account,
    const char* order_id,
    const char* text,
    int rating,
    fpv_funpay_error_t* error) {
  if (!account || !order_id || !text) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  if (rating < 1) {
    rating = 1;
  } else if (rating > 5) {
    rating = 5;
  }

  char author_buf[32];
  char rating_buf[8];
  snprintf(author_buf, sizeof(author_buf), "%" PRIu64, account->id);
  snprintf(rating_buf, sizeof(rating_buf), "%d", rating);

  const char* keys[] = {
      "authorId",
      "text",
      "rating",
      "csrf_token",
      "orderId"};
  const char* values[] = {
      author_buf,
      text,
      rating_buf,
      account->csrf_token,
      order_id};
  char* form = fpv_funpay_form_encode(keys, values, 5);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "orders/review",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      false,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    return result;
  }

  if (response.status == 400 || response.status != 200) {
    fpv_json_value_t* json = NULL;
    fpv_json_error_t json_error;
    char* message = NULL;
    if (fpv_json_parse(response.body, response.body_size, &json, &json_error) == FPV_OK) {
      const fpv_json_value_t* msg = fpv_json_object_get(json, "msg");
      if (fpv_json_string(msg)) {
        message = fpv_strdup(fpv_json_string(msg));
      }
    }
    fpv_json_destroy(json);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        message ? message : "Review request failed",
        "orders/review",
        "POST",
        response.status);
    fpv_free(message);
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_IO;
  }

  fpv_funpay_http_response_clear(&response);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_refund(
    fpv_funpay_account_t* account,
    const char* order_id,
    fpv_funpay_error_t* error) {
  if (!account || !order_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  const char* keys[] = {"id", "csrf_token"};
  const char* values[] = {order_id, account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 2);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "orders/refund",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      false,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  const fpv_json_value_t* error_val = fpv_json_object_get(json, "error");
  bool has_error = fpv_json_bool(error_val, false);
  if (has_error) {
    const fpv_json_value_t* msg = fpv_json_object_get(json, "msg");
    const char* msg_text = fpv_json_string(msg);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        msg_text ? msg_text : "Refund failed",
        "orders/refund",
        "POST",
        response.status);
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_IO;
  }
  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_upload_image(
    fpv_funpay_account_t* account,
    const void* data,
    size_t size,
    const char* filename,
    uint64_t* out_image_id,
    fpv_funpay_error_t* error) {
  if (!account || !data || size == 0 || !out_image_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  *out_image_id = 0;
  const char* name = filename && filename[0] ? filename : "fpv_image.bin";
  fpv_funpay_http_form_part_t parts[] = {
      {"file", NULL, data, size, name, "application/octet-stream"},
      {"file_id", "0", NULL, 0, NULL, NULL}};

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  char* url = fpv_funpay_build_url("file/addChatImage");
  if (!url) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_string_builder_t cookie_builder;
  fpv_funpay_sb_reset(&cookie_builder);
  fpv_funpay_sb_append(&cookie_builder, "golden_key=");
  fpv_funpay_sb_append(&cookie_builder, account->golden_key);
  if (account->phpsessid) {
    fpv_funpay_sb_append(&cookie_builder, "; PHPSESSID=");
    fpv_funpay_sb_append(&cookie_builder, account->phpsessid);
  }
  char* cookie = fpv_funpay_sb_detach(&cookie_builder);
  if (!cookie) {
    fpv_free(url);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_response_t response;
  fpv_mutex_lock(&account->request_mutex);
  fpv_result_t result = fpv_funpay_http_request_multipart(
      account->http,
      url,
      cookie,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      parts,
      sizeof(parts) / sizeof(parts[0]),
      &response,
      error);
  fpv_mutex_unlock(&account->request_mutex);
  fpv_free(cookie);
  fpv_free(url);
  if (result != FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    return result;
  }

  if (response.phpsessid && response.phpsessid[0]) {
    fpv_free(account->phpsessid);
    account->phpsessid = fpv_strdup(response.phpsessid);
    fpv_funpay_session_store(account);
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }
  const fpv_json_value_t* file_id = fpv_json_object_get(json, "fileId");
  uint64_t parsed = 0;
  if (!fpv_json_number_to_uint64(file_id, &parsed) || parsed == 0) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Image upload failed",
        "file/addChatImage",
        "POST",
        response.status);
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_PARSE;
  }
  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);
  *out_image_id = parsed;
  return FPV_OK;
}


fpv_chat_t* fpv_funpay_account_find_chat_by_name(
    fpv_funpay_account_t* account,
    const char* name,
    bool refresh,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated || !name) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return NULL;
  }

  for (size_t i = 0; i < account->chats.count; i++) {
    fpv_chat_t* chat = account->chats.chats[i];
    if (chat && chat->title && strcmp(chat->title, name) == 0) {
      return fpv_chat_clone(chat);
    }
  }

  if (!refresh) {
    return NULL;
  }

  fpv_result_t result = fpv_funpay_account_request_chats(
      account,
      NULL,
      NULL,
      error);
  if (result != FPV_OK) {
    return NULL;
  }

  for (size_t i = 0; i < account->chats.count; i++) {
    fpv_chat_t* chat = account->chats.chats[i];
    if (chat && chat->title && strcmp(chat->title, name) == 0) {
      return fpv_chat_clone(chat);
    }
  }
  return NULL;
}


static bool fpv_funpay_sb_append_json(
    fpv_string_builder_t* builder,
    const char* text) {
  if (!builder || !text) {
    return true;
  }
  for (const unsigned char* ptr = (const unsigned char*)text; *ptr; ptr++) {
    unsigned char ch = *ptr;
    switch (ch) {
      case '\\':
      case '"': {
        char escaped[2];
        escaped[0] = '\\';
        escaped[1] = (char)ch;
        if (!fpv_funpay_sb_append_n(builder, escaped, 2)) {
          return false;
        }
        break;
      }
      case '\n':
        if (!fpv_funpay_sb_append(builder, "\\n")) {
          return false;
        }
        break;
      case '\r':
        if (!fpv_funpay_sb_append(builder, "\\r")) {
          return false;
        }
        break;
      case '\t':
        if (!fpv_funpay_sb_append(builder, "\\t")) {
          return false;
        }
        break;
      default:
        if (ch < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04X", ch);
          if (!fpv_funpay_sb_append(builder, escaped)) {
            return false;
          }
        } else {
          if (!fpv_funpay_sb_append_n(builder, (const char*)&ch, 1)) {
            return false;
          }
        }
        break;
    }
  }
  return true;
}


static fpv_result_t fpv_funpay_account_send_internal(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    const char* text,
    uint64_t image_id,
    fpv_message_t** out_message,
    fpv_funpay_error_t* error) {
  if (out_message) {
    *out_message = NULL;
  }
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }
  if (!text && image_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"chat_node\",\"id\":%" PRIu64 ",\"tag\":\"00000000\","
      "\"data\":{\"node\":%" PRIu64 ",\"last_message\":-1,\"content\":\"\"}}",
      chat_id,
      chat_id);
  fpv_funpay_sb_append(&objects, "]");
  char* objects_json = fpv_funpay_sb_detach(&objects);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_string_builder_t request;
  fpv_funpay_sb_reset(&request);
  fpv_funpay_sb_append(
      &request,
      "{\"action\":\"chat_message\",\"data\":{");
  fpv_funpay_sb_append_format(
      &request,
      "\"node\":%" PRIu64 ",\"last_message\":-1,",
      chat_id);
  if (image_id > 0) {
    fpv_funpay_sb_append_format(
        &request,
        "\"image_id\":%" PRIu64 ",",
        image_id);
  }
  fpv_funpay_sb_append(&request, "\"content\":\"");
  if (image_id == 0) {
    fpv_string_builder_t content;
    fpv_funpay_sb_reset(&content);
    fpv_funpay_sb_append(&content, FPV_FUNPAY_BOT_PREFIX);
    fpv_funpay_sb_append(&content, text ? text : "");
    char* content_raw = fpv_funpay_sb_detach(&content);
    if (!content_raw) {
      fpv_free(objects_json);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    bool escaped = fpv_funpay_sb_append_json(&request, content_raw);
    fpv_free(content_raw);
    if (!escaped) {
      fpv_free(objects_json);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_funpay_sb_append(&request, "\"}}");
  char* request_json = fpv_funpay_sb_detach(&request);
  if (!request_json) {
    fpv_free(objects_json);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, request_json, account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
  fpv_free(request_json);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "runner/",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      true,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse send message response",
        "runner/",
        "POST",
        response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* response_val =
      fpv_json_object_get(json, "response");
  const fpv_json_value_t* error_val =
      response_val ? fpv_json_object_get(response_val, "error") : NULL;
  const char* error_text = fpv_json_string(error_val);
  if (error_text && error_text[0]) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        error_text,
        "runner/",
        "POST",
        response.status);
    return FPV_ERR_IO;
  }

  const fpv_json_value_t* objects_val =
      fpv_json_object_get(json, "objects");
  size_t count = fpv_json_array_size(objects_val);
  fpv_message_t* last_message = NULL;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects_val, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type || strcmp(type, "chat_node") != 0) {
      continue;
    }
    const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
    const fpv_json_value_t* msg_val = fpv_json_object_get(data_val, "messages");
    fpv_message_t** messages = NULL;
    size_t message_count = 0;
    fpv_result_t parse_result = fpv_funpay_parse_messages(
        account,
        msg_val,
        chat_id,
        chat_name,
        &messages,
        &message_count);
    if (parse_result != FPV_OK || message_count == 0) {
      continue;
    }
    last_message = messages[message_count - 1];
    for (size_t m = 0; m + 1 < message_count; m++) {
      fpv_message_destroy(messages[m]);
    }
    fpv_free(messages);
    break;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);
  if (!last_message) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "No message in response",
        "runner/",
        "POST",
        response.status);
    return FPV_ERR_PARSE;
  }
  if (out_message) {
    *out_message = last_message;
  } else {
    fpv_message_destroy(last_message);
  }
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_send_message(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    const char* text,
    fpv_message_t** out_message,
    fpv_funpay_error_t* error) {
  return fpv_funpay_account_send_internal(
      account,
      chat_id,
      chat_name,
      text,
      0,
      out_message,
      error);
}


fpv_result_t fpv_funpay_account_send_image(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    uint64_t image_id,
    fpv_message_t** out_message,
    fpv_funpay_error_t* error) {
  if (image_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  return fpv_funpay_account_send_internal(
      account,
      chat_id,
      chat_name,
      NULL,
      image_id,
      out_message,
      error);
}


fpv_result_t fpv_funpay_account_get_trade_lots(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id,
    bool is_currency,
    fpv_lot_t*** out_lots,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (out_lots) {
    *out_lots = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }
  if (subcategory_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char url[128];
  const char* prefix = is_currency ? "chips" : "lots";
  snprintf(
      url,
      sizeof(url),
      "https://funpay.com/%s/%" PRIu64 "/trade",
      prefix,
      subcategory_id);
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lots fetch: GET %s (subcategory=%" PRIu64 ", currency=%d).",
      url,
      subcategory_id,
      is_currency ? 1 : 0);
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lots fetch: status=%ld bytes=%zu.",
      response.status,
      response.body_size);

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lots fetch: HTML parse failed (status=%ld).",
        response.status);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        url,
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lots fetch: unauthorized (status=%ld).",
        response.status);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  const char* items_source = "a.tc-item";
  fpv_html_node_list_t items = fpv_html_find_all_by_class(root, "a", "tc-item");
  if (items.count == 0) {
    fpv_html_node_list_destroy(&items);
    items_source = "*.tc-item";
    items = fpv_html_find_all_by_class(root, NULL, "tc-item");
  }
  if (items.count == 0) {
    fpv_html_node_list_destroy(&items);
    items_source = "data-offer attrs";
    const char* attrs[] = {
        "data-offer",
        "data-offer-id",
        "data-offerid",
        "data-id"};
    items = fpv_funpay_find_all_by_attrs(
        root,
        NULL,
        attrs,
        sizeof(attrs) / sizeof(attrs[0]));
  }
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lots fetch: nodes=%zu source=%s.",
      items.count,
      items_source);
  fpv_lot_t** lots = NULL;
  size_t count = 0;
  size_t parsed_count = 0;
  size_t skipped_no_id = 0;
  size_t duplicate_count = 0;
  for (size_t i = 0; i < items.count; i++) {
    xmlNode* node = items.nodes[i];
    if (!node) {
      continue;
    }
    uint64_t lot_id = 0;
    if (!fpv_funpay_parse_lot_id(node, &lot_id)) {
      skipped_no_id++;
      continue;
    }
    char lot_id_buf[32];
    snprintf(lot_id_buf, sizeof(lot_id_buf), "%" PRIu64, lot_id);
    bool exists = false;
    for (size_t j = 0; j < count; j++) {
      if (lots[j] && lots[j]->id &&
          strcmp(lots[j]->id, lot_id_buf) == 0) {
        exists = true;
        break;
      }
    }
    if (exists) {
      duplicate_count++;
      continue;
    }
    xmlNode* title_node = fpv_funpay_find_lot_title_node(node);
    char* title = title_node ? fpv_html_node_text(title_node) : NULL;
    bool active = true;
    if (fpv_html_node_has_class(node, "warning") ||
        fpv_html_node_has_class(node, "inactive") ||
        fpv_html_node_has_class(node, "disabled")) {
      active = false;
    } else if (fpv_funpay_find_first_by_class_substr(node->children, NULL, "warning") ||
               fpv_funpay_find_first_by_class_substr(node->children, NULL, "inactive") ||
               fpv_funpay_find_first_by_class_substr(node->children, NULL, "disabled")) {
      active = false;
    }
    fpv_lot_t* lot =
        fpv_lot_create(lot_id_buf, title ? title : "", 0.0, NULL, 0, active);
    fpv_free(title);
    if (!lot) {
      for (size_t j = 0; j < count; j++) {
        fpv_lot_destroy(lots[j]);
      }
      fpv_free(lots);
      fpv_html_node_list_destroy(&items);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_lot_t** grown = (fpv_lot_t**)realloc(
        lots,
        (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_lot_destroy(lot);
      for (size_t j = 0; j < count; j++) {
        fpv_lot_destroy(lots[j]);
      }
      fpv_free(lots);
      fpv_html_node_list_destroy(&items);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    lots = grown;
    lots[count++] = lot;
    parsed_count++;
  }

  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lots fetch: parsed=%zu skipped_no_id=%zu duplicates=%zu.",
      parsed_count,
      skipped_no_id,
      duplicate_count);
  fpv_html_node_list_destroy(&items);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  if (out_lots) {
    *out_lots = lots;
  } else {
    for (size_t i = 0; i < count; i++) {
      fpv_lot_destroy(lots[i]);
    }
    fpv_free(lots);
  }
  if (out_count) {
    *out_count = count;
  }
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_lot_sections(
    fpv_funpay_account_t* account,
    fpv_funpay_lot_section_t** out_sections,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (out_sections) {
    *out_sections = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  char url[128];
  snprintf(url, sizeof(url), "https://funpay.com/users/%" PRIu64 "/", account->id);
  fpv_funpay_logf(account, FPV_LOG_INFO, "Lot sections fetch: GET %s.", url);
  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      NULL,
      0,
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot sections fetch: status=%ld bytes=%zu.",
      response.status,
      response.body_size);

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lot sections fetch: HTML parse failed (status=%ld).",
        response.status);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        url,
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lot sections fetch: unauthorized (status=%ld).",
        response.status);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  const char* container_source = "div.offer-list-title-container";
  fpv_html_node_list_t containers =
      fpv_html_find_all_by_class(root, "div", "offer-list-title-container");
  bool links_only = false;
  if (containers.count == 0) {
    fpv_html_node_list_destroy(&containers);
    containers = fpv_html_find_all_by_class(root, "a", NULL);
    container_source = "a (links-only)";
    links_only = true;
  }
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot sections parse: containers=%zu source=%s.",
      containers.count,
      container_source);
  fpv_funpay_lot_section_t* sections = NULL;
  size_t count = 0;
  size_t parsed_count = 0;
  size_t skipped_no_href = 0;
  size_t skipped_non_lot = 0;
  size_t skipped_bad_id = 0;
  size_t duplicate_count = 0;
  for (size_t i = 0; i < containers.count; i++) {
    xmlNode* node = containers.nodes[i];
    xmlNode* link = links_only ? node : fpv_funpay_find_first_tag(node->children, "a");
    if (!link) {
      continue;
    }
    char* href = fpv_html_node_attr(link, "href");
    if (!href) {
      skipped_no_href++;
      continue;
    }
    if (!strstr(href, "/lots/") && !strstr(href, "/chips/")) {
      fpv_free(href);
      skipped_non_lot++;
      continue;
    }
    bool is_currency_section = strstr(href, "chips") != NULL;
    uint64_t sub_id = 0;
    bool parsed = fpv_funpay_parse_lot_section_id(href, &sub_id);
    if (!parsed && !strstr(href, "offer")) {
      parsed = fpv_funpay_parse_last_uint64(href, &sub_id);
    }
    if (!parsed) {
      if (fpv_funpay_debug_messages(account)) {
        uint64_t debug_id = 0;
        uint64_t debug_last = 0;
        uint64_t debug_query = 0;
        bool parsed_href = fpv_funpay_parse_href_id(href, &debug_id);
        bool parsed_last = fpv_funpay_parse_last_uint64(href, &debug_last);
        bool parsed_query =
            fpv_funpay_parse_query_param_u64(href, "id", &debug_query) ||
            fpv_funpay_parse_query_param_u64(href, "sub", &debug_query) ||
            fpv_funpay_parse_query_param_u64(href, "subcat", &debug_query) ||
            fpv_funpay_parse_query_param_u64(href, "subcategory", &debug_query);
        fpv_funpay_logf(
            account,
            FPV_LOG_INFO,
            "Lot sections parse: debug parsed_href=%d parsed_last=%d "
            "parsed_query=%d id_href=%" PRIu64 " id_last=%" PRIu64 " id_query=%" PRIu64 ".",
            parsed_href ? 1 : 0,
            parsed_last ? 1 : 0,
            parsed_query ? 1 : 0,
            debug_id,
            debug_last,
            debug_query);
        fpv_funpay_log_href_debug(account, "Lot sections parse: debug", href);
      }
      fpv_funpay_logf(
          account,
          FPV_LOG_INFO,
          "Lot sections parse: bad id href=%s.",
          href);
    } else if (fpv_funpay_debug_messages(account)) {
      fpv_funpay_logf(
          account,
          FPV_LOG_INFO,
          "Lot sections parse: section href=%s id=%" PRIu64 ".",
          href,
          sub_id);
    }
    fpv_free(href);
    if (!parsed) {
      skipped_bad_id++;
      continue;
    }
    bool exists = false;
    for (size_t j = 0; j < count; j++) {
      if (sections[j].id == sub_id &&
          sections[j].is_currency == is_currency_section) {
        exists = true;
        break;
      }
    }
    if (exists) {
      duplicate_count++;
      continue;
    }
    fpv_funpay_lot_section_t* grown =
        (fpv_funpay_lot_section_t*)realloc(
            sections,
            (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(sections);
      fpv_html_node_list_destroy(&containers);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    sections = grown;
    sections[count].id = sub_id;
    sections[count].is_currency = is_currency_section;
    count++;
    parsed_count++;
  }

  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot sections parse: parsed=%zu skipped_no_href=%zu skipped_non_lot=%zu "
      "skipped_bad_id=%zu duplicates=%zu.",
      parsed_count,
      skipped_no_href,
      skipped_non_lot,
      skipped_bad_id,
      duplicate_count);
  fpv_html_node_list_destroy(&containers);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  if (out_sections) {
    *out_sections = sections;
  } else {
    fpv_free(sections);
  }
  if (out_count) {
    *out_count = count;
  }
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_set_lot_active(
    fpv_funpay_account_t* account,
    uint64_t lot_id,
    bool active,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }
  if (lot_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  char url[160];
  snprintf(
      url,
      sizeof(url),
      "https://funpay.com/lots/offerEdit?offer=%" PRIu64 "&location=offer",
      lot_id);
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        url,
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  xmlNode* header_node =
      fpv_html_find_first_by_class(root, "h1", "page-header");
  if (header_node) {
    char* header_text = fpv_html_node_text(header_node);
    if (header_text &&
        strcmp(header_text,
               "\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB4\xD0\xBB\xD0\xBE\xD0\xB6"
               "\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBD\xD0\xB5 "
               "\xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xBE") == 0) {
      fpv_free(header_text);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_NOT_FOUND,
          "Lot not found",
          url,
          "GET",
          response.status);
      return FPV_ERR_NOT_FOUND;
    }
    fpv_free(header_text);
  }

  long response_status = response.status;
  char* active_on_value = NULL;
  char* active_off_value = NULL;
  fpv_html_node_list_t inputs = fpv_funpay_find_all_tag(root, "input");
  for (size_t i = 0; i < inputs.count; i++) {
    xmlNode* node = inputs.nodes[i];
    if (!node) {
      continue;
    }
    char* name = fpv_html_node_attr(node, "name");
    if (!name || !name[0]) {
      fpv_free(name);
      continue;
    }
    if (strcmp(name, "active") != 0) {
      fpv_free(name);
      continue;
    }
    char* type = fpv_html_node_attr(node, "type");
    char* value = fpv_html_node_attr(node, "value");
    if (type && strcmp(type, "checkbox") == 0) {
      if (!active_on_value) {
        active_on_value = fpv_strdup(value && value[0] ? value : "on");
      }
    } else if (!active_off_value) {
      active_off_value = fpv_strdup(value ? value : "");
    }
    fpv_free(type);
    fpv_free(value);
    fpv_free(name);
  }
  fpv_html_node_list_destroy(&inputs);

  fpv_funpay_form_t form;
  memset(&form, 0, sizeof(form));
  bool form_ok = fpv_funpay_form_parse_from_html(root, &form);
  if (!form_ok && error && error->code == FPV_FUNPAY_OK) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot edit form",
        url,
        "GET",
        response_status);
  }
  if (form_ok) {
    const char* on_value =
        (active_on_value && active_on_value[0]) ? active_on_value : "on";
    const char* off_value = active_off_value ? active_off_value : "";
    if (!fpv_funpay_form_set(&form, "active", active ? on_value : off_value)) {
      form_ok = false;
    }
  }
  fpv_free(active_on_value);
  fpv_free(active_off_value);

  if (form_ok && !fpv_funpay_form_has_key(&form, "location")) {
    if (!fpv_funpay_form_set(&form, "location", "trade")) {
      form_ok = false;
    }
  }
  if (account->csrf_token && !fpv_funpay_form_has_key(&form, "csrf_token")) {
    if (form_ok &&
        !fpv_funpay_form_set(&form, "csrf_token", account->csrf_token)) {
      form_ok = false;
    }
  }

  if (!form_ok) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Failed to prepare lot update form",
          url,
          "GET",
          response_status);
    }
    fpv_funpay_form_clear(&form);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* form_body = fpv_funpay_form_encode_fields(&form);
  fpv_funpay_form_clear(&form);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  if (!form_body) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Failed to encode lot update form",
          url,
          "GET",
          response_status);
    }
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t save_headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};
  fpv_funpay_http_response_t save_response;
  memset(&save_response, 0, sizeof(save_response));
  result = fpv_funpay_account_request(
      account,
      "POST",
      "lots/offerSave",
      save_headers,
      sizeof(save_headers) / sizeof(save_headers[0]),
      form_body,
      false,
      true,
      &save_response,
      error);
  fpv_free(form_body);
  if (result != FPV_OK) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Lot update request failed",
          "lots/offerSave",
          "POST",
          save_response.status);
    }
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(save_response.body, save_response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lot secrets update response parse failed (status=%ld).",
        save_response.status);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot save response",
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* error_val = fpv_json_object_get(json, "error");
  const char* error_text = fpv_json_string(error_val);
  if (error_text && error_text[0]) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lot secrets update failed: %s",
        error_text);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        error_text,
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_IO;
  }

  fpv_funpay_logf(account, FPV_LOG_INFO, "Lot secrets update ok.");
  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&save_response);
  return FPV_OK;
}

fpv_result_t fpv_funpay_account_set_lot_secrets(
    fpv_funpay_account_t* account,
    uint64_t lot_id,
    const char* secrets,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }
  if (lot_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  size_t secrets_len = secrets ? strlen(secrets) : 0;
  char preview[96];
  size_t preview_len = 0;
  if (secrets && secrets[0]) {
    while (secrets[preview_len] &&
           secrets[preview_len] != '\n' &&
           preview_len < sizeof(preview) - 1) {
      preview[preview_len] = secrets[preview_len];
      preview_len++;
    }
  }
  preview[preview_len] = '\0';

  char url[160];
  snprintf(
      url,
      sizeof(url),
      "https://funpay.com/lots/offerEdit?offer=%" PRIu64 "&location=offer",
      lot_id);
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot secrets update start: lot_id=%" PRIu64 " secrets_len=%zu preview=%s",
      lot_id,
      secrets_len,
      preview);
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse HTML",
        url,
        "GET",
        response.status);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  xmlNode* header_node =
      fpv_html_find_first_by_class(root, "h1", "page-header");
  if (header_node) {
    char* header_text = fpv_html_node_text(header_node);
    if (header_text &&
        strcmp(header_text,
               "\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB4\xD0\xBB\xD0\xBE\xD0\xB6"
               "\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBD\xD0\xB5 "
               "\xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xBE") == 0) {
      fpv_free(header_text);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_NOT_FOUND,
          "Lot not found",
          url,
          "GET",
          response.status);
      return FPV_ERR_NOT_FOUND;
    }
    fpv_free(header_text);
  }

  char* auto_delivery_on = NULL;
  bool auto_delivery_checked = false;
  fpv_html_node_list_t inputs = fpv_funpay_find_all_tag(root, "input");
  for (size_t i = 0; i < inputs.count; i++) {
    xmlNode* node = inputs.nodes[i];
    if (!node) {
      continue;
    }
    char* name = fpv_html_node_attr(node, "name");
    if (!name || strcmp(name, "auto_delivery") != 0) {
      fpv_free(name);
      continue;
    }
    char* type = fpv_html_node_attr(node, "type");
    if (!type || strcmp(type, "checkbox") != 0) {
      fpv_free(type);
      fpv_free(name);
      continue;
    }
    char* checked = fpv_html_node_attr(node, "checked");
    auto_delivery_checked = checked != NULL;
    fpv_free(checked);
    char* value = fpv_html_node_attr(node, "value");
    if (value && value[0]) {
      auto_delivery_on = fpv_strdup(value);
    } else {
      auto_delivery_on = fpv_strdup("on");
    }
    fpv_free(value);
    fpv_free(type);
    fpv_free(name);
    break;
  }
  fpv_html_node_list_destroy(&inputs);

  long response_status = response.status;
  fpv_funpay_form_t form;
  memset(&form, 0, sizeof(form));
  bool form_ok = fpv_funpay_form_parse_from_html(root, &form);
  if (!form_ok && error && error->code == FPV_FUNPAY_OK) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot edit form",
        url,
        "GET",
        response_status);
  }
  bool form_has_secrets =
      form_ok && fpv_funpay_form_has_key(&form, "secrets");
  bool form_has_auto_delivery =
      form_ok && fpv_funpay_form_has_key(&form, "auto_delivery");
  const char* form_location =
      form_ok ? fpv_funpay_form_get(&form, "location") : NULL;
  fpv_funpay_logf(
      account,
      FPV_LOG_INFO,
      "Lot secrets form: ok=%d secrets=%d auto_delivery=%d checked=%d on_value=%s location=%s",
      form_ok ? 1 : 0,
      form_has_secrets ? 1 : 0,
      form_has_auto_delivery ? 1 : 0,
      auto_delivery_checked ? 1 : 0,
      auto_delivery_on ? auto_delivery_on : "",
      form_location ? form_location : "");
  if (!form_has_secrets) {
    fpv_funpay_logf(
        account,
        FPV_LOG_WARNING,
        "Lot secrets form missing textarea; cannot update secrets.");
    fpv_funpay_log_html_snippet(account, response.body);
  }
  if (form_ok &&
      !fpv_funpay_form_set(&form, "secrets", secrets ? secrets : "")) {
    form_ok = false;
  }
  const char* auto_delivery_value =
      auto_delivery_on && auto_delivery_on[0] ? auto_delivery_on : "on";
  if (form_ok &&
      !fpv_funpay_form_set(&form, "auto_delivery", auto_delivery_value)) {
    form_ok = false;
  }
  if (form_ok && !fpv_funpay_form_has_key(&form, "location")) {
    if (!fpv_funpay_form_set(&form, "location", "offer")) {
      form_ok = false;
    }
  }
  if (account->csrf_token && !fpv_funpay_form_has_key(&form, "csrf_token")) {
    if (form_ok &&
        !fpv_funpay_form_set(&form, "csrf_token", account->csrf_token)) {
      form_ok = false;
    }
  }

  if (!form_ok) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Failed to prepare lot secrets form",
          url,
          "GET",
          response_status);
    }
    fpv_free(auto_delivery_on);
    fpv_funpay_form_clear(&form);
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char* form_body = fpv_funpay_form_encode_fields(&form);
  fpv_funpay_form_clear(&form);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);
  fpv_free(auto_delivery_on);
  if (!form_body) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Failed to encode lot secrets form",
          url,
          "GET",
          response_status);
    }
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t save_headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};
  fpv_funpay_http_response_t save_response;
  memset(&save_response, 0, sizeof(save_response));
  result = fpv_funpay_account_request(
      account,
      "POST",
      "lots/offerSave",
      save_headers,
      sizeof(save_headers) / sizeof(save_headers[0]),
      form_body,
      false,
      true,
      &save_response,
      error);
  fpv_free(form_body);
  if (result != FPV_OK) {
    if (error && error->code == FPV_FUNPAY_OK) {
      fpv_funpay_error_set(
          error,
          FPV_FUNPAY_ERR_REQUEST_FAILED,
          "Lot secrets update request failed",
          "lots/offerSave",
          "POST",
          save_response.status);
    }
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(save_response.body, save_response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot save response",
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* error_val = fpv_json_object_get(json, "error");
  const char* error_text = fpv_json_string(error_val);
  if (error_text && error_text[0]) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        error_text,
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_IO;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&save_response);
  return FPV_OK;
}


static bool fpv_funpay_parse_offer_id_from_string(
    const char* text,
    uint64_t* out_id) {
  if (!text || !out_id) {
    return false;
  }
  const char* param = strstr(text, "offer=");
  if (param) {
    param += strlen("offer=");
    const char* start = param;
    while (*param && isdigit((unsigned char)*param)) {
      param++;
    }
    if (param > start) {
      char* temp = fpv_strdup_n(start, (size_t)(param - start));
      if (!temp) {
        return false;
      }
      char* end = NULL;
      unsigned long long parsed = strtoull(temp, &end, 10);
      bool ok = end && *end == '\0';
      fpv_free(temp);
      if (ok) {
        *out_id = (uint64_t)parsed;
        return true;
      }
    }
  }
  return fpv_funpay_parse_href_id(text, out_id);
}


fpv_result_t fpv_funpay_account_clone_lot(
    fpv_funpay_account_t* account,
    uint64_t lot_id,
    const char* title,
    const char* original_title,
    uint64_t* out_lot_id,
    fpv_funpay_error_t* error) {
  if (out_lot_id) {
    *out_lot_id = 0;
  }
  if (!account || lot_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  char src_url[160];
  snprintf(
      src_url,
      sizeof(src_url),
      "https://funpay.com/lots/offerEdit?offer=%" PRIu64,
      lot_id);
  fpv_funpay_form_t source_form;
  memset(&source_form, 0, sizeof(source_form));
  fpv_result_t result = fpv_funpay_account_fetch_lot_form(
      account,
      src_url,
      true,
      &source_form,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_funpay_form_t target_form;
  memset(&target_form, 0, sizeof(target_form));
  result = fpv_funpay_account_fetch_lot_form(
      account,
      "https://funpay.com/lots/offer?id=0",
      false,
      &target_form,
      error);
  if (result != FPV_OK) {
    fpv_funpay_form_clear(&source_form);
    return result;
  }

  const char* excluded[] = {
      "csrf_token",
      "offer",
      "offer_id",
      "offerId",
      "lot_id",
      "lotId",
      "id"};
  if (!fpv_funpay_form_copy_common_fields(
          &target_form,
          &source_form,
          excluded,
          sizeof(excluded) / sizeof(excluded[0]))) {
    fpv_funpay_form_clear(&source_form);
    fpv_funpay_form_clear(&target_form);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (!fpv_funpay_form_override_title(&target_form, title, original_title)) {
    fpv_funpay_form_clear(&source_form);
    fpv_funpay_form_clear(&target_form);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (account->csrf_token &&
      !fpv_funpay_form_has_key(&target_form, "csrf_token")) {
    if (!fpv_funpay_form_set(&target_form, "csrf_token", account->csrf_token)) {
      fpv_funpay_form_clear(&source_form);
      fpv_funpay_form_clear(&target_form);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  char* form_body = fpv_funpay_form_encode_fields(&target_form);
  fpv_funpay_form_clear(&source_form);
  fpv_funpay_form_clear(&target_form);
  if (!form_body) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t save_headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};
  fpv_funpay_http_response_t save_response;
  result = fpv_funpay_account_request(
      account,
      "POST",
      "lots/offerSave",
      save_headers,
      sizeof(save_headers) / sizeof(save_headers[0]),
      form_body,
      false,
      true,
      &save_response,
      error);
  fpv_free(form_body);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(save_response.body, save_response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot save response",
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* error_val = fpv_json_object_get(json, "error");
  const char* error_text = fpv_json_string(error_val);
  if (error_text && error_text[0]) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        error_text,
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_IO;
  }

  if (out_lot_id) {
    const fpv_json_value_t* id_val = fpv_json_object_get(json, "offer");
    if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
      id_val = fpv_json_object_get(json, "id");
      if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
        id_val = fpv_json_object_get(json, "offer_id");
        if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
          const char* id_text = fpv_json_string(id_val);
          if (id_text && id_text[0]) {
            char* end = NULL;
            unsigned long long parsed = strtoull(id_text, &end, 10);
            if (end && *end == '\0') {
              *out_lot_id = (uint64_t)parsed;
            }
          }
          const char* redirect = fpv_json_string(fpv_json_object_get(json, "redirect"));
          if (!*out_lot_id && redirect) {
            fpv_funpay_parse_offer_id_from_string(redirect, out_lot_id);
          }
        }
      }
    }
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&save_response);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_clone_lot_from_url(
    fpv_funpay_account_t* account,
    const char* lot_url,
    const char* title,
    uint64_t* out_lot_id,
    fpv_funpay_error_t* error) {
  if (out_lot_id) {
    *out_lot_id = 0;
  }
  if (!account || !lot_url || !lot_url[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  char* normalized = fpv_funpay_normalize_lot_url(lot_url);
  if (!normalized) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Invalid lot link",
        NULL,
        "GET",
        0);
    return FPV_ERR_PARSE;
  }

  if (fpv_funpay_is_user_profile_url(normalized)) {
    char* resolved = NULL;
    fpv_result_t resolve_result =
        fpv_funpay_resolve_profile_lot_url(account, normalized, &resolved, error);
    if (resolve_result != FPV_OK) {
      fpv_free(normalized);
      return resolve_result;
    }
    fpv_free(normalized);
    normalized = resolved;
  }

  uint64_t lot_id = 0;
  fpv_funpay_parse_offer_id_from_string(normalized, &lot_id);

  fpv_funpay_http_response_t response;
  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*")};
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      normalized,
      headers,
      sizeof(headers) / sizeof(headers[0]),
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    fpv_free(normalized);
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot HTML",
        normalized,
        "GET",
        response.status);
    fpv_free(normalized);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  fpv_funpay_public_lot_t lot;
  memset(&lot, 0, sizeof(lot));
  lot.title = fpv_funpay_clone_extract_title(root);
  lot.description = fpv_funpay_clone_extract_description(root);
  const char* preferred_currency = fpv_funpay_account_currency(account);
  lot.has_price = fpv_funpay_clone_extract_price(
      root,
      preferred_currency,
      &lot.price);
  fpv_funpay_public_lot_fill_from_json_ld(root, &lot);

  uint64_t* allowed_ids = NULL;
  size_t allowed_count = 0;
  if (account->id != 0) {
    fpv_funpay_error_t sub_error;
    memset(&sub_error, 0, sizeof(sub_error));
    fpv_result_t sub_result = fpv_funpay_account_get_lot_subcategories(
        account,
        &allowed_ids,
        &allowed_count,
        &sub_error);
    if (sub_result != FPV_OK) {
      fpv_funpay_error_clear(&sub_error);
      fpv_free(allowed_ids);
      allowed_ids = NULL;
      allowed_count = 0;
    } else {
      fpv_funpay_error_clear(&sub_error);
    }
  }

  bool sub_ok = fpv_funpay_public_lot_extract_subcategory_id(
      root,
      lot_id,
      allowed_ids,
      allowed_count,
      &lot.subcategory_id);
  if (!sub_ok && allowed_count > 0) {
    fpv_funpay_public_lot_extract_subcategory_id(
        root,
        lot_id,
        NULL,
        0,
        &lot.subcategory_id);
  }
  fpv_free(allowed_ids);

  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  const char* final_title = (title && title[0]) ? title : lot.title;
  if (!final_title || !final_title[0] || !lot.has_price || lot.subcategory_id == 0) {
    fpv_funpay_public_lot_clear(&lot);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Lot link is missing required fields",
        normalized,
        "GET",
        0);
    fpv_free(normalized);
    return FPV_ERR_PARSE;
  }

  fpv_funpay_form_t target_form;
  memset(&target_form, 0, sizeof(target_form));
  result = fpv_funpay_account_fetch_lot_form(
      account,
      "https://funpay.com/lots/offer?id=0",
      false,
      &target_form,
      error);
  if (result != FPV_OK) {
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return result;
  }

  const char* title_keys[] = {"title", "name", "lot_title", "offer_title"};
  const char* title_key = fpv_funpay_form_find_first_key(
      &target_form,
      title_keys,
      sizeof(title_keys) / sizeof(title_keys[0]));
  if (!title_key) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Lot link is missing title field",
        normalized,
        "GET",
        0);
    fpv_free(normalized);
    return FPV_ERR_PARSE;
  }
  if (!fpv_funpay_form_set(&target_form, title_key, final_title)) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (lot.description && lot.description[0]) {
    const char* desc_keys[] = {
        "description",
        "text",
        "offer_text",
        "details",
        "lot_description",
        "comment"};
    const char* desc_key = fpv_funpay_form_find_first_key(
        &target_form,
        desc_keys,
        sizeof(desc_keys) / sizeof(desc_keys[0]));
    if (desc_key && !fpv_funpay_form_set(&target_form, desc_key, lot.description)) {
      fpv_funpay_form_clear(&target_form);
      fpv_funpay_public_lot_clear(&lot);
      fpv_free(normalized);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  const char* price_keys[] = {
      "price",
      "price_rub",
      "price_usd",
      "price_eur",
      "cost",
      "amount"};
  const char* price_key = fpv_funpay_form_find_first_key(
      &target_form,
      price_keys,
      sizeof(price_keys) / sizeof(price_keys[0]));
  if (!price_key) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Lot link is missing price field",
        normalized,
        "GET",
        0);
    fpv_free(normalized);
    return FPV_ERR_PARSE;
  }
  char price_buf[64];
  fpv_funpay_format_price_value(lot.price, price_buf, sizeof(price_buf));
  if (!fpv_funpay_form_set(&target_form, price_key, price_buf)) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* sub_keys[] = {
      "subcategory",
      "subcategory_id",
      "subcategoryId",
      "subCategoryId",
      "sub_category",
      "node_id",
      "nodeId",
      "game_id",
      "gameId",
      "category_id",
      "categoryId"};
  const char* sub_key = fpv_funpay_form_find_first_key(
      &target_form,
      sub_keys,
      sizeof(sub_keys) / sizeof(sub_keys[0]));
  if (!sub_key) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Lot link is missing category field",
        normalized,
        "GET",
        0);
    fpv_free(normalized);
    return FPV_ERR_PARSE;
  }
  char sub_buf[32];
  snprintf(sub_buf, sizeof(sub_buf), "%" PRIu64, lot.subcategory_id);
  if (!fpv_funpay_form_set(&target_form, sub_key, sub_buf)) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (!fpv_funpay_form_set_if_present(&target_form, "active", "")) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_funpay_form_set_if_present(&target_form, "deactivate_after_sale", "");

  const char* location = strstr(normalized, "chips") ? "chips" : "trade";
  if (!fpv_funpay_form_set_if_present(&target_form, "location", location)) {
    fpv_funpay_form_clear(&target_form);
    fpv_funpay_public_lot_clear(&lot);
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (account->csrf_token &&
      !fpv_funpay_form_has_key(&target_form, "csrf_token")) {
    if (!fpv_funpay_form_set(&target_form, "csrf_token", account->csrf_token)) {
      fpv_funpay_form_clear(&target_form);
      fpv_funpay_public_lot_clear(&lot);
      fpv_free(normalized);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  char* form_body = fpv_funpay_form_encode_fields(&target_form);
  fpv_funpay_form_clear(&target_form);
  fpv_funpay_public_lot_clear(&lot);
  if (!form_body) {
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t save_headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};
  fpv_funpay_http_response_t save_response;
  result = fpv_funpay_account_request(
      account,
      "POST",
      "lots/offerSave",
      save_headers,
      sizeof(save_headers) / sizeof(save_headers[0]),
      form_body,
      false,
      true,
      &save_response,
      error);
  fpv_free(form_body);
  fpv_free(normalized);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(save_response.body, save_response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse lot save response",
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* error_val = fpv_json_object_get(json, "error");
  const char* error_text = fpv_json_string(error_val);
  if (error_text && error_text[0]) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&save_response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_REQUEST_FAILED,
        error_text,
        "lots/offerSave",
        "POST",
        save_response.status);
    return FPV_ERR_IO;
  }

  if (out_lot_id) {
    const fpv_json_value_t* id_val = fpv_json_object_get(json, "offer");
    if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
      id_val = fpv_json_object_get(json, "id");
      if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
        id_val = fpv_json_object_get(json, "offer_id");
        if (!fpv_json_number_to_uint64(id_val, out_lot_id) || *out_lot_id == 0) {
          const char* id_text = fpv_json_string(id_val);
          if (id_text && id_text[0]) {
            char* end = NULL;
            unsigned long long parsed = strtoull(id_text, &end, 10);
            if (end && *end == '\0') {
              *out_lot_id = (uint64_t)parsed;
            }
          }
          const char* redirect = fpv_json_string(fpv_json_object_get(json, "redirect"));
          if (!*out_lot_id && redirect) {
            fpv_funpay_parse_offer_id_from_string(redirect, out_lot_id);
          }
        }
      }
    }
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&save_response);
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_lot_subcategories(
    fpv_funpay_account_t* account,
    uint64_t** out_ids,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (out_ids) {
    *out_ids = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  char url[128];
  snprintf(url, sizeof(url), "https://funpay.com/users/%" PRIu64 "/", account->id);
  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      NULL,
      0,
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }
  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* username_node =
      fpv_html_find_first_by_class(root, "div", "user-link-name");
  if (!username_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        url,
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_html_node_list_t containers =
      fpv_html_find_all_by_class(root, "div", "offer-list-title-container");
  bool links_only = false;
  if (containers.count == 0) {
    fpv_html_node_list_destroy(&containers);
    containers = fpv_html_find_all_by_class(root, "a", NULL);
    links_only = true;
  }
  uint64_t* ids = NULL;
  size_t count = 0;
  for (size_t i = 0; i < containers.count; i++) {
    xmlNode* node = containers.nodes[i];
    xmlNode* link = links_only ? node : fpv_funpay_find_first_tag(node->children, "a");
    if (!link) {
      continue;
    }
    char* href = fpv_html_node_attr(link, "href");
    if (!href) {
      continue;
    }
    if (!strstr(href, "/lots/") && !strstr(href, "/chips/")) {
      fpv_free(href);
      continue;
    }
    if (strstr(href, "chips")) {
      fpv_free(href);
      continue;
    }
    uint64_t sub_id = 0;
    bool parsed = fpv_funpay_parse_lot_section_id(href, &sub_id);
    if (!parsed && !strstr(href, "offer")) {
      parsed = fpv_funpay_parse_last_uint64(href, &sub_id);
    }
    fpv_free(href);
    if (!parsed) {
      continue;
    }
    if (fpv_funpay_list_contains_u64(ids, count, sub_id)) {
      continue;
    }
    uint64_t* grown = (uint64_t*)realloc(ids, (count + 1) * sizeof(*grown));
    if (!grown) {
      fpv_free(ids);
      fpv_html_node_list_destroy(&containers);
      fpv_funpay_http_response_clear(&response);
      fpv_html_destroy(doc);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    ids = grown;
    ids[count++] = sub_id;
  }

  fpv_html_node_list_destroy(&containers);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  if (out_ids) {
    *out_ids = ids;
  } else {
    fpv_free(ids);
  }
  if (out_count) {
    *out_count = count;
  }
  return FPV_OK;
}


bool fpv_funpay_account_get_subcategory_category(
    fpv_funpay_account_t* account,
    uint64_t subcategory_id,
    uint64_t* out_category_id,
    const char** out_category_name) {
  if (!account) {
    return false;
  }
  fpv_funpay_subcategory_t* sub =
      fpv_funpay_catalog_find_subcategory(account, subcategory_id);
  if (!sub) {
    return false;
  }
  if (out_category_id) {
    *out_category_id = sub->category_id;
  }
  if (out_category_name) {
    fpv_funpay_category_t* cat =
        fpv_funpay_catalog_find_category(account, sub->category_id);
    *out_category_name = cat ? cat->name : NULL;
  }
  return true;
}


fpv_result_t fpv_funpay_account_raise_lots(
    fpv_funpay_account_t* account,
    uint64_t category_id,
    const uint64_t* subcategory_ids,
    size_t subcategory_count,
    uint32_t* out_wait_seconds,
    fpv_funpay_error_t* error) {
  if (out_wait_seconds) {
    *out_wait_seconds = 0;
  }
  if (!account || !account->initiated || !subcategory_ids || subcategory_count == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  size_t param_count = 2 + subcategory_count;
  const char** keys = (const char**)calloc(param_count, sizeof(*keys));
  const char** values = (const char**)calloc(param_count, sizeof(*values));
  if (!keys || !values) {
    fpv_free(keys);
    fpv_free(values);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char game_id_buf[32];
  char node_id_buf[32];
  snprintf(game_id_buf, sizeof(game_id_buf), "%" PRIu64, category_id);
  snprintf(node_id_buf, sizeof(node_id_buf), "%" PRIu64, subcategory_ids[0]);

  keys[0] = "game_id";
  values[0] = game_id_buf;
  keys[1] = "node_id";
  values[1] = node_id_buf;

  char** node_values = (char**)calloc(subcategory_count, sizeof(*node_values));
  if (!node_values) {
    fpv_free(keys);
    fpv_free(values);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < subcategory_count; i++) {
    node_values[i] = (char*)calloc(32, 1);
    if (!node_values[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_free(node_values[j]);
      }
      fpv_free(node_values);
      fpv_free(keys);
      fpv_free(values);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    snprintf(node_values[i], 32, "%" PRIu64, subcategory_ids[i]);
    keys[2 + i] = "node_ids[]";
    values[2 + i] = node_values[i];
  }

  char* form = fpv_funpay_form_encode(keys, values, param_count);
  for (size_t i = 0; i < subcategory_count; i++) {
    fpv_free(node_values[i]);
  }
  fpv_free(node_values);
  fpv_free(keys);
  fpv_free(values);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "lots/raise",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      true,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse raise response",
        "lots/raise",
        "POST",
        response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* error_val =
      fpv_json_object_get(json, "error");
  bool has_error = fpv_json_bool(error_val, false);
  const fpv_json_value_t* msg_val =
      fpv_json_object_get(json, "msg");
  if (!msg_val) {
    msg_val = fpv_json_object_get(json, "MSG");
  }
  const char* msg = fpv_json_string(msg_val);

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);

  if (!has_error) {
    if (out_wait_seconds) {
      *out_wait_seconds = 3600;
    }
    return FPV_OK;
  }

  if (msg && out_wait_seconds) {
    *out_wait_seconds = fpv_funpay_parse_wait_time(msg);
  }
  return FPV_ERR_INVALID_STATE;
}


fpv_result_t fpv_funpay_account_get_orders(
    fpv_funpay_account_t* account,
    fpv_order_t*** out_orders,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      "https://funpay.com/orders/trade",
      NULL,
      0,
      NULL,
      false,
      true,
      &response,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* login_node =
      fpv_html_find_first_by_class(root, "div", "content-account-login");
  if (login_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        "https://funpay.com/orders/trade",
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }
  fpv_html_node_list_t orders =
      fpv_html_find_all_by_class(root, "a", "tc-item");
  if (orders.count == 0) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&orders);
    *out_orders = NULL;
    *out_count = 0;
    return FPV_OK;
  }

  fpv_order_t** list =
      (fpv_order_t**)calloc(orders.count, sizeof(*list));
  if (!list) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&orders);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t count = 0;
  for (size_t i = 0; i < orders.count; i++) {
    xmlNode* node = orders.nodes[i];
    bool refunded = fpv_html_node_has_class(node, "warning");
    bool paid = fpv_html_node_has_class(node, "info");
    fpv_order_status_t status = FPV_ORDER_DELIVERED;
    if (refunded) {
      status = FPV_ORDER_REFUNDED;
    } else if (paid) {
      status = FPV_ORDER_PAID;
    }

    xmlNode* id_node = fpv_html_find_first_by_class(node->children, "div", "tc-order");
    char* id_text = id_node ? fpv_html_node_text(id_node) : NULL;
    if (!id_text) {
      continue;
    }
    fpv_funpay_trim(id_text);
    if (id_text[0] == '#') {
      memmove(id_text, id_text + 1, strlen(id_text));
    }

    xmlNode* desc_node = fpv_html_find_first_by_class(node->children, "div", "order-desc");
    char* desc_text = NULL;
    if (desc_node) {
      xmlNode* desc_inner = fpv_html_find_first_by_class(desc_node->children, "div", NULL);
      desc_text = desc_inner ? fpv_html_node_text(desc_inner) : NULL;
    }
    if (desc_text) {
      fpv_funpay_trim(desc_text);
    }

    xmlNode* price_node = fpv_html_find_first_by_class(node->children, "div", "tc-price");
    char* price_text = price_node ? fpv_html_node_text(price_node) : NULL;
    double amount = 0.0;
    const char* currency = NULL;
    if (price_text) {
      fpv_funpay_trim(price_text);
      fpv_funpay_parse_price(price_text, &amount, &currency);
    }

    xmlNode* buyer_name_node =
        fpv_html_find_first_by_class(node->children, "div", "media-user-name");
    char* buyer_username = NULL;
    char* buyer_id = NULL;
    if (buyer_name_node) {
      xmlNode* span_node = fpv_html_find_first_by_class(buyer_name_node->children, "span", NULL);
      if (span_node) {
        buyer_username = fpv_html_node_text(span_node);
        if (buyer_username) {
          fpv_funpay_trim(buyer_username);
        }
        char* href = fpv_html_node_attr(span_node, "data-href");
        if (href) {
          char* end = href + strlen(href);
          while (end > href && end[-1] == '/') {
            end--;
          }
          char* start = end;
          while (start > href && isdigit((unsigned char)start[-1])) {
            start--;
          }
          if (start < end) {
            buyer_id = fpv_strdup_n(start, (size_t)(end - start));
          }
          fpv_free(href);
        }
      }
    }

    xmlNode* sub_node = fpv_html_find_first_by_class(node->children, "div", "text-muted");
    char* subcategory = sub_node ? fpv_html_node_text(sub_node) : NULL;
    if (subcategory) {
      fpv_funpay_trim(subcategory);
    }

    xmlNode* date_node = fpv_html_find_first_by_class(node->children, "div", "tc-date-time");
    char* date_text = date_node ? fpv_html_node_text(date_node) : NULL;
    if (date_text) {
      fpv_funpay_trim(date_text);
    }
    uint64_t created_at = fpv_funpay_parse_order_date(date_text);

    fpv_order_t* order = fpv_order_create(
        id_text,
        NULL,
        NULL,
        buyer_id,
        buyer_username,
        status,
        amount,
        currency,
        created_at,
        created_at,
        1U,
        desc_text,
        subcategory);

    fpv_free(id_text);
    fpv_free(desc_text);
    fpv_free(price_text);
    fpv_free(buyer_username);
    fpv_free(buyer_id);
    fpv_free(subcategory);
    fpv_free(date_text);

    if (!order) {
      continue;
    }
    list[count++] = order;
  }

  fpv_html_node_list_destroy(&orders);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  *out_orders = list;
  *out_count = count;
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_orders_page(
    fpv_funpay_account_t* account,
    const char* state_filter,
    const char* continue_from,
    fpv_order_t*** out_orders,
    size_t* out_count,
    char** out_continue,
    fpv_funpay_error_t* error) {
  if (!account || !out_orders || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_orders = NULL;
  *out_count = 0;
  if (out_continue) {
    *out_continue = NULL;
  }
  if (!account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  const char* base_url = "https://funpay.com/orders/trade";
  char* url = NULL;
  if ((state_filter && state_filter[0]) ||
      (continue_from && continue_from[0])) {
    const char* keys[2];
    const char* values[2];
    size_t count = 0;
    if (state_filter && state_filter[0]) {
      keys[count] = "state";
      values[count] = state_filter;
      count++;
    }
    if (continue_from && continue_from[0]) {
      keys[count] = "continue";
      values[count] = continue_from;
      count++;
    }
    char* query = fpv_funpay_form_encode(keys, values, count);
    if (query) {
      size_t size = strlen(base_url) + strlen(query) + 2;
      url = (char*)malloc(size);
      if (url) {
        snprintf(url, size, "%s?%s", base_url, query);
      }
    }
    fpv_free(query);
  } else {
    url = fpv_strdup(base_url);
  }
  if (!url) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "GET",
      url,
      NULL,
      0,
      NULL,
      false,
      true,
      &response,
      error);
  fpv_free(url);
  if (result != FPV_OK) {
    return result;
  }

  fpv_html_doc_t* doc = fpv_html_parse(response.body, response.body_size);
  if (!doc || !doc->doc) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  xmlNode* login_node =
      fpv_html_find_first_by_class(root, "div", "content-account-login");
  if (login_node) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_UNAUTHORIZED,
        "Unauthorized",
        "https://funpay.com/orders/trade",
        "GET",
        response.status);
    return FPV_ERR_INVALID_STATE;
  }

  if (out_continue) {
    xmlNode* cont_node =
        fpv_funpay_find_first_by_attr(root, "input", "name", "continue");
    char* cont_val = cont_node ? fpv_html_node_attr(cont_node, "value") : NULL;
    if (cont_val && cont_val[0]) {
      *out_continue = cont_val;
    } else {
      fpv_free(cont_val);
    }
  }

  fpv_html_node_list_t orders =
      fpv_html_find_all_by_class(root, "a", "tc-item");
  if (orders.count == 0) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&orders);
    return FPV_OK;
  }

  fpv_order_t** list =
      (fpv_order_t**)calloc(orders.count, sizeof(*list));
  if (!list) {
    fpv_funpay_http_response_clear(&response);
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&orders);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t count = 0;
  for (size_t i = 0; i < orders.count; i++) {
    xmlNode* node = orders.nodes[i];
    bool refunded = fpv_html_node_has_class(node, "warning");
    bool paid = fpv_html_node_has_class(node, "info");
    fpv_order_status_t status = FPV_ORDER_DELIVERED;
    if (refunded) {
      status = FPV_ORDER_REFUNDED;
    } else if (paid) {
      status = FPV_ORDER_PAID;
    }

    xmlNode* id_node = fpv_html_find_first_by_class(node->children, "div", "tc-order");
    char* id_text = id_node ? fpv_html_node_text(id_node) : NULL;
    if (!id_text) {
      continue;
    }
    fpv_funpay_trim(id_text);
    if (id_text[0] == '#') {
      memmove(id_text, id_text + 1, strlen(id_text));
    }

    xmlNode* desc_node = fpv_html_find_first_by_class(node->children, "div", "order-desc");
    char* desc_text = NULL;
    if (desc_node) {
      xmlNode* desc_inner = fpv_html_find_first_by_class(desc_node->children, "div", NULL);
      desc_text = desc_inner ? fpv_html_node_text(desc_inner) : NULL;
    }
    if (desc_text) {
      fpv_funpay_trim(desc_text);
    }

    xmlNode* price_node = fpv_html_find_first_by_class(node->children, "div", "tc-price");
    char* price_text = price_node ? fpv_html_node_text(price_node) : NULL;
    double amount = 0.0;
    const char* currency = NULL;
    if (price_text) {
      fpv_funpay_trim(price_text);
      fpv_funpay_parse_price(price_text, &amount, &currency);
    }

    xmlNode* buyer_name_node =
        fpv_html_find_first_by_class(node->children, "div", "media-user-name");
    char* buyer_username = NULL;
    char* buyer_id = NULL;
    if (buyer_name_node) {
      xmlNode* span_node = fpv_html_find_first_by_class(buyer_name_node->children, "span", NULL);
      if (span_node) {
        buyer_username = fpv_html_node_text(span_node);
        if (buyer_username) {
          fpv_funpay_trim(buyer_username);
        }
        char* href = fpv_html_node_attr(span_node, "data-href");
        if (href) {
          char* end = href + strlen(href);
          while (end > href && end[-1] == '/') {
            end--;
          }
          char* start = end;
          while (start > href && isdigit((unsigned char)start[-1])) {
            start--;
          }
          if (start < end) {
            buyer_id = fpv_strdup_n(start, (size_t)(end - start));
          }
          fpv_free(href);
        }
      }
    }

    xmlNode* sub_node = fpv_html_find_first_by_class(node->children, "div", "text-muted");
    char* subcategory = sub_node ? fpv_html_node_text(sub_node) : NULL;
    if (subcategory) {
      fpv_funpay_trim(subcategory);
    }

    xmlNode* date_node = fpv_html_find_first_by_class(node->children, "div", "tc-date-time");
    char* date_text = date_node ? fpv_html_node_text(date_node) : NULL;
    if (date_text) {
      fpv_funpay_trim(date_text);
    }
    uint64_t created_at = fpv_funpay_parse_order_date(date_text);

    fpv_order_t* order = fpv_order_create(
        id_text,
        NULL,
        NULL,
        buyer_id,
        buyer_username,
        status,
        amount,
        currency,
        created_at,
        created_at,
        1U,
        desc_text,
        subcategory);

    fpv_free(id_text);
    fpv_free(desc_text);
    fpv_free(price_text);
    fpv_free(buyer_username);
    fpv_free(buyer_id);
    fpv_free(subcategory);
    fpv_free(date_text);

    if (!order) {
      continue;
    }
    list[count++] = order;
  }

  fpv_html_node_list_destroy(&orders);
  fpv_funpay_http_response_clear(&response);
  fpv_html_destroy(doc);

  *out_orders = list;
  *out_count = count;
  return FPV_OK;
}

typedef struct fpv_funpay_author {
  uint64_t id;
  char* name;
  char* badge;
} fpv_funpay_author_t;
