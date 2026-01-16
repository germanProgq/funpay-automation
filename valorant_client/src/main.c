#include <ctype.h>
#include <curl/curl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define VALORANT_USERNAME "martinyxy10"
#define VALORANT_PASSWORD "k&-8qvyi"
#define VALORANT_REGION "eu"
#define VALORANT_CLIENT_ID "play-valorant-web-prod"
#define VALORANT_SCOPE "account openid"

#define VALORANT_DATA_PATH "valorant_data.txt"
#define VALORANT_CLIENT_VERSION "release-08.08-shipping-2-000000"
#define VALORANT_CLIENT_PLATFORM "eyJwbGF0Zm9ybVR5cGUiOiAiUEMiLCAicGxhdGZvcm1PUyI6ICJXaW5kb3dzIiwgInBsYXRmb3JtT1NWZXJzaW9uIjogIjEwLjAuMTkwNDMuMS4yNTYuNjRiaXQiLCAicGxhdGZvcm1DaGlwc2V0IjogIlVua25vd24ifQ=="
#define VALORANT_USER_AGENT \
  "RiotClient/57.0.0.4639025.4624825 rso-auth (Windows;10;19044)"
#define VALORANT_QR_CONFIG_AGENT \
  "RiotGamesApi/24.9.1.4445 client-config (Windows;10;;Professional, x64) riot_client/0"
#define VALORANT_QR_AUTH_AGENT \
  "RiotGamesApi/24.9.1.4445 rso-auth (Windows;10;;Professional, x64) riot_client/0"
#define VALORANT_QR_LOGIN_AGENT \
  "RiotGamesApi/24.9.1.4445 rso-authenticator (Windows;10;;Professional, x64) riot_client/0"
#define VALORANT_QR_TOKEN_AGENT \
  "RiotGamesApi/24.10.1.4471 rso-auth (Windows;10;;Professional, x64) riot_client/0"
#define VALORANT_QR_REDIRECT_URI "http://localhost/redirect"
#define VALORANT_QR_SCOPE "openid link ban lol_region account"
#define VALORANT_QR_DEFAULT_COUNTRY "en-US"

typedef struct http_response {
  char* data;
  size_t size;
  long status;
} http_response_t;

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

typedef struct http_location_capture {
  char* location;
} http_location_capture_t;

static bool header_starts_with_ignore_case(
    const char* buffer,
    size_t len,
    const char* prefix) {
  size_t prefix_len = strlen(prefix);
  if (len < prefix_len) {
    return false;
  }
  for (size_t i = 0; i < prefix_len; i++) {
    if (tolower((unsigned char)buffer[i]) != tolower((unsigned char)prefix[i])) {
      return false;
    }
  }
  return true;
}

static size_t http_location_header_cb(
    char* buffer,
    size_t size,
    size_t nitems,
    void* userdata) {
  size_t total = size * nitems;
  http_location_capture_t* capture = (http_location_capture_t*)userdata;
  if (!capture || total == 0) {
    return total;
  }

  const char* prefix = "Location:";
  if (!header_starts_with_ignore_case(buffer, total, prefix)) {
    return total;
  }

  const char* start = buffer + strlen(prefix);
  const char* end = buffer + total;
  while (start < end && isspace((unsigned char)*start)) {
    start++;
  }
  while (end > start && (end[-1] == '\r' || end[-1] == '\n')) {
    end--;
  }
  if (end <= start) {
    return total;
  }

  char* location = strndup_string(start, (size_t)(end - start));
  if (location) {
    free(capture->location);
    capture->location = location;
  }
  return total;
}

static bool http_request_with_location(
    CURL* curl,
    const char* method,
    const char* url,
    struct curl_slist* headers,
    const char* body,
    http_response_t* out,
    char** out_location) {
  if (out_location) {
    *out_location = NULL;
  }
  if (!curl || !method || !url || !out) {
    return false;
  }

  http_location_capture_t capture = {0};
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, http_location_header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

  bool ok = http_request(curl, method, url, headers, body, out);

  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, NULL);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);

  if (out_location) {
    *out_location = capture.location;
  } else {
    free(capture.location);
  }

  return ok;
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

static char* extract_json_value(const char* json, const char* key) {
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

  if (*cursor == '"') {
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

  const char* start = cursor;
  while (*cursor && *cursor != ',' && *cursor != '}' && *cursor != '\n' &&
         *cursor != '\r') {
    cursor++;
  }
  const char* end = cursor;
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  if (end == start) {
    return NULL;
  }
  return strndup_string(start, (size_t)(end - start));
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

static const char* get_country_code(void) {
  const char* value = getenv("VALORANT_COUNTRY_CODE");
  if (!value || !value[0]) {
    value = getenv("VALORANT_COUNTRY");
  }
  if (!value || !value[0]) {
    value = getenv("VALORANT_LANGUAGE");
  }
  if (!value || !value[0]) {
    value = VALORANT_QR_DEFAULT_COUNTRY;
  }
  return value;
}

typedef struct language_region_map {
  const char* language;
  const char* region;
} language_region_map_t;

static const language_region_map_t k_language_regions[] = {
    {"en-US", "NA"},
    {"ko-KR", "KR"},
    {"ja-JP", "JP"},
    {"zh-CN", "CN"},
    {"zh-TW", "TW"},
    {"es-ES", "EUW"},
    {"fr-FR", "EUW"},
    {"de-DE", "EUW"},
    {"ru-RU", "RU"},
    {"ar-SA", "TR"},
    {"th-TH", "TH"},
    {"vi-VN", "VN"},
    {"id-ID", "ID"},
    {"ms-MY", "MY"},
    {"pl-PL", "EUN"},
    {"tr-TR", "TR"},
    {"ro-RO", "EUN"},
    {"hu-HU", "EUN"},
    {"el-GR", "EUN"},
    {"cs-CZ", "EUN"},
    {"pt-BR", "BR"},
    {"it-IT", "EUW"},
};

static const size_t k_language_region_count =
    sizeof(k_language_regions) / sizeof(k_language_regions[0]);

static const char* region_for_language(const char* language) {
  const char* override = getenv("VALORANT_QR_REGION");
  if (override && override[0]) {
    return override;
  }
  if (!language || !language[0]) {
    return "NA";
  }
  for (size_t i = 0; i < k_language_region_count; i++) {
    if (strings_equal_ignore_case(language, k_language_regions[i].language)) {
      return k_language_regions[i].region;
    }
  }
  return "NA";
}

static char* normalize_language_code(const char* language) {
  if (!language) {
    return NULL;
  }
  char* out = dup_string(language);
  if (!out) {
    return NULL;
  }
  for (char* p = out; *p; p++) {
    if (*p == '-') {
      *p = '_';
    }
  }
  return out;
}

static void seed_prng_once(void) {
  static bool seeded = false;
  if (!seeded) {
    unsigned int seed = (unsigned int)time(NULL);
#ifdef _WIN32
    seed ^= (unsigned int)GetCurrentProcessId();
#else
    seed ^= (unsigned int)getpid();
#endif
    srand(seed);
    seeded = true;
  }
}

static bool random_bytes(unsigned char* out, size_t len) {
  if (!out || len == 0) {
    return false;
  }
#ifdef _WIN32
  seed_prng_once();
  for (size_t i = 0; i < len; i++) {
    out[i] = (unsigned char)(rand() & 0xff);
  }
  return true;
#else
  FILE* file = fopen("/dev/urandom", "rb");
  if (file) {
    size_t read_bytes = fread(out, 1, len, file);
    fclose(file);
    if (read_bytes == len) {
      return true;
    }
  }
  seed_prng_once();
  for (size_t i = 0; i < len; i++) {
    out[i] = (unsigned char)(rand() & 0xff);
  }
  return true;
#endif
}

static bool random_hex_string(char* out, size_t hex_len) {
  if (!out || hex_len == 0 || (hex_len % 2) != 0) {
    return false;
  }
  size_t byte_len = hex_len / 2;
  unsigned char* bytes = (unsigned char*)malloc(byte_len);
  if (!bytes) {
    return false;
  }
  if (!random_bytes(bytes, byte_len)) {
    free(bytes);
    return false;
  }
  static const char* hex = "0123456789abcdef";
  for (size_t i = 0; i < byte_len; i++) {
    out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
    out[i * 2 + 1] = hex[bytes[i] & 0x0f];
  }
  out[hex_len] = '\0';
  free(bytes);
  return true;
}

static bool uuid_v4_string(char out[37]) {
  if (!out) {
    return false;
  }
  unsigned char bytes[16];
  if (!random_bytes(bytes, sizeof(bytes))) {
    return false;
  }
  bytes[6] = (unsigned char)((bytes[6] & 0x0f) | 0x40);
  bytes[8] = (unsigned char)((bytes[8] & 0x3f) | 0x80);
  snprintf(out, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
           bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
           bytes[12], bytes[13], bytes[14], bytes[15]);
  return true;
}

static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  usleep(ms * 1000);
#endif
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

static char* build_login_body(const char* username, const char* password) {
  char* escaped_user = json_escape(username);
  char* escaped_pass = json_escape(password);
  if (!escaped_user || !escaped_pass) {
    free(escaped_user);
    free(escaped_pass);
    return NULL;
  }

  const char* tmpl = "{\"type\":\"auth\",\"username\":\"%s\",\"password\":\"%s\"}";

  size_t len = (size_t)snprintf(NULL, 0, tmpl, escaped_user, escaped_pass);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    free(escaped_user);
    free(escaped_pass);
    return NULL;
  }
  snprintf(out, len + 1, tmpl, escaped_user, escaped_pass);
  free(escaped_user);
  free(escaped_pass);
  return out;
}

static char* build_auth_body(
    const char* client_id,
    const char* redirect_uri,
    const char* scope) {
  if (!client_id || !redirect_uri) {
    return NULL;
  }

  const char* tmpl_with_scope =
      "{\"client_id\":\"%s\",\"nonce\":\"1\",\"redirect_uri\":\"%s\","
      "\"response_type\":\"token id_token\",\"scope\":\"%s\"}";
  const char* tmpl_no_scope =
      "{\"client_id\":\"%s\",\"nonce\":\"1\",\"redirect_uri\":\"%s\","
      "\"response_type\":\"token id_token\"}";

  bool use_scope = scope && scope[0];
  const char* tmpl = use_scope ? tmpl_with_scope : tmpl_no_scope;
  size_t len = use_scope
                   ? (size_t)snprintf(NULL, 0, tmpl, client_id, redirect_uri, scope)
                   : (size_t)snprintf(NULL, 0, tmpl, client_id, redirect_uri);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  if (use_scope) {
    snprintf(out, len + 1, tmpl, client_id, redirect_uri, scope);
  } else {
    snprintf(out, len + 1, tmpl, client_id, redirect_uri);
  }
  return out;
}

static char* read_account_xp_url(const char* path) {
  FILE* file = fopen(path, "r");
  if (!file) {
    return NULL;
  }

  char line[4096];
  while (fgets(line, sizeof(line), file)) {
    if (!strstr(line, "AccountXP_GetPlayer") &&
        !strstr(line, "account-xp/v1/players")) {
      continue;
    }

    char* url = strstr(line, "http");
    if (!url) {
      continue;
    }

    size_t len = strlen(url);
    while (len > 0 && isspace((unsigned char)url[len - 1])) {
      len--;
    }

    fclose(file);
    return strndup_string(url, len);
  }

  fclose(file);
  return NULL;
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

static char* apply_region_to_url(const char* url, const char* region) {
  if (!url) {
    return NULL;
  }
  if (!region || !region[0]) {
    return dup_string(url);
  }

  const char* ap_domain = "pd.ap.a.pvp.net";
  const char* ap_match = strstr(url, ap_domain);
  if (!ap_match) {
    return dup_string(url);
  }

  char region_domain[64];
  snprintf(region_domain, sizeof(region_domain), "pd.%s.a.pvp.net", region);
  return replace_placeholder(url, ap_domain, region_domain);
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

static bool status_ok(const http_response_t* response) {
  if (!response) {
    return false;
  }
  return response->status >= 200 && response->status < 300;
}

static bool status_unauthorized(const http_response_t* response) {
  if (!response) {
    return false;
  }
  return response->status == 401 || response->status == 403;
}

static bool status_redirect(const http_response_t* response) {
  if (!response) {
    return false;
  }
  return response->status >= 300 && response->status < 400;
}

typedef struct riot_auth_attempt {
  const char* label;
  const char* client_id;
  const char* redirect_uri;
  const char* scope;
} riot_auth_attempt_t;

static const riot_auth_attempt_t k_auth_attempts[] = {
    {"valorant-web-basic", VALORANT_CLIENT_ID, "https://playvalorant.com/opt_in", NULL},
    {"valorant-web-scope", VALORANT_CLIENT_ID, "https://playvalorant.com/opt_in",
     VALORANT_SCOPE},
    {"valorant-web-offline", VALORANT_CLIENT_ID, "https://playvalorant.com/opt_in",
     "openid account offline_access"},
    {"valorant-web-offline-alt", VALORANT_CLIENT_ID, "https://playvalorant.com/opt_in",
     "account openid offline_access"},
    {"riot-client-basic", "riot-client", "http://localhost/redirect", NULL},
    {"riot-client-scope", "riot-client", "http://localhost/redirect",
     VALORANT_SCOPE},
    {"riot-client-offline", "riot-client", "http://localhost/redirect",
     "openid account offline_access"},
};

static const size_t k_auth_attempt_count =
    sizeof(k_auth_attempts) / sizeof(k_auth_attempts[0]);

typedef struct riot_auth_host {
  const char* label;
  const char* url;
} riot_auth_host_t;

static const riot_auth_host_t k_auth_hosts[] = {
    {"auth", "https://auth.riotgames.com/api/v1/authorization"},
    {"authenticate", "https://authenticate.riotgames.com/api/v1/authorization"},
};

static const size_t k_auth_host_count =
    sizeof(k_auth_hosts) / sizeof(k_auth_hosts[0]);

static bool response_is_rate_limited(const char* body) {
  if (!body) {
    return false;
  }
  return strstr(body, "Cloudflare") || strstr(body, "Error 1015") ||
         strstr(body, "rate limited") || strstr(body, "cf-error-details");
}

static bool login_error_is_auth_failure(const char* body) {
  if (!body) {
    return false;
  }
  char* code = extract_json_string(body, "error");
  bool is_auth = false;
  if (code) {
    is_auth = strcmp(code, "auth_failure") == 0;
  } else if (strstr(body, "auth_failure")) {
    is_auth = true;
  }
  free(code);
  return is_auth;
}

static char* riot_login(
    CURL* curl,
    const char* auth_url,
    const char* client_id,
    const char* redirect_uri,
    const char* scope,
    const char* username,
    const char* password,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !auth_url || !client_id || !redirect_uri || !username || !password) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return NULL;
  }

  char* auth_body = build_auth_body(client_id, redirect_uri, scope);
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
  const char* origin = "https://auth.riotgames.com";
  if (strstr(auth_url, "authenticate.riotgames.com")) {
    origin = "https://authenticate.riotgames.com";
  }

  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.9");
  origin_header = format_header("Origin", origin);
  referer_header = format_header("Referer", origin);
  if (origin_header) {
    headers = curl_slist_append(headers, origin_header);
  }
  if (referer_header) {
    headers = curl_slist_append(headers, referer_header);
  }

  if (!http_request(curl, "POST", auth_url, headers, auth_body, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      if (response_is_rate_limited(response.data)) {
        *out_error = dup_string("rate_limited");
      } else {
        *out_error =
            response.data ? dup_string(response.data) : dup_string("auth_init_failed");
      }
    }
    goto cleanup;
  }
  response_reset(&response);

  login_body = build_login_body(username, password);
  if (!login_body) {
    if (out_error) {
      *out_error = dup_string("login_body_failed");
    }
    goto cleanup;
  }

  if (!http_request(curl, "PUT", auth_url, headers, login_body, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      if (response_is_rate_limited(response.data)) {
        *out_error = dup_string("rate_limited");
      } else {
        *out_error =
            response.data ? dup_string(response.data) : dup_string("login_failed");
      }
    }
    goto cleanup;
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

    char* code = read_line_stdin("Enter Riot MFA code: ");
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
    if (!http_request(curl, "PUT", auth_url, headers, mfa_body, &response) ||
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
      if (response_is_rate_limited(response.data)) {
        *out_error = dup_string("rate_limited");
      } else {
        *out_error =
            response.data ? dup_string(response.data) : dup_string("auth_failed");
      }
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
  if (!access_token) {
    if (out_error) {
      *out_error = dup_string(uri ? "access_token_missing" : "uri_missing");
    }
    goto cleanup;
  }

cleanup:
  free(auth_body);
  free(login_body);
  free(auth_type);
  free(uri);
  free(origin_header);
  free(referer_header);
  response_reset(&response);
  curl_slist_free_all(headers);
  return access_token;
}

static char* valorant_login_with_creds(
    CURL* curl,
    const char* username,
    const char* password,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl || !username || !password) {
    if (out_error) {
      *out_error = dup_string("invalid_arguments");
    }
    return NULL;
  }

  char* access_token = NULL;
  char* login_error = NULL;
  char* auth_failure_error = NULL;

  for (size_t host_idx = 0;
       host_idx < k_auth_host_count && !access_token;
       host_idx++) {
    const char* auth_url = k_auth_hosts[host_idx].url;
    bool saw_auth_failure = false;

    for (size_t i = 0; i < k_auth_attempt_count && !access_token; i++) {
      free(login_error);
      login_error = NULL;
      access_token = riot_login(
          curl,
          auth_url,
          k_auth_attempts[i].client_id,
          k_auth_attempts[i].redirect_uri,
          k_auth_attempts[i].scope,
          username,
          password,
          &login_error);
      if (!access_token && login_error) {
        if (strcmp(login_error, "rate_limited") == 0) {
          fprintf(stderr,
                  "Login attempt (%s/%s) failed: rate limited by Cloudflare.\n",
                  k_auth_hosts[host_idx].label,
                  k_auth_attempts[i].label);
          break;
        }

        char* error_code = extract_json_string(login_error, "error");
        char* error_desc = extract_json_string(login_error, "error_description");
        char* country = extract_json_string(login_error, "country");
        char* captcha = extract_json_string(login_error, "captcha");
        bool auth_failure = false;
        if (error_code) {
          auth_failure = strcmp(error_code, "auth_failure") == 0;
        } else {
          auth_failure = login_error_is_auth_failure(login_error);
        }
        if (auth_failure) {
          saw_auth_failure = true;
          if (!auth_failure_error) {
            auth_failure_error = dup_string(login_error);
          }
        }
        if (error_code || error_desc || country || captcha) {
          fprintf(stderr, "Login attempt (%s/%s) failed:",
                  k_auth_hosts[host_idx].label,
                  k_auth_attempts[i].label);
          if (error_code) {
            fprintf(stderr, " error=%s", error_code);
          }
          if (error_desc) {
            fprintf(stderr, " desc=%s", error_desc);
          }
          if (country) {
            fprintf(stderr, " country=%s", country);
          }
          if (captcha) {
            fprintf(stderr, " captcha=%s", captcha);
          }
          fprintf(stderr, "\n");
        } else {
          fprintf(stderr, "Login attempt (%s/%s) failed: %s\n",
                  k_auth_hosts[host_idx].label,
                  k_auth_attempts[i].label,
                  login_error);
        }

        const char* debug_auth = getenv("VALORANT_DEBUG_AUTH");
        if (debug_auth && debug_auth[0]) {
          fprintf(stderr, "Auth response: %s\n", login_error);
        }
        free(error_code);
        free(error_desc);
        free(country);
        free(captcha);
      }
    }
    if (saw_auth_failure) {
      break;
    }
  }

  if (!access_token) {
    if (out_error) {
      if (auth_failure_error) {
        *out_error = auth_failure_error;
        auth_failure_error = NULL;
        free(login_error);
        login_error = NULL;
      } else {
        *out_error = login_error;
        login_error = NULL;
      }
    } else {
      free(login_error);
    }
    free(auth_failure_error);
    return NULL;
  }

  free(login_error);
  free(auth_failure_error);
  return access_token;
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

static bool has_browser_env(void) {
  const char* env_token = getenv("VALORANT_ACCESS_TOKEN");
  if (env_token && env_token[0]) {
    return true;
  }
  const char* env_url = getenv("VALORANT_REDIRECT_URL");
  if (env_url && env_url[0]) {
    return true;
  }
  const char* env_file = getenv("VALORANT_REDIRECT_FILE");
  if (env_file && env_file[0]) {
    return true;
  }
  return false;
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

  const char* redirect_uri = "https://playvalorant.com/opt_in";
  char* redirect_encoded = url_encode(redirect_uri);
  char* scope_encoded = url_encode("openid account");
  if (!redirect_encoded || !scope_encoded) {
    if (out_error) {
      *out_error = dup_string("browser_url_encode_failed");
    }
    free(redirect_encoded);
    free(scope_encoded);
    return NULL;
  }

  char login_url[512];
  snprintf(
      login_url,
      sizeof(login_url),
      "https://authenticate.riotgames.com/?client_id=%s&redirect_uri=%s&"
      "response_type=token%%20id_token&scope=%s&nonce=1&method=riot_identity&"
      "platform=web",
      VALORANT_CLIENT_ID,
      redirect_encoded,
      scope_encoded);
  free(redirect_encoded);
  free(scope_encoded);

  printf("Open this URL in your browser to login:\n%s\n", login_url);
  printf("After login, copy the FULL redirect URL (should start with %s#access_token=...).\n",
         redirect_uri);
  fflush(stdout);

  const int max_attempts = 3;
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    char* pasted = read_line_stdin("Paste redirect URL: ");
    if (!pasted || !pasted[0]) {
      free(pasted);
      if (out_error) {
        *out_error = dup_string("browser_no_url");
      }
      return NULL;
    }

    fprintf(stderr, "Parsing redirect URL...\n");
    char* access_token = normalize_access_token(pasted);
    if (access_token) {
      free(pasted);
      return access_token;
    }

    if (strstr(pasted, "authenticate.riotgames.com") ||
        strstr(pasted, "auth.riotgames.com/authorize") ||
        strstr(pasted, "redirect_uri=")) {
      fprintf(stderr,
              "You pasted the login URL. Please paste the final redirect URL "
              "after login (playvalorant.com/...#access_token=...).\n");
    } else {
      fprintf(stderr,
              "No access_token found in pasted URL. Please try again.\n");
    }
    free(pasted);
  }

  if (out_error) {
    *out_error = dup_string("browser_token_missing");
  }
  return NULL;
}

static struct curl_slist* build_qr_headers(
    const char* user_agent,
    const char* baggage_header,
    const char* trace_header,
    const char* country_header,
    bool json_body) {
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");
  headers = curl_slist_append(headers, "Connection: keep-alive");
  if (json_body) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
  }
  if (user_agent && user_agent[0]) {
    char* ua_header = format_header("User-Agent", user_agent);
    if (ua_header) {
      headers = curl_slist_append(headers, ua_header);
      free(ua_header);
    }
  }
  if (baggage_header) {
    headers = curl_slist_append(headers, baggage_header);
  }
  if (trace_header) {
    headers = curl_slist_append(headers, trace_header);
  }
  if (country_header) {
    headers = curl_slist_append(headers, country_header);
  }
  return headers;
}

static char* build_clientconfig_url(const char* region) {
  if (!region || !region[0]) {
    region = "NA";
  }
  const char* tmpl =
      "https://clientconfig.rpg.riotgames.com/api/v1/config/public?"
      "os=windows&region=%s&app=Riot%%20Client&version=97.0.1.2366&"
      "patchline=KeystoneFoundationLiveWin";
  size_t len = (size_t)snprintf(NULL, 0, tmpl, region);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  snprintf(out, len + 1, tmpl, region);
  return out;
}

static char* build_qr_login_body(const char* language) {
  char* escaped_lang = json_escape(language);
  if (!escaped_lang) {
    return NULL;
  }
  const char* tmpl =
      "{\"client_id\":\"riot-client\",\"language\":\"%s\","
      "\"platform\":\"windows\",\"remember\":false,\"type\":\"auth\","
      "\"qrcode\":{}}";
  size_t len = (size_t)snprintf(NULL, 0, tmpl, escaped_lang);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    free(escaped_lang);
    return NULL;
  }
  snprintf(out, len + 1, tmpl, escaped_lang);
  free(escaped_lang);
  return out;
}

static char* build_login_token_body(const char* login_token) {
  char* escaped = json_escape(login_token);
  if (!escaped) {
    return NULL;
  }
  const char* tmpl =
      "{\"authentication_type\":null,\"code_verifier\":\"\","
      "\"login_token\":\"%s\",\"persist_login\":false}";
  size_t len = (size_t)snprintf(NULL, 0, tmpl, escaped);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    free(escaped);
    return NULL;
  }
  snprintf(out, len + 1, tmpl, escaped);
  free(escaped);
  return out;
}

static char* build_qr_authorization_body(const char* nonce) {
  char* escaped_nonce = json_escape(nonce);
  if (!escaped_nonce) {
    return NULL;
  }
  const char* tmpl =
      "{\"acr_values\":\"\",\"claims\":\"\",\"client_id\":\"riot-client\","
      "\"code_challenge\":\"\",\"code_challenge_method\":\"\",\"nonce\":\"%s\","
      "\"redirect_uri\":\"%s\",\"response_type\":\"token id_token\","
      "\"scope\":\"%s\"}";
  size_t len = (size_t)snprintf(NULL, 0, tmpl, escaped_nonce,
                                VALORANT_QR_REDIRECT_URI, VALORANT_QR_SCOPE);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    free(escaped_nonce);
    return NULL;
  }
  snprintf(out, len + 1, tmpl, escaped_nonce, VALORANT_QR_REDIRECT_URI,
           VALORANT_QR_SCOPE);
  free(escaped_nonce);
  return out;
}

static char* riot_login_qr(
    CURL* curl,
    const char* country_code,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl) {
    if (out_error) {
      *out_error = dup_string("invalid_curl");
    }
    return NULL;
  }

  const char* country = country_code && country_code[0]
                            ? country_code
                            : VALORANT_QR_DEFAULT_COUNTRY;
  const char* region = region_for_language(country);
  char* language = normalize_language_code(country);
  if (!language) {
    if (out_error) {
      *out_error = dup_string("qr_language_failed");
    }
    return NULL;
  }

  char sdk_sid[37];
  if (!uuid_v4_string(sdk_sid)) {
    if (out_error) {
      *out_error = dup_string("qr_sdk_sid_failed");
    }
    free(language);
    return NULL;
  }

  char trace_id[33];
  char parent_id[17];
  char traceparent_value[64];
  if (!random_hex_string(trace_id, sizeof(trace_id) - 1) ||
      !random_hex_string(parent_id, sizeof(parent_id) - 1)) {
    if (out_error) {
      *out_error = dup_string("qr_trace_failed");
    }
    free(language);
    return NULL;
  }
  snprintf(traceparent_value, sizeof(traceparent_value), "00-%s-%s-00",
           trace_id, parent_id);

  char baggage_value[64];
  snprintf(baggage_value, sizeof(baggage_value), "sdksid=%s", sdk_sid);

  char* baggage_header = format_header("baggage", baggage_value);
  char* trace_header = format_header("traceparent", traceparent_value);
  char* country_header = format_header("country-code", country);

  http_response_t response = {0};
  struct curl_slist* headers = NULL;
  char* config_url = NULL;
  char* login_body = NULL;
  char* cluster = NULL;
  char* suuid = NULL;
  char* timestamp = NULL;
  char* login_token = NULL;
  char* login_url = NULL;
  char* token_body = NULL;
  char* auth_body = NULL;
  char* uri = NULL;
  char* access_token = NULL;

  config_url = build_clientconfig_url(region);
  if (!config_url) {
    if (out_error) {
      *out_error = dup_string("qr_config_url_failed");
    }
    goto cleanup;
  }
  headers = build_qr_headers(VALORANT_QR_CONFIG_AGENT, baggage_header,
                             trace_header, country_header, false);
  if (!http_request(curl, "GET", config_url, headers, NULL, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_config_failed");
    }
    goto cleanup;
  }
  curl_slist_free_all(headers);
  headers = NULL;
  response_reset(&response);

  headers = build_qr_headers(VALORANT_QR_AUTH_AGENT, baggage_header,
                             trace_header, country_header, false);
  if (!http_request(curl, "GET",
                    "https://auth.riotgames.com/.well-known/openid-configuration",
                    headers, NULL, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_openid_failed");
    }
    goto cleanup;
  }
  curl_slist_free_all(headers);
  headers = NULL;
  response_reset(&response);

  login_body = build_qr_login_body(language);
  if (!login_body) {
    if (out_error) {
      *out_error = dup_string("qr_login_body_failed");
    }
    goto cleanup;
  }
  headers = build_qr_headers(VALORANT_QR_LOGIN_AGENT, baggage_header,
                             trace_header, country_header, true);
  if (!http_request(curl, "POST",
                    "https://authenticate.riotgames.com/api/v1/login",
                    headers, login_body, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_login_failed");
    }
    goto cleanup;
  }

  cluster = extract_json_string(response.data, "cluster");
  suuid = extract_json_string(response.data, "suuid");
  timestamp = extract_json_value(response.data, "timestamp");
  if (!cluster || !suuid || !timestamp) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_login_data_missing");
    }
    goto cleanup;
  }

  size_t login_len = strlen(cluster) + strlen(suuid) + strlen(timestamp) + 128;
  login_url = (char*)malloc(login_len);
  if (!login_url) {
    if (out_error) {
      *out_error = dup_string("qr_login_url_failed");
    }
    goto cleanup;
  }
  snprintf(login_url, login_len,
           "https://qrlogin.riotgames.com/riotmobile?cluster=%s&suuid=%s&"
           "timestamp=%s&utm_source=riotclient&utm_medium=client&"
           "utm_campaign=qrlogin-riotmobile",
           cluster, suuid, timestamp);

  printf("Open or scan this Riot Mobile login URL:\n%s\n", login_url);
  fflush(stdout);

  int timeout_sec = env_int("VALORANT_QR_TIMEOUT", 120);
  int poll_ms = env_int("VALORANT_QR_POLL_MS", 2000);
  if (poll_ms < 500) {
    poll_ms = 500;
  }
  time_t start = time(NULL);
  while (true) {
    if (timeout_sec > 0 &&
        difftime(time(NULL), start) > (double)timeout_sec) {
      if (out_error) {
        *out_error = dup_string("qr_login_timeout");
      }
      goto cleanup;
    }

    response_reset(&response);
    curl_slist_free_all(headers);
    headers = build_qr_headers(VALORANT_QR_LOGIN_AGENT, baggage_header,
                               trace_header, country_header, false);
    if (!http_request(curl, "GET",
                      "https://authenticate.riotgames.com/api/v1/login",
                      headers, NULL, &response) ||
        !status_ok(&response)) {
      if (out_error) {
        *out_error = response.data ? dup_string(response.data)
                                   : dup_string("qr_login_poll_failed");
      }
      goto cleanup;
    }

    char* type = extract_json_string(response.data, "type");
    if (type && strcmp(type, "success") == 0) {
      login_token = extract_json_string(response.data, "login_token");
      free(type);
      if (!login_token) {
        if (out_error) {
          *out_error = response.data ? dup_string(response.data)
                                     : dup_string("qr_login_token_missing");
        }
        goto cleanup;
      }
      break;
    }
    if (type && strcmp(type, "error") == 0) {
      if (out_error) {
        *out_error = response.data ? dup_string(response.data)
                                   : dup_string("qr_login_error");
      }
      free(type);
      goto cleanup;
    }
    free(type);
    sleep_ms((unsigned int)poll_ms);
  }

  response_reset(&response);
  curl_slist_free_all(headers);
  headers = NULL;
  token_body = build_login_token_body(login_token);
  if (!token_body) {
    if (out_error) {
      *out_error = dup_string("qr_token_body_failed");
    }
    goto cleanup;
  }
  headers = build_qr_headers(VALORANT_QR_TOKEN_AGENT, baggage_header,
                             trace_header, country_header, true);
  if (!http_request(curl, "POST",
                    "https://auth.riotgames.com/api/v1/login-token",
                    headers, token_body, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_token_exchange_failed");
    }
    goto cleanup;
  }

  char nonce[37];
  if (!uuid_v4_string(nonce)) {
    if (out_error) {
      *out_error = dup_string("qr_nonce_failed");
    }
    goto cleanup;
  }
  auth_body = build_qr_authorization_body(nonce);
  if (!auth_body) {
    if (out_error) {
      *out_error = dup_string("qr_auth_body_failed");
    }
    goto cleanup;
  }

  response_reset(&response);
  curl_slist_free_all(headers);
  headers = build_qr_headers(VALORANT_QR_TOKEN_AGENT, baggage_header,
                             trace_header, country_header, true);
  if (!http_request(curl, "POST",
                    "https://auth.riotgames.com/api/v1/authorization",
                    headers, auth_body, &response) ||
      !status_ok(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("qr_authorization_failed");
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
    *out_error = response.data ? dup_string(response.data)
                               : dup_string("qr_access_token_missing");
  }

cleanup:
  curl_slist_free_all(headers);
  response_reset(&response);
  free(config_url);
  free(login_body);
  free(cluster);
  free(suuid);
  free(timestamp);
  free(login_token);
  free(login_url);
  free(token_body);
  free(auth_body);
  free(uri);
  free(baggage_header);
  free(trace_header);
  free(country_header);
  free(language);
  return access_token;
}

static char* valorant_login(
    CURL* curl,
    const char* username,
    const char* password,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }

  char* access_token = NULL;
  char* last_error = NULL;
  bool have_browser_env = has_browser_env();
  bool use_qr = env_is_truthy("VALORANT_QR_LOGIN");
  bool have_creds =
      username && password && username[0] && password[0] &&
      strcmp(username, "YOUR_USERNAME") != 0 &&
      strcmp(password, "YOUR_PASSWORD") != 0;

  if (have_browser_env) {
    access_token = riot_login_browser(&last_error);
    if (access_token) {
      return access_token;
    }
  }

  if (use_qr) {
    const char* country_code = get_country_code();
    char* qr_error = NULL;
    access_token = riot_login_qr(curl, country_code, &qr_error);
    if (access_token) {
      free(last_error);
      return access_token;
    }
    if (qr_error) {
      free(last_error);
      last_error = qr_error;
    }
  }

  if (have_creds) {
    char* creds_error = NULL;
    access_token = valorant_login_with_creds(curl, username, password, &creds_error);
    if (access_token) {
      free(last_error);
      return access_token;
    }
    if (creds_error) {
      free(last_error);
      last_error = creds_error;
    }
  }

  char* local_error = NULL;
  access_token = riot_login_local(curl, &local_error);
  if (access_token) {
    free(last_error);
    return access_token;
  }
  if (local_error) {
    free(last_error);
    last_error = local_error;
  }

  if (!have_browser_env) {
    char* browser_error = NULL;
    access_token = riot_login_browser(&browser_error);
    if (access_token) {
      free(last_error);
      return access_token;
    }
    if (browser_error) {
      free(last_error);
      last_error = browser_error;
    }
  }

  if (out_error) {
    *out_error = last_error;
  } else {
    free(last_error);
  }
  return NULL;
}

static bool valorant_fetch_auth_data(
    CURL* curl,
    const char* access_token,
    char** out_entitlements,
    char** out_user_id,
    bool* out_unauthorized) {
  if (out_entitlements) {
    *out_entitlements = NULL;
  }
  if (out_user_id) {
    *out_user_id = NULL;
  }
  if (out_unauthorized) {
    *out_unauthorized = false;
  }
  if (!curl || !access_token || !out_entitlements || !out_user_id) {
    return false;
  }

  char* bearer = (char*)malloc(strlen(access_token) + 8);
  if (!bearer) {
    fprintf(stderr, "Out of memory.\n");
    return false;
  }
  snprintf(bearer, strlen(access_token) + 8, "Bearer %s", access_token);

  http_response_t response = {0};
  struct curl_slist* headers = NULL;

  char* auth_header = format_header("Authorization", bearer);
  if (!auth_header) {
    fprintf(stderr, "Out of memory.\n");
    free(bearer);
    return false;
  }
  headers = curl_slist_append(headers, auth_header);
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  free(auth_header);

  fprintf(stderr, "Requesting entitlements token...\n");
  const char* entitlements_url =
      "https://entitlements.auth.riotgames.com/api/token/v1";
  if (!http_request(curl, "POST", entitlements_url, headers, "{}", &response) ||
      !status_ok(&response)) {
    if (status_unauthorized(&response) && out_unauthorized) {
      *out_unauthorized = true;
    }
    fprintf(stderr, "Entitlements request failed (status %ld).\n", response.status);
    if (response.data) {
      fprintf(stderr, "%s\n", response.data);
    }
    curl_slist_free_all(headers);
    response_reset(&response);
    free(bearer);
    return false;
  }

  char* entitlements = extract_json_string(response.data, "entitlements_token");
  curl_slist_free_all(headers);
  response_reset(&response);
  if (!entitlements) {
    fprintf(stderr, "Failed to extract entitlements token.\n");
    free(bearer);
    return false;
  }

  const char* userinfo_url = "https://auth.riotgames.com/userinfo";
  headers = NULL;
  auth_header = format_header("Authorization", bearer);
  if (!auth_header) {
    fprintf(stderr, "Out of memory.\n");
    free(entitlements);
    free(bearer);
    return false;
  }
  headers = curl_slist_append(headers, auth_header);
  headers = curl_slist_append(headers, "Accept: application/json");
  free(auth_header);

  fprintf(stderr, "Entitlements OK. Fetching user info...\n");
  if (!http_request(curl, "GET", userinfo_url, headers, NULL, &response) ||
      !status_ok(&response)) {
    if (status_unauthorized(&response) && out_unauthorized) {
      *out_unauthorized = true;
    }
    fprintf(stderr, "Userinfo request failed (status %ld).\n", response.status);
    if (response.data) {
      fprintf(stderr, "%s\n", response.data);
    }
    curl_slist_free_all(headers);
    response_reset(&response);
    free(entitlements);
    free(bearer);
    return false;
  }

  char* user_id = extract_json_string(response.data, "sub");
  curl_slist_free_all(headers);
  response_reset(&response);
  if (!user_id) {
    fprintf(stderr, "Failed to extract user_id.\n");
    free(entitlements);
    free(bearer);
    return false;
  }

  free(bearer);
  *out_entitlements = entitlements;
  *out_user_id = user_id;
  return true;
}

static char* valorant_refresh_access_token(CURL* curl, char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (!curl) {
    if (out_error) {
      *out_error = dup_string("invalid_curl");
    }
    return NULL;
  }

  char* redirect_encoded = url_encode("https://playvalorant.com/opt_in");
  char* scope_encoded = url_encode("account openid");
  char* response_encoded = url_encode("token id_token");
  if (!redirect_encoded || !scope_encoded || !response_encoded) {
    if (out_error) {
      *out_error = dup_string("refresh_url_encode_failed");
    }
    free(redirect_encoded);
    free(scope_encoded);
    free(response_encoded);
    return NULL;
  }

  const char* tmpl =
      "https://auth.riotgames.com/authorize?redirect_uri=%s&client_id=%s&"
      "response_type=%s&nonce=1&scope=%s";
  size_t url_len =
      (size_t)snprintf(NULL, 0, tmpl, redirect_encoded, VALORANT_CLIENT_ID,
                       response_encoded, scope_encoded);
  char* url = (char*)malloc(url_len + 1);
  if (!url) {
    if (out_error) {
      *out_error = dup_string("refresh_url_build_failed");
    }
    free(redirect_encoded);
    free(scope_encoded);
    free(response_encoded);
    return NULL;
  }
  snprintf(url, url_len + 1, tmpl, redirect_encoded, VALORANT_CLIENT_ID,
           response_encoded, scope_encoded);
  free(redirect_encoded);
  free(scope_encoded);
  free(response_encoded);

  http_response_t response = {0};
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Accept: application/json");

  char* location = NULL;
  bool ok = http_request_with_location(curl, "GET", url, headers, NULL, &response,
                                       &location);
  curl_slist_free_all(headers);
  free(url);

  if (!ok) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("refresh_request_failed");
    }
    response_reset(&response);
    free(location);
    return NULL;
  }

  if (!status_ok(&response) && !status_redirect(&response)) {
    if (out_error) {
      *out_error = response.data ? dup_string(response.data)
                                 : dup_string("refresh_request_failed");
    }
    response_reset(&response);
    free(location);
    return NULL;
  }

  char* token = NULL;
  if (location) {
    token = extract_query_value(location, "access_token");
    if (!token) {
      token = extract_access_token_from_text(location);
    }
  }
  if (!token && response.data) {
    token = extract_access_token_from_text(response.data);
  }
  if (!token && out_error) {
    *out_error = response.data ? dup_string(response.data)
                               : dup_string("refresh_token_missing");
  }

  response_reset(&response);
  free(location);
  return token;
}

static bool valorant_refresh_session(
    CURL* curl,
    char** access_token,
    char** entitlements,
    char** user_id,
    bool* out_unauthorized,
    char** out_error) {
  if (out_error) {
    *out_error = NULL;
  }
  if (out_unauthorized) {
    *out_unauthorized = false;
  }
  if (!curl || !access_token || !*access_token || !entitlements || !user_id) {
    if (out_error) {
      *out_error = dup_string("refresh_invalid_arguments");
    }
    return false;
  }

  char* refresh_error = NULL;
  char* new_token = valorant_refresh_access_token(curl, &refresh_error);
  if (!new_token) {
    if (out_error) {
      *out_error = refresh_error ? refresh_error : dup_string("refresh_failed");
    } else {
      free(refresh_error);
    }
    return false;
  }
  free(refresh_error);

  char* new_entitlements = NULL;
  char* new_user_id = NULL;
  bool unauthorized = false;
  if (!valorant_fetch_auth_data(curl, new_token, &new_entitlements, &new_user_id,
                                &unauthorized)) {
    if (out_unauthorized) {
      *out_unauthorized = unauthorized;
    }
    free(new_token);
    free(new_entitlements);
    free(new_user_id);
    if (out_error) {
      *out_error = dup_string("refresh_auth_failed");
    }
    return false;
  }

  free(*access_token);
  free(*entitlements);
  free(*user_id);
  *access_token = new_token;
  *entitlements = new_entitlements;
  *user_id = new_user_id;
  return true;
}

int main(void) {
  const char* username = getenv("VALORANT_USERNAME");
  const char* password = getenv("VALORANT_PASSWORD");
  if (!username || !username[0]) {
    username = VALORANT_USERNAME;
  }
  if (!password || !password[0]) {
    password = VALORANT_PASSWORD;
  }

  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    fprintf(stderr, "curl_global_init failed.\n");
    return 1;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "curl_easy_init failed.\n");
    curl_global_cleanup();
    return 1;
  }

  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
  const char* user_agent = getenv("VALORANT_USER_AGENT");
  if (!user_agent || !user_agent[0]) {
    user_agent = VALORANT_USER_AGENT;
  }
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);

  const char* refresh_on_login_env = getenv("VALORANT_REFRESH_ON_LOGIN");
  bool refresh_on_login = refresh_on_login_env
                              ? env_is_truthy("VALORANT_REFRESH_ON_LOGIN")
                              : true;
  if (env_is_truthy("VALORANT_REFRESH_DISABLE")) {
    refresh_on_login = false;
  }
  bool refresh_required = env_is_truthy("VALORANT_REFRESH_REQUIRED");
  int refresh_interval = env_int("VALORANT_REFRESH_INTERVAL_SEC", 0);
  if (refresh_interval > 0) {
    refresh_on_login = true;
  }

  char* account_url_template = read_account_xp_url(VALORANT_DATA_PATH);
  if (!account_url_template) {
    fprintf(stderr, "Failed to read account XP URL from %s.\n", VALORANT_DATA_PATH);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 1;
  }

  char* regional_template =
      apply_region_to_url(account_url_template, VALORANT_REGION);
  free(account_url_template);
  if (!regional_template) {
    fprintf(stderr, "Failed to build regional account XP URL.\n");
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 1;
  }

  bool success = false;
  for (int attempt = 0; attempt < 2 && !success; attempt++) {
    bool retry = false;
    http_response_t response = {0};
    struct curl_slist* headers = NULL;
    char* access_token = NULL;
    char* entitlements = NULL;
    char* user_id = NULL;
    char* bearer = NULL;
    char* client_version = NULL;
    char* account_url = NULL;

    char* login_error = NULL;
    access_token = valorant_login(curl, username, password, &login_error);
    if (!access_token) {
      fprintf(stderr, "Login failed.\n");
      if (login_error) {
        if (strcmp(login_error, "rate_limited") == 0) {
          fprintf(stderr, "Last login error: rate limited by Cloudflare.\n");
        } else {
          char* error_code = extract_json_string(login_error, "error");
          char* error_desc = extract_json_string(login_error, "error_description");
          char* country = extract_json_string(login_error, "country");
          if (error_code || error_desc || country) {
            fprintf(stderr, "Last login error:");
            if (error_code) {
              fprintf(stderr, " error=%s", error_code);
            }
            if (error_desc) {
              fprintf(stderr, " desc=%s", error_desc);
            }
            if (country) {
              fprintf(stderr, " country=%s", country);
            }
            fprintf(stderr, "\n");
          } else {
            fprintf(stderr, "Last login error: %s\n", login_error);
          }
          free(error_code);
          free(error_desc);
          free(country);
        }
      }
      free(login_error);
      goto attempt_cleanup;
    }
    free(login_error);

    char* normalized_token = normalize_access_token(access_token);
    free(access_token);
    access_token = normalized_token;
    if (!access_token) {
      fprintf(stderr, "Failed to normalize access token.\n");
      goto attempt_cleanup;
    }

    bool unauthorized = false;
    if (!valorant_fetch_auth_data(curl, access_token, &entitlements, &user_id,
                                  &unauthorized)) {
      if (unauthorized && attempt == 0) {
        fprintf(stderr, "Access token rejected. Re-authenticating...\n");
        retry = true;
      }
      goto attempt_cleanup;
    }

    if (refresh_on_login || refresh_required) {
      fprintf(stderr, "Refreshing access token...\n");
      char* refresh_error = NULL;
      bool refreshed = valorant_refresh_session(
          curl, &access_token, &entitlements, &user_id, NULL, &refresh_error);
      if (!refreshed) {
        fprintf(stderr, "Access token refresh failed.\n");
        if (refresh_error) {
          fprintf(stderr, "%s\n", refresh_error);
        }
        if (refresh_required) {
          free(refresh_error);
          goto attempt_cleanup;
        }
      }
      free(refresh_error);
    }

    bearer = (char*)malloc(strlen(access_token) + 8);
    if (!bearer) {
      fprintf(stderr, "Out of memory.\n");
      goto attempt_cleanup;
    }
    snprintf(bearer, strlen(access_token) + 8, "Bearer %s", access_token);

    char config_url[128];
    snprintf(config_url, sizeof(config_url),
             "https://shared.%s.a.pvp.net/v1/config/%s",
             VALORANT_REGION, VALORANT_REGION);

    char* auth_header = format_header("Authorization", bearer);
    char* ent_header = format_header("X-Riot-Entitlements-JWT", entitlements);
    char* platform_header =
        format_header("X-Riot-ClientPlatform", VALORANT_CLIENT_PLATFORM);
    if (auth_header && ent_header && platform_header) {
      headers = curl_slist_append(headers, auth_header);
      headers = curl_slist_append(headers, ent_header);
      headers = curl_slist_append(headers, platform_header);
      headers = curl_slist_append(headers, "Accept: application/json");

      if (http_request(curl, "GET", config_url, headers, NULL, &response) &&
          status_ok(&response)) {
        client_version = extract_json_string(response.data, "clientVersion");
        if (!client_version) {
          client_version = extract_json_string(response.data, "riotClientVersion");
        }
      }
    }

    free(auth_header);
    free(ent_header);
    free(platform_header);
    curl_slist_free_all(headers);
    headers = NULL;
    response_reset(&response);

    if (!client_version) {
      client_version = dup_string(VALORANT_CLIENT_VERSION);
    }
    if (!client_version) {
      fprintf(stderr, "Out of memory.\n");
      goto attempt_cleanup;
    }

    account_url = replace_placeholder(regional_template, "{user_id}", user_id);
    if (!account_url) {
      fprintf(stderr, "Failed to build account XP URL.\n");
      goto attempt_cleanup;
    }

    auth_header = format_header("Authorization", bearer);
    ent_header = format_header("X-Riot-Entitlements-JWT", entitlements);
    char* version_header = format_header("X-Riot-ClientVersion", client_version);
    platform_header =
        format_header("X-Riot-ClientPlatform", VALORANT_CLIENT_PLATFORM);
    if (!auth_header || !ent_header || !version_header || !platform_header) {
      fprintf(stderr, "Out of memory building headers.\n");
      free(auth_header);
      free(ent_header);
      free(version_header);
      free(platform_header);
      goto attempt_cleanup;
    }

    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, ent_header);
    headers = curl_slist_append(headers, version_header);
    headers = curl_slist_append(headers, platform_header);
    headers = curl_slist_append(headers, "Accept: application/json");

    free(auth_header);
    free(ent_header);
    free(version_header);
    free(platform_header);

    if (!http_request(curl, "GET", account_url, headers, NULL, &response)) {
      fprintf(stderr, "Account XP request failed.\n");
      goto attempt_cleanup;
    }

    if (status_unauthorized(&response)) {
      if (attempt == 0) {
        fprintf(stderr, "Account XP request unauthorized. Re-authenticating...\n");
        retry = true;
      } else {
        fprintf(stderr, "Account XP request unauthorized after re-auth.\n");
      }
      goto attempt_cleanup;
    }

    printf("Status: %ld\n", response.status);
    if (response.data) {
      printf("%s\n", response.data);
    }
    success = true;

    if (refresh_interval > 0) {
      curl_slist_free_all(headers);
      headers = NULL;
      response_reset(&response);
      free(account_url);
      account_url = NULL;
      free(client_version);
      client_version = NULL;
      free(bearer);
      bearer = NULL;

      fprintf(stderr, "Starting refresh loop every %d seconds.\n", refresh_interval);
      while (true) {
        sleep_ms((unsigned int)refresh_interval * 1000U);
        char* refresh_error = NULL;
        bool refreshed = valorant_refresh_session(
            curl, &access_token, &entitlements, &user_id, NULL, &refresh_error);
        if (refreshed) {
          fprintf(stderr, "Access token refreshed.\n");
          continue;
        }
        fprintf(stderr, "Access token refresh failed.\n");
        if (refresh_error) {
          fprintf(stderr, "%s\n", refresh_error);
        }
        free(refresh_error);
        if (refresh_required) {
          success = false;
          break;
        }
      }
    }

  attempt_cleanup:
    curl_slist_free_all(headers);
    response_reset(&response);
    free(account_url);
    free(client_version);
    free(user_id);
    free(entitlements);
    free(bearer);
    free(access_token);

    if (success || !retry) {
      break;
    }
  }

  free(regional_template);
  curl_easy_cleanup(curl);
  curl_global_cleanup();
  return success ? 0 : 1;
}
