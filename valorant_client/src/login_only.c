#include <ctype.h>
#include <curl/curl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "core/data/fpv_db.h"

#include "core/base/fpv_time.h"


#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#define VALORANT_CLIENT_ID "play-valorant-web-prod"
#define VALORANT_SCOPE "account openid"
#define VALORANT_REDIRECT_URI "https://playvalorant.com/opt_in"
#define VALORANT_AUTH_URL "https://auth.riotgames.com/api/v1/authorization"
#define VALORANT_ENTITLEMENTS_URL "https://entitlements.auth.riotgames.com/api/token/v1/"
#define VALORANT_USERINFO_URL "https://auth.riotgames.com/userinfo"
#define VALORANT_USER_AGENT \
  "RiotClient/57.0.0.4639025.4624825 rso-auth (Windows;10;19044)"
#define VALORANT_DATA_PATH "valorant_data.txt"
#define VALORANT_REGION_DEFAULT "eu"
#define VALORANT_CLIENT_VERSION_DEFAULT "release-09.09-shipping-11-2953160"
#define VALORANT_CLIENT_PLATFORM_DEFAULT \
  "eyJwbGF0Zm9ybVR5cGUiOiAiUEMiLCAicGxhdGZvcm1PUyI6ICJXaW5kb3dzIiwgInBsYXRmb3JtT1NWZXJzaW9uIjogIjEwLjAuMTkwNDMuMS4yNTYuNjRiaXQiLCAicGxhdGZvcm1DaGlwc2V0IjogIlVua25vd24ifQ=="
#define VALORANT_ITEM_TYPE_SKINS "e7c63390-eda7-46e0-bb7a-a6abdacd2433"
#define VALORANT_STORE_ENTITLEMENTS_TEMPLATE \
  "https://pd.ap.a.pvp.net/store/v1/entitlements/{user_id}/{item_type_id}"
#define VALORANT_DB_URL_DEFAULT \
  "postgres://fpv:fpv_password@localhost:5433/fpv_valorant"
#define DEFAULT_TOKEN_FILE "valorant_access_token.txt"

typedef struct http_response {
  char* data;
  size_t size;
  long status;
} http_response_t;

static char* dup_string(const char* value);
static char* strndup_string(const char* value, size_t len);

static void response_reset(http_response_t* response) {
  if (!response) {
    return;
  }
  free(response->data);
  response->data = NULL;
  response->size = 0;
  response->status = 0;
}

static size_t http_write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
  size_t total = size * nmemb;
  http_response_t* response = (http_response_t*)userdata;
  if (!response || total == 0) {
    return total;
  }

  char* grown = (char*)realloc(response->data, response->size + total + 1);
  if (!grown) {
    return 0;
  }

  response->data = grown;
  memcpy(response->data + response->size, ptr, total);
  response->size += total;
  response->data[response->size] = '\0';
  return total;
}

static bool http_request(
    CURL* curl,
    const char* method,
    const char* url,
    struct curl_slist* headers,
    const char* body,
    http_response_t* out) {
  if (!curl || !method || !url || !out) {
    return false;
  }

  response_reset(out);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : NULL);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body ? (long)strlen(body) : 0L);

  CURLcode code = curl_easy_perform(curl);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);

  if (code != CURLE_OK) {
    fprintf(stderr, "curl error for %s %s: %s\n", method, url, curl_easy_strerror(code));
    return false;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
  return true;
}

static bool http_request_with_error(
    CURL* curl,
    const char* method,
    const char* url,
    struct curl_slist* headers,
    const char* body,
    http_response_t* out,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !method || !url || !out) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return false;
  }

  response_reset(out);

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : NULL);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body ? (long)strlen(body) : 0L);

  CURLcode code = curl_easy_perform(curl);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);

  if (code != CURLE_OK) {
    if (out_error) {
      *out_error = dup_string(curl_easy_strerror(code));
    }
    return false;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status);
  return true;
}

static char* dup_string(const char* value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, value, len);
  out[len] = '\0';
  return out;
}

static char* strndup_string(const char* value, size_t len) {
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, value, len);
  out[len] = '\0';
  return out;
}

static char* json_escape(const char* value) {
  if (!value) {
    return NULL;
  }

  size_t length = 0;
  for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
    switch (*p) {
      case '\\':
      case '"':
        length += 2;
        break;
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        length += 2;
        break;
      default:
        if (*p < 0x20) {
          length += 6;
        } else {
          length += 1;
        }
        break;
    }
  }

  char* out = (char*)malloc(length + 1);
  if (!out) {
    return NULL;
  }

  size_t idx = 0;
  for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
    switch (*p) {
      case '\\':
        out[idx++] = '\\';
        out[idx++] = '\\';
        break;
      case '"':
        out[idx++] = '\\';
        out[idx++] = '"';
        break;
      case '\b':
        out[idx++] = '\\';
        out[idx++] = 'b';
        break;
      case '\f':
        out[idx++] = '\\';
        out[idx++] = 'f';
        break;
      case '\n':
        out[idx++] = '\\';
        out[idx++] = 'n';
        break;
      case '\r':
        out[idx++] = '\\';
        out[idx++] = 'r';
        break;
      case '\t':
        out[idx++] = '\\';
        out[idx++] = 't';
        break;
      default:
        if (*p < 0x20) {
          static const char* hex = "0123456789abcdef";
          out[idx++] = '\\';
          out[idx++] = 'u';
          out[idx++] = '0';
          out[idx++] = '0';
          out[idx++] = hex[(*p >> 4) & 0xf];
          out[idx++] = hex[*p & 0xf];
        } else {
          out[idx++] = (char)*p;
        }
        break;
    }
  }

  out[idx] = '\0';
  return out;
}

static char* extract_json_string(const char* json, const char* key) {
  if (!json || !key) {
    return NULL;
  }

  size_t key_len = strlen(key);
  size_t pattern_len = key_len + 2;
  char* pattern = (char*)malloc(pattern_len + 1);
  if (!pattern) {
    return NULL;
  }

  pattern[0] = '"';
  memcpy(pattern + 1, key, key_len);
  pattern[pattern_len - 1] = '"';
  pattern[pattern_len] = '\0';

  const char* match = strstr(json, pattern);
  free(pattern);
  if (!match) {
    return NULL;
  }

  const char* cursor = match + pattern_len;
  while (*cursor && *cursor != ':') {
    cursor++;
  }
  if (*cursor != ':') {
    return NULL;
  }
  cursor++;
  while (*cursor && isspace((unsigned char)*cursor)) {
    cursor++;
  }
  if (*cursor != '"') {
    return NULL;
  }
  cursor++;

  size_t capacity = strlen(cursor) + 1;
  char* out = (char*)malloc(capacity);
  if (!out) {
    return NULL;
  }

  size_t idx = 0;
  while (*cursor) {
    if (*cursor == '\\' && cursor[1]) {
      cursor++;
      out[idx++] = *cursor++;
      continue;
    }
    if (*cursor == '"') {
      out[idx] = '\0';
      return out;
    }
    out[idx++] = *cursor++;
  }

  free(out);
  return NULL;
}

static char* extract_query_value(const char* uri, const char* key) {
  if (!uri || !key) {
    return NULL;
  }

  char* needle = (char*)malloc(strlen(key) + 2);
  if (!needle) {
    return NULL;
  }
  snprintf(needle, strlen(key) + 2, "%s=", key);

  const char* start = strstr(uri, needle);
  free(needle);
  if (!start) {
    return NULL;
  }

  start += strlen(key) + 1;
  const char* end = start;
  while (*end && *end != '&' && *end != '#') {
    end++;
  }

  return strndup_string(start, (size_t)(end - start));
}

static char* url_encode(const char* value) {
  if (!value) {
    return NULL;
  }

  size_t len = 0;
  for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
      len += 1;
    } else {
      len += 3;
    }
  }

  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }

  static const char* hex = "0123456789ABCDEF";
  size_t idx = 0;
  for (const unsigned char* p = (const unsigned char*)value; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
      out[idx++] = (char)*p;
    } else {
      out[idx++] = '%';
      out[idx++] = hex[(*p >> 4) & 0x0f];
      out[idx++] = hex[*p & 0x0f];
    }
  }
  out[idx] = '\0';
  return out;
}

static char* extract_access_token_from_text(const char* text) {
  if (!text) {
    return NULL;
  }

  const char* marker = "access_token";
  const char* pos = strstr(text, marker);
  while (pos) {
    const char* cursor = pos + strlen(marker);
    if (strncmp(cursor, "=", 1) == 0) {
      cursor += 1;
    } else if (strncmp(cursor, "%3D", 3) == 0) {
      cursor += 3;
    } else if (strncmp(cursor, "\\u003d", 6) == 0) {
      cursor += 6;
    } else {
      pos = strstr(pos + 1, marker);
      continue;
    }

    const char* end = cursor;
    while (*end) {
      if (*end == '&' || *end == '"' || *end == '\\') {
        break;
      }
      if (strncmp(end, "%26", 3) == 0 || strncmp(end, "\\u0026", 6) == 0) {
        break;
      }
      end++;
    }
    if (end > cursor) {
      return strndup_string(cursor, (size_t)(end - cursor));
    }
    return NULL;
  }

  return NULL;
}

static char* normalize_access_token(const char* token_or_url) {
  if (!token_or_url) {
    return NULL;
  }

  char* token = extract_access_token_from_text(token_or_url);
  if (!token) {
    token = extract_query_value(token_or_url, "access_token");
  }
  if (token) {
    return token;
  }
  return dup_string(token_or_url);
}

static char* build_login_body(
    const char* username,
    const char* password,
    const char* captcha_token) {
  char* escaped_user = json_escape(username);
  char* escaped_pass = json_escape(password);
  char* escaped_captcha = NULL;
  if (captcha_token && captcha_token[0]) {
    escaped_captcha = json_escape(captcha_token);
  }
  if (!escaped_user || !escaped_pass || (captcha_token && !escaped_captcha)) {
    free(escaped_user);
    free(escaped_pass);
    free(escaped_captcha);
    return NULL;
  }

  const char* tmpl_no_captcha =
      "{\"type\":\"auth\",\"username\":\"%s\",\"password\":\"%s\"}";
  const char* tmpl_with_captcha =
      "{\"type\":\"auth\",\"username\":\"%s\",\"password\":\"%s\",\"captcha\":\"%s\"}";
  const bool use_captcha = escaped_captcha && escaped_captcha[0];
  const char* tmpl = use_captcha ? tmpl_with_captcha : tmpl_no_captcha;

  size_t len = use_captcha
                   ? (size_t)snprintf(NULL, 0, tmpl, escaped_user, escaped_pass,
                                      escaped_captcha)
                   : (size_t)snprintf(NULL, 0, tmpl, escaped_user, escaped_pass);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    free(escaped_user);
    free(escaped_pass);
    free(escaped_captcha);
    return NULL;
  }
  if (use_captcha) {
    snprintf(out, len + 1, tmpl, escaped_user, escaped_pass, escaped_captcha);
  } else {
    snprintf(out, len + 1, tmpl, escaped_user, escaped_pass);
  }
  free(escaped_user);
  free(escaped_pass);
  free(escaped_captcha);
  return out;
}

static char* build_auth_body(void) {
  const char* tmpl =
      "{\"client_id\":\"%s\",\"nonce\":\"1\",\"redirect_uri\":\"%s\","
      "\"response_type\":\"token id_token\",\"response_mode\":\"query\","
      "\"scope\":\"%s\"}";

  size_t len = (size_t)snprintf(NULL, 0, tmpl, VALORANT_CLIENT_ID,
                                VALORANT_REDIRECT_URI, VALORANT_SCOPE);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  snprintf(out, len + 1, tmpl, VALORANT_CLIENT_ID, VALORANT_REDIRECT_URI,
           VALORANT_SCOPE);
  return out;
}

static char* format_header(const char* name, const char* value) {
  if (!name || !value) {
    return NULL;
  }

  size_t len = strlen(name) + 2 + strlen(value);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }

  snprintf(out, len + 1, "%s: %s", name, value);
  return out;
}

static char* replace_placeholder(
    const char* input,
    const char* placeholder,
    const char* value) {
  if (!input || !placeholder || !value) {
    return NULL;
  }

  const char* pos = strstr(input, placeholder);
  if (!pos) {
    return dup_string(input);
  }

  size_t head_len = (size_t)(pos - input);
  size_t tail_len = strlen(pos + strlen(placeholder));
  size_t value_len = strlen(value);
  size_t out_len = head_len + value_len + tail_len;

  char* out = (char*)malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  memcpy(out, input, head_len);
  memcpy(out + head_len, value, value_len);
  memcpy(out + head_len + value_len, pos + strlen(placeholder), tail_len);
  out[out_len] = '\0';
  return out;
}

static char* replace_url_host(
    const char* url,
    const char* host_start,
    size_t host_len,
    const char* new_host) {
  if (!url || !host_start || !new_host) {
    return NULL;
  }
  size_t prefix_len = (size_t)(host_start - url);
  size_t new_host_len = strlen(new_host);
  size_t suffix_len = strlen(host_start + host_len);
  size_t out_len = prefix_len + new_host_len + suffix_len;

  char* out = (char*)malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  memcpy(out, url, prefix_len);
  memcpy(out + prefix_len, new_host, new_host_len);
  memcpy(out + prefix_len + new_host_len, host_start + host_len, suffix_len);
  out[out_len] = '\0';
  return out;
}

static char* apply_region_to_url(const char* url, const char* region) {
  if (!url) {
    return NULL;
  }
  if (!region || !region[0]) {
    return dup_string(url);
  }

  const char* scheme = strstr(url, "://");
  if (!scheme) {
    return dup_string(url);
  }
  const char* host_start = scheme + 3;
  const char* host_end = strchr(host_start, '/');
  if (!host_end) {
    host_end = url + strlen(url);
  }
  if (host_end <= host_start) {
    return dup_string(url);
  }

  size_t host_len = (size_t)(host_end - host_start);
  char* host = strndup_string(host_start, host_len);
  if (!host) {
    return NULL;
  }

  const char* suffix = ".a.pvp.net";
  char* new_host = NULL;
  if (strncmp(host, "pd.", 3) == 0 && strstr(host, suffix)) {
    size_t len = strlen("pd..a.pvp.net") + strlen(region) + 1;
    new_host = (char*)malloc(len);
    if (new_host) {
      snprintf(new_host, len, "pd.%s.a.pvp.net", region);
    }
  } else if (strncmp(host, "shared.", 7) == 0 && strstr(host, suffix)) {
    size_t len = strlen("shared..a.pvp.net") + strlen(region) + 1;
    new_host = (char*)malloc(len);
    if (new_host) {
      snprintf(new_host, len, "shared.%s.a.pvp.net", region);
    }
  } else if (strncmp(host, "glz-", 4) == 0 && strstr(host, suffix)) {
    size_t len = strlen("glz--1..a.pvp.net") + strlen(region) * 2 + 1;
    new_host = (char*)malloc(len);
    if (new_host) {
      snprintf(new_host, len, "glz-%s-1.%s.a.pvp.net", region, region);
    }
  }

  if (!new_host) {
    free(host);
    return dup_string(url);
  }

  char* updated = replace_url_host(url, host_start, host_len, new_host);
  free(host);
  free(new_host);
  return updated;
}

static bool status_ok(const http_response_t* response) {
  if (!response) {
    return false;
  }
  return response->status >= 200 && response->status < 300;
}

static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
}

static bool strings_equal_ignore_case(const char* a, const char* b) {
  if (!a || !b) {
    return false;
  }
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return false;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static bool env_is_truthy(const char* name) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return false;
  }
  if (strcmp(value, "1") == 0) {
    return true;
  }
  if (strings_equal_ignore_case(value, "true") ||
      strings_equal_ignore_case(value, "yes") ||
      strings_equal_ignore_case(value, "on")) {
    return true;
  }
  return false;
}

static int env_int(const char* name, int default_value) {
  const char* value = getenv(name);
  if (!value || !value[0]) {
    return default_value;
  }
  char* end = NULL;
  long parsed = strtol(value, &end, 10);
  if (end == value || parsed <= 0 || parsed > INT_MAX) {
    return default_value;
  }
  return (int)parsed;
}

static char* normalize_region(const char* region) {
  if (!region || !region[0]) {
    return NULL;
  }
  char* out = dup_string(region);
  if (!out) {
    return NULL;
  }
  for (char* cursor = out; *cursor; cursor++) {
    *cursor = (char)tolower((unsigned char)*cursor);
  }
  return out;
}

static bool response_indicates_captcha(const char* json, char** out_site_key) {
  if (out_site_key) {
    *out_site_key = NULL;
  }
  if (!json) {
    return false;
  }

  bool required = false;
  char* error = extract_json_string(json, "error");
  if (error && strcmp(error, "captcha_required") == 0) {
    required = true;
  }
  char* captcha = extract_json_string(json, "captcha");
  if (captcha) {
    required = true;
  }
  char* site_key = extract_json_string(json, "captcha_site_key");
  if (site_key) {
    required = true;
  }
  if (!required && strstr(json, "\"captcha\"")) {
    required = true;
  }

  if (out_site_key) {
    *out_site_key = site_key;
  } else {
    free(site_key);
  }
  free(error);
  free(captcha);
  return required;
}

static void print_login_error_details(const char* message) {
  if (!message || !message[0]) {
    return;
  }
  if (message[0] != '{') {
    fprintf(stderr, "%s\n", message);
    return;
  }

  char* error = extract_json_string(message, "error");
  char* desc = extract_json_string(message, "error_description");
  char* country = extract_json_string(message, "country");
  char* captcha = extract_json_string(message, "captcha");
  char* site_key = extract_json_string(message, "captcha_site_key");

  bool printed = false;
  if (error) {
    fprintf(stderr, "error=%s\n", error);
    printed = true;
  }
  if (desc) {
    fprintf(stderr, "description=%s\n", desc);
    printed = true;
  }
  if (country) {
    fprintf(stderr, "country=%s\n", country);
    printed = true;
  }
  if (captcha) {
    fprintf(stderr, "captcha=%s\n", captcha);
    printed = true;
  }
  if (site_key) {
    fprintf(stderr, "captcha_site_key=%s\n", site_key);
    printed = true;
  }
  if (!printed) {
    fprintf(stderr, "%s\n", message);
  }
  if (env_is_truthy("VALORANT_DEBUG_AUTH")) {
    fprintf(stderr, "raw=%s\n", message);
  }

  free(error);
  free(desc);
  free(country);
  free(captcha);
  free(site_key);
}

static void trim_newline(char* value) {
  if (!value) {
    return;
  }
  size_t len = strlen(value);
  while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
    value[len - 1] = '\0';
    len--;
  }
}

static char* read_line_stdin(const char* prompt) {
  if (prompt) {
    fputs(prompt, stdout);
    fflush(stdout);
  }

  char* line = NULL;
  size_t capacity = 0;
  ssize_t len = getline(&line, &capacity, stdin);
  if (len < 0) {
    free(line);
    return NULL;
  }
  trim_newline(line);
  return line;
}

static char* read_text_file(const char* path) {
  if (!path || !path[0]) {
    return NULL;
  }

  FILE* file = fopen(path, "r");
  if (!file) {
    return NULL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }

  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  char* buffer = (char*)malloc((size_t)size + 1);
  if (!buffer) {
    fclose(file);
    return NULL;
  }

  size_t read_size = fread(buffer, 1, (size_t)size, file);
  fclose(file);
  buffer[read_size] = '\0';
  trim_newline(buffer);
  return buffer;
}

typedef struct valorant_endpoint {
  char* key;
  char* method;
  char* url;
} valorant_endpoint_t;

typedef struct valorant_endpoint_list {
  valorant_endpoint_t* items;
  size_t count;
} valorant_endpoint_list_t;

static void valorant_endpoint_clear(valorant_endpoint_t* endpoint) {
  if (!endpoint) {
    return;
  }
  free(endpoint->key);
  free(endpoint->method);
  free(endpoint->url);
  endpoint->key = NULL;
  endpoint->method = NULL;
  endpoint->url = NULL;
}

static void valorant_endpoint_list_clear(valorant_endpoint_list_t* list) {
  if (!list || !list->items) {
    return;
  }
  for (size_t i = 0; i < list->count; i++) {
    valorant_endpoint_clear(&list->items[i]);
  }
  free(list->items);
  list->items = NULL;
  list->count = 0;
}

static bool valorant_method_supported(const char* method) {
  if (!method) {
    return false;
  }
  return strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0 ||
         strcmp(method, "PUT") == 0 || strcmp(method, "DELETE") == 0;
}

static char* valorant_normalize_method(const char* start, size_t len) {
  if (!start || len == 0) {
    return NULL;
  }
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = (char)toupper((unsigned char)start[i]);
  }
  out[len] = '\0';
  return out;
}

static char* trim_trailing_whitespace_copy(const char* text) {
  if (!text) {
    return NULL;
  }
  size_t len = strlen(text);
  while (len > 0 && isspace((unsigned char)text[len - 1])) {
    len--;
  }
  return strndup_string(text, len);
}

static bool parse_valorant_endpoint_line(
    const char* line,
    valorant_endpoint_t* out) {
  if (!line || !out) {
    return false;
  }

  const char* open = strchr(line, '[');
  if (!open) {
    return false;
  }
  const char* close = strchr(open, ']');
  if (!close || close <= open + 1) {
    return false;
  }

  valorant_endpoint_t endpoint = {0};
  endpoint.key = strndup_string(open + 1, (size_t)(close - open - 1));
  if (!endpoint.key) {
    return false;
  }

  const char* cursor = close + 1;
  while (*cursor && isspace((unsigned char)*cursor)) {
    cursor++;
  }
  const char* method_start = cursor;
  while (*cursor && isalpha((unsigned char)*cursor)) {
    cursor++;
  }
  size_t method_len = (size_t)(cursor - method_start);
  if (method_len == 0) {
    valorant_endpoint_clear(&endpoint);
    return false;
  }
  endpoint.method = valorant_normalize_method(method_start, method_len);
  if (!endpoint.method || !valorant_method_supported(endpoint.method)) {
    valorant_endpoint_clear(&endpoint);
    return false;
  }

  const char* url_start = strstr(cursor, "http");
  if (url_start) {
    endpoint.url = trim_trailing_whitespace_copy(url_start);
  }

  *out = endpoint;
  return true;
}

static valorant_endpoint_list_t load_valorant_endpoints(const char* path) {
  valorant_endpoint_list_t list = {0};
  if (!path || !path[0]) {
    return list;
  }

  FILE* file = fopen(path, "r");
  if (!file) {
    return list;
  }

  char line[4096];
  while (fgets(line, sizeof(line), file)) {
    valorant_endpoint_t endpoint = {0};
    if (!parse_valorant_endpoint_line(line, &endpoint)) {
      continue;
    }
    valorant_endpoint_t* grown =
        (valorant_endpoint_t*)realloc(list.items,
                                      sizeof(*list.items) * (list.count + 1));
    if (!grown) {
      valorant_endpoint_clear(&endpoint);
      break;
    }
    list.items = grown;
    list.items[list.count++] = endpoint;
  }

  fclose(file);
  return list;
}

static bool file_exists(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
  FILE* file = fopen(path, "r");
  if (!file) {
    return false;
  }
  fclose(file);
  return true;
}

static char* build_home_path(const char* home, const char* suffix) {
  if (!home || !suffix) {
    return NULL;
  }
  size_t len = strlen(home) + strlen(suffix) + 2;
  char* out = (char*)malloc(len);
  if (!out) {
    return NULL;
  }
  snprintf(out, len, "%s/%s", home, suffix);
  return out;
}

typedef struct riot_lockfile {
  char* port;
  char* password;
  char* protocol;
} riot_lockfile_t;

static void riot_lockfile_clear(riot_lockfile_t* lockfile) {
  if (!lockfile) {
    return;
  }
  free(lockfile->port);
  free(lockfile->password);
  free(lockfile->protocol);
  lockfile->port = NULL;
  lockfile->password = NULL;
  lockfile->protocol = NULL;
}

static char* find_lockfile_path(void) {
  const char* env_path = getenv("RIOT_LOCKFILE");
  if (env_path && env_path[0] && file_exists(env_path)) {
    return dup_string(env_path);
  }

  const char* home = getenv("HOME");
  if (home && home[0]) {
    const char* candidates[] = {
        "Library/Application Support/Riot Games/Riot Client/lockfile",
        "Library/Application Support/Riot Games/lockfile",
        "Library/Application Support/VALORANT/lockfile",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      char* path = build_home_path(home, candidates[i]);
      if (path && file_exists(path)) {
        return path;
      }
      free(path);
    }
  }

  const char* local_appdata = getenv("LOCALAPPDATA");
  if (local_appdata && local_appdata[0]) {
    const char* candidates[] = {
        "Riot Games/Riot Client/lockfile",
        "VALORANT/lockfile",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
      size_t len = strlen(local_appdata) + strlen(candidates[i]) + 2;
      char* path = (char*)malloc(len);
      if (!path) {
        continue;
      }
      snprintf(path, len, "%s/%s", local_appdata, candidates[i]);
      if (file_exists(path)) {
        return path;
      }
      free(path);
    }
  }

  const char* program_files = getenv("PROGRAMFILES");
  if (program_files && program_files[0]) {
    size_t len = strlen(program_files) +
                 strlen("Riot Games/Riot Client/lockfile") + 2;
    char* path = (char*)malloc(len);
    if (path) {
      snprintf(path, len, "%s/%s", program_files,
               "Riot Games/Riot Client/lockfile");
      if (file_exists(path)) {
        return path;
      }
      free(path);
    }
  }

  return NULL;
}

static bool parse_lockfile(const char* path, riot_lockfile_t* out) {
  if (!path || !out) {
    return false;
  }

  FILE* file = fopen(path, "r");
  if (!file) {
    return false;
  }

  char line[512];
  if (!fgets(line, sizeof(line), file)) {
    fclose(file);
    return false;
  }
  fclose(file);
  trim_newline(line);

  char* tokens[5] = {0};
  char* cursor = line;
  for (size_t i = 0; i < 5; i++) {
    tokens[i] = (i == 0) ? strtok(cursor, ":") : strtok(NULL, ":");
    cursor = NULL;
    if (!tokens[i]) {
      return false;
    }
  }

  out->port = dup_string(tokens[2]);
  out->password = dup_string(tokens[3]);
  out->protocol = dup_string(tokens[4]);
  if (!out->port || !out->password || !out->protocol) {
    riot_lockfile_clear(out);
    return false;
  }
  return true;
}

static char* base64_encode(const unsigned char* data, size_t len) {
  static const char* table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!data) {
    return NULL;
  }

  size_t out_len = 4 * ((len + 2) / 3);
  char* out = (char*)malloc(out_len + 1);
  if (!out) {
    return NULL;
  }

  size_t i = 0;
  size_t j = 0;
  while (i < len) {
    size_t remaining = len - i;
    uint32_t octet_a = data[i++];
    uint32_t octet_b = remaining > 1 ? data[i++] : 0;
    uint32_t octet_c = remaining > 2 ? data[i++] : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out[j++] = table[(triple >> 18) & 0x3f];
    out[j++] = table[(triple >> 12) & 0x3f];
    out[j++] = remaining > 1 ? table[(triple >> 6) & 0x3f] : '=';
    out[j++] = remaining > 2 ? table[triple & 0x3f] : '=';
  }

  out[out_len] = '\0';
  return out;
}

typedef struct valorant_request_context {
  const char* user_id;
  const char* region;
  const char* item_type_id;
} valorant_request_context_t;

static char* build_env_key(const char* placeholder) {
  if (!placeholder || !placeholder[0]) {
    return NULL;
  }
  size_t prefix_len = strlen("VALORANT_");
  size_t name_len = strlen(placeholder);
  char* key = (char*)malloc(prefix_len + name_len + 1);
  if (!key) {
    return NULL;
  }
  memcpy(key, "VALORANT_", prefix_len);
  for (size_t i = 0; i < name_len; i++) {
    unsigned char ch = (unsigned char)placeholder[i];
    if (isalnum(ch)) {
      key[prefix_len + i] = (char)toupper(ch);
    } else {
      key[prefix_len + i] = '_';
    }
  }
  key[prefix_len + name_len] = '\0';
  return key;
}

static const char* lookup_placeholder_value(
    const char* placeholder,
    const valorant_request_context_t* context) {
  if (!placeholder || !placeholder[0] || !context) {
    return NULL;
  }
  if (strcmp(placeholder, "user_id") == 0 ||
      strcmp(placeholder, "player_id") == 0) {
    return context->user_id;
  }
  if (strcmp(placeholder, "region") == 0) {
    return context->region;
  }
  if (strcmp(placeholder, "item_type_id") == 0) {
    return context->item_type_id;
  }
  if (strchr(placeholder, ',') || strchr(placeholder, ' ')) {
    return NULL;
  }

  char* env_key = build_env_key(placeholder);
  if (!env_key) {
    return NULL;
  }
  const char* value = getenv(env_key);
  free(env_key);
  if (!value || !value[0]) {
    return NULL;
  }
  return value;
}

static char* resolve_placeholders(
    const char* url,
    const valorant_request_context_t* context,
    char** out_missing) {
  if (out_missing) {
    *out_missing = NULL;
  }
  if (!url || !context) {
    return NULL;
  }

  char* resolved = dup_string(url);
  if (!resolved) {
    return NULL;
  }

  while (true) {
    char* start = strchr(resolved, '{');
    if (!start) {
      break;
    }
    char* end = strchr(start, '}');
    if (!end || end <= start + 1) {
      break;
    }

    char* placeholder = strndup_string(start + 1, (size_t)(end - start - 1));
    if (!placeholder) {
      free(resolved);
      return NULL;
    }
    const char* value = lookup_placeholder_value(placeholder, context);
    if (!value) {
      if (out_missing) {
        *out_missing = placeholder;
      } else {
        free(placeholder);
      }
      free(resolved);
      return NULL;
    }

    size_t token_len = strlen(placeholder) + 2;
    char* token = (char*)malloc(token_len + 1);
    if (!token) {
      free(placeholder);
      free(resolved);
      return NULL;
    }
    snprintf(token, token_len + 1, "{%s}", placeholder);
    free(placeholder);

    char* replaced = replace_placeholder(resolved, token, value);
    free(token);
    free(resolved);
    resolved = replaced;
    if (!resolved) {
      return NULL;
    }
  }

  return resolved;
}

static char* read_env_or_prompt(const char* env_name, const char* prompt) {
  const char* env_value = getenv(env_name);
  if (env_value && env_value[0]) {
    return dup_string(env_value);
  }
  return read_line_stdin(prompt);
}

static bool write_text_file(const char* path, const char* text) {
  if (!path || !path[0] || !text) {
    return false;
  }

  FILE* file = fopen(path, "w");
  if (!file) {
    return false;
  }

  size_t len = strlen(text);
  size_t written = fwrite(text, 1, len, file);
  fclose(file);
  return written == len;
}

static char* read_command_output(const char* command) {
  if (!command || !command[0]) {
    return NULL;
  }

  FILE* pipe = popen(command, "r");
  if (!pipe) {
    return NULL;
  }

  char* buffer = NULL;
  size_t size = 0;
  char chunk[4096];
  size_t read_len = 0;
  while ((read_len = fread(chunk, 1, sizeof(chunk), pipe)) > 0) {
    char* grown = (char*)realloc(buffer, size + read_len + 1);
    if (!grown) {
      free(buffer);
      buffer = NULL;
      break;
    }
    buffer = grown;
    memcpy(buffer + size, chunk, read_len);
    size += read_len;
    buffer[size] = '\0';
  }

  pclose(pipe);
  if (buffer) {
    trim_newline(buffer);
  }
  return buffer;
}

static char* read_clipboard_text(void) {
#ifdef _WIN32
  return read_command_output("powershell -NoProfile -Command Get-Clipboard");
#elif __APPLE__
  return read_command_output("pbpaste");
#else
  char* text = read_command_output("xclip -selection clipboard -o 2>/dev/null");
  if (text && text[0]) {
    return text;
  }
  free(text);
  return read_command_output("xsel --clipboard --output 2>/dev/null");
#endif
}

static bool open_browser_url(const char* url) {
  if (!url || !url[0]) {
    return false;
  }
#ifdef _WIN32
  HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
  return (INT_PTR)result > 32;
#elif __APPLE__
  size_t len = strlen(url) + 32;
  char* cmd = (char*)malloc(len);
  if (!cmd) {
    return false;
  }
  snprintf(cmd, len, "open '%s' >/dev/null 2>&1", url);
  int rc = system(cmd);
  free(cmd);
  return rc == 0;
#else
  size_t len = strlen(url) + 40;
  char* cmd = (char*)malloc(len);
  if (!cmd) {
    return false;
  }
  snprintf(cmd, len, "xdg-open '%s' >/dev/null 2>&1", url);
  int rc = system(cmd);
  free(cmd);
  return rc == 0;
#endif
}

static bool text_looks_like_token(const char* text) {
  if (!text) {
    return false;
  }
  size_t len = strlen(text);
  if (len < 20) {
    return false;
  }
  for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
    if (isspace(*p)) {
      return false;
    }
  }
  return true;
}

static char* extract_token_from_text(const char* text) {
  if (!text || !text[0]) {
    return NULL;
  }
  char* token = extract_query_value(text, "access_token");
  if (!token) {
    token = extract_access_token_from_text(text);
  }
  if (!token && !env_is_truthy("VALORANT_REJECT_RAW_TOKEN") &&
      text_looks_like_token(text)) {
    token = dup_string(text);
  }
  return token;
}

static char* poll_clipboard_for_token(int timeout_sec, int poll_ms) {
  if (poll_ms <= 0) {
    return NULL;
  }
  if (poll_ms < 250) {
    poll_ms = 250;
  }

  time_t start = time(NULL);
  while (true) {
    char* clipboard = read_clipboard_text();
    if (clipboard && clipboard[0]) {
      char* token = extract_token_from_text(clipboard);
      free(clipboard);
      if (token) {
        return token;
      }
    } else {
      free(clipboard);
    }

    if (timeout_sec > 0 &&
        difftime(time(NULL), start) >= (double)timeout_sec) {
      break;
    }
    sleep_ms((unsigned int)poll_ms);
  }
  return NULL;
}

static char* riot_login_local(CURL* curl, char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl) {
    if (out_error) {
      *out_error = dup_string("invalid_curl");
    }
    return NULL;
  }

  char* lockfile_path = find_lockfile_path();
  if (!lockfile_path) {
    if (out_error) {
      *out_error = dup_string("lockfile_not_found");
    }
    return NULL;
  }

  riot_lockfile_t lockfile = {0};
  if (!parse_lockfile(lockfile_path, &lockfile)) {
    if (out_error) {
      *out_error = dup_string("lockfile_parse_failed");
    }
    free(lockfile_path);
    return NULL;
  }
  free(lockfile_path);

  const char* protocol = lockfile.protocol && lockfile.protocol[0]
                              ? lockfile.protocol
                              : "https";
  size_t url_len = strlen(protocol) + strlen(lockfile.port) +
                   strlen("://127.0.0.1:/rso-auth/v1/authorization") + 1;
  char* url = (char*)malloc(url_len);
  if (!url) {
    if (out_error) {
      *out_error = dup_string("local_url_failed");
    }
    riot_lockfile_clear(&lockfile);
    return NULL;
  }
  snprintf(url, url_len, "%s://127.0.0.1:%s/rso-auth/v1/authorization",
           protocol, lockfile.port);

  size_t creds_len = strlen("riot:") + strlen(lockfile.password) + 1;
  char* creds = (char*)malloc(creds_len);
  if (!creds) {
    if (out_error) {
      *out_error = dup_string("local_creds_failed");
    }
    free(url);
    riot_lockfile_clear(&lockfile);
    return NULL;
  }
  snprintf(creds, creds_len, "riot:%s", lockfile.password);

  char* encoded = base64_encode((const unsigned char*)creds, strlen(creds));
  free(creds);
  if (!encoded) {
    if (out_error) {
      *out_error = dup_string("local_auth_encode_failed");
    }
    free(url);
    riot_lockfile_clear(&lockfile);
    return NULL;
  }

  char* auth_value = (char*)malloc(strlen("Basic ") + strlen(encoded) + 1);
  if (!auth_value) {
    if (out_error) {
      *out_error = dup_string("local_auth_header_failed");
    }
    free(encoded);
    free(url);
    riot_lockfile_clear(&lockfile);
    return NULL;
  }
  snprintf(auth_value, strlen("Basic ") + strlen(encoded) + 1, "Basic %s",
           encoded);
  free(encoded);

  struct curl_slist* headers = NULL;
  char* auth_header = format_header("Authorization", auth_value);
  free(auth_value);
  if (!auth_header) {
    if (out_error) {
      *out_error = dup_string("local_auth_header_failed");
    }
    free(url);
    riot_lockfile_clear(&lockfile);
    return NULL;
  }
  headers = curl_slist_append(headers, auth_header);
  headers = curl_slist_append(headers, "Accept: application/json");
  free(auth_header);

  http_response_t response = {0};
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  bool ok = http_request(curl, "GET", url, headers, NULL, &response);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

  free(url);
  curl_slist_free_all(headers);
  riot_lockfile_clear(&lockfile);

  if (!ok || !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("local_auth_failed");
    }
    response_reset(&response);
    return NULL;
  }

  char* token = extract_json_string(response.data, "accessToken");
  if (!token) {
    token = extract_json_string(response.data, "access_token");
  }
  if (!token && out_error) {
    *out_error = response.data ? dup_string(response.data)
                               : dup_string("local_token_missing");
  }
  response_reset(&response);
  return token;
}

static char* riot_login_browser(char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }

  const char* env_token = getenv("VALORANT_ACCESS_TOKEN");
  if (env_token && env_token[0]) {
    char* token = normalize_access_token(env_token);
    if (!token && out_error) {
      *out_error = dup_string("browser_token_missing");
    }
    return token;
  }

  const char* env_url = getenv("VALORANT_REDIRECT_URL");
  if (env_url && env_url[0]) {
    char* token = normalize_access_token(env_url);
    if (!token && out_error) {
      *out_error = dup_string("browser_token_missing");
    }
    return token;
  }

  const char* env_file = getenv("VALORANT_REDIRECT_FILE");
  if (env_file && env_file[0]) {
    char* content = read_text_file(env_file);
    if (!content) {
      if (out_error) {
        *out_error = dup_string("browser_file_read_failed");
      }
      return NULL;
    }
    char* token = normalize_access_token(content);
    if (!token && out_error) {
      *out_error = dup_string("browser_token_missing");
    }
    free(content);
    return token;
  }

  char* redirect_encoded = url_encode(VALORANT_REDIRECT_URI);
  char* scope_encoded = url_encode(VALORANT_SCOPE);
  if (!redirect_encoded || !scope_encoded) {
    if (out_error) {
      *out_error = dup_string("browser_url_encode_failed");
    }
    free(redirect_encoded);
    free(scope_encoded);
    return NULL;
  }

  const char* tmpl =
      "https://auth.riotgames.com/authorize?redirect_uri=%s&client_id=%s&"
      "response_type=token%%20id_token&nonce=1&scope=%s";
  size_t login_len =
      (size_t)snprintf(NULL, 0, tmpl, redirect_encoded, VALORANT_CLIENT_ID,
                       scope_encoded);
  char* login_url = (char*)malloc(login_len + 1);
  if (!login_url) {
    if (out_error) {
      *out_error = dup_string("browser_url_build_failed");
    }
    free(redirect_encoded);
    free(scope_encoded);
    return NULL;
  }
  snprintf(login_url, login_len + 1, tmpl, redirect_encoded, VALORANT_CLIENT_ID,
           scope_encoded);
  free(redirect_encoded);
  free(scope_encoded);

  bool auto_open = !env_is_truthy("VALORANT_DISABLE_BROWSER_OPEN");
  if (auto_open) {
    if (!open_browser_url(login_url)) {
      fprintf(stderr, "Failed to auto-open browser. Please open the URL manually.\n");
    }
  }

  printf("Open this URL in your browser to login:\n%s\n", login_url);
  printf(
      "After login, copy the FULL redirect URL (should start with %s#access_token=...).\n",
      VALORANT_REDIRECT_URI);
  fflush(stdout);

  bool clipboard_enabled = !env_is_truthy("VALORANT_DISABLE_CLIPBOARD");
  if (clipboard_enabled) {
    int timeout_sec = env_int("VALORANT_CLIPBOARD_TIMEOUT_SEC", 0);
    int poll_ms = env_int("VALORANT_CLIPBOARD_POLL_MS", 750);
    if (timeout_sec > 0) {
      fprintf(stderr,
              "Waiting for redirect URL or access token in clipboard (%ds). "
              "Copy it from the browser (Cmd+C).\n",
              timeout_sec);
    } else {
      fprintf(stderr,
              "Waiting for redirect URL or access token in clipboard. "
              "Copy it from the browser (Cmd+C).\n");
    }
    char* token = poll_clipboard_for_token(timeout_sec, poll_ms);
    if (token) {
      free(login_url);
      return token;
    }
    fprintf(stderr, "Clipboard polling timed out.\n");
  }

  if (env_is_truthy("VALORANT_PASTE_PROMPT")) {
    const int max_attempts = 3;
    for (int attempt = 1; attempt <= max_attempts; attempt++) {
      char* pasted = read_line_stdin("Paste redirect URL or access token: ");
      if (!pasted || !pasted[0]) {
        free(pasted);
        if (out_error) {
          *out_error = dup_string("browser_no_url");
        }
        free(login_url);
        return NULL;
      }

      fprintf(stderr, "Parsing redirect URL...\n");
      char* access_token = extract_token_from_text(pasted);
      if (access_token) {
        free(pasted);
        free(login_url);
        return access_token;
      }

      if (strstr(pasted, "auth.riotgames.com/authorize") ||
          strstr(pasted, "authenticate.riotgames.com") ||
          strstr(pasted, "redirect_uri=")) {
        fprintf(stderr,
                "You pasted the login URL. Please paste the final redirect URL "
                "after login (playvalorant.com/...#access_token=...).\n");
      } else {
        fprintf(stderr,
                "No access_token found in pasted input. Please try again.\n");
      }
      free(pasted);
    }
  }
  free(login_url);

  if (out_error) {
    *out_error = dup_string("browser_token_missing");
  }
  return NULL;
}

static char* riot_login(
    CURL* curl,
    const char* username,
    const char* password,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !username || !password || !username[0] || !password[0]) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return NULL;
  }

  char* auth_body = build_auth_body();
  if (!auth_body) {
    if (out_error) {
      *out_error = dup_string("auth_body_failed");
    }
    return NULL;
  }

  http_response_t response = {0};
  struct curl_slist* headers = NULL;
  char* login_body = NULL;
  char* auth_type = NULL;
  char* uri = NULL;
  char* access_token = NULL;
  char* origin_header = NULL;
  char* referer_header = NULL;
  char* captcha_token = NULL;
  const char* env_captcha = getenv("VALORANT_CAPTCHA_TOKEN");
  if (env_captcha && env_captcha[0]) {
    captcha_token = dup_string(env_captcha);
  }

  for (int attempt = 0; attempt < 2; attempt++) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
    origin_header = format_header("Origin", "https://auth.riotgames.com");
    referer_header = format_header("Referer", "https://auth.riotgames.com");
    if (origin_header) {
      headers = curl_slist_append(headers, origin_header);
    }
    if (referer_header) {
      headers = curl_slist_append(headers, referer_header);
    }

    if (!http_request(curl, "POST", VALORANT_AUTH_URL, headers, auth_body, &response) ||
        !status_ok(&response)) {
      if (out_error) {
        *out_error =
            response.data ? dup_string(response.data) : dup_string("auth_init_failed");
      }
      goto cleanup;
    }
    response_reset(&response);

    login_body = build_login_body(username, password, captcha_token);
    if (!login_body) {
      if (out_error) {
        *out_error = dup_string("login_body_failed");
      }
      goto cleanup;
    }

    if (!http_request(curl, "PUT", VALORANT_AUTH_URL, headers, login_body, &response) ||
        !status_ok(&response)) {
      if (!captcha_token) {
        char* site_key = NULL;
        if (response_indicates_captcha(response.data, &site_key)) {
          if (site_key) {
            fprintf(stderr, "Captcha required (site key: %s).\n", site_key);
          } else {
            fprintf(stderr, "Captcha required.\n");
          }
          free(site_key);
          char* prompt_token =
              read_env_or_prompt("VALORANT_CAPTCHA_TOKEN", "Captcha token: ");
          if (prompt_token && prompt_token[0]) {
            captcha_token = prompt_token;
            free(origin_header);
            free(referer_header);
            origin_header = NULL;
            referer_header = NULL;
            free(login_body);
            login_body = NULL;
            response_reset(&response);
            curl_slist_free_all(headers);
            headers = NULL;
            continue;
          }
          free(prompt_token);
        }
      }
      if (out_error) {
        *out_error =
            response.data ? dup_string(response.data) : dup_string("login_failed");
      }
      goto cleanup;
    }
    break;
  }

  auth_type = extract_json_string(response.data, "type");
  if (!auth_type) {
    if (out_error) {
      *out_error = dup_string("login_type_missing");
    }
    goto cleanup;
  }

  if (strcmp(auth_type, "multifactor") == 0) {
    free(auth_type);
    auth_type = NULL;

    char* code = read_env_or_prompt("VALORANT_MFA_CODE", "Enter Riot MFA code: ");
    if (!code || !code[0]) {
      if (out_error) {
        *out_error = dup_string("mfa_required");
      }
      free(code);
      goto cleanup;
    }

    const char* mfa_tmpl =
        "{\"type\":\"multifactor\",\"code\":\"%s\",\"rememberDevice\":true}";
    size_t mfa_len = (size_t)snprintf(NULL, 0, mfa_tmpl, code);
    char* mfa_body = (char*)malloc(mfa_len + 1);
    if (!mfa_body) {
      if (out_error) {
        *out_error = dup_string("mfa_body_failed");
      }
      free(code);
      goto cleanup;
    }
    snprintf(mfa_body, mfa_len + 1, mfa_tmpl, code);
    free(code);

    response_reset(&response);
    if (!http_request(curl, "PUT", VALORANT_AUTH_URL, headers, mfa_body, &response) ||
        !status_ok(&response)) {
      if (out_error) {
        *out_error = response.data ? dup_string(response.data)
                                   : dup_string("mfa_failed");
      }
      free(mfa_body);
      goto cleanup;
    }
    free(mfa_body);

    auth_type = extract_json_string(response.data, "type");
    if (!auth_type) {
      if (out_error) {
        *out_error = dup_string("mfa_type_missing");
      }
      goto cleanup;
    }
  }

  if (strcmp(auth_type, "response") != 0) {
    if (out_error) {
      *out_error =
          response.data ? dup_string(response.data) : dup_string("auth_failed");
    }
    goto cleanup;
  }

  uri = extract_json_string(response.data, "uri");
  if (uri) {
    access_token = extract_query_value(uri, "access_token");
  }
  if (!access_token) {
    access_token = extract_access_token_from_text(response.data);
  }
  if (!access_token && out_error) {
    *out_error = dup_string(uri ? "access_token_missing" : "uri_missing");
  }

cleanup:
  free(auth_body);
  free(login_body);
  free(auth_type);
  free(uri);
  free(origin_header);
  free(referer_header);
  free(captcha_token);
  response_reset(&response);
  curl_slist_free_all(headers);
  return access_token;
}

static char* build_bearer_header(const char* token) {
  if (!token || !token[0]) {
    return NULL;
  }
  size_t len = strlen("Bearer ") + strlen(token);
  char* value = (char*)malloc(len + 1);
  if (!value) {
    return NULL;
  }
  snprintf(value, len + 1, "Bearer %s", token);
  char* header = format_header("Authorization", value);
  free(value);
  return header;
}

static char* fetch_entitlements_token(
    CURL* curl,
    const char* access_token,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !access_token || !access_token[0]) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return NULL;
  }

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  char* auth_header = build_bearer_header(access_token);
  if (!auth_header) {
    if (out_error) {
      *out_error = dup_string("entitlements_auth_header_failed");
    }
    curl_slist_free_all(headers);
    return NULL;
  }
  headers = curl_slist_append(headers, auth_header);
  free(auth_header);

  http_response_t response = {0};
  char* request_error = NULL;
  bool ok = http_request_with_error(
      curl,
      "POST",
      VALORANT_ENTITLEMENTS_URL,
      headers,
      "{}",
      &response,
      &request_error);
  curl_slist_free_all(headers);

  if (!ok) {
    if (out_error) {
      *out_error = request_error ? request_error : dup_string("entitlements_failed");
    } else {
      free(request_error);
    }
    response_reset(&response);
    return NULL;
  }
  free(request_error);

  if (!status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("entitlements_failed");
    }
    response_reset(&response);
    return NULL;
  }

  char* entitlements = extract_json_string(response.data, "entitlements_token");
  if (!entitlements && out_error) {
    *out_error = dup_string("entitlements_missing");
  }
  response_reset(&response);
  return entitlements;
}

static char* fetch_user_id(
    CURL* curl,
    const char* access_token,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !access_token || !access_token[0]) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return NULL;
  }

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  char* auth_header = build_bearer_header(access_token);
  if (!auth_header) {
    if (out_error) {
      *out_error = dup_string("userinfo_auth_header_failed");
    }
    curl_slist_free_all(headers);
    return NULL;
  }
  headers = curl_slist_append(headers, auth_header);
  free(auth_header);

  http_response_t response = {0};
  char* request_error = NULL;
  bool ok = http_request_with_error(
      curl,
      "GET",
      VALORANT_USERINFO_URL,
      headers,
      NULL,
      &response,
      &request_error);
  curl_slist_free_all(headers);

  if (!ok) {
    if (out_error) {
      *out_error = request_error ? request_error : dup_string("userinfo_failed");
    } else {
      free(request_error);
    }
    response_reset(&response);
    return NULL;
  }
  free(request_error);

  if (!status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("userinfo_failed");
    }
    response_reset(&response);
    return NULL;
  }

  char* user_id = extract_json_string(response.data, "sub");
  if (!user_id && out_error) {
    *out_error = dup_string("userinfo_missing");
  }
  response_reset(&response);
  return user_id;
}

static struct curl_slist* build_valorant_api_headers(
    const char* access_token,
    const char* entitlements,
    bool include_json) {
  if (!access_token || !access_token[0]) {
    return NULL;
  }

  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  if (include_json) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }

  char* auth_header = build_bearer_header(access_token);
  if (!auth_header) {
    curl_slist_free_all(headers);
    return NULL;
  }
  headers = curl_slist_append(headers, auth_header);
  free(auth_header);

  if (entitlements && entitlements[0]) {
    char* ent_header = format_header("X-Riot-Entitlements-JWT", entitlements);
    if (!ent_header) {
      curl_slist_free_all(headers);
      return NULL;
    }
    headers = curl_slist_append(headers, ent_header);
    free(ent_header);
  }

  const char* client_version = getenv("VALORANT_CLIENT_VERSION");
  if (!client_version || !client_version[0]) {
    client_version = VALORANT_CLIENT_VERSION_DEFAULT;
  }
  char* version_header = format_header("X-Riot-ClientVersion", client_version);
  if (!version_header) {
    curl_slist_free_all(headers);
    return NULL;
  }
  headers = curl_slist_append(headers, version_header);
  free(version_header);

  const char* client_platform = getenv("VALORANT_CLIENT_PLATFORM");
  if (!client_platform || !client_platform[0]) {
    client_platform = VALORANT_CLIENT_PLATFORM_DEFAULT;
  }
  char* platform_header = format_header("X-Riot-ClientPlatform", client_platform);
  if (!platform_header) {
    curl_slist_free_all(headers);
    return NULL;
  }
  headers = curl_slist_append(headers, platform_header);
  free(platform_header);

  return headers;
}

static fpv_db_t* open_valorant_db(fpv_result_t* out_result) {
  if (out_result) {
    *out_result = FPV_OK;
  }
  const char* db_url = getenv("FPV_DB_URL");
  if (!db_url || !db_url[0]) {
    db_url = getenv("VALORANT_DB_URL");
  }
  if (!db_url || !db_url[0]) {
    db_url = VALORANT_DB_URL_DEFAULT;
  }

  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = db_url;

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

  return db;
}

static fpv_result_t store_valorant_fetch(
    fpv_db_t* db,
    const char* user_id,
    const char* region,
    const char* endpoint_key,
    const char* method,
    const char* url,
    long status_code,
    bool success,
    const char* response_body,
    const char* error,
    uint64_t fetched_at_ms,
    unsigned int sequence) {
  if (!db || !endpoint_key || !method) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  const char* safe_user = user_id && user_id[0] ? user_id : "unknown";
  const char* safe_region = region && region[0] ? region : "unknown";

  char id_buf[64];
  char status_buf[32];
  char success_buf[8];
  char fetched_buf[32];
  const char* status_param = NULL;

  snprintf(id_buf, sizeof(id_buf), "%" PRIu64 "-%u", fetched_at_ms, sequence);
  if (status_code > 0) {
    snprintf(status_buf, sizeof(status_buf), "%ld", status_code);
    status_param = status_buf;
  }
  snprintf(success_buf, sizeof(success_buf), "%d", success ? 1 : 0);
  snprintf(fetched_buf, sizeof(fetched_buf), "%" PRIu64, fetched_at_ms);

  const char* params[] = {
      id_buf,
      safe_user,
      safe_region,
      endpoint_key,
      method,
      url,
      status_param,
      success_buf,
      response_body,
      error,
      fetched_buf};

  return fpv_db_exec_params(
      db,
      "INSERT INTO fpv_valorant_api_fetches ("
      "id, user_id, region, endpoint_key, method, url, status_code, success, "
      "response_body, error, fetched_at_ms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);",
      params,
      11);
}

static char* build_request_body(
    const valorant_endpoint_t* endpoint,
    const valorant_request_context_t* context) {
  if (!endpoint || !endpoint->method) {
    return NULL;
  }
  if (strcmp(endpoint->method, "POST") != 0 &&
      strcmp(endpoint->method, "PUT") != 0) {
    return NULL;
  }
  if (endpoint->key &&
      strcmp(endpoint->key, "DisplayNameService_FetchPlayers_BySubjects") == 0 &&
      context && context->user_id && context->user_id[0]) {
    size_t len =
        (size_t)snprintf(NULL, 0, "[\"%s\"]", context->user_id);
    char* body = (char*)malloc(len + 1);
    if (!body) {
      return NULL;
    }
    snprintf(body, len + 1, "[\"%s\"]", context->user_id);
    return body;
  }
  return dup_string("{}");
}

static fpv_result_t valorant_fetch_and_store_all(
    fpv_db_t* db,
    CURL* curl,
    const char* access_token,
    const char* entitlements,
    const char* user_id,
    const char* region,
    const char* item_type_id,
    const char* data_path) {
  if (!db || !curl || !access_token || !access_token[0] || !user_id ||
      !user_id[0] || !region || !region[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  valorant_endpoint_list_t endpoints = load_valorant_endpoints(data_path);
  if (endpoints.count == 0) {
    fprintf(stderr, "No endpoints loaded from %s\n",
            data_path ? data_path : "(null)");
    return FPV_ERR_NOT_FOUND;
  }

  fpv_result_t result = fpv_db_begin(db);
  if (result != FPV_OK) {
    valorant_endpoint_list_clear(&endpoints);
    return result;
  }

  valorant_request_context_t context;
  context.user_id = user_id;
  context.region = region;
  context.item_type_id = item_type_id;

  bool skip_mutations = env_is_truthy("VALORANT_SKIP_MUTATIONS");
  bool verbose = env_is_truthy("VALORANT_VERBOSE");
  bool print_responses = env_is_truthy("VALORANT_PRINT_RESPONSES");
  size_t success_count = 0;
  size_t failure_count = 0;
  size_t skipped_count = 0;
  unsigned int sequence = 0;

  for (size_t i = 0; i < endpoints.count; i++) {
    valorant_endpoint_t* endpoint = &endpoints.items[i];
    char* resolved_url = NULL;
    char* missing_placeholder = NULL;
    char* error_message = NULL;
    http_response_t response = {0};
    long status_code = 0;
    bool success = false;
    bool attempted = false;

    if (!endpoint->url || !endpoint->url[0]) {
      error_message = dup_string("missing_url");
      skipped_count++;
    } else if (skip_mutations &&
               strcmp(endpoint->method, "GET") != 0) {
      error_message = dup_string("method_blocked");
      skipped_count++;
    } else {
      char* regional = apply_region_to_url(endpoint->url, region);
      if (regional) {
        resolved_url = resolve_placeholders(regional, &context, &missing_placeholder);
        free(regional);
      }
      if (!resolved_url) {
        if (missing_placeholder) {
          size_t len = strlen("missing_placeholder:") +
                       strlen(missing_placeholder) + 1;
          error_message = (char*)malloc(len);
          if (error_message) {
            snprintf(error_message, len, "missing_placeholder:%s",
                     missing_placeholder);
          }
        } else {
          error_message = dup_string("missing_placeholder");
        }
        skipped_count++;
      } else {
        attempted = true;
        bool include_json =
            strcmp(endpoint->method, "POST") == 0 ||
            strcmp(endpoint->method, "PUT") == 0;
        struct curl_slist* headers =
            build_valorant_api_headers(access_token, entitlements, include_json);
        if (!headers) {
          error_message = dup_string("headers_failed");
          failure_count++;
        } else {
          char* body = build_request_body(endpoint, &context);
          char* request_error = NULL;
          bool ok = http_request_with_error(
              curl,
              endpoint->method,
              resolved_url,
              headers,
              body,
              &response,
              &request_error);
          curl_slist_free_all(headers);
          free(body);

          if (!ok) {
            error_message =
                request_error ? request_error : dup_string("request_failed");
            request_error = NULL;
            failure_count++;
          } else {
            status_code = response.status;
            success = status_ok(&response);
            if (!success) {
              error_message = dup_string("http_status");
              failure_count++;
            } else {
              success_count++;
            }
          }
          free(request_error);
        }
      }
    }

    uint64_t fetched_at_ms = fpv_time_now_ms();
    sequence++;
    const char* stored_url = resolved_url ? resolved_url : endpoint->url;
    result = store_valorant_fetch(
        db,
        user_id,
        region,
        endpoint->key ? endpoint->key : "unknown",
        endpoint->method ? endpoint->method : "GET",
        stored_url,
        status_code,
        success,
        response.data,
        error_message,
        fetched_at_ms,
        sequence);

    if (result != FPV_OK) {
      fprintf(stderr, "Failed to store fetch result: %s\n", fpv_db_error(db));
      response_reset(&response);
      free(resolved_url);
      free(missing_placeholder);
      free(error_message);
      fpv_db_rollback(db);
      valorant_endpoint_list_clear(&endpoints);
      return result;
    }

    if (verbose) {
      fprintf(stderr, "[%s] %s %s -> %ld%s\n",
              endpoint->key ? endpoint->key : "unknown",
              endpoint->method ? endpoint->method : "GET",
              stored_url ? stored_url : "(skipped)",
              status_code,
              attempted ? "" : " (skipped)");
      if (print_responses && response.data) {
        fprintf(stderr, "%s\n", response.data);
      }
    }

    response_reset(&response);
    free(resolved_url);
    free(missing_placeholder);
    free(error_message);
  }

  result = fpv_db_commit(db);
  if (result != FPV_OK) {
    fpv_db_rollback(db);
    valorant_endpoint_list_clear(&endpoints);
    return result;
  }

  fprintf(stdout,
          "Valorant API fetches complete: %zu success, %zu failed, %zu skipped.\n",
          success_count,
          failure_count,
          skipped_count);

  valorant_endpoint_list_clear(&endpoints);
  return FPV_OK;
}

int main(void) {
  const char* username = getenv("VALORANT_USERNAME");
  const char* password = getenv("VALORANT_PASSWORD");
  const char* token_path = getenv("VALORANT_TOKEN_FILE");
  const char* data_path = getenv("VALORANT_DATA_PATH");
  const char* region_env = getenv("VALORANT_REGION");
  const char* item_type_id = getenv("VALORANT_ITEM_TYPE_ID");
  bool browser_first = env_is_truthy("VALORANT_BROWSER_LOGIN");
  bool browser_fallback = !env_is_truthy("VALORANT_DISABLE_BROWSER_LOGIN");
  bool local_login = !env_is_truthy("VALORANT_DISABLE_LOCAL_LOGIN");
  bool skip_fetch = env_is_truthy("VALORANT_SKIP_FETCH");
  char* username_input = NULL;
  char* password_input = NULL;
  char* region = NULL;
  char* entitlements = NULL;
  char* user_id = NULL;
  fpv_db_t* db = NULL;
  int exit_code = 0;

  if (!token_path || !token_path[0]) {
    token_path = DEFAULT_TOKEN_FILE;
  }
  if (!data_path || !data_path[0]) {
    data_path = VALORANT_DATA_PATH;
  }
  if (!item_type_id || !item_type_id[0]) {
    item_type_id = VALORANT_ITEM_TYPE_SKINS;
  }
  region = normalize_region(region_env);
  if (!region) {
    region = dup_string(VALORANT_REGION_DEFAULT);
  }
  if (!region) {
    fprintf(stderr, "Failed to resolve region.\n");
    return 1;
  }

  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    fprintf(stderr, "curl_global_init failed.\n");
    free(username_input);
    free(password_input);
    return 1;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "curl_easy_init failed.\n");
    curl_global_cleanup();
    free(username_input);
    free(password_input);
    return 1;
  }

  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, VALORANT_USER_AGENT);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);

  char* login_error = NULL;
  char* access_token = NULL;
  bool tried_browser = false;
  if (browser_first) {
    access_token = riot_login_browser(&login_error);
    tried_browser = true;
    if (!access_token && login_error) {
      fprintf(stderr, "Browser login failed. Trying credential login...\n");
      print_login_error_details(login_error);
      free(login_error);
      login_error = NULL;
    }
  }
  if (!access_token && local_login) {
    access_token = riot_login_local(curl, &login_error);
    if (!access_token && login_error) {
      if (env_is_truthy("VALORANT_DEBUG_AUTH")) {
        fprintf(stderr, "Local client login failed.\n");
        print_login_error_details(login_error);
      }
      free(login_error);
      login_error = NULL;
    }
  }
  if (!access_token) {
    if (!username || !username[0]) {
      username_input = read_line_stdin("Riot username: ");
      username = username_input;
    }
    if (!password || !password[0]) {
      password_input = read_line_stdin("Riot password: ");
      password = password_input;
    }
    if (username && username[0] && password && password[0]) {
      access_token = riot_login(curl, username, password, &login_error);
    }
  }
  if (!access_token && browser_fallback && !tried_browser) {
    if (login_error) {
      fprintf(stderr, "Credential login failed. Trying browser login...\n");
      print_login_error_details(login_error);
    }
    free(login_error);
    login_error = NULL;
    access_token = riot_login_browser(&login_error);
    tried_browser = true;
  }
  if (!access_token) {
    fprintf(stderr, "Login failed.\n");
    print_login_error_details(login_error);
    free(login_error);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(username_input);
    free(password_input);
    return 1;
  }
  free(login_error);

  char* normalized = normalize_access_token(access_token);
  free(access_token);
  access_token = normalized;
  if (!access_token) {
    fprintf(stderr, "Failed to parse access token.\n");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(username_input);
    free(password_input);
    return 1;
  }

  if (!write_text_file(token_path, access_token)) {
    fprintf(stderr, "Failed to write token to %s.\n", token_path);
    free(access_token);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    free(username_input);
    free(password_input);
    free(region);
    return 1;
  }

  printf("Access token saved to %s\n", token_path);

  if (!skip_fetch) {
    fpv_result_t db_result = FPV_OK;
    db = open_valorant_db(&db_result);
    if (!db) {
      fprintf(stderr,
              "Failed to open database (result=%d). "
              "Set FPV_DB_URL or VALORANT_DB_URL and ensure the backend is enabled.\n",
              db_result);
      exit_code = 1;
      goto cleanup;
    }

    char* entitlement_error = NULL;
    entitlements = fetch_entitlements_token(curl, access_token, &entitlement_error);
    if (!entitlements) {
      fprintf(stderr, "Failed to fetch entitlements: %s\n",
              entitlement_error ? entitlement_error : "unknown_error");
      free(entitlement_error);
      exit_code = 1;
      goto cleanup;
    }
    free(entitlement_error);

    char* user_error = NULL;
    user_id = fetch_user_id(curl, access_token, &user_error);
    if (!user_id) {
      fprintf(stderr, "Failed to fetch user id: %s\n",
              user_error ? user_error : "unknown_error");
      free(user_error);
      exit_code = 1;
      goto cleanup;
    }
    free(user_error);

    fpv_result_t fetch_result = valorant_fetch_and_store_all(
        db,
        curl,
        access_token,
        entitlements,
        user_id,
        region,
        item_type_id,
        data_path);
    if (fetch_result != FPV_OK) {
      fprintf(stderr, "Valorant fetch failed (result=%d).\n", fetch_result);
      exit_code = 1;
      goto cleanup;
    }
  }

cleanup:
  free(access_token);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  free(username_input);
  free(password_input);
  free(region);
  free(entitlements);
  free(user_id);
  if (db) {
    fpv_db_close(db);
  }
  return exit_code;
}
