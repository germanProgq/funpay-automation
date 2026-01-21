/* FunPay Vertex identity parsing helpers. */

#include "identity/core/fpv_identity_internal.h"


#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char* fpv_trim_copy(const char* value) {
  if (!value) {
    return NULL;
  }
  const char* start = value;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char* end = start + strlen(start);
  while (end > start && isspace((unsigned char)*(end - 1))) {
    end--;
  }
  size_t len = (size_t)(end - start);
  char* out = (char*)malloc(len + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

char* fpv_identity_normalize_email(const char* email) {
  char* trimmed = fpv_trim_copy(email);
  if (!trimmed) {
    return NULL;
  }
  for (char* ptr = trimmed; *ptr; ptr++) {
    *ptr = (char)tolower((unsigned char)*ptr);
  }
  return trimmed;
}

bool fpv_identity_validate_email(const char* email) {
  if (!email || !email[0]) {
    return false;
  }
  const char* at = strchr(email, '@');
  if (!at || at == email || at[1] == '\0') {
    return false;
  }
  const char* dot = strchr(at + 1, '.');
  if (!dot || dot == at + 1 || dot[1] == '\0') {
    return false;
  }
  for (const char* ptr = email; *ptr; ptr++) {
    if (isspace((unsigned char)*ptr)) {
      return false;
    }
  }
  return true;
}

bool fpv_identity_validate_password(const char* password) {
  if (!password) {
    return false;
  }
  size_t len = strlen(password);
  return len >= 8 && len <= 256;
}

bool fpv_identity_role_valid(fpv_role_t role) {
  return role >= FPV_ROLE_OWNER && role <= FPV_ROLE_VIEWER;
}

bool fpv_identity_role_at_least(fpv_role_t have, fpv_role_t need) {
  if (!fpv_identity_role_valid(have) || !fpv_identity_role_valid(need)) {
    return false;
  }
  return have <= need;
}
void fpv_identity_user_auth_clear(fpv_identity_user_auth_t* auth) {
  if (!auth) {
    return;
  }
  fpv_user_destroy(auth->user);
  fpv_free(auth->password_salt);
  fpv_free(auth->password_hash);
  memset(auth, 0, sizeof(*auth));
}

int fpv_identity_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

int fpv_identity_ascii_strcasecmp(const char* left, const char* right) {
  size_t index = 0;
  while (left[index] != '\0' || right[index] != '\0') {
    int a = fpv_identity_ascii_tolower((unsigned char)left[index]);
    int b = fpv_identity_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
    if (left[index] == '\0' || right[index] == '\0') {
      break;
    }
    index++;
  }
  return 0;
}

bool fpv_identity_parse_bool(const char* text, bool* out) {
  if (!text || !out) {
    return false;
  }
  if (text[0] == '1' && text[1] == '\0') {
    *out = true;
    return true;
  }
  if (text[0] == '0' && text[1] == '\0') {
    *out = false;
    return true;
  }
  if (fpv_identity_ascii_strcasecmp(text, "true") == 0) {
    *out = true;
    return true;
  }
  if (fpv_identity_ascii_strcasecmp(text, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool fpv_identity_parse_uint64(const char* text, uint64_t* out) {
  if (!text || !out || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (uint64_t)parsed;
  return true;
}

bool fpv_identity_parse_uint32(const char* text, uint32_t* out) {
  if (!text || !out || !text[0]) {
    return false;
  }
  char* end = NULL;
  unsigned long parsed = strtoul(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (uint32_t)parsed;
  return true;
}

bool fpv_identity_parse_int(const char* text, int* out) {
  if (!text || !out || !text[0]) {
    return false;
  }
  char* end = NULL;
  long parsed = strtol(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (int)parsed;
  return true;
}

bool fpv_identity_parse_double(const char* text, double* out) {
  if (!text || !out || !text[0]) {
    return false;
  }
  char* end = NULL;
  double parsed = strtod(text, &end);
  if (!end || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}
