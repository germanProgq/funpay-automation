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

#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_ini.h"
#include "fpv_core/fpv_types.h"
#include "fpv_json.h"
#include "fpv_localization.h"
#include "fpv_string.h"

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
  };
  return fpv_run_tests(tests, sizeof(tests) / sizeof(tests[0]));
}
