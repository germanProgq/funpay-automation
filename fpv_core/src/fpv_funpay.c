/* FunPay Vertex FunPay API integration. */

#include "fpv_core/fpv_funpay.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "fpv_funpay_http.h"
#include "fpv_html.h"
#include "fpv_json.h"
#include "fpv_core/fpv_log.h"
#include "fpv_platform.h"
#include "fpv_string.h"
#include "fpv_time.h"

#define FPV_FUNPAY_BASE_URL "https://funpay.com/"

static const char* FPV_FUNPAY_BOT_PREFIX = "\xE2\x81\xA4";
static const char* FPV_FUNPAY_IMAGE_TEXT =
    "\xD0\x98\xD0\xB7\xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB6"
    "\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5";
static const char* FPV_FUNPAY_TODAY =
    "\xD1\x81\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F";
static const char* FPV_FUNPAY_YESTERDAY =
    "\xD0\xB2\xD1\x87\xD0\xB5\xD1\x80\xD0\xB0";
static const char* FPV_FUNPAY_TODAY_EN = "today";
static const char* FPV_FUNPAY_YESTERDAY_EN = "yesterday";

static fpv_result_t fpv_funpay_account_request_chats(
    fpv_funpay_account_t* account,
    fpv_chat_t*** out_chats,
    size_t* out_count,
    fpv_funpay_error_t* error);
static fpv_result_t fpv_funpay_parse_messages(
    fpv_funpay_account_t* account,
    const fpv_json_value_t* messages,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count);
static xmlNode* fpv_funpay_find_first_by_class_substr(
    xmlNode* node,
    const char* tag,
    const char* class_substr);
static char* fpv_funpay_find_first_attr_value(
    xmlNode* node,
    const char* tag,
    const char* attr_name);
static bool fpv_funpay_debug_messages(const fpv_funpay_account_t* account);
static void fpv_funpay_logf(
    fpv_funpay_account_t* account,
    fpv_log_level_t level,
    const char* format,
    ...);
static void fpv_funpay_log_html_snippet(
    fpv_funpay_account_t* account,
    const char* html);

typedef struct fpv_funpay_month {
  const char* name;
  int month;
} fpv_funpay_month_t;

static const fpv_funpay_month_t fpv_funpay_months[] = {
    {"\xD1\x8F\xD0\xBD\xD0\xB2\xD0\xB0\xD1\x80\xD1\x8F", 1},
    {"\xD1\x84\xD0\xB5\xD0\xB2\xD1\x80\xD0\xB0\xD0\xBB\xD1\x8F", 2},
    {"\xD0\xBC\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0", 3},
    {"\xD0\xB0\xD0\xBF\xD1\x80\xD0\xB5\xD0\xBB\xD1\x8F", 4},
    {"\xD0\xBC\xD0\xB0\xD1\x8F", 5},
    {"\xD0\xB8\xD1\x8E\xD0\xBD\xD1\x8F", 6},
    {"\xD0\xB8\xD1\x8E\xD0\xBB\xD1\x8F", 7},
    {"\xD0\xB0\xD0\xB2\xD0\xB3\xD1\x83\xD1\x81\xD1\x82\xD0\xB0", 8},
    {"\xD1\x81\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F", 9},
    {"\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F", 10},
    {"\xD0\xBD\xD0\xBE\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F", 11},
    {"\xD0\xB4\xD0\xB5\xD0\xBA\xD0\xB0\xD0\xB1\xD1\x80\xD1\x8F", 12},
    {"january", 1},
    {"february", 2},
    {"march", 3},
    {"april", 4},
    {"may", 5},
    {"june", 6},
    {"july", 7},
    {"august", 8},
    {"september", 9},
    {"october", 10},
    {"november", 11},
    {"december", 12}};

typedef struct fpv_funpay_chat_store {
  fpv_chat_t** chats;
  size_t count;
} fpv_funpay_chat_store_t;

typedef enum fpv_funpay_subcategory_type {
  FPV_FUNPAY_SUBCATEGORY_COMMON = 0,
  FPV_FUNPAY_SUBCATEGORY_CURRENCY = 1
} fpv_funpay_subcategory_type_t;

typedef struct fpv_funpay_category {
  uint64_t id;
  char* name;
} fpv_funpay_category_t;

typedef struct fpv_funpay_subcategory {
  uint64_t id;
  fpv_funpay_subcategory_type_t type;
  uint64_t category_id;
  char* name;
} fpv_funpay_subcategory_t;

typedef struct fpv_funpay_catalog {
  fpv_funpay_category_t* categories;
  size_t category_count;
  fpv_funpay_subcategory_t* subcategories;
  size_t subcategory_count;
} fpv_funpay_catalog_t;

typedef struct fpv_funpay_account {
  char* golden_key;
  char* user_agent;
  fpv_funpay_http_client_t* http;
  char* csrf_token;
  char* phpsessid;
  char* username;
  char* currency;
  uint64_t id;
  uint32_t active_sales;
  uint32_t active_purchases;
  uint64_t last_update_ms;
  bool initiated;
  fpv_funpay_chat_store_t chats;
  fpv_funpay_catalog_t catalog;
  fpv_mutex_t request_mutex;
  struct fpv_funpay_runner* runner;
  fpv_logger_t* logger;
  bool debug_log_messages;
} fpv_funpay_account_t;

typedef struct fpv_funpay_order_state {
  char* id;
  fpv_order_status_t status;
} fpv_funpay_order_state_t;

typedef struct fpv_funpay_last_message {
  uint64_t chat_id;
  char* text;
  char* time_text;
} fpv_funpay_last_message_t;

typedef struct fpv_funpay_last_message_id {
  uint64_t chat_id;
  uint64_t message_id;
} fpv_funpay_last_message_id_t;

typedef struct fpv_funpay_init_message {
  uint64_t chat_id;
  char* text;
} fpv_funpay_init_message_t;

typedef struct fpv_funpay_bot_ids {
  uint64_t chat_id;
  uint64_t* ids;
  size_t count;
} fpv_funpay_bot_ids_t;

typedef struct fpv_funpay_runner {
  fpv_funpay_account_t* account;
  bool make_msg_requests;
  bool make_order_requests;
  bool first_request;
  char* last_msg_tag;
  char* last_order_tag;
  fpv_funpay_order_state_t* orders;
  size_t order_count;
  fpv_funpay_last_message_t* last_messages;
  size_t last_message_count;
  fpv_funpay_last_message_id_t* last_message_ids;
  size_t last_message_id_count;
  fpv_funpay_init_message_t* init_messages;
  size_t init_message_count;
  fpv_funpay_bot_ids_t* bot_ids;
  size_t bot_id_count;
} fpv_funpay_runner_t;

typedef struct fpv_string_builder {
  char* data;
  size_t length;
  size_t capacity;
} fpv_string_builder_t;

typedef struct fpv_funpay_chat_request {
  uint64_t chat_id;
  const char* chat_name;
} fpv_funpay_chat_request_t;

typedef struct fpv_funpay_chat_history {
  uint64_t chat_id;
  char* chat_name;
  fpv_message_t** messages;
  size_t message_count;
} fpv_funpay_chat_history_t;

static void fpv_funpay_sb_reset(fpv_string_builder_t* builder) {
  if (!builder) {
    return;
  }
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
}

static bool fpv_funpay_sb_reserve(fpv_string_builder_t* builder, size_t extra) {
  size_t required = builder->length + extra + 1;
  if (required <= builder->capacity) {
    return true;
  }
  size_t next = builder->capacity == 0 ? 64 : builder->capacity;
  while (next < required) {
    next *= 2;
  }
  char* grown = (char*)realloc(builder->data, next);
  if (!grown) {
    return false;
  }
  builder->data = grown;
  builder->capacity = next;
  return true;
}

static bool fpv_funpay_sb_append(
    fpv_string_builder_t* builder,
    const char* text) {
  if (!text) {
    return true;
  }
  size_t len = strlen(text);
  if (!fpv_funpay_sb_reserve(builder, len)) {
    return false;
  }
  memcpy(builder->data + builder->length, text, len);
  builder->length += len;
  builder->data[builder->length] = '\0';
  return true;
}

static bool fpv_funpay_sb_append_n(
    fpv_string_builder_t* builder,
    const char* text,
    size_t len) {
  if (!text || len == 0) {
    return true;
  }
  if (!fpv_funpay_sb_reserve(builder, len)) {
    return false;
  }
  memcpy(builder->data + builder->length, text, len);
  builder->length += len;
  builder->data[builder->length] = '\0';
  return true;
}

static bool fpv_funpay_sb_append_format(
    fpv_string_builder_t* builder,
    const char* fmt,
    ...) {
  va_list args;
  va_start(args, fmt);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    va_end(copy);
    return false;
  }
  if (!fpv_funpay_sb_reserve(builder, (size_t)needed)) {
    va_end(copy);
    return false;
  }
  vsnprintf(
      builder->data + builder->length,
      builder->capacity - builder->length,
      fmt,
      copy);
  builder->length += (size_t)needed;
  builder->data[builder->length] = '\0';
  va_end(copy);
  return true;
}

static char* fpv_funpay_sb_detach(fpv_string_builder_t* builder) {
  if (!builder || !builder->data) {
    return NULL;
  }
  char* result = builder->data;
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
  return result;
}

static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static bool fpv_funpay_is_ascii(const char* value) {
  if (!value) {
    return true;
  }
  while (*value) {
    if ((unsigned char)*value > 0x7F) {
      return false;
    }
    value++;
  }
  return true;
}

static char* fpv_funpay_ascii_lower(const char* value) {
  size_t len = strlen(value);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (char)fpv_ascii_tolower((unsigned char)value[i]);
  }
  out[len] = '\0';
  return out;
}

static void fpv_funpay_trim(char* text) {
  if (!text || !text[0]) {
    return;
  }
  size_t len = strlen(text);
  size_t start = 0;
  while (start < len && isspace((unsigned char)text[start])) {
    start++;
  }
  size_t end = len;
  while (end > start && isspace((unsigned char)text[end - 1])) {
    end--;
  }
  if (start > 0) {
    memmove(text, text + start, end - start);
  }
  text[end - start] = '\0';
}

/* Truncate UTF-8 text to a max codepoint count without splitting sequences. */
static char* fpv_funpay_truncate_utf8(const char* text, size_t max_chars) {
  if (!text) {
    return NULL;
  }
  if (max_chars == 0) {
    return fpv_strdup("");
  }
  size_t text_len = strlen(text);
  size_t count = 0;
  size_t index = 0;
  while (text[index] != '\0' && count < max_chars) {
    unsigned char lead = (unsigned char)text[index];
    size_t step = 1;
    if (lead < 0x80) {
      step = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      step = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      step = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      step = 4;
    } else {
      step = 1;
    }
    if (step > 1) {
      for (size_t i = 1; i < step; i++) {
        if (index + i >= text_len) {
          step = 1;
          break;
        }
        unsigned char next = (unsigned char)text[index + i];
        if ((next & 0xC0) != 0x80) {
          step = 1;
          break;
        }
      }
    }
    index += step;
    count++;
  }
  return fpv_strdup_n(text, index);
}

static void fpv_funpay_error_set(
    fpv_funpay_error_t* error,
    fpv_funpay_error_code_t code,
    const char* message,
    const char* url,
    const char* method,
    long status) {
  if (!error) {
    return;
  }
  fpv_funpay_error_clear(error);
  error->code = code;
  error->http_status = status;
  error->message = fpv_strdup(message);
  error->url = fpv_strdup(url);
  error->method = fpv_strdup(method);
}

void fpv_funpay_error_clear(fpv_funpay_error_t* error) {
  if (!error) {
    return;
  }
  fpv_free(error->url);
  fpv_free(error->method);
  fpv_free(error->message);
  error->url = NULL;
  error->method = NULL;
  error->message = NULL;
  error->http_status = 0;
  error->code = FPV_FUNPAY_OK;
}

void fpv_funpay_balance_clear(fpv_funpay_balance_t* balance) {
  if (!balance) {
    return;
  }
  memset(balance, 0, sizeof(*balance));
}

void fpv_funpay_order_detail_clear(fpv_funpay_order_detail_t* detail) {
  if (!detail) {
    return;
  }
  fpv_free(detail->id);
  fpv_free(detail->currency);
  fpv_free(detail->title);
  fpv_free(detail->description);
  fpv_free(detail->buyer_username);
  fpv_free(detail->seller_username);
  fpv_free(detail->review.text);
  fpv_free(detail->review.reply);
  memset(detail, 0, sizeof(*detail));
}

static char* fpv_funpay_random_tag(void) {
  static bool seeded = false;
  if (!seeded) {
    srand((unsigned int)fpv_time_now_ms());
    seeded = true;
  }
  static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  char* tag = (char*)malloc(11);
  if (!tag) {
    return NULL;
  }
  for (size_t i = 0; i < 10; i++) {
    tag[i] = alphabet[rand() % (sizeof(alphabet) - 1)];
  }
  tag[10] = '\0';
  return tag;
}

static bool fpv_funpay_is_unreserved(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
    return true;
  }
  if (c >= '0' && c <= '9') {
    return true;
  }
  return c == '-' || c == '_' || c == '.' || c == '~';
}

static char* fpv_funpay_url_encode(const char* value) {
  if (!value) {
    return fpv_strdup("");
  }
  fpv_string_builder_t builder;
  fpv_funpay_sb_reset(&builder);
  for (const unsigned char* ptr = (const unsigned char*)value; *ptr; ptr++) {
    if (fpv_funpay_is_unreserved((char)*ptr)) {
      fpv_funpay_sb_append_n(&builder, (const char*)ptr, 1);
    } else {
      fpv_funpay_sb_append_format(&builder, "%%%02X", *ptr);
    }
  }
  return fpv_funpay_sb_detach(&builder);
}

static char* fpv_funpay_form_encode(
    const char* const* keys,
    const char* const* values,
    size_t count) {
  fpv_string_builder_t builder;
  fpv_funpay_sb_reset(&builder);
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      fpv_funpay_sb_append(&builder, "&");
    }
    char* key = fpv_funpay_url_encode(keys[i]);
    char* val = fpv_funpay_url_encode(values[i]);
    if (!key || !val) {
      fpv_free(key);
      fpv_free(val);
      fpv_free(builder.data);
      builder.data = NULL;
      return NULL;
    }
    fpv_funpay_sb_append(&builder, key);
    fpv_funpay_sb_append(&builder, "=");
    fpv_funpay_sb_append(&builder, val);
    fpv_free(key);
    fpv_free(val);
  }
  return fpv_funpay_sb_detach(&builder);
}

typedef struct fpv_funpay_form_field {
  char* key;
  char* value;
} fpv_funpay_form_field_t;

typedef struct fpv_funpay_form {
  fpv_funpay_form_field_t* fields;
  size_t count;
} fpv_funpay_form_t;

static void fpv_funpay_form_clear(fpv_funpay_form_t* form) {
  if (!form) {
    return;
  }
  for (size_t i = 0; i < form->count; i++) {
    fpv_free(form->fields[i].key);
    fpv_free(form->fields[i].value);
  }
  fpv_free(form->fields);
  form->fields = NULL;
  form->count = 0;
}

static bool fpv_funpay_form_set(
    fpv_funpay_form_t* form,
    const char* key,
    const char* value) {
  if (!form || !key) {
    return false;
  }
  for (size_t i = 0; i < form->count; i++) {
    if (form->fields[i].key &&
        strcmp(form->fields[i].key, key) == 0) {
      char* next_value = fpv_strdup(value ? value : "");
      if (!next_value) {
        return false;
      }
      fpv_free(form->fields[i].value);
      form->fields[i].value = next_value;
      return true;
    }
  }
  fpv_funpay_form_field_t* grown =
      (fpv_funpay_form_field_t*)realloc(
          form->fields,
          (form->count + 1) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  form->fields = grown;
  form->fields[form->count].key = fpv_strdup(key);
  form->fields[form->count].value = fpv_strdup(value ? value : "");
  if (!form->fields[form->count].key ||
      !form->fields[form->count].value) {
    fpv_free(form->fields[form->count].key);
    fpv_free(form->fields[form->count].value);
    return false;
  }
  form->count++;
  return true;
}

static char* fpv_funpay_form_encode_fields(
    const fpv_funpay_form_t* form) {
  if (!form || form->count == 0) {
    return fpv_strdup("");
  }
  fpv_string_builder_t builder;
  fpv_funpay_sb_reset(&builder);
  for (size_t i = 0; i < form->count; i++) {
    if (i > 0) {
      fpv_funpay_sb_append(&builder, "&");
    }
    const char* key_raw = form->fields[i].key ? form->fields[i].key : "";
    const char* val_raw = form->fields[i].value ? form->fields[i].value : "";
    char* key = fpv_funpay_url_encode(key_raw);
    char* val = fpv_funpay_url_encode(val_raw);
    if (!key || !val) {
      fpv_free(key);
      fpv_free(val);
      fpv_free(builder.data);
      builder.data = NULL;
      return NULL;
    }
    fpv_funpay_sb_append(&builder, key);
    fpv_funpay_sb_append(&builder, "=");
    fpv_funpay_sb_append(&builder, val);
    fpv_free(key);
    fpv_free(val);
  }
  return fpv_funpay_sb_detach(&builder);
}

static char* fpv_funpay_build_url(const char* path) {
  if (!path) {
    return NULL;
  }
  if (strncmp(path, "http://", 7) == 0 ||
      strncmp(path, "https://", 8) == 0) {
    return fpv_strdup(path);
  }

  size_t base_len = strlen(FPV_FUNPAY_BASE_URL);
  size_t path_len = strlen(path);
  bool base_slash = base_len > 0 && FPV_FUNPAY_BASE_URL[base_len - 1] == '/';
  bool path_slash = path[0] == '/';
  const char* path_start = path;
  size_t path_copy_len = path_len;
  if (base_slash && path_slash) {
    path_start = path + 1;
    path_copy_len = path_len - 1;
  }
  bool needs_slash = !base_slash && !path_slash;
  size_t size = base_len + path_copy_len + (needs_slash ? 1 : 0) + 1;
  char* url = (char*)malloc(size);
  if (!url) {
    return NULL;
  }
  memcpy(url, FPV_FUNPAY_BASE_URL, base_len);
  size_t offset = base_len;
  if (needs_slash) {
    url[offset++] = '/';
  }
  memcpy(url + offset, path_start, path_copy_len);
  url[offset + path_copy_len] = '\0';
  return url;
}

static bool fpv_funpay_time_is_tag(const char* value) {
  if (!value || strlen(value) != 5) {
    return false;
  }
  return isdigit((unsigned char)value[0]) &&
      isdigit((unsigned char)value[1]) &&
      value[2] == ':' &&
      isdigit((unsigned char)value[3]) &&
      isdigit((unsigned char)value[4]);
}

static int fpv_funpay_month_from_name(const char* name) {
  if (!name) {
    return 0;
  }
  bool ascii = fpv_funpay_is_ascii(name);
  char* lowered = ascii ? fpv_funpay_ascii_lower(name) : NULL;
  const char* key = ascii && lowered ? lowered : name;
  int month = 0;
  for (size_t i = 0; i < sizeof(fpv_funpay_months) / sizeof(fpv_funpay_months[0]); i++) {
    if (strcmp(fpv_funpay_months[i].name, key) == 0) {
      month = fpv_funpay_months[i].month;
      break;
    }
  }
  fpv_free(lowered);
  return month;
}

static uint64_t fpv_funpay_parse_order_date(const char* text) {
  if (!text) {
    return 0;
  }

  char* trimmed = fpv_strdup(text);
  if (!trimmed) {
    return 0;
  }
  fpv_funpay_trim(trimmed);

  const char* comma = strchr(trimmed, ',');
  if (!comma) {
    fpv_free(trimmed);
    return 0;
  }

  size_t date_len = (size_t)(comma - trimmed);
  char* date_part = fpv_strdup_n(trimmed, date_len);
  char* time_part = fpv_strdup(comma + 1);
  if (!date_part || !time_part) {
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    return 0;
  }
  fpv_funpay_trim(date_part);
  fpv_funpay_trim(time_part);

  int hour = 0;
  int minute = 0;
  if (sscanf(time_part, "%d:%d", &hour, &minute) != 2) {
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    return 0;
  }

  time_t now = time(NULL);
  struct tm current;
#if defined(_WIN32)
  localtime_s(&current, &now);
#else
  localtime_r(&now, &current);
#endif

  struct tm result = current;
  result.tm_hour = hour;
  result.tm_min = minute;
  result.tm_sec = 0;
  result.tm_isdst = -1;

  char* date_lower = fpv_funpay_is_ascii(date_part)
      ? fpv_funpay_ascii_lower(date_part)
      : NULL;

  if (strcmp(date_part, FPV_FUNPAY_TODAY) == 0 ||
      (date_lower && strcmp(date_lower, FPV_FUNPAY_TODAY_EN) == 0)) {
    time_t ts = mktime(&result);
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    fpv_free(date_lower);
    return ts == (time_t)-1 ? 0 : (uint64_t)ts * 1000ULL;
  }

  if (strcmp(date_part, FPV_FUNPAY_YESTERDAY) == 0 ||
      (date_lower && strcmp(date_lower, FPV_FUNPAY_YESTERDAY_EN) == 0)) {
    result.tm_mday -= 1;
    time_t ts = mktime(&result);
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    fpv_free(date_lower);
    return ts == (time_t)-1 ? 0 : (uint64_t)ts * 1000ULL;
  }

  int day = 0;
  int year = 0;
  char month_name[32] = {0};
  int items = sscanf(date_part, "%d %31s %d", &day, month_name, &year);
  if (items < 2) {
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    fpv_free(date_lower);
    return 0;
  }
  if (items == 2) {
    year = current.tm_year + 1900;
  }
  int month = fpv_funpay_month_from_name(month_name);
  if (month == 0) {
    fpv_free(date_part);
    fpv_free(time_part);
    fpv_free(trimmed);
    fpv_free(date_lower);
    return 0;
  }

  result.tm_year = year - 1900;
  result.tm_mon = month - 1;
  result.tm_mday = day;

  time_t ts = mktime(&result);
  fpv_free(date_part);
  fpv_free(time_part);
  fpv_free(trimmed);
  fpv_free(date_lower);
  return ts == (time_t)-1 ? 0 : (uint64_t)ts * 1000ULL;
}

static const char* fpv_funpay_currency_from_symbol(const char* symbol) {
  if (!symbol) {
    return NULL;
  }
  if (strcmp(symbol, "\xE2\x82\xBD") == 0) {
    return "RUB";
  }
  if (strcmp(symbol, "$") == 0) {
    return "USD";
  }
  if (strcmp(symbol, "\xE2\x82\xAC") == 0) {
    return "EUR";
  }
  return NULL;
}

static bool fpv_funpay_parse_price(
    const char* text,
    double* amount,
    const char** currency) {
  if (!text || !amount || !currency) {
    return false;
  }
  *currency = NULL;
  *amount = 0.0;

  char* copy = fpv_strdup(text);
  if (!copy) {
    return false;
  }
  fpv_funpay_trim(copy);

  size_t len = strlen(copy);
  size_t end = len;
  while (end > 0 && isspace((unsigned char)copy[end - 1])) {
    end--;
  }

  const char* symbol = NULL;
  if (end >= 3 && (unsigned char)copy[end - 3] == 0xE2 &&
      (unsigned char)copy[end - 2] == 0x82) {
    symbol = copy + end - 3;
    *currency = fpv_funpay_currency_from_symbol(symbol);
  } else if (end >= 1) {
    symbol = copy + end - 1;
    *currency = fpv_funpay_currency_from_symbol(symbol);
  }

  char* price_end = NULL;
  if (*currency) {
    price_end = (char*)symbol;
  } else {
    price_end = copy + end;
  }

  while (price_end > copy && isspace((unsigned char)price_end[-1])) {
    price_end--;
  }
  *price_end = '\0';

  for (char* ptr = copy; *ptr; ptr++) {
    if (*ptr == ',') {
      *ptr = '.';
    }
  }

  char* scan = copy;
  for (char* ptr = copy; *ptr; ptr++) {
    if (*ptr != ' ') {
      *scan++ = *ptr;
    }
  }
  *scan = '\0';

  char* endptr = NULL;
  double parsed = strtod(copy, &endptr);
  if (!endptr || *endptr != '\0') {
    fpv_free(copy);
    return false;
  }

  *amount = parsed;
  fpv_free(copy);
  return true;
}

static bool fpv_funpay_parse_user_id_from_href(
    const char* href,
    uint64_t* out_id) {
  if (!href || !out_id) {
    return false;
  }
  const char* marker = strstr(href, "/users/");
  if (!marker) {
    marker = strstr(href, "users/");
  }
  if (!marker) {
    return false;
  }
  marker = strchr(marker, '/');
  if (!marker) {
    return false;
  }
  marker++;
  if (strncmp(marker, "users/", 6) == 0) {
    marker += 6;
  }
  if (!*marker) {
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(marker, &end, 10);
  if (!end) {
    return false;
  }
  *out_id = (uint64_t)parsed;
  return parsed > 0;
}

static int fpv_funpay_parse_rating_from_class(const char* class_value) {
  if (!class_value) {
    return 0;
  }
  const char* cursor = class_value;
  while ((cursor = strstr(cursor, "rating")) != NULL) {
    cursor += strlen("rating");
    if (*cursor >= '0' && *cursor <= '9') {
      int value = 0;
      while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (*cursor - '0');
        cursor++;
      }
      if (value >= 1 && value <= 5) {
        return value;
      }
    }
  }
  return 0;
}

static fpv_funpay_http_header_t fpv_funpay_header(
    const char* name,
    const char* value) {
  fpv_funpay_http_header_t header;
  header.name = name;
  header.value = value;
  return header;
}

static xmlNode* fpv_funpay_find_first_tag(xmlNode* node, const char* tag) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (cur->name && strcmp((const char*)cur->name, tag) == 0) {
        return cur;
      }
      if (cur->children) {
        xmlNode* found = fpv_funpay_find_first_tag(cur->children, tag);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}

static void fpv_funpay_collect_tag(
    xmlNode* node,
    const char* tag,
    fpv_html_node_list_t* list) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (cur->name && strcmp((const char*)cur->name, tag) == 0) {
        xmlNode** grown = (xmlNode**)realloc(
            list->nodes,
            (list->count + 1) * sizeof(*grown));
        if (!grown) {
          return;
        }
        list->nodes = grown;
        list->nodes[list->count++] = cur;
      }
      if (cur->children) {
        fpv_funpay_collect_tag(cur->children, tag, list);
      }
    }
  }
}

static fpv_html_node_list_t fpv_funpay_find_all_tag(
    xmlNode* node,
    const char* tag) {
  fpv_html_node_list_t list;
  list.nodes = NULL;
  list.count = 0;
  if (!node || !tag) {
    return list;
  }
  fpv_funpay_collect_tag(node, tag, &list);
  return list;
}

static xmlNode* fpv_funpay_find_first_by_attr(
    xmlNode* node,
    const char* tag,
    const char* attr,
    const char* value) {
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        xmlChar* attr_value = xmlGetProp(cur, (const xmlChar*)attr);
        if (attr_value) {
          bool match = true;
          if (value && strcmp((const char*)attr_value, value) != 0) {
            match = false;
          }
          xmlFree(attr_value);
          if (match) {
            return cur;
          }
        }
      }
      if (cur->children) {
        xmlNode* found =
            fpv_funpay_find_first_by_attr(cur->children, tag, attr, value);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}

static bool fpv_funpay_parse_uint64_attr(
    xmlNode* node,
    const char* attr,
    uint64_t* out_value) {
  if (!node || !attr || !out_value) {
    return false;
  }
  char* value = fpv_html_node_attr(node, attr);
  if (!value || !value[0]) {
    fpv_free(value);
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(value, &end, 10);
  fpv_free(value);
  if (!end || *end != '\0') {
    return false;
  }
  *out_value = (uint64_t)parsed;
  return true;
}

static bool fpv_funpay_parse_double_attr(
    xmlNode* node,
    const char* attr,
    double* out_value) {
  if (!node || !attr || !out_value) {
    return false;
  }
  char* value = fpv_html_node_attr(node, attr);
  if (!value || !value[0]) {
    fpv_free(value);
    return false;
  }
  char* end = NULL;
  double parsed = strtod(value, &end);
  fpv_free(value);
  if (!end || *end != '\0') {
    return false;
  }
  *out_value = parsed;
  return true;
}

static bool fpv_funpay_list_contains_u64(
    const uint64_t* values,
    size_t count,
    uint64_t value) {
  for (size_t i = 0; i < count; i++) {
    if (values[i] == value) {
      return true;
    }
  }
  return false;
}

static fpv_result_t fpv_funpay_account_request(
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

static fpv_result_t fpv_funpay_account_store_chat(
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

static fpv_chat_t** fpv_funpay_account_clone_chats(
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
  while (end > href && end[-1] == '/') {
    end--;
  }
  const char* start = end;
  while (start > href && isdigit((unsigned char)start[-1])) {
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
  fpv_free(temp);
  if (!parse_end || *parse_end != '\0') {
    return false;
  }
  *out_id = (uint64_t)parsed;
  return true;
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
        if (fpv_funpay_parse_href_id(href, &sub_id)) {
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
  return account;
}

void fpv_funpay_account_destroy(fpv_funpay_account_t* account) {
  if (!account) {
    return;
  }
  fpv_free(account->golden_key);
  fpv_free(account->user_agent);
  fpv_funpay_http_client_destroy(account->http);
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
    bool debug_messages) {
  if (!account) {
    return;
  }
  account->logger = logger;
  account->debug_log_messages = debug_messages;
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
      fpv_html_find_first_by_class(root, "div", "user-link-name");
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

  fpv_html_node_list_t items = fpv_html_find_all_by_class(root, "a", "tc-item");
  fpv_lot_t** lots = NULL;
  size_t count = 0;
  for (size_t i = 0; i < items.count; i++) {
    xmlNode* node = items.nodes[i];
    if (!node) {
      continue;
    }
    uint64_t lot_id = 0;
    if (!fpv_funpay_parse_uint64_attr(node, "data-offer", &lot_id)) {
      continue;
    }
    char lot_id_buf[32];
    snprintf(lot_id_buf, sizeof(lot_id_buf), "%" PRIu64, lot_id);
    xmlNode* title_node =
        fpv_html_find_first_by_class(node->children, "div", "tc-desc-text");
    char* title = title_node ? fpv_html_node_text(title_node) : NULL;
    bool active = !fpv_html_node_has_class(node, "warning");
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
  }

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

  fpv_html_node_list_t containers =
      fpv_html_find_all_by_class(root, "div", "offer-list-title-container");
  fpv_funpay_lot_section_t* sections = NULL;
  size_t count = 0;
  for (size_t i = 0; i < containers.count; i++) {
    xmlNode* node = containers.nodes[i];
    xmlNode* link = fpv_funpay_find_first_tag(node->children, "a");
    if (!link) {
      continue;
    }
    char* href = fpv_html_node_attr(link, "href");
    if (!href) {
      continue;
    }
    bool is_currency_section = strstr(href, "chips") != NULL;
    uint64_t sub_id = 0;
    bool parsed = fpv_funpay_parse_href_id(href, &sub_id);
    fpv_free(href);
    if (!parsed) {
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
  }

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
      "https://funpay.com/lots/offerEdit?offer=%" PRIu64,
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

  fpv_funpay_form_t form;
  memset(&form, 0, sizeof(form));
  bool form_ok = true;
  if (!fpv_funpay_form_set(&form, "active", "") ||
      !fpv_funpay_form_set(&form, "deactivate_after_sale", "")) {
    form_ok = false;
  }

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
    char* type = fpv_html_node_attr(node, "type");
    if (type && strcmp(type, "checkbox") == 0) {
      char* checked = fpv_html_node_attr(node, "checked");
      if (checked) {
        if (form_ok && !fpv_funpay_form_set(&form, name, "on")) {
          form_ok = false;
        }
        fpv_free(checked);
      }
      fpv_free(type);
      fpv_free(name);
      continue;
    }
    if (type && strcmp(type, "radio") == 0) {
      char* checked = fpv_html_node_attr(node, "checked");
      if (checked) {
        char* value = fpv_html_node_attr(node, "value");
        if (form_ok && !fpv_funpay_form_set(&form, name, value ? value : "")) {
          form_ok = false;
        }
        fpv_free(value);
        fpv_free(checked);
      }
      fpv_free(type);
      fpv_free(name);
      continue;
    }
    fpv_free(type);
    if (strcmp(name, "active") == 0 ||
        strcmp(name, "deactivate_after_sale") == 0) {
      fpv_free(name);
      continue;
    }
    char* value = fpv_html_node_attr(node, "value");
    if (form_ok && !fpv_funpay_form_set(&form, name, value ? value : "")) {
      form_ok = false;
    }
    fpv_free(value);
    fpv_free(name);
  }
  fpv_html_node_list_destroy(&inputs);

  fpv_html_node_list_t textareas = fpv_funpay_find_all_tag(root, "textarea");
  for (size_t i = 0; i < textareas.count; i++) {
    xmlNode* node = textareas.nodes[i];
    if (!node) {
      continue;
    }
    char* name = fpv_html_node_attr(node, "name");
    if (!name || !name[0]) {
      fpv_free(name);
      continue;
    }
    char* value = fpv_html_node_text(node);
    if (form_ok && !fpv_funpay_form_set(&form, name, value ? value : "")) {
      form_ok = false;
    }
    fpv_free(value);
    fpv_free(name);
  }
  fpv_html_node_list_destroy(&textareas);

  fpv_html_node_list_t selects = fpv_funpay_find_all_tag(root, "select");
  for (size_t i = 0; i < selects.count; i++) {
    xmlNode* node = selects.nodes[i];
    if (!node) {
      continue;
    }
    char* name = fpv_html_node_attr(node, "name");
    if (!name || !name[0]) {
      fpv_free(name);
      continue;
    }
    char* selected_value = NULL;
    for (xmlNode* child = node->children; child; child = child->next) {
      if (child->type != XML_ELEMENT_NODE) {
        continue;
      }
      if (!child->name ||
          strcmp((const char*)child->name, "option") != 0) {
        continue;
      }
      char* selected = fpv_html_node_attr(child, "selected");
      if (selected) {
        fpv_free(selected);
        selected_value = fpv_html_node_attr(child, "value");
        if (!selected_value) {
          selected_value = fpv_html_node_text(child);
        }
        break;
      }
    }
    if (selected_value) {
      if (form_ok &&
          !fpv_funpay_form_set(&form, name, selected_value)) {
        form_ok = false;
      }
      fpv_free(selected_value);
    }
    fpv_free(name);
  }
  fpv_html_node_list_destroy(&selects);

  if (form_ok &&
      !fpv_funpay_form_set(&form, "active", active ? "on" : "")) {
    form_ok = false;
  }
  if (form_ok && !fpv_funpay_form_set(&form, "location", "trade")) {
    form_ok = false;
  }
  if (account->csrf_token) {
    if (form_ok &&
        !fpv_funpay_form_set(&form, "csrf_token", account->csrf_token)) {
      form_ok = false;
    }
  }

  if (!form_ok) {
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
  uint64_t* ids = NULL;
  size_t count = 0;
  for (size_t i = 0; i < containers.count; i++) {
    xmlNode* node = containers.nodes[i];
    xmlNode* link = fpv_funpay_find_first_tag(node->children, "a");
    if (!link) {
      continue;
    }
    char* href = fpv_html_node_attr(link, "href");
    if (!href) {
      continue;
    }
    if (strstr(href, "chips")) {
      fpv_free(href);
      continue;
    }
    uint64_t sub_id = 0;
    bool parsed = fpv_funpay_parse_href_id(href, &sub_id);
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

static uint32_t fpv_funpay_parse_wait_time(const char* message) {
  if (!message) {
    return 0;
  }
  if (strcmp(message, "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD1\x81\xD0\xB5\xD0\xBA\xD1\x83\xD0\xBD\xD0\xB4\xD1\x83.") == 0) {
    return 2;
  }
  if (strcmp(message, "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBC\xD0\xB8\xD0\xBD\xD1\x83\xD1\x82\xD1\x83.") == 0) {
    return 60;
  }
  if (strcmp(message, "\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBE\xD0\xB6\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD1\x87\xD0\xB0\xD1\x81.") == 0) {
    return 3600;
  }
  if (strstr(message, "\xD1\x81\xD0\xB5\xD0\xBA")) {
    char* copy = fpv_strdup(message);
    if (!copy) {
      return 0;
    }
    char* token = strtok(copy, " ");
    uint32_t value = 0;
    while (token) {
      if (isdigit((unsigned char)token[0])) {
        value = (uint32_t)strtoul(token, NULL, 10);
        break;
      }
      token = strtok(NULL, " ");
    }
    fpv_free(copy);
    return value;
  }
  if (strstr(message, "\xD0\xBC\xD0\xB8\xD0\xBD")) {
    char* copy = fpv_strdup(message);
    if (!copy) {
      return 0;
    }
    char* token = strtok(copy, " ");
    uint32_t value = 0;
    while (token) {
      if (isdigit((unsigned char)token[0])) {
        value = (uint32_t)strtoul(token, NULL, 10);
        break;
      }
      token = strtok(NULL, " ");
    }
    fpv_free(copy);
    if (value > 0) {
      return (value - 1U) * 60U;
    }
    return 0;
  }
  if (strstr(message, "\xD1\x87\xD0\xB0\xD1\x81")) {
    char* copy = fpv_strdup(message);
    if (!copy) {
      return 0;
    }
    char* token = strtok(copy, " ");
    uint32_t value = 0;
    while (token) {
      if (isdigit((unsigned char)token[0])) {
        value = (uint32_t)strtoul(token, NULL, 10);
        break;
      }
      token = strtok(NULL, " ");
    }
    fpv_free(copy);
    return value * 3600U;
  }
  return 10;
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

static fpv_result_t fpv_funpay_parse_chat_bookmarks(
    fpv_funpay_account_t* account,
    const char* html,
    fpv_chat_t*** out_chats,
    size_t* out_count) {
  if (out_chats) {
    *out_chats = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }

  fpv_html_doc_t* doc = fpv_html_parse(html, strlen(html));
  if (!doc || !doc->doc) {
    if (fpv_funpay_debug_messages(account)) {
      fpv_funpay_logf(account, FPV_LOG_WARNING,
                      "Failed to parse chat bookmarks HTML.");
      fpv_funpay_log_html_snippet(account, html);
    }
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  fpv_html_node_list_t chats =
      fpv_html_find_all_by_class(root, "a", "contact-item");
  if (chats.count == 0) {
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&chats);
    return FPV_OK;
  }

  fpv_chat_t** list =
      (fpv_chat_t**)calloc(chats.count, sizeof(*list));
  if (!list) {
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&chats);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t count = 0;
  for (size_t i = 0; i < chats.count; i++) {
    xmlNode* node = chats.nodes[i];
    char* id_attr = fpv_html_node_attr(node, "data-id");
    if (!id_attr) {
      continue;
    }

    xmlNode* msg_node =
        fpv_html_find_first_by_class(node->children, "div", "contact-item-message");
    if (!msg_node) {
      static const char* chat_candidates[] = {
          "contact-item-message",
          "contact-item__message",
          "chat-item-message",
          "chat-item__message",
          "message-text",
          "msg-text"};
      for (size_t c = 0; c < sizeof(chat_candidates) / sizeof(chat_candidates[0]); c++) {
        msg_node = fpv_funpay_find_first_by_class_substr(
            node->children,
            NULL,
            chat_candidates[c]);
        if (msg_node) {
          break;
        }
      }
    }
    if (!msg_node) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Chat bookmark missing message node (chat_id=%s).",
            id_attr ? id_attr : "?");
      }
      fpv_free(id_attr);
      continue;
    }
    char* last_message = fpv_html_node_text(msg_node);
    if (last_message) {
      fpv_funpay_trim(last_message);
      if (!last_message[0]) {
        fpv_free(last_message);
        last_message = NULL;
      }
    }
    if (!last_message) {
      static const char* attr_candidates[] = {
          "data-message",
          "data-text",
          "data-content",
          "data-last-message"};
      for (size_t c = 0;
           c < sizeof(attr_candidates) / sizeof(attr_candidates[0]);
           c++) {
        char* attr_text =
            fpv_funpay_find_first_attr_value(msg_node, NULL, attr_candidates[c]);
        if (attr_text) {
          fpv_funpay_trim(attr_text);
          if (attr_text[0]) {
            last_message = attr_text;
            break;
          }
          fpv_free(attr_text);
        }
      }
    }
    if (!last_message) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Chat bookmark message empty (chat_id=%s).",
            id_attr ? id_attr : "?");
      }
      fpv_free(id_attr);
      continue;
    }
    if (strncmp(last_message, FPV_FUNPAY_BOT_PREFIX, strlen(FPV_FUNPAY_BOT_PREFIX)) == 0) {
      memmove(
          last_message,
          last_message + strlen(FPV_FUNPAY_BOT_PREFIX),
          strlen(last_message) - strlen(FPV_FUNPAY_BOT_PREFIX) + 1);
    }

    xmlNode* time_node =
        fpv_html_find_first_by_class(node->children, "div", "contact-item-time");
    char* time_text = time_node ? fpv_html_node_text(time_node) : NULL;
    if (time_text) {
      fpv_funpay_trim(time_text);
    }

    xmlNode* name_node =
        fpv_html_find_first_by_class(node->children, "div", "media-user-name");
    char* title = name_node ? fpv_html_node_text(name_node) : NULL;
    if (title) {
      fpv_funpay_trim(title);
    }

    bool unread = fpv_html_node_has_class(node, "unread");
    fpv_chat_t* chat = fpv_chat_create(
        id_attr,
        title,
        last_message,
        time_text,
        unread ? 1U : 0U,
        NULL);
    fpv_free(id_attr);
    fpv_free(last_message);
    fpv_free(time_text);
    fpv_free(title);
    if (!chat) {
      continue;
    }
    list[count++] = chat;
    if (account) {
      fpv_funpay_account_store_chat(account, chat);
    }
  }

  fpv_html_destroy(doc);
  fpv_html_node_list_destroy(&chats);
  if (out_chats) {
    *out_chats = list;
  } else {
    for (size_t i = 0; i < count; i++) {
      fpv_chat_destroy(list[i]);
    }
    fpv_free(list);
  }
  if (out_count) {
    *out_count = count;
  }
  return FPV_OK;
}

static fpv_result_t fpv_funpay_account_request_chats(
    fpv_funpay_account_t* account,
    fpv_chat_t*** out_chats,
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

  char* tag = fpv_funpay_random_tag();
  if (!tag) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"chat_bookmarks\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false}",
      account->id,
      tag);
  fpv_funpay_sb_append(&objects, "]");

  char* objects_json = fpv_funpay_sb_detach(&objects);
  fpv_free(tag);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
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
        "Failed to parse chat bookmarks response",
        "runner/",
        "POST",
        response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* objects_val =
      fpv_json_object_get(json, "objects");
  size_t count = fpv_json_array_size(objects_val);
  fpv_result_t parse_result = FPV_ERR_PARSE;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects_val, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type || strcmp(type, "chat_bookmarks") != 0) {
      continue;
    }
    const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
    const fpv_json_value_t* html_val = fpv_json_object_get(data_val, "html");
    const char* html = fpv_json_string(html_val);
    if (!html) {
      continue;
    }
    parse_result = fpv_funpay_parse_chat_bookmarks(
        account,
        html,
        out_chats,
        out_count);
    break;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);
  return parse_result == FPV_ERR_PARSE ? FPV_ERR_PARSE : FPV_OK;
}

static fpv_result_t fpv_funpay_account_get_orders(
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

static fpv_funpay_author_t* fpv_funpay_author_find(
    fpv_funpay_author_t* authors,
    size_t count,
    uint64_t id) {
  for (size_t i = 0; i < count; i++) {
    if (authors[i].id == id) {
      return &authors[i];
    }
  }
  return NULL;
}

static xmlNode* fpv_funpay_find_first_by_class_substr(
    xmlNode* node,
    const char* tag,
    const char* class_substr) {
  if (!node || !class_substr || !class_substr[0]) {
    return NULL;
  }
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        char* classes = fpv_html_node_attr(cur, "class");
        if (classes) {
          bool match = strstr(classes, class_substr) != NULL;
          fpv_free(classes);
          if (match) {
            return cur;
          }
        }
      }
      if (cur->children) {
        xmlNode* found =
            fpv_funpay_find_first_by_class_substr(cur->children, tag, class_substr);
        if (found) {
          return found;
        }
      }
    }
  }
  return NULL;
}

static char* fpv_funpay_find_first_attr_value(
    xmlNode* node,
    const char* tag,
    const char* attr_name) {
  if (!node || !attr_name || !attr_name[0]) {
    return NULL;
  }
  for (xmlNode* cur = node; cur; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      if (!tag || (cur->name && strcmp((const char*)cur->name, tag) == 0)) {
        char* value = fpv_html_node_attr(cur, attr_name);
        if (value && value[0]) {
          return value;
        }
        fpv_free(value);
      }
      if (cur->children) {
        char* nested =
            fpv_funpay_find_first_attr_value(cur->children, tag, attr_name);
        if (nested) {
          return nested;
        }
      }
    }
  }
  return NULL;
}

static bool fpv_funpay_debug_messages(const fpv_funpay_account_t* account) {
  return account && account->logger && account->debug_log_messages;
}

static void fpv_funpay_logf(
    fpv_funpay_account_t* account,
    fpv_log_level_t level,
    const char* format,
    ...) {
  if (!account || !account->logger || !format) {
    return;
  }
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  fpv_logger_log(account->logger, level, "funpay", buffer, fpv_time_now_ms());
}

static void fpv_funpay_log_html_snippet(
    fpv_funpay_account_t* account,
    const char* html) {
  if (!fpv_funpay_debug_messages(account) || !html) {
    return;
  }
  char snippet[512];
  size_t len = 0;
  for (const char* ptr = html; *ptr && len < sizeof(snippet) - 1; ptr++) {
    char ch = *ptr;
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      ch = ' ';
    }
    snippet[len++] = ch;
  }
  snippet[len] = '\0';
  fpv_funpay_logf(account, FPV_LOG_WARNING,
                  "Message HTML snippet: %s", snippet);
}

static fpv_result_t fpv_funpay_parse_messages(
    fpv_funpay_account_t* account,
    const fpv_json_value_t* messages,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count) {
  if (out_messages) {
    *out_messages = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!messages || !fpv_json_is_type(messages, FPV_JSON_ARRAY)) {
    return FPV_ERR_PARSE;
  }

  size_t msg_count = fpv_json_array_size(messages);
  if (msg_count == 0) {
    return FPV_OK;
  }
  fpv_message_t** list =
      (fpv_message_t**)calloc(msg_count, sizeof(*list));
  if (!list) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_author_t* authors = NULL;
  size_t author_count = 0;

  size_t out_idx = 0;
  for (size_t i = 0; i < msg_count; i++) {
    const fpv_json_value_t* msg = fpv_json_array_get(messages, i);
    const fpv_json_value_t* id_val = fpv_json_object_get(msg, "id");
    const fpv_json_value_t* author_val = fpv_json_object_get(msg, "author");
    const fpv_json_value_t* html_val = fpv_json_object_get(msg, "html");
    if (!id_val || !author_val || !html_val) {
      continue;
    }

    uint64_t id_num = 0;
    uint64_t author_id = 0;
    fpv_json_number_to_uint64(id_val, &id_num);
    fpv_json_number_to_uint64(author_val, &author_id);
    const char* html = fpv_json_string(html_val);
    if (!html) {
      continue;
    }

    fpv_html_doc_t* doc = fpv_html_parse(html, strlen(html));
    if (!doc || !doc->doc) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Failed to parse message HTML (id=%" PRIu64 ", chat=%" PRIu64 ").",
            id_num,
            chat_id);
        fpv_funpay_log_html_snippet(account, html);
      }
      fpv_html_destroy(doc);
      continue;
    }

    xmlNode* root = xmlDocGetRootElement(doc->doc);
    fpv_funpay_author_t* author = fpv_funpay_author_find(authors, author_count, author_id);
    if (!author) {
      fpv_funpay_author_t* grown = (fpv_funpay_author_t*)realloc(
          authors,
          (author_count + 1) * sizeof(*grown));
      if (!grown) {
        fpv_html_destroy(doc);
        continue;
      }
      authors = grown;
      author = &authors[author_count];
      author->id = author_id;
      author->name = NULL;
      author->badge = NULL;
      author_count++;
    }

    xmlNode* author_node =
        fpv_html_find_first_by_class(root, "div", "media-user-name");
    if (author_node) {
      xmlNode* badge_node =
          fpv_html_find_first_by_class(author_node->children, "span", NULL);
      if (badge_node && !author->badge) {
        author->badge = fpv_html_node_text(badge_node);
        if (author->badge) {
          fpv_funpay_trim(author->badge);
          if (author->badge[0] == '\0' || strcmp(author->badge, "0") == 0) {
            fpv_free(author->badge);
            author->badge = NULL;
          }
        }
      }
      xmlNode* author_link =
          fpv_html_find_first_by_class(author_node->children, "a", NULL);
      if (author_link && !author->name) {
        author->name = fpv_html_node_text(author_link);
        if (author->name) {
          fpv_funpay_trim(author->name);
        }
      }
    }

    char* message_text = NULL;
    char* image_url = NULL;
    bool by_bot = false;
    const char* text_source = NULL;

    xmlNode* image_node =
        fpv_html_find_first_by_class(root, "a", "chat-img-link");
    if (image_node) {
      image_url = fpv_html_node_attr(image_node, "href");
      if (image_url) {
        text_source = "image";
      }
    } else {
      if (author_id == 0) {
        xmlNode* alert_node =
            fpv_html_find_first_by_class(root, "div", "alert-info");
        message_text = alert_node ? fpv_html_node_text(alert_node) : NULL;
        if (message_text) {
          text_source = "class:alert-info";
        }
      } else {
        xmlNode* text_node =
            fpv_html_find_first_by_class(root, NULL, "chat-msg-text");
        message_text = text_node ? fpv_html_node_text(text_node) : NULL;
        if (message_text) {
          text_source = "class:chat-msg-text";
        }
      }
    }

    if (message_text) {
      fpv_funpay_trim(message_text);
      if (!message_text[0]) {
        fpv_free(message_text);
        message_text = NULL;
      }
    }

    if (!message_text && !image_url) {
      static const char* json_keys[] = {"text", "message", "content", "msg"};
      for (size_t k = 0; k < sizeof(json_keys) / sizeof(json_keys[0]); k++) {
        const fpv_json_value_t* txt_val = fpv_json_object_get(msg, json_keys[k]);
        const char* raw = fpv_json_string(txt_val);
        if (raw && raw[0]) {
          message_text = fpv_strdup(raw);
          if (message_text) {
            fpv_funpay_trim(message_text);
            if (message_text[0]) {
              text_source = json_keys[k];
              break;
            }
            fpv_free(message_text);
            message_text = NULL;
          }
        }
      }
    }

    if (!message_text && !image_url) {
      static const char* candidates[] = {
          "chat-msg-text",
          "chat-message-text",
          "chat-message__text",
          "chat-msg__text",
          "message-text",
          "msg-text",
          "chat-message",
          "chat-msg"};
      for (size_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
        xmlNode* text_node =
            fpv_funpay_find_first_by_class_substr(root, NULL, candidates[c]);
        if (text_node) {
          message_text = fpv_html_node_text(text_node);
          if (message_text) {
            fpv_funpay_trim(message_text);
            if (message_text[0]) {
              text_source = candidates[c];
              break;
            }
            fpv_free(message_text);
            message_text = NULL;
          }
        }
      }
    }

    if (!message_text && !image_url) {
      static const char* attr_candidates[] = {
          "data-text",
          "data-message",
          "data-content",
          "data-msg",
          "data-original",
          "data-original-title"};
      for (size_t c = 0;
           c < sizeof(attr_candidates) / sizeof(attr_candidates[0]);
           c++) {
        char* attr_text =
            fpv_funpay_find_first_attr_value(root, NULL, attr_candidates[c]);
        if (attr_text) {
          fpv_funpay_trim(attr_text);
          if (attr_text[0]) {
            message_text = attr_text;
            text_source = attr_candidates[c];
            break;
          }
          fpv_free(attr_text);
        }
      }
    }

    if (!message_text && !image_url) {
      message_text = fpv_html_node_text(root);
      if (message_text) {
        fpv_funpay_trim(message_text);
        if (message_text[0]) {
          text_source = "root-text";
        } else {
          fpv_free(message_text);
          message_text = NULL;
        }
      }
    }

    if (message_text) {
      if (strncmp(message_text, FPV_FUNPAY_BOT_PREFIX, strlen(FPV_FUNPAY_BOT_PREFIX)) == 0) {
        memmove(
            message_text,
            message_text + strlen(FPV_FUNPAY_BOT_PREFIX),
            strlen(message_text) - strlen(FPV_FUNPAY_BOT_PREFIX) + 1);
        by_bot = true;
        fpv_funpay_trim(message_text);
        if (!message_text[0]) {
          fpv_free(message_text);
          message_text = NULL;
        }
      }
    }

    if (fpv_funpay_debug_messages(account)) {
      size_t text_len = message_text ? strlen(message_text) : 0;
      const char* source = text_source ? text_source : (image_url ? "image" : "none");
      char* preview = NULL;
      if (message_text && message_text[0]) {
        preview = fpv_funpay_truncate_utf8(message_text, 120);
      }
      fpv_funpay_logf(
          account,
          (message_text || image_url) ? FPV_LOG_INFO : FPV_LOG_WARNING,
          "Message parse: id=%" PRIu64 " chat=%" PRIu64 " author=%" PRIu64
          " source=%s text_len=%zu image=%s preview=\"%s\"",
          id_num,
          chat_id,
          author_id,
          source,
          text_len,
          image_url ? "yes" : "no",
          preview ? preview : "");
      fpv_free(preview);
      if (!message_text && !image_url) {
        fpv_funpay_log_html_snippet(account, html);
      }
    }

    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRIu64, id_num);
    char chat_buf[32];
    snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
    char author_buf[32];
    snprintf(author_buf, sizeof(author_buf), "%" PRIu64, author_id);

    fpv_message_direction_t direction =
        (account && account->id == author_id) ? FPV_MESSAGE_OUTBOUND : FPV_MESSAGE_INBOUND;
    const char* sender_name = author ? author->name : NULL;
    if (!sender_name && author_id == 0) {
      sender_name = "FunPay";
    }

    fpv_message_t* message = fpv_message_create(
        id_buf,
        chat_buf,
        chat_name,
        author_buf,
        sender_name,
        message_text,
        image_url,
        author ? author->badge : NULL,
        direction,
        by_bot,
        fpv_time_now_ms());

    fpv_free(message_text);
    fpv_free(image_url);
    fpv_html_destroy(doc);

    if (!message) {
      continue;
    }
    list[out_idx++] = message;
  }

  for (size_t i = 0; i < out_idx; i++) {
    fpv_message_t* message = list[i];
    if (!message || !message->sender_id) {
      continue;
    }
    uint64_t author_id = strtoull(message->sender_id, NULL, 10);
    fpv_funpay_author_t* author =
        fpv_funpay_author_find(authors, author_count, author_id);
    if (author) {
      if (!message->sender_name && author->name) {
        message->sender_name = fpv_strdup(author->name);
      }
      if (!message->badge && author->badge) {
        message->badge = fpv_strdup(author->badge);
      }
    }
    if (!message->sender_name && author_id == 0) {
      message->sender_name = fpv_strdup("FunPay");
    }
  }

  for (size_t i = 0; i < author_count; i++) {
    fpv_free(authors[i].name);
    fpv_free(authors[i].badge);
  }
  fpv_free(authors);

  if (out_messages) {
    *out_messages = list;
  } else {
    for (size_t i = 0; i < out_idx; i++) {
      fpv_message_destroy(list[i]);
    }
    fpv_free(list);
  }
  if (out_count) {
    *out_count = out_idx;
  }
  return FPV_OK;
}

static fpv_result_t fpv_funpay_account_get_chat_histories(
    fpv_funpay_account_t* account,
    const fpv_funpay_chat_request_t* chats,
    size_t chat_count,
    fpv_funpay_chat_history_t** out_histories,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!out_histories || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
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
  if (chat_count == 0) {
    *out_histories = NULL;
    *out_count = 0;
    return FPV_OK;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  for (size_t i = 0; i < chat_count; i++) {
    if (i > 0) {
      fpv_funpay_sb_append(&objects, ",");
    }
    fpv_funpay_sb_append_format(
        &objects,
        "{\"type\":\"chat_node\",\"id\":%" PRIu64 ",\"tag\":\"00000000\","
        "\"data\":{\"node\":%" PRIu64 ",\"last_message\":-1,\"content\":\"\"}}",
        chats[i].chat_id,
        chats[i].chat_id);
  }
  fpv_funpay_sb_append(&objects, "]");
  char* objects_json = fpv_funpay_sb_detach(&objects);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
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
    return FPV_ERR_PARSE;
  }

  fpv_funpay_chat_history_t* histories =
      (fpv_funpay_chat_history_t*)calloc(chat_count, sizeof(*histories));
  if (!histories) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t history_count = 0;
  const fpv_json_value_t* objects_val =
      fpv_json_object_get(json, "objects");
  size_t objects_count = fpv_json_array_size(objects_val);
  for (size_t i = 0; i < objects_count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects_val, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type || strcmp(type, "chat_node") != 0) {
      continue;
    }
    const fpv_json_value_t* id_val = fpv_json_object_get(obj, "id");
    uint64_t chat_id = 0;
    fpv_json_number_to_uint64(id_val, &chat_id);
    const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
    const fpv_json_value_t* msg_val = fpv_json_object_get(data_val, "messages");
    const char* name = NULL;
    for (size_t c = 0; c < chat_count; c++) {
      if (chats[c].chat_id == chat_id) {
        name = chats[c].chat_name;
        break;
      }
    }

    fpv_message_t** messages = NULL;
    size_t msg_count = 0;
    fpv_result_t parse_result = fpv_funpay_parse_messages(
        account,
        msg_val,
        chat_id,
        name,
        &messages,
        &msg_count);
    if (parse_result != FPV_OK) {
      continue;
    }
    histories[history_count].chat_id = chat_id;
    histories[history_count].chat_name = name ? fpv_strdup(name) : NULL;
    histories[history_count].messages = messages;
    histories[history_count].message_count = msg_count;
    history_count++;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);

  *out_histories = histories;
  *out_count = history_count;
  return FPV_OK;
}

static void fpv_funpay_chat_history_destroy(
    fpv_funpay_chat_history_t* histories,
    size_t count) {
  if (!histories) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < histories[i].message_count; j++) {
      fpv_message_destroy(histories[i].messages[j]);
    }
    fpv_free(histories[i].messages);
    fpv_free(histories[i].chat_name);
  }
  free(histories);
}

fpv_result_t fpv_funpay_account_get_chat_history(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!out_messages || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_messages = NULL;
  *out_count = 0;
  fpv_funpay_chat_request_t request = {chat_id, chat_name};
  fpv_funpay_chat_history_t* histories = NULL;
  size_t history_count = 0;
  fpv_result_t result = fpv_funpay_account_get_chat_histories(
      account,
      &request,
      1,
      &histories,
      &history_count,
      error);
  if (result != FPV_OK) {
    return result;
  }
  if (history_count == 0) {
    fpv_funpay_chat_history_destroy(histories, history_count);
    return FPV_OK;
  }

  fpv_message_t** messages = histories[0].messages;
  size_t count = histories[0].message_count;
  histories[0].messages = NULL;
  histories[0].message_count = 0;

  fpv_funpay_chat_history_destroy(histories, history_count);

  *out_messages = messages;
  *out_count = count;
  return FPV_OK;
}

void fpv_funpay_event_destroy(fpv_funpay_event_t* event) {
  if (!event) {
    return;
  }
  fpv_free(event->runner_tag);
  fpv_chat_destroy(event->chat);
  if (event->chats) {
    for (size_t i = 0; i < event->chat_count; i++) {
      fpv_chat_destroy(event->chats[i]);
    }
    fpv_free(event->chats);
  }
  fpv_message_destroy(event->message);
  fpv_order_destroy(event->order);
  free(event);
}

void fpv_funpay_event_batch_destroy(fpv_funpay_event_batch_t* batch) {
  if (!batch || !batch->events) {
    return;
  }
  for (size_t i = 0; i < batch->count; i++) {
    fpv_funpay_event_destroy(batch->events[i]);
  }
  fpv_free(batch->events);
  batch->events = NULL;
  batch->count = 0;
}

static fpv_funpay_event_t* fpv_funpay_event_create(
    fpv_funpay_event_type_t type,
    const char* tag) {
  fpv_funpay_event_t* event =
      (fpv_funpay_event_t*)calloc(1, sizeof(*event));
  if (!event) {
    return NULL;
  }
  event->type = type;
  event->timestamp_ms = fpv_time_now_ms();
  event->runner_tag = tag ? fpv_strdup(tag) : NULL;
  return event;
}

static fpv_funpay_order_state_t* fpv_funpay_runner_find_order(
    fpv_funpay_runner_t* runner,
    const char* order_id) {
  if (!runner || !order_id) {
    return NULL;
  }
  for (size_t i = 0; i < runner->order_count; i++) {
    if (runner->orders[i].id &&
        strcmp(runner->orders[i].id, order_id) == 0) {
      return &runner->orders[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_funpay_runner_update_order(
    fpv_funpay_runner_t* runner,
    const fpv_order_t* order) {
  fpv_funpay_order_state_t* state =
      fpv_funpay_runner_find_order(runner, order->id);
  if (state) {
    state->status = order->status;
    return FPV_OK;
  }

  fpv_funpay_order_state_t* grown = (fpv_funpay_order_state_t*)realloc(
      runner->orders,
      (runner->order_count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  runner->orders = grown;
  runner->orders[runner->order_count].id = fpv_strdup(order->id);
  runner->orders[runner->order_count].status = order->status;
  if (!runner->orders[runner->order_count].id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  runner->order_count++;
  return FPV_OK;
}

static fpv_funpay_last_message_t* fpv_funpay_runner_find_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->last_message_count; i++) {
    if (runner->last_messages[i].chat_id == chat_id) {
      return &runner->last_messages[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_funpay_runner_set_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* text,
    const char* time_text) {
  fpv_funpay_last_message_t* state =
      fpv_funpay_runner_find_last_message(runner, chat_id);
  if (!state) {
    fpv_funpay_last_message_t* grown = (fpv_funpay_last_message_t*)realloc(
        runner->last_messages,
        (runner->last_message_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->last_messages = grown;
    state = &runner->last_messages[runner->last_message_count++];
    state->chat_id = chat_id;
    state->text = NULL;
    state->time_text = NULL;
  }
  fpv_free(state->text);
  fpv_free(state->time_text);
  state->text = text ? fpv_strdup(text) : NULL;
  state->time_text = time_text ? fpv_strdup(time_text) : NULL;
  if ((text && !state->text) || (time_text && !state->time_text)) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

static fpv_funpay_last_message_id_t* fpv_funpay_runner_find_last_message_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->last_message_id_count; i++) {
    if (runner->last_message_ids[i].chat_id == chat_id) {
      return &runner->last_message_ids[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_funpay_runner_set_last_message_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  fpv_funpay_last_message_id_t* state =
      fpv_funpay_runner_find_last_message_id(runner, chat_id);
  if (!state) {
    fpv_funpay_last_message_id_t* grown =
        (fpv_funpay_last_message_id_t*)realloc(
            runner->last_message_ids,
            (runner->last_message_id_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->last_message_ids = grown;
    state = &runner->last_message_ids[runner->last_message_id_count++];
    state->chat_id = chat_id;
  }
  state->message_id = message_id;
  return FPV_OK;
}

static fpv_funpay_init_message_t* fpv_funpay_runner_find_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->init_message_count; i++) {
    if (runner->init_messages[i].chat_id == chat_id) {
      return &runner->init_messages[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_funpay_runner_set_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* text) {
  fpv_funpay_init_message_t* state =
      fpv_funpay_runner_find_init_message(runner, chat_id);
  if (!state) {
    fpv_funpay_init_message_t* grown = (fpv_funpay_init_message_t*)realloc(
        runner->init_messages,
        (runner->init_message_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->init_messages = grown;
    state = &runner->init_messages[runner->init_message_count++];
    state->chat_id = chat_id;
    state->text = NULL;
  }
  fpv_free(state->text);
  state->text = text ? fpv_strdup(text) : NULL;
  return (text && !state->text) ? FPV_ERR_OUT_OF_MEMORY : FPV_OK;
}

static void fpv_funpay_runner_clear_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->init_message_count; i++) {
    if (runner->init_messages[i].chat_id == chat_id) {
      fpv_free(runner->init_messages[i].text);
      runner->init_messages[i] = runner->init_messages[runner->init_message_count - 1];
      runner->init_message_count--;
      return;
    }
  }
}

static fpv_funpay_bot_ids_t* fpv_funpay_runner_find_bot_ids(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->bot_id_count; i++) {
    if (runner->bot_ids[i].chat_id == chat_id) {
      return &runner->bot_ids[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_funpay_runner_add_bot_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  fpv_funpay_bot_ids_t* state =
      fpv_funpay_runner_find_bot_ids(runner, chat_id);
  if (!state) {
    fpv_funpay_bot_ids_t* grown = (fpv_funpay_bot_ids_t*)realloc(
        runner->bot_ids,
        (runner->bot_id_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->bot_ids = grown;
    state = &runner->bot_ids[runner->bot_id_count++];
    state->chat_id = chat_id;
    state->ids = NULL;
    state->count = 0;
  }
  uint64_t* ids = (uint64_t*)realloc(
      state->ids,
      (state->count + 1) * sizeof(*ids));
  if (!ids) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  state->ids = ids;
  state->ids[state->count++] = message_id;
  return FPV_OK;
}

static bool fpv_funpay_bot_id_contains(
    const fpv_funpay_bot_ids_t* state,
    uint64_t message_id) {
  if (!state) {
    return false;
  }
  for (size_t i = 0; i < state->count; i++) {
    if (state->ids[i] == message_id) {
      return true;
    }
  }
  return false;
}

static void fpv_funpay_bot_id_cleanup(
    fpv_funpay_bot_ids_t* state,
    uint64_t last_seen_id) {
  if (!state) {
    return;
  }
  size_t write = 0;
  for (size_t i = 0; i < state->count; i++) {
    if (state->ids[i] > last_seen_id) {
      state->ids[write++] = state->ids[i];
    }
  }
  state->count = write;
  if (state->count == 0) {
    fpv_free(state->ids);
    state->ids = NULL;
  }
}

fpv_funpay_runner_t* fpv_funpay_runner_create(
    fpv_funpay_account_t* account,
    const fpv_funpay_runner_config_t* config,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return NULL;
  }
  if (account->runner) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_RUNNER_ALREADY_ATTACHED,
        "Runner already attached",
        NULL,
        NULL,
        0);
    return NULL;
  }

  fpv_funpay_runner_t* runner =
      (fpv_funpay_runner_t*)calloc(1, sizeof(*runner));
  if (!runner) {
    return NULL;
  }

  runner->account = account;
  runner->make_msg_requests = config ? !config->disable_message_requests : true;
  runner->make_order_requests = config ? !config->disable_order_requests : true;
  runner->first_request = true;
  runner->last_msg_tag = fpv_funpay_random_tag();
  runner->last_order_tag = fpv_funpay_random_tag();

  if (!runner->last_msg_tag || !runner->last_order_tag) {
    fpv_funpay_runner_destroy(runner);
    return NULL;
  }

  account->runner = runner;
  return runner;
}

void fpv_funpay_runner_destroy(fpv_funpay_runner_t* runner) {
  if (!runner) {
    return;
  }
  if (runner->account && runner->account->runner == runner) {
    runner->account->runner = NULL;
  }
  fpv_free(runner->last_msg_tag);
  fpv_free(runner->last_order_tag);
  for (size_t i = 0; i < runner->order_count; i++) {
    fpv_free(runner->orders[i].id);
  }
  fpv_free(runner->orders);
  for (size_t i = 0; i < runner->last_message_count; i++) {
    fpv_free(runner->last_messages[i].text);
    fpv_free(runner->last_messages[i].time_text);
  }
  fpv_free(runner->last_messages);
  fpv_free(runner->last_message_ids);
  for (size_t i = 0; i < runner->init_message_count; i++) {
    fpv_free(runner->init_messages[i].text);
  }
  fpv_free(runner->init_messages);
  for (size_t i = 0; i < runner->bot_id_count; i++) {
    fpv_free(runner->bot_ids[i].ids);
  }
  fpv_free(runner->bot_ids);
  free(runner);
}

fpv_result_t fpv_funpay_runner_mark_by_bot(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  if (!runner) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  return fpv_funpay_runner_add_bot_id(runner, chat_id, message_id);
}

fpv_result_t fpv_funpay_runner_update_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* message_text,
    const char* message_time) {
  if (!runner) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* text = message_text ? message_text : FPV_FUNPAY_IMAGE_TEXT;
  char* truncated = fpv_funpay_truncate_utf8(text, 250);
  if (!truncated) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_funpay_runner_set_last_message(
      runner,
      chat_id,
      truncated,
      message_time);
  fpv_free(truncated);
  return result;
}

void fpv_funpay_runner_set_message_requests(
    fpv_funpay_runner_t* runner,
    bool enabled) {
  if (!runner) {
    return;
  }
  runner->make_msg_requests = enabled;
  fpv_free(runner->last_message_ids);
  runner->last_message_ids = NULL;
  runner->last_message_id_count = 0;
}

static fpv_result_t fpv_funpay_runner_get_updates(
    fpv_funpay_runner_t* runner,
    fpv_json_value_t** out_json,
    fpv_funpay_error_t* error) {
  if (!runner || !out_json) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"orders_counters\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false},",
      runner->account->id,
      runner->last_order_tag);
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"chat_bookmarks\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false}",
      runner->account->id,
      runner->last_msg_tag);
  fpv_funpay_sb_append(&objects, "]");
  char* objects_json = fpv_funpay_sb_detach(&objects);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", runner->account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
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
      runner->account,
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
  result = fpv_json_parse(response.body, response.body_size, &json, &json_error);
  fpv_funpay_http_response_clear(&response);
  if (result != FPV_OK) {
    fpv_json_destroy(json);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse updates JSON",
        "runner/",
        "POST",
        0);
    return FPV_ERR_PARSE;
  }

  *out_json = json;
  return FPV_OK;
}

static fpv_result_t fpv_funpay_event_list_append(
    fpv_funpay_event_t*** events,
    size_t* count,
    fpv_funpay_event_t* event) {
  fpv_funpay_event_t** grown = (fpv_funpay_event_t**)realloc(
      *events,
      (*count + 1) * sizeof(**events));
  if (!grown) {
    fpv_funpay_event_destroy(event);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  *events = grown;
  (*events)[(*count)++] = event;
  return FPV_OK;
}

static fpv_result_t fpv_funpay_runner_parse_chat_updates(
    fpv_funpay_runner_t* runner,
    const fpv_json_value_t* obj,
    fpv_funpay_event_t*** events,
    size_t* event_count) {
  const fpv_json_value_t* tag_val = fpv_json_object_get(obj, "tag");
  const char* tag = fpv_json_string(tag_val);
  if (tag) {
    fpv_free(runner->last_msg_tag);
    runner->last_msg_tag = fpv_strdup(tag);
  }

  const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
  const fpv_json_value_t* html_val = fpv_json_object_get(data_val, "html");
  const char* html = fpv_json_string(html_val);
  if (!html) {
    return FPV_ERR_PARSE;
  }

  fpv_chat_t** parsed_chats = NULL;
  size_t parsed_count = 0;
  fpv_result_t parse_result = fpv_funpay_parse_chat_bookmarks(
      runner->account,
      html,
      &parsed_chats,
      &parsed_count);
  if (parse_result != FPV_OK) {
    return parse_result;
  }

  fpv_chat_t** lcmc_chats = NULL;
  size_t lcmc_count = 0;

  for (size_t i = 0; i < parsed_count; i++) {
    fpv_chat_t* chat = parsed_chats[i];
    uint64_t chat_id = chat->id ? strtoull(chat->id, NULL, 10) : 0;
    fpv_funpay_last_message_t* last =
        fpv_funpay_runner_find_last_message(runner, chat_id);
    bool skip = false;
    if (last && last->text && chat->last_message_text &&
        strcmp(last->text, chat->last_message_text) == 0) {
      if (last->time_text && chat->last_message_time) {
        if (!fpv_funpay_time_is_tag(chat->last_message_time) ||
            strcmp(last->time_text, chat->last_message_time) == 0) {
          skip = true;
        }
      } else {
        skip = true;
      }
    }

    if (skip) {
      fpv_chat_destroy(chat);
      continue;
    }

    fpv_funpay_runner_set_last_message(
        runner,
        chat_id,
        chat->last_message_text,
        chat->last_message_time);

    if (runner->first_request) {
      fpv_funpay_event_t* event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_INITIAL_CHAT, runner->last_msg_tag);
      if (!event) {
        fpv_chat_destroy(chat);
        continue;
      }
      event->chat = chat;
      fpv_funpay_event_list_append(events, event_count, event);
      fpv_funpay_runner_set_init_message(
          runner,
          chat_id,
          chat->last_message_text);
    } else {
      fpv_chat_t** grown = (fpv_chat_t**)realloc(
          lcmc_chats,
          (lcmc_count + 1) * sizeof(*grown));
      if (!grown) {
        fpv_chat_destroy(chat);
        continue;
      }
      lcmc_chats = grown;
      lcmc_chats[lcmc_count++] = chat;
    }
  }

  if (lcmc_count > 0) {
    if (runner->account->chats.count == 0) {
      fpv_funpay_account_request_chats(
          runner->account,
          NULL,
          NULL,
          NULL);
    }
    fpv_funpay_event_t* list_event =
        fpv_funpay_event_create(FPV_FUNPAY_EVENT_CHATS_LIST_CHANGED, runner->last_msg_tag);
    if (list_event) {
      list_event->chats =
          fpv_funpay_account_clone_chats(runner->account, &list_event->chat_count);
      fpv_funpay_event_list_append(events, event_count, list_event);
    }
  }

  if (!runner->make_msg_requests) {
    for (size_t i = 0; i < lcmc_count; i++) {
      fpv_funpay_event_t* event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED, runner->last_msg_tag);
      if (!event) {
        fpv_chat_destroy(lcmc_chats[i]);
        continue;
      }
      event->chat = lcmc_chats[i];
      fpv_funpay_event_list_append(events, event_count, event);
    }
    fpv_free(lcmc_chats);
    fpv_free(parsed_chats);
    return FPV_OK;
  }

  size_t index = 0;
  while (index < lcmc_count) {
    size_t batch_size = lcmc_count - index;
    if (batch_size > 10) {
      batch_size = 10;
    }
    fpv_funpay_chat_request_t* batch =
        (fpv_funpay_chat_request_t*)calloc(batch_size, sizeof(*batch));
    if (!batch) {
      break;
    }
    for (size_t i = 0; i < batch_size; i++) {
      fpv_chat_t* chat = lcmc_chats[index + i];
      batch[i].chat_id = chat->id ? strtoull(chat->id, NULL, 10) : 0;
      batch[i].chat_name = chat->title;
    }

    fpv_funpay_chat_history_t* histories = NULL;
    size_t history_count = 0;
    fpv_result_t hist_result = fpv_funpay_account_get_chat_histories(
        runner->account,
        batch,
        batch_size,
        &histories,
        &history_count,
        NULL);

    for (size_t i = 0; i < batch_size; i++) {
      fpv_chat_t* chat = lcmc_chats[index + i];
      fpv_funpay_event_t* lcmc_event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED, runner->last_msg_tag);
      if (lcmc_event) {
        lcmc_event->chat = chat;
        fpv_funpay_event_list_append(events, event_count, lcmc_event);
      } else {
        fpv_chat_destroy(chat);
      }

      if (hist_result != FPV_OK) {
        continue;
      }

      fpv_funpay_chat_history_t* history = NULL;
      for (size_t h = 0; h < history_count; h++) {
        if (histories[h].chat_id == batch[i].chat_id) {
          history = &histories[h];
          break;
        }
      }
      if (!history || history->message_count == 0) {
        continue;
      }

      fpv_funpay_last_message_id_t* last_id =
          fpv_funpay_runner_find_last_message_id(runner, history->chat_id);
      size_t filtered_count = 0;
      fpv_message_t** filtered =
          (fpv_message_t**)calloc(history->message_count, sizeof(*filtered));
      if (!filtered) {
        continue;
      }

      for (size_t m = 0; m < history->message_count; m++) {
        fpv_message_t* msg = history->messages[m];
        uint64_t msg_id = msg->id ? strtoull(msg->id, NULL, 10) : 0;
        if (last_id && msg_id <= last_id->message_id) {
          continue;
        }
        filtered[filtered_count++] = msg;
      }

      if (filtered_count == 0) {
        fpv_free(filtered);
        continue;
      }

      fpv_funpay_bot_ids_t* bot_state =
          fpv_funpay_runner_find_bot_ids(runner, history->chat_id);
      if (bot_state) {
        for (size_t m = 0; m < filtered_count; m++) {
          fpv_message_t* msg = filtered[m];
          uint64_t msg_id = msg->id ? strtoull(msg->id, NULL, 10) : 0;
          if (fpv_funpay_bot_id_contains(bot_state, msg_id)) {
            msg->by_bot = true;
          }
        }
      }

      if (!last_id) {
        fpv_funpay_init_message_t* init =
            fpv_funpay_runner_find_init_message(runner, history->chat_id);
        if (init && init->text) {
          size_t temp_count = 0;
          fpv_message_t** temp =
              (fpv_message_t**)calloc(filtered_count, sizeof(*temp));
          if (temp) {
            for (size_t m = filtered_count; m-- > 0;) {
              fpv_message_t* msg = filtered[m];
              const char* text = msg->text;
              if (!text && msg->image_url) {
                text = FPV_FUNPAY_IMAGE_TEXT;
              }
              if (text && strcmp(text, init->text) == 0) {
                break;
              }
              temp[temp_count++] = msg;
            }
            if (temp_count > 0) {
              fpv_free(filtered);
              filtered = NULL;
              filtered_count = 0;
              filtered = (fpv_message_t**)calloc(temp_count, sizeof(*filtered));
              if (filtered) {
                for (size_t m = 0; m < temp_count; m++) {
                  filtered[m] = temp[temp_count - 1 - m];
                }
                filtered_count = temp_count;
              }
            }
            fpv_free(temp);
          }
          fpv_funpay_runner_clear_init_message(runner, history->chat_id);
        } else {
          fpv_message_t* last_msg = filtered[filtered_count - 1];
          fpv_free(filtered);
          filtered = (fpv_message_t**)calloc(1, sizeof(*filtered));
          if (filtered) {
            filtered[0] = last_msg;
            filtered_count = 1;
          } else {
            filtered_count = 0;
          }
        }
      }

      if (filtered_count == 0) {
        fpv_free(filtered);
        continue;
      }

      fpv_message_t** dedup =
          (fpv_message_t**)calloc(filtered_count, sizeof(*dedup));
      size_t dedup_count = 0;
      if (dedup) {
        dedup[dedup_count++] = filtered[0];
        for (size_t m = 1; m < filtered_count; m++) {
          fpv_message_t* prev = filtered[m - 1];
          fpv_message_t* current = filtered[m];
          const char* prev_text = prev->text ? prev->text : "";
          const char* curr_text = current->text ? current->text : "";
          if (strcmp(prev_text, curr_text) != 0 ||
              strcmp(prev->sender_id ? prev->sender_id : "",
                     current->sender_id ? current->sender_id : "") != 0) {
            dedup[dedup_count++] = current;
          }
        }
      }

      uint64_t last_seen = 0;
      fpv_message_t* last_message = dedup ? dedup[dedup_count - 1] : filtered[filtered_count - 1];
      if (last_message && last_message->id) {
        last_seen = strtoull(last_message->id, NULL, 10);
      }
      fpv_funpay_runner_set_last_message_id(
          runner,
          history->chat_id,
          last_seen);
      if (bot_state) {
        fpv_funpay_bot_id_cleanup(bot_state, last_seen);
      }

      fpv_message_t** final_list = dedup ? dedup : filtered;
      size_t final_count = dedup ? dedup_count : filtered_count;
      for (size_t m = 0; m < final_count; m++) {
        fpv_funpay_event_t* msg_event =
            fpv_funpay_event_create(FPV_FUNPAY_EVENT_NEW_MESSAGE, runner->last_msg_tag);
        if (!msg_event) {
          continue;
        }
        fpv_message_t* cloned = fpv_message_clone(final_list[m]);
        if (!cloned) {
          fpv_funpay_event_destroy(msg_event);
          continue;
        }
        msg_event->message = cloned;
        fpv_funpay_event_list_append(events, event_count, msg_event);
      }
      fpv_free(dedup);
      fpv_free(filtered);
    }

    fpv_free(batch);
    fpv_funpay_chat_history_destroy(histories, history_count);
    index += batch_size;
  }

  fpv_free(lcmc_chats);
  fpv_free(parsed_chats);
  return FPV_OK;
}

static fpv_result_t fpv_funpay_runner_parse_order_updates(
    fpv_funpay_runner_t* runner,
    const fpv_json_value_t* obj,
    fpv_funpay_event_t*** events,
    size_t* event_count) {
  const fpv_json_value_t* tag_val = fpv_json_object_get(obj, "tag");
  const char* tag = fpv_json_string(tag_val);
  if (tag) {
    fpv_free(runner->last_order_tag);
    runner->last_order_tag = fpv_strdup(tag);
  }

  const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
  if (!runner->first_request) {
    const fpv_json_value_t* buyer_val = fpv_json_object_get(data_val, "buyer");
    const fpv_json_value_t* seller_val = fpv_json_object_get(data_val, "seller");
    uint64_t buyer = 0;
    uint64_t seller = 0;
    fpv_json_number_to_uint64(buyer_val, &buyer);
    fpv_json_number_to_uint64(seller_val, &seller);

    fpv_funpay_event_t* list_event =
        fpv_funpay_event_create(FPV_FUNPAY_EVENT_ORDERS_LIST_CHANGED, runner->last_order_tag);
    if (list_event) {
      list_event->purchases = (uint32_t)buyer;
      list_event->sales = (uint32_t)seller;
      fpv_funpay_event_list_append(events, event_count, list_event);
    }
  }

  if (!runner->make_order_requests) {
    return FPV_OK;
  }

  fpv_order_t** orders = NULL;
  size_t order_count = 0;
  fpv_result_t result = fpv_funpay_account_get_orders(
      runner->account,
      &orders,
      &order_count,
      NULL);
  if (result != FPV_OK) {
    return result;
  }

  for (size_t i = 0; i < order_count; i++) {
    fpv_order_t* order = orders[i];
    fpv_funpay_order_state_t* state =
        fpv_funpay_runner_find_order(runner, order->id);
    if (!state) {
      fpv_order_status_t status = order->status;
      fpv_funpay_event_t* event = NULL;
      if (runner->first_request) {
        event = fpv_funpay_event_create(FPV_FUNPAY_EVENT_INITIAL_ORDER, runner->last_order_tag);
      } else {
        event = fpv_funpay_event_create(FPV_FUNPAY_EVENT_NEW_ORDER, runner->last_order_tag);
      }
      if (event) {
        event->order = order;
        fpv_funpay_event_list_append(events, event_count, event);
      }
      fpv_funpay_runner_update_order(runner, order);
      if (!runner->first_request && status == FPV_ORDER_DELIVERED) {
        fpv_funpay_event_t* status_event =
            fpv_funpay_event_create(
                FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED,
                runner->last_order_tag);
        if (status_event) {
          status_event->order = fpv_order_clone(order);
          fpv_funpay_event_list_append(events, event_count, status_event);
        }
      }
      if (!event) {
        fpv_order_destroy(order);
      }
      continue;
    }

    if (state->status != order->status) {
      fpv_funpay_event_t* status_event =
          fpv_funpay_event_create(
              FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED,
              runner->last_order_tag);
      if (status_event) {
        status_event->order = order;
        fpv_funpay_event_list_append(events, event_count, status_event);
      }
      fpv_funpay_runner_update_order(runner, order);
      if (!status_event) {
        fpv_order_destroy(order);
      }
    } else {
      fpv_order_destroy(order);
    }
  }

  fpv_free(orders);
  return FPV_OK;
}

fpv_result_t fpv_funpay_runner_poll(
    fpv_funpay_runner_t* runner,
    fpv_funpay_event_batch_t* batch,
    fpv_funpay_error_t* error) {
  if (!runner || !batch) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  batch->events = NULL;
  batch->count = 0;

  fpv_json_value_t* updates = NULL;
  fpv_result_t result = fpv_funpay_runner_get_updates(
      runner,
      &updates,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_funpay_event_t** events = NULL;
  size_t event_count = 0;

  const fpv_json_value_t* objects = fpv_json_object_get(updates, "objects");
  size_t count = fpv_json_array_size(objects);
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type) {
      continue;
    }
    if (strcmp(type, "chat_bookmarks") == 0) {
      fpv_funpay_runner_parse_chat_updates(
          runner,
          obj,
          &events,
          &event_count);
    } else if (strcmp(type, "orders_counters") == 0) {
      fpv_funpay_runner_parse_order_updates(
          runner,
          obj,
          &events,
          &event_count);
    }
  }

  runner->first_request = false;
  fpv_json_destroy(updates);

  batch->events = events;
  batch->count = event_count;
  return FPV_OK;
}
