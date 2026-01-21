#include "funpay/core/fpv_funpay_internal.h"


#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char* FPV_FUNPAY_BOT_PREFIX = "\xE2\x81\xA4";
const char* FPV_FUNPAY_IMAGE_TEXT =
    "\xD0\x98\xD0\xB7\xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB6"
    "\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5";
const char* FPV_FUNPAY_TODAY =
    "\xD1\x81\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F";
const char* FPV_FUNPAY_YESTERDAY =
    "\xD0\xB2\xD1\x87\xD0\xB5\xD1\x80\xD0\xB0";
const char* FPV_FUNPAY_TODAY_EN = "today";
const char* FPV_FUNPAY_YESTERDAY_EN = "yesterday";

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

void fpv_funpay_sb_reset(fpv_string_builder_t* builder) {
  if (!builder) {
    return;
  }
  builder->data = NULL;
  builder->length = 0;
  builder->capacity = 0;
}


bool fpv_funpay_sb_reserve(fpv_string_builder_t* builder, size_t extra) {
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


bool fpv_funpay_sb_append(
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


bool fpv_funpay_sb_append_n(
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


bool fpv_funpay_sb_append_format(
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


char* fpv_funpay_sb_detach(fpv_string_builder_t* builder) {
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


bool fpv_funpay_is_digit_ascii(unsigned char value) {
  return value >= '0' && value <= '9';
}


bool fpv_funpay_is_ascii(const char* value) {
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


char* fpv_funpay_ascii_lower(const char* value) {
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


void fpv_funpay_trim(char* text) {
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

char* fpv_funpay_truncate_utf8(const char* text, size_t max_chars) {
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


void fpv_funpay_error_set(
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


char* fpv_funpay_random_tag(void) {
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


char* fpv_funpay_url_encode(const char* value) {
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


char* fpv_funpay_form_encode(
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


void fpv_funpay_form_clear(fpv_funpay_form_t* form) {
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


bool fpv_funpay_form_set(
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


char* fpv_funpay_form_encode_fields(
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


char* fpv_funpay_build_url(const char* path) {
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


bool fpv_funpay_time_is_tag(const char* value) {
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


uint64_t fpv_funpay_parse_order_date(const char* text) {
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


const char* fpv_funpay_currency_from_symbol(const char* symbol) {
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


const char* fpv_funpay_currency_from_cy(const char* cy) {
  if (!cy) {
    return NULL;
  }
  if (strcmp(cy, "rub") == 0 || strcmp(cy, "RUB") == 0) {
    return "RUB";
  }
  if (strcmp(cy, "usd") == 0 || strcmp(cy, "USD") == 0) {
    return "USD";
  }
  if (strcmp(cy, "eur") == 0 || strcmp(cy, "EUR") == 0) {
    return "EUR";
  }
  return NULL;
}


bool fpv_funpay_parse_price(
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


bool fpv_funpay_parse_user_id_from_href(
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


int fpv_funpay_parse_rating_from_class(const char* class_value) {
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


fpv_funpay_http_header_t fpv_funpay_header(
    const char* name,
    const char* value) {
  fpv_funpay_http_header_t header;
  header.name = name;
  header.value = value;
  return header;
}


bool fpv_funpay_list_contains_u64(
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


const char* fpv_funpay_form_get(
    const fpv_funpay_form_t* form,
    const char* key) {
  if (!form || !key) {
    return NULL;
  }
  for (size_t i = 0; i < form->count; i++) {
    if (form->fields[i].key &&
        strcmp(form->fields[i].key, key) == 0) {
      return form->fields[i].value;
    }
  }
  return NULL;
}


bool fpv_funpay_form_has_key(
    const fpv_funpay_form_t* form,
    const char* key) {
  return fpv_funpay_form_get(form, key) != NULL;
}


static bool fpv_funpay_form_key_in_list(
    const char* key,
    const char* const* keys,
    size_t key_count) {
  if (!key || !keys) {
    return false;
  }
  for (size_t i = 0; i < key_count; i++) {
    if (keys[i] && strcmp(key, keys[i]) == 0) {
      return true;
    }
  }
  return false;
}


bool fpv_funpay_form_copy_common_fields(
    fpv_funpay_form_t* dest,
    const fpv_funpay_form_t* src,
    const char* const* excluded_keys,
    size_t excluded_count) {
  if (!dest || !src) {
    return false;
  }
  for (size_t i = 0; i < src->count; i++) {
    const char* key = src->fields[i].key;
    if (!key || !key[0]) {
      continue;
    }
    if (fpv_funpay_form_key_in_list(key, excluded_keys, excluded_count)) {
      continue;
    }
    if (!fpv_funpay_form_has_key(dest, key)) {
      continue;
    }
    const char* value = src->fields[i].value ? src->fields[i].value : "";
    if (!fpv_funpay_form_set(dest, key, value)) {
      return false;
    }
  }
  return true;
}


const char* fpv_funpay_form_find_first_key(
    const fpv_funpay_form_t* form,
    const char* const* keys,
    size_t key_count) {
  if (!form || !keys) {
    return NULL;
  }
  for (size_t i = 0; i < key_count; i++) {
    if (keys[i] && fpv_funpay_form_has_key(form, keys[i])) {
      return keys[i];
    }
  }
  return NULL;
}


bool fpv_funpay_form_set_if_present(
    fpv_funpay_form_t* form,
    const char* key,
    const char* value) {
  if (!form || !key) {
    return false;
  }
  if (!fpv_funpay_form_has_key(form, key)) {
    return true;
  }
  return fpv_funpay_form_set(form, key, value);
}


bool fpv_funpay_form_override_title(
    fpv_funpay_form_t* form,
    const char* title,
    const char* original_title) {
  if (!form || !title || !title[0]) {
    return true;
  }
  const char* candidates[] = {"title", "name", "lot_title", "offer_title"};
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (fpv_funpay_form_has_key(form, candidates[i])) {
      return fpv_funpay_form_set(form, candidates[i], title);
    }
  }
  if (!original_title || !original_title[0]) {
    return true;
  }
  for (size_t i = 0; i < form->count; i++) {
    const char* key = form->fields[i].key;
    const char* value = form->fields[i].value;
    if (!key || !value) {
      continue;
    }
    if (strcmp(value, original_title) == 0) {
      return fpv_funpay_form_set(form, key, title);
    }
  }
  return true;
}


bool fpv_funpay_form_parse_from_html(
    xmlNode* root,
    fpv_funpay_form_t* form) {
  if (!root || !form) {
    return false;
  }

  bool ok = true;
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
      bool is_checked = checked != NULL;
      fpv_free(checked);
      char* value = NULL;
      if (is_checked) {
        value = fpv_html_node_attr(node, "value");
      }
      if (is_checked) {
        const char* final_value = value && value[0] ? value : "on";
        if (ok && !fpv_funpay_form_set(form, name, final_value)) {
          ok = false;
        }
      } else if (!fpv_funpay_form_has_key(form, name)) {
        if (ok && !fpv_funpay_form_set(form, name, "")) {
          ok = false;
        }
      }
      fpv_free(value);
      fpv_free(type);
      fpv_free(name);
      continue;
    }
    if (type && strcmp(type, "radio") == 0) {
      char* checked = fpv_html_node_attr(node, "checked");
      if (checked) {
        char* value = fpv_html_node_attr(node, "value");
        if (ok && !fpv_funpay_form_set(form, name, value ? value : "")) {
          ok = false;
        }
        fpv_free(value);
        fpv_free(checked);
      }
      fpv_free(type);
      fpv_free(name);
      continue;
    }
    fpv_free(type);
    char* value = fpv_html_node_attr(node, "value");
    if (ok && !fpv_funpay_form_set(form, name, value ? value : "")) {
      ok = false;
    }
    fpv_free(value);
    fpv_free(name);
  }
  fpv_html_node_list_destroy(&inputs);
  if (!ok) {
    return false;
  }

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
    if (ok && !fpv_funpay_form_set(form, name, value ? value : "")) {
      ok = false;
    }
    fpv_free(value);
    fpv_free(name);
    if (!ok) {
      break;
    }
  }
  fpv_html_node_list_destroy(&textareas);
  if (!ok) {
    return false;
  }

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
      if (ok && !fpv_funpay_form_set(form, name, selected_value)) {
        ok = false;
      }
      fpv_free(selected_value);
    }
    fpv_free(name);
    if (!ok) {
      break;
    }
  }
  fpv_html_node_list_destroy(&selects);
  return ok;
}


uint32_t fpv_funpay_parse_wait_time(const char* message) {
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


bool fpv_funpay_debug_messages(const fpv_funpay_account_t* account) {
  return account && account->logger && account->debug_log_messages;
}


void fpv_funpay_logf(
    fpv_funpay_account_t* account,
    fpv_log_level_t level,
    const char* format,
    ...) {
  if (!account || !format) {
    return;
  }
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  uint64_t now_ms = fpv_time_now_ms();
  if (account->logger) {
    fpv_logger_log(account->logger, level, "funpay", buffer, now_ms);
  }
  if (account->bus) {
    fpv_event_t* event =
        fpv_event_create_log(level, "funpay", buffer, now_ms);
    if (event) {
      if (fpv_event_bus_publish(account->bus, event) != FPV_OK) {
        fpv_event_destroy(event);
      }
    }
  }
}


void fpv_funpay_log_html_snippet(
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
