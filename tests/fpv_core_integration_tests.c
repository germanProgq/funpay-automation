/* FunPay Vertex core integration tests. */

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

#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_models.h"
#include "fpv_core/fpv_storage.h"
#include "fpv_core/fpv_types.h"
#include "funpay/http/fpv_funpay_http.h"

#include "telegram/core/fpv_telegram.h"

#include "core/base/fpv_string.h"


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

static bool fpv_test_write_file(const char* path, const char* content) {
  if (!path) {
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  const char* data = content ? content : "";
  size_t length = strlen(data);
  bool ok = length == 0 || fwrite(data, 1, length, file) == length;
  fclose(file);
  return ok;
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

static void fpv_test_cleanup_storage(
    const fpv_storage_layout_t* layout,
    const char* root) {
  if (layout) {
    fpv_test_remove_dir(layout->cache_dir);
    fpv_test_remove_dir(layout->products_dir);
    fpv_test_remove_dir(layout->data_dir);
    fpv_test_remove_dir(layout->config_dir);
    fpv_test_remove_dir(layout->logs_dir);
    fpv_test_remove_dir(layout->plugins_dir);
  }
  fpv_test_remove_dir(root);
}

typedef struct fpv_funpay_mock_state {
  int call_count;
} fpv_funpay_mock_state_t;

static fpv_result_t fpv_test_funpay_http_mock(
    fpv_funpay_http_client_t* client,
    const char* method,
    const char* url,
    const char* cookie,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const char* body,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error,
    void* user_data) {
  fpv_funpay_mock_state_t* state = (fpv_funpay_mock_state_t*)user_data;
  if (state) {
    state->call_count++;
  }
  const char* html =
      "<html><body data-app-data='{\"userId\":123,\"csrf-token\":\"token\"}'>"
      "<div class=\"user-link-name\">TestUser</div>"
      "<span class=\"badge-trade\">5</span>"
      "<span class=\"badge-orders\">2</span>"
      "<a class=\"user-cy-switcher menu-item-currency\" data-cy=\"usd\"></a>"
      "<a class=\"user-cy-switcher menu-item-currency\" data-cy=\"eur\"></a>"
      "</body></html>";
  if (!response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  response->status = 200;
  response->phpsessid = NULL;
  response->body = fpv_strdup(html);
  response->body_size = response->body ? strlen(response->body) : 0;
  return response->body ? FPV_OK : FPV_ERR_OUT_OF_MEMORY;
}

static bool test_funpay_refresh_mock(void) {
  fpv_funpay_account_config_t config;
  memset(&config, 0, sizeof(config));
  config.golden_key = "key";
  config.user_agent = "fpv-tests";
  config.timeout_ms = 1000;
  fpv_funpay_error_t error;
  fpv_funpay_account_t* account = fpv_funpay_account_create(&config, &error);
  if (!fpv_test_expect(account != NULL, "funpay account create failed")) {
    return false;
  }
  fpv_funpay_mock_state_t state;
  memset(&state, 0, sizeof(state));
  fpv_funpay_http_set_mock(fpv_test_funpay_http_mock, NULL, &state);
  fpv_result_t result = fpv_funpay_account_refresh(account, &error);
  fpv_funpay_http_clear_mock();
  bool ok = fpv_test_expect(result == FPV_OK, "funpay refresh failed");
  ok = ok && fpv_test_expect(state.call_count == 1, "funpay http call count mismatch");
  ok = ok && fpv_test_expect(fpv_funpay_account_id(account) == 123,
                             "funpay account id mismatch");
  const char* username = fpv_funpay_account_username(account);
  ok = ok && fpv_test_expect(username && strcmp(username, "TestUser") == 0,
                             "funpay username mismatch");
  const char* currency = fpv_funpay_account_currency(account);
  ok = ok && fpv_test_expect(currency && strcmp(currency, "RUB") == 0,
                             "funpay currency mismatch");
  ok = ok && fpv_test_expect(fpv_funpay_account_active_sales(account) == 5,
                             "funpay sales mismatch");
  ok = ok && fpv_test_expect(fpv_funpay_account_active_purchases(account) == 2,
                             "funpay purchases mismatch");
  fpv_funpay_account_destroy(account);
  return ok;
}

typedef struct fpv_tg_mock_state {
  int call_count;
  int64_t last_chat_id;
} fpv_tg_mock_state_t;

static fpv_result_t fpv_test_tg_http_mock(
    const char* method,
    const char* url,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_tg_http_response_t* response,
    void* user_data) {
  fpv_tg_mock_state_t* state = (fpv_tg_mock_state_t*)user_data;
  if (state) {
    state->call_count++;
    state->last_chat_id = 0;
    if (body) {
      const char* key = "\"chat_id\":";
      const char* pos = strstr(body, key);
      if (pos) {
        pos += strlen(key);
        state->last_chat_id = (int64_t)strtoll(pos, NULL, 10);
      }
    }
  }
  if (!response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* json = "{\"ok\":true,\"result\":{\"message_id\":42}}";
  response->status = 200;
  response->body = fpv_strdup(json);
  response->body_size = response->body ? strlen(response->body) : 0;
  return response->body ? FPV_OK : FPV_ERR_OUT_OF_MEMORY;
}

static bool test_telegram_send_mock(void) {
  char* root = fpv_test_create_temp_dir("fpvtg");
  if (!fpv_test_expect(root != NULL, "temp root dir failed")) {
    return false;
  }
  char* data_dir = fpv_test_join_path(root, "data");
  char* config_dir = fpv_test_join_path(root, "config");
  char* logs_dir = fpv_test_join_path(root, "logs");
  char* plugins_dir = fpv_test_join_path(root, "plugins");
  if (!data_dir || !config_dir || !logs_dir || !plugins_dir) {
    fpv_free(data_dir);
    fpv_free(config_dir);
    fpv_free(logs_dir);
    fpv_free(plugins_dir);
    fpv_test_remove_dir(root);
    fpv_free(root);
    return false;
  }

  fpv_storage_layout_t layout;
  fpv_result_t prep = fpv_storage_prepare(
      data_dir, config_dir, logs_dir, plugins_dir, &layout);
  fpv_free(data_dir);
  fpv_free(config_dir);
  fpv_free(logs_dir);
  fpv_free(plugins_dir);
  if (!fpv_test_expect(prep == FPV_OK, "storage prepare failed")) {
    fpv_test_cleanup_storage(&layout, root);
    fpv_free(root);
    return false;
  }

  char* notifications_path = fpv_test_join_path(layout.cache_dir, "notifications.json");
  bool wrote = notifications_path &&
      fpv_test_write_file(notifications_path, "{\"9001\":{\"2\":1}}");
  if (!fpv_test_expect(wrote, "notifications write failed")) {
    fpv_test_remove_file(notifications_path);
    fpv_free(notifications_path);
    fpv_test_cleanup_storage(&layout, root);
    fpv_storage_layout_destroy(&layout);
    fpv_free(root);
    return false;
  }

  char* locales_dir = fpv_test_join_path(FPV_TEST_DATA_DIR, "locales");
  if (!fpv_test_expect(locales_dir != NULL, "locales path failed")) {
    fpv_test_remove_file(notifications_path);
    fpv_free(notifications_path);
    fpv_test_cleanup_storage(&layout, root);
    fpv_storage_layout_destroy(&layout);
    fpv_free(root);
    return false;
  }
  fpv_telegram_service_t* service = fpv_telegram_service_create(
      "TOKEN",
      NULL,
      &layout,
      locales_dir,
      "eng",
      NULL,
      NULL,
      NULL,
      NULL,
      NULL);
  fpv_free(locales_dir);
  if (!fpv_test_expect(service != NULL, "telegram service create failed")) {
    fpv_test_remove_file(notifications_path);
    fpv_free(notifications_path);
    fpv_test_cleanup_storage(&layout, root);
    fpv_storage_layout_destroy(&layout);
    fpv_free(root);
    return false;
  }

  fpv_tg_mock_state_t state;
  memset(&state, 0, sizeof(state));
  fpv_telegram_set_http_mock(fpv_test_tg_http_mock, NULL, &state);

  fpv_message_t message;
  memset(&message, 0, sizeof(message));
  message.id = (char*)"1";
  message.chat_id = (char*)"123";
  message.chat_name = (char*)"Buyer";
  message.sender_id = (char*)"456";
  message.sender_name = (char*)"Buyer";
  message.text = (char*)"Hello";
  message.by_bot = false;
  message.created_at_ms = 0;
  const fpv_message_t* messages[] = {&message};

  fpv_telegram_service_notify_new_message(
      service, messages, 1, 123, "Buyer", 999);

  fpv_telegram_clear_http_mock();

  bool ok = fpv_test_expect(state.call_count == 1, "telegram http call count mismatch");
  ok = ok && fpv_test_expect(state.last_chat_id == 9001, "telegram chat id mismatch");

  fpv_telegram_service_destroy(service);
  fpv_test_remove_file(notifications_path);
  fpv_free(notifications_path);
  fpv_test_cleanup_storage(&layout, root);
  fpv_storage_layout_destroy(&layout);
  fpv_free(root);
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
      {"funpay_refresh_mock", test_funpay_refresh_mock},
      {"telegram_send_mock", test_telegram_send_mock},
  };
  return fpv_run_tests(tests, sizeof(tests) / sizeof(tests[0]));
}
