/* FunPay Vertex identity and access management implementation. */

#include "fpv_core/fpv_identity.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
#include <stdlib.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#endif

#include "fpv_db.h"
#include "fpv_string.h"
#include "fpv_time.h"

struct fpv_identity_store {
  fpv_db_t* db;
};

static void fpv_secure_zero(void* data, size_t len) {
  volatile unsigned char* ptr = (volatile unsigned char*)data;
  while (len--) {
    *ptr++ = 0;
  }
}

static bool fpv_random_bytes(void* data, size_t len) {
  if (!data || len == 0) {
    return false;
  }
#if defined(_WIN32)
  return BCRYPT_SUCCESS(BCryptGenRandom(
      NULL, (PUCHAR)data, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
  arc4random_buf(data, len);
  return true;
#else
#if defined(__linux__)
  ssize_t read_len = getrandom(data, len, 0);
  if (read_len == (ssize_t)len) {
    return true;
  }
#endif
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    return false;
  }
  unsigned char* ptr = (unsigned char*)data;
  size_t remaining = len;
  while (remaining > 0) {
    ssize_t chunk = read(fd, ptr, remaining);
    if (chunk <= 0) {
      if (chunk < 0 && errno == EINTR) {
        continue;
      }
      close(fd);
      return false;
    }
    ptr += (size_t)chunk;
    remaining -= (size_t)chunk;
  }
  close(fd);
  return true;
#endif
}

static char* fpv_hex_encode(const uint8_t* data, size_t len) {
  static const char hex[] = "0123456789abcdef";
  if (!data || len == 0) {
    return fpv_strdup("");
  }
  char* out = (char*)malloc(len * 2 + 1);
  if (!out) {
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = hex[(data[i] >> 4) & 0x0F];
    out[i * 2 + 1] = hex[data[i] & 0x0F];
  }
  out[len * 2] = '\0';
  return out;
}

static bool fpv_hex_decode(
    const char* hex,
    uint8_t* out,
    size_t out_len) {
  if (!hex || !out) {
    return false;
  }
  size_t len = strlen(hex);
  if (len != out_len * 2) {
    return false;
  }
  for (size_t i = 0; i < out_len; i++) {
    char high = hex[i * 2];
    char low = hex[i * 2 + 1];
    int hi = isdigit((unsigned char)high) ? high - '0'
             : isxdigit((unsigned char)high) ? (tolower((unsigned char)high) - 'a' + 10)
                                             : -1;
    int lo = isdigit((unsigned char)low) ? low - '0'
             : isxdigit((unsigned char)low) ? (tolower((unsigned char)low) - 'a' + 10)
                                            : -1;
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

static char* fpv_trim_copy(const char* value) {
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

static char* fpv_identity_normalize_email(const char* email) {
  char* trimmed = fpv_trim_copy(email);
  if (!trimmed) {
    return NULL;
  }
  for (char* ptr = trimmed; *ptr; ptr++) {
    *ptr = (char)tolower((unsigned char)*ptr);
  }
  return trimmed;
}

static bool fpv_identity_validate_email(const char* email) {
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

static bool fpv_identity_validate_password(const char* password) {
  if (!password) {
    return false;
  }
  size_t len = strlen(password);
  return len >= 8 && len <= 256;
}

static bool fpv_identity_role_valid(fpv_role_t role) {
  return role >= FPV_ROLE_OWNER && role <= FPV_ROLE_VIEWER;
}

static bool fpv_identity_role_at_least(fpv_role_t have, fpv_role_t need) {
  if (!fpv_identity_role_valid(have) || !fpv_identity_role_valid(need)) {
    return false;
  }
  return have <= need;
}

typedef struct fpv_sha256_ctx {
  uint32_t state[8];
  uint64_t bitcount;
  uint8_t buffer[64];
  size_t buffer_len;
} fpv_sha256_ctx_t;

static uint32_t fpv_sha256_rotr(uint32_t value, uint32_t shift) {
  return (value >> shift) | (value << (32 - shift));
}

static uint32_t fpv_sha256_load_be(const uint8_t* data) {
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         ((uint32_t)data[3]);
}

static void fpv_sha256_store_be(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)(value);
}

static const uint32_t fpv_sha256_k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void fpv_sha256_transform(uint32_t state[8], const uint8_t block[64]) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; i++) {
    w[i] = fpv_sha256_load_be(block + i * 4);
  }
  for (size_t i = 16; i < 64; i++) {
    uint32_t s0 = fpv_sha256_rotr(w[i - 15], 7) ^
                  fpv_sha256_rotr(w[i - 15], 18) ^
                  (w[i - 15] >> 3);
    uint32_t s1 = fpv_sha256_rotr(w[i - 2], 17) ^
                  fpv_sha256_rotr(w[i - 2], 19) ^
                  (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (size_t i = 0; i < 64; i++) {
    uint32_t s1 = fpv_sha256_rotr(e, 6) ^
                  fpv_sha256_rotr(e, 11) ^
                  fpv_sha256_rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + s1 + ch + fpv_sha256_k[i] + w[i];
    uint32_t s0 = fpv_sha256_rotr(a, 2) ^
                  fpv_sha256_rotr(a, 13) ^
                  fpv_sha256_rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

static void fpv_sha256_init(fpv_sha256_ctx_t* ctx) {
  ctx->state[0] = 0x6a09e667U;
  ctx->state[1] = 0xbb67ae85U;
  ctx->state[2] = 0x3c6ef372U;
  ctx->state[3] = 0xa54ff53aU;
  ctx->state[4] = 0x510e527fU;
  ctx->state[5] = 0x9b05688cU;
  ctx->state[6] = 0x1f83d9abU;
  ctx->state[7] = 0x5be0cd19U;
  ctx->bitcount = 0;
  ctx->buffer_len = 0;
}

static void fpv_sha256_update(
    fpv_sha256_ctx_t* ctx,
    const uint8_t* data,
    size_t len) {
  if (!ctx || !data || len == 0) {
    return;
  }
  ctx->bitcount += (uint64_t)len * 8U;
  size_t offset = 0;
  while (offset < len) {
    size_t space = 64 - ctx->buffer_len;
    size_t chunk = len - offset;
    if (chunk > space) {
      chunk = space;
    }
    memcpy(ctx->buffer + ctx->buffer_len, data + offset, chunk);
    ctx->buffer_len += chunk;
    offset += chunk;
    if (ctx->buffer_len == 64) {
      fpv_sha256_transform(ctx->state, ctx->buffer);
      ctx->buffer_len = 0;
    }
  }
}

static void fpv_sha256_final(fpv_sha256_ctx_t* ctx, uint8_t out[32]) {
  uint8_t length[8];
  uint64_t bits = ctx->bitcount;
  for (size_t i = 0; i < 8; i++) {
    length[7 - i] = (uint8_t)(bits & 0xFFU);
    bits >>= 8;
  }
  uint8_t pad = 0x80U;
  fpv_sha256_update(ctx, &pad, 1);
  uint8_t zero = 0x00U;
  while (ctx->buffer_len != 56) {
    fpv_sha256_update(ctx, &zero, 1);
  }
  fpv_sha256_update(ctx, length, sizeof(length));
  for (size_t i = 0; i < 8; i++) {
    fpv_sha256_store_be(out + i * 4, ctx->state[i]);
  }
  fpv_secure_zero(ctx, sizeof(*ctx));
}

static void fpv_sha256_digest(
    const uint8_t* data,
    size_t len,
    uint8_t out[32]) {
  fpv_sha256_ctx_t ctx;
  fpv_sha256_init(&ctx);
  fpv_sha256_update(&ctx, data, len);
  fpv_sha256_final(&ctx, out);
}

static void fpv_hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const uint8_t* data,
    size_t data_len,
    uint8_t out[32]) {
  uint8_t k0[64];
  memset(k0, 0, sizeof(k0));
  if (key_len > sizeof(k0)) {
    fpv_sha256_digest(key, key_len, k0);
  } else if (key_len > 0) {
    memcpy(k0, key, key_len);
  }
  uint8_t ipad[64];
  uint8_t opad[64];
  for (size_t i = 0; i < 64; i++) {
    ipad[i] = (uint8_t)(k0[i] ^ 0x36U);
    opad[i] = (uint8_t)(k0[i] ^ 0x5cU);
  }

  fpv_sha256_ctx_t inner;
  fpv_sha256_init(&inner);
  fpv_sha256_update(&inner, ipad, sizeof(ipad));
  fpv_sha256_update(&inner, data, data_len);
  uint8_t inner_hash[32];
  fpv_sha256_final(&inner, inner_hash);

  fpv_sha256_ctx_t outer;
  fpv_sha256_init(&outer);
  fpv_sha256_update(&outer, opad, sizeof(opad));
  fpv_sha256_update(&outer, inner_hash, sizeof(inner_hash));
  fpv_sha256_final(&outer, out);

  fpv_secure_zero(k0, sizeof(k0));
  fpv_secure_zero(ipad, sizeof(ipad));
  fpv_secure_zero(opad, sizeof(opad));
  fpv_secure_zero(inner_hash, sizeof(inner_hash));
}

static bool fpv_pbkdf2_sha256(
    const uint8_t* password,
    size_t password_len,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations,
    uint8_t* out,
    size_t out_len) {
  if (!password || !salt || !out || iterations == 0 || out_len == 0) {
    return false;
  }
  uint32_t block_count =
      (uint32_t)((out_len + 31) / 32);
  uint8_t u[32];
  uint8_t t[32];
  for (uint32_t block = 1; block <= block_count; block++) {
    uint8_t salt_block[64];
    size_t salt_block_len = 0;
    if (salt_len + 4 > sizeof(salt_block)) {
      return false;
    }
    memcpy(salt_block, salt, salt_len);
    salt_block_len = salt_len;
    salt_block[salt_block_len++] = (uint8_t)(block >> 24);
    salt_block[salt_block_len++] = (uint8_t)(block >> 16);
    salt_block[salt_block_len++] = (uint8_t)(block >> 8);
    salt_block[salt_block_len++] = (uint8_t)(block);

    fpv_hmac_sha256(password, password_len, salt_block, salt_block_len, u);
    memcpy(t, u, sizeof(u));
    for (uint32_t iter = 1; iter < iterations; iter++) {
      fpv_hmac_sha256(password, password_len, u, sizeof(u), u);
      for (size_t i = 0; i < sizeof(t); i++) {
        t[i] ^= u[i];
      }
    }

    size_t offset = (block - 1) * 32U;
    size_t remaining = out_len - offset;
    size_t chunk = remaining > 32U ? 32U : remaining;
    memcpy(out + offset, t, chunk);
  }
  fpv_secure_zero(u, sizeof(u));
  fpv_secure_zero(t, sizeof(t));
  return true;
}

static bool fpv_secure_equal(const uint8_t* left, const uint8_t* right, size_t len) {
  if (!left || !right) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= (uint8_t)(left[i] ^ right[i]);
  }
  return diff == 0;
}

static fpv_data_retention_policy_t fpv_identity_default_retention(void) {
  fpv_data_retention_policy_t policy;
  policy.audit_log_days = 365;
  policy.price_history_days = 365;
  policy.competitor_listing_days = 90;
  policy.order_history_days = 365;
  return policy;
}

static char* fpv_identity_random_id(size_t bytes) {
  uint8_t buffer[32];
  if (bytes > sizeof(buffer)) {
    return NULL;
  }
  if (!fpv_random_bytes(buffer, bytes)) {
    return NULL;
  }
  char* hex = fpv_hex_encode(buffer, bytes);
  fpv_secure_zero(buffer, sizeof(buffer));
  return hex;
}

static char* fpv_identity_hash_password(
    const char* password,
    const uint8_t* salt,
    size_t salt_len,
    uint32_t iterations) {
  uint8_t derived[32];
  if (!fpv_pbkdf2_sha256(
          (const uint8_t*)password,
          strlen(password),
          salt,
          salt_len,
          iterations,
          derived,
          sizeof(derived))) {
    return NULL;
  }
  char* hex = fpv_hex_encode(derived, sizeof(derived));
  fpv_secure_zero(derived, sizeof(derived));
  return hex;
}

static char* fpv_identity_hash_token(const char* token) {
  uint8_t digest[32];
  fpv_sha256_digest((const uint8_t*)token, strlen(token), digest);
  char* hex = fpv_hex_encode(digest, sizeof(digest));
  fpv_secure_zero(digest, sizeof(digest));
  return hex;
}

typedef struct fpv_identity_user_auth {
  fpv_user_t* user;
  char* password_salt;
  char* password_hash;
  uint32_t password_iterations;
} fpv_identity_user_auth_t;

static void fpv_identity_user_auth_clear(fpv_identity_user_auth_t* auth) {
  if (!auth) {
    return;
  }
  fpv_user_destroy(auth->user);
  fpv_free(auth->password_salt);
  fpv_free(auth->password_hash);
  memset(auth, 0, sizeof(*auth));
}

static int fpv_identity_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static int fpv_identity_ascii_strcasecmp(const char* left, const char* right) {
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

static bool fpv_identity_parse_bool(const char* text, bool* out) {
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

static bool fpv_identity_parse_uint64(const char* text, uint64_t* out) {
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

static bool fpv_identity_parse_uint32(const char* text, uint32_t* out) {
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

static bool fpv_identity_parse_int(const char* text, int* out) {
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

static fpv_result_t fpv_identity_db_query_single(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    fpv_db_result_t** out_rows) {
  if (out_rows) {
    *out_rows = NULL;
  }
  if (!store || !store->db || !sql || !out_rows) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result =
      fpv_db_query(store->db, sql, params, param_count, &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_NOT_FOUND;
  }
  *out_rows = rows;
  return FPV_OK;
}

static fpv_result_t fpv_identity_db_exists(
    const fpv_identity_store_t* store,
    const char* sql,
    const char* const* params,
    size_t param_count,
    bool* out_exists) {
  if (out_exists) {
    *out_exists = false;
  }
  if (!store || !store->db || !sql || !out_exists) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result =
      fpv_db_query(store->db, sql, params, param_count, &rows);
  if (result != FPV_OK) {
    return result;
  }
  *out_exists = rows && rows->row_count > 0;
  fpv_db_result_destroy(rows);
  return FPV_OK;
}

static fpv_user_t* fpv_identity_user_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  bool email_verified = false;
  bool active = false;
  uint64_t created_at = 0;
  uint64_t last_login = 0;
  if (!fpv_identity_parse_bool(row[3], &email_verified) ||
      !fpv_identity_parse_bool(row[4], &active) ||
      !fpv_identity_parse_uint64(row[5], &created_at) ||
      !fpv_identity_parse_uint64(row[6], &last_login)) {
    return NULL;
  }
  return fpv_user_create(
      row[0],
      row[1],
      row[2],
      email_verified,
      active,
      created_at,
      last_login);
}

static fpv_organization_t* fpv_identity_organization_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  fpv_data_retention_policy_t retention;
  if (!fpv_identity_parse_uint32(row[4], &retention.audit_log_days) ||
      !fpv_identity_parse_uint32(row[5], &retention.price_history_days) ||
      !fpv_identity_parse_uint32(row[6], &retention.competitor_listing_days) ||
      !fpv_identity_parse_uint32(row[7], &retention.order_history_days)) {
    return NULL;
  }
  uint64_t created_at = 0;
  uint64_t updated_at = 0;
  if (!fpv_identity_parse_uint64(row[8], &created_at) ||
      !fpv_identity_parse_uint64(row[9], &updated_at)) {
    return NULL;
  }
  return fpv_organization_create(
      row[0],
      row[1],
      row[2],
      row[3],
      &retention,
      created_at,
      updated_at);
}

static fpv_team_t* fpv_identity_team_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  bool active = false;
  uint64_t created_at = 0;
  uint64_t updated_at = 0;
  if (!fpv_identity_parse_bool(row[3], &active) ||
      !fpv_identity_parse_uint64(row[4], &created_at) ||
      !fpv_identity_parse_uint64(row[5], &updated_at)) {
    return NULL;
  }
  return fpv_team_create(
      row[0],
      row[1],
      row[2],
      active,
      created_at,
      updated_at);
}

static fpv_account_t* fpv_identity_account_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[3]) {
    return NULL;
  }
  bool active = false;
  uint64_t linked_at = 0;
  uint64_t last_sync = 0;
  if (!fpv_identity_parse_bool(row[7], &active) ||
      !fpv_identity_parse_uint64(row[8], &linked_at) ||
      !fpv_identity_parse_uint64(row[9], &last_sync)) {
    return NULL;
  }
  return fpv_account_create(
      row[0],
      row[1],
      row[2],
      row[3],
      row[4],
      row[5],
      row[6],
      active,
      linked_at,
      last_sync);
}

static fpv_identity_invite_t* fpv_identity_invite_from_row(char** row) {
  if (!row || !row[0] || !row[1] || !row[4]) {
    return NULL;
  }
  int role_value = 0;
  if (!fpv_identity_parse_int(row[4], &role_value) ||
      !fpv_identity_role_valid((fpv_role_t)role_value)) {
    return NULL;
  }
  uint64_t created_at = 0;
  uint64_t expires_at = 0;
  uint64_t accepted_at = 0;
  if (!fpv_identity_parse_uint64(row[5], &created_at) ||
      !fpv_identity_parse_uint64(row[6], &expires_at) ||
      !fpv_identity_parse_uint64(row[7], &accepted_at)) {
    return NULL;
  }
  fpv_identity_invite_t* invite =
      (fpv_identity_invite_t*)calloc(1, sizeof(*invite));
  if (!invite) {
    return NULL;
  }
  invite->id = fpv_strdup(row[0]);
  invite->organization_id = fpv_strdup(row[1]);
  invite->team_id = fpv_strdup(row[2]);
  invite->email = fpv_strdup(row[3]);
  invite->role = (fpv_role_t)role_value;
  invite->created_at_ms = created_at;
  invite->expires_at_ms = expires_at;
  invite->accepted_at_ms = accepted_at;
  invite->accepted_user_id = fpv_strdup(row[8]);
  invite->created_by_user_id = fpv_strdup(row[9]);
  if (!invite->id || !invite->organization_id) {
    fpv_identity_invite_destroy(invite);
    return NULL;
  }
  return invite;
}

static fpv_result_t fpv_identity_fetch_user_auth_by_email(
    const fpv_identity_store_t* store,
    const char* normalized_email,
    fpv_identity_user_auth_t* out_auth) {
  if (!out_auth) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  memset(out_auth, 0, sizeof(*out_auth));
  const char* params[] = {normalized_email};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, email, display_name, email_verified, active, created_at_ms, "
      "last_login_ms, password_salt, password_hash, password_iterations "
      "FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  char** row = rows->rows[0];
  fpv_user_t* user = fpv_identity_user_from_row(row);
  if (!user || !row[7] || !row[8] || !row[9]) {
    fpv_user_destroy(user);
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  uint32_t iterations = 0;
  if (!fpv_identity_parse_uint32(row[9], &iterations)) {
    fpv_user_destroy(user);
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  out_auth->user = user;
  out_auth->password_salt = fpv_strdup(row[7]);
  out_auth->password_hash = fpv_strdup(row[8]);
  out_auth->password_iterations = iterations;
  fpv_db_result_destroy(rows);
  if (!out_auth->password_salt || !out_auth->password_hash) {
    fpv_identity_user_auth_clear(out_auth);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

static fpv_result_t fpv_identity_fetch_account_id_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    char** out_id) {
  if (out_id) {
    *out_id = NULL;
  }
  if (!out_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id FROM fpv_accounts WHERE organization_id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows->rows[0] || !rows->rows[0][0]) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_INTERNAL;
  }
  *out_id = fpv_strdup(rows->rows[0][0]);
  fpv_db_result_destroy(rows);
  if (!*out_id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}

static fpv_result_t fpv_identity_fetch_invite_by_hash(
    const fpv_identity_store_t* store,
    const char* token_hash,
    fpv_identity_invite_t** out_invite) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (!out_invite) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {token_hash};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, email, role, created_at_ms, "
      "expires_at_ms, accepted_at_ms, accepted_user_id, created_by_user_id "
      "FROM fpv_invites WHERE token_hash = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_identity_invite_t* invite = fpv_identity_invite_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!invite) {
    return FPV_ERR_INTERNAL;
  }
  *out_invite = invite;
  return FPV_OK;
}

fpv_identity_store_t* fpv_identity_store_open(
    const char* data_dir,
    fpv_result_t* out_result) {
  if (out_result) {
    *out_result = FPV_OK;
  }

  const char* db_url = getenv("FPV_DB_URL");
  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = db_url;
  config.data_dir = data_dir;

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

  fpv_identity_store_t* store =
      (fpv_identity_store_t*)calloc(1, sizeof(*store));
  if (!store) {
    if (out_result) {
      *out_result = FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_db_close(db);
    return NULL;
  }
  store->db = db;
  return store;
}

void fpv_identity_store_destroy(fpv_identity_store_t* store) {
  if (!store) {
    return;
  }
  fpv_db_close(store->db);
  free(store);
}

bool fpv_identity_store_has_users(const fpv_identity_store_t* store) {
  if (!store || !store->db) {
    return false;
  }
  bool exists = false;
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users LIMIT 1;",
      NULL,
      0,
      &exists);
  return result == FPV_OK && exists;
}

fpv_result_t fpv_identity_create_user(
    fpv_identity_store_t* store,
    const char* email,
    const char* display_name,
    const char* password,
    bool email_verified,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !email || !password) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* normalized = fpv_identity_normalize_email(email);
  if (!normalized || !fpv_identity_validate_email(normalized)) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!fpv_identity_validate_password(password)) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool exists = false;
  const char* exists_params[] = {normalized};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE email_normalized = $1 LIMIT 1;",
      exists_params,
      1,
      &exists);
  if (result != FPV_OK) {
    fpv_free(normalized);
    return result;
  }
  if (exists) {
    fpv_free(normalized);
    return FPV_ERR_INVALID_STATE;
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    fpv_free(normalized);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint8_t salt[16];
  if (!fpv_random_bytes(salt, sizeof(salt))) {
    fpv_free(id);
    fpv_free(normalized);
    return FPV_ERR_INTERNAL;
  }
  const uint32_t iterations = 200000;
  char* salt_hex = fpv_hex_encode(salt, sizeof(salt));
  char* hash_hex = fpv_identity_hash_password(
      password, salt, sizeof(salt), iterations);
  fpv_secure_zero(salt, sizeof(salt));
  if (!salt_hex || !hash_hex) {
    fpv_free(id);
    fpv_free(normalized);
    fpv_free(salt_hex);
    fpv_free(hash_hex);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  uint64_t now_ms = fpv_time_now_ms();
  fpv_user_t* user = fpv_user_create(
      id,
      normalized,
      display_name,
      email_verified,
      true,
      now_ms,
      0);
  if (!user) {
    fpv_free(id);
    fpv_free(normalized);
    fpv_free(salt_hex);
    fpv_free(hash_hex);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  char created_buf[32];
  char last_login_buf[32];
  char iterations_buf[32];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(last_login_buf, sizeof(last_login_buf), "%llu",
           (unsigned long long)0ULL);
  snprintf(iterations_buf, sizeof(iterations_buf), "%u", iterations);
  const char* params[] = {
      id,
      normalized,
      normalized,
      display_name,
      email_verified ? "1" : "0",
      "1",
      created_buf,
      last_login_buf,
      salt_hex,
      hash_hex,
      iterations_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_users (id, email, email_normalized, display_name, "
      "email_verified, active, created_at_ms, last_login_ms, password_salt, "
      "password_hash, password_iterations) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);",
      params,
      11);
  fpv_free(id);
  fpv_free(normalized);
  fpv_free(salt_hex);
  fpv_free(hash_hex);
  if (result != FPV_OK) {
    fpv_user_destroy(user);
    return result;
  }
  if (out_user) {
    *out_user = user;
  } else {
    fpv_user_destroy(user);
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_authenticate(
    fpv_identity_store_t* store,
    const char* email,
    const char* password,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !email || !password) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* normalized = fpv_identity_normalize_email(email);
  if (!normalized) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_user_auth_t auth;
  fpv_result_t result = fpv_identity_fetch_user_auth_by_email(
      store, normalized, &auth);
  fpv_free(normalized);
  if (result != FPV_OK) {
    return result;
  }
  if (!auth.user || !auth.user->active) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_NOT_FOUND;
  }
  uint8_t salt[16];
  if (!fpv_hex_decode(auth.password_salt, salt, sizeof(salt))) {
    fpv_secure_zero(salt, sizeof(salt));
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_INTERNAL;
  }
  char* hash_hex = fpv_identity_hash_password(
      password,
      salt,
      sizeof(salt),
      auth.password_iterations);
  fpv_secure_zero(salt, sizeof(salt));
  if (!hash_hex) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  bool match = strlen(hash_hex) == strlen(auth.password_hash) &&
               fpv_secure_equal(
                   (const uint8_t*)hash_hex,
                   (const uint8_t*)auth.password_hash,
                   strlen(auth.password_hash));
  fpv_free(hash_hex);
  if (!match) {
    fpv_identity_user_auth_clear(&auth);
    return FPV_ERR_INVALID_STATE;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char now_buf[32];
  snprintf(now_buf, sizeof(now_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {now_buf, auth.user->id};
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_users SET last_login_ms = $1 WHERE id = $2;",
      params,
      2);
  if (result != FPV_OK) {
    fpv_identity_user_auth_clear(&auth);
    return result;
  }
  auth.user->last_login_ms = now_ms;
  if (out_user) {
    *out_user = fpv_user_clone(auth.user);
    if (!*out_user) {
      fpv_identity_user_auth_clear(&auth);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_identity_user_auth_clear(&auth);
  return FPV_OK;
}

fpv_result_t fpv_identity_create_organization(
    fpv_identity_store_t* store,
    const char* name,
    const char* timezone,
    const char* currency,
    const fpv_data_retention_policy_t* retention,
    const char* owner_user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_default_team,
    fpv_user_role_t** out_owner_role) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (out_default_team) {
    *out_default_team = NULL;
  }
  if (out_owner_role) {
    *out_owner_role = NULL;
  }
  if (!store || !store->db || !name || !owner_user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool owner_exists = false;
  const char* owner_params[] = {owner_user_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
      owner_params,
      1,
      &owner_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!owner_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  char* org_id = fpv_identity_random_id(16);
  if (!org_id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* team_id = fpv_identity_random_id(16);
  if (!team_id) {
    fpv_free(org_id);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  fpv_data_retention_policy_t policy =
      retention ? *retention : fpv_identity_default_retention();

  char created_buf[32];
  char updated_buf[32];
  char audit_buf[16];
  char price_buf[16];
  char competitor_buf[16];
  char order_buf[16];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(audit_buf, sizeof(audit_buf), "%u", policy.audit_log_days);
  snprintf(price_buf, sizeof(price_buf), "%u", policy.price_history_days);
  snprintf(competitor_buf, sizeof(competitor_buf), "%u",
           policy.competitor_listing_days);
  snprintf(order_buf, sizeof(order_buf), "%u", policy.order_history_days);

  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  const char* org_params[] = {
      org_id,
      name,
      timezone,
      currency,
      audit_buf,
      price_buf,
      competitor_buf,
      order_buf,
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_organizations (id, name, timezone, currency, "
      "retention_audit_log_days, retention_price_history_days, "
      "retention_competitor_listing_days, retention_order_history_days, "
      "created_at_ms, updated_at_ms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10);",
      org_params,
      10);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  const char* team_params[] = {
      team_id,
      org_id,
      "Default",
      "1",
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_teams (id, organization_id, name, active, "
      "created_at_ms, updated_at_ms) VALUES ($1, $2, $3, $4, $5, $6);",
      team_params,
      6);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", FPV_ROLE_OWNER);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* role_params[] = {
      owner_user_id,
      org_id,
      team_id,
      role_buf,
      assigned_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      role_params,
      5);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  result = fpv_db_commit(store->db);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  if (out_organization) {
    *out_organization = fpv_organization_create(
        org_id,
        name,
        timezone,
        currency,
        &policy,
        now_ms,
        now_ms);
    if (!*out_organization) {
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_default_team) {
    *out_default_team = fpv_team_create(
        team_id,
        org_id,
        "Default",
        true,
        now_ms,
        now_ms);
    if (!*out_default_team) {
      fpv_organization_destroy(*out_organization);
      if (out_organization) {
        *out_organization = NULL;
      }
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_owner_role) {
    *out_owner_role = fpv_user_role_create(
        owner_user_id,
        org_id,
        team_id,
        FPV_ROLE_OWNER,
        now_ms);
    if (!*out_owner_role) {
      fpv_team_destroy(*out_default_team);
      if (out_default_team) {
        *out_default_team = NULL;
      }
      fpv_organization_destroy(*out_organization);
      if (out_organization) {
        *out_organization = NULL;
      }
      fpv_free(org_id);
      fpv_free(team_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  fpv_free(org_id);
  fpv_free(team_id);
  return FPV_OK;
}

fpv_result_t fpv_identity_create_team(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* name,
    bool active,
    fpv_team_t** out_team) {
  if (out_team) {
    *out_team = NULL;
  }
  if (!store || !store->db || !organization_id || !name) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      org_params,
      1,
      &org_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!org_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  char created_buf[32];
  char updated_buf[32];
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(updated_buf, sizeof(updated_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {
      id,
      organization_id,
      name,
      active ? "1" : "0",
      created_buf,
      updated_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_teams (id, organization_id, name, active, "
      "created_at_ms, updated_at_ms) VALUES ($1, $2, $3, $4, $5, $6);",
      params,
      6);
  if (result != FPV_OK) {
    fpv_free(id);
    return result;
  }
  if (out_team) {
    *out_team = fpv_team_create(
        id,
        organization_id,
        name,
        active,
        now_ms,
        now_ms);
    if (!*out_team) {
      fpv_free(id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(id);
  return FPV_OK;
}

fpv_result_t fpv_identity_assign_role(
    fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t role,
    fpv_user_role_t** out_role) {
  if (out_role) {
    *out_role = NULL;
  }
  if (!store || !store->db || !user_id || !organization_id ||
      !fpv_identity_role_valid(role)) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool user_exists = false;
  const char* user_params[] = {user_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_users WHERE id = $1 LIMIT 1;",
      user_params,
      1,
      &user_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!user_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      org_params,
      1,
      &org_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!org_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  if (team_id) {
    bool team_ok = false;
    const char* team_params[] = {team_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 "
        "LIMIT 1;",
        team_params,
        2,
        &team_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!team_ok) {
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  bool exists = false;
  const char* role_params[] = {user_id, organization_id, team_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      role_params,
      3,
      &exists);
  if (result != FPV_OK) {
    return result;
  }
  if (exists) {
    return FPV_ERR_INVALID_STATE;
  }

  uint64_t now_ms = fpv_time_now_ms();
  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", role);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* params[] = {
      user_id,
      organization_id,
      team_id,
      role_buf,
      assigned_buf};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      params,
      5);
  if (result != FPV_OK) {
    return result;
  }
  if (out_role) {
    *out_role = fpv_user_role_create(
        user_id,
        organization_id,
        team_id,
        role,
        now_ms);
    if (!*out_role) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_create_invite(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* email,
    fpv_role_t role,
    const char* created_by_user_id,
    uint64_t expires_at_ms,
    fpv_identity_invite_t** out_invite,
    char** out_token) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (out_token) {
    *out_token = NULL;
  }
  if (!store || !store->db || !organization_id || !fpv_identity_role_valid(role)) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      org_params,
      1,
      &org_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!org_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  if (team_id) {
    bool team_ok = false;
    const char* team_params[] = {team_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 "
        "LIMIT 1;",
        team_params,
        2,
        &team_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!team_ok) {
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  char* normalized_email = NULL;
  if (email && email[0]) {
    normalized_email = fpv_identity_normalize_email(email);
    if (!normalized_email || !fpv_identity_validate_email(normalized_email)) {
      fpv_free(normalized_email);
      return FPV_ERR_INVALID_ARGUMENT;
    }
  }
  char* id = fpv_identity_random_id(16);
  if (!id) {
    fpv_free(normalized_email);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* token = fpv_identity_random_id(24);
  if (!token) {
    fpv_free(id);
    fpv_free(normalized_email);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    fpv_free(id);
    fpv_free(normalized_email);
    fpv_free(token);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  uint64_t now_ms = fpv_time_now_ms();
  uint64_t expiry = expires_at_ms;
  if (expiry == 0) {
    expiry = now_ms + 7ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
  }
  char role_buf[8];
  char created_buf[32];
  char expires_buf[32];
  char accepted_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", role);
  snprintf(created_buf, sizeof(created_buf), "%llu",
           (unsigned long long)now_ms);
  snprintf(expires_buf, sizeof(expires_buf), "%llu",
           (unsigned long long)expiry);
  snprintf(accepted_buf, sizeof(accepted_buf), "%llu",
           (unsigned long long)0ULL);
  const char* params[] = {
      id,
      organization_id,
      team_id,
      normalized_email,
      role_buf,
      created_buf,
      expires_buf,
      accepted_buf,
      NULL,
      created_by_user_id,
      token_hash};
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_invites (id, organization_id, team_id, email, role, "
      "created_at_ms, expires_at_ms, accepted_at_ms, accepted_user_id, "
      "created_by_user_id, token_hash) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);",
      params,
      11);
  if (result != FPV_OK) {
    fpv_free(id);
    fpv_free(normalized_email);
    fpv_free(token);
    fpv_free(token_hash);
    return result;
  }

  if (out_invite) {
    fpv_identity_invite_t* invite =
        (fpv_identity_invite_t*)calloc(1, sizeof(*invite));
    if (!invite) {
      fpv_free(id);
      fpv_free(normalized_email);
      fpv_free(token);
      fpv_free(token_hash);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    invite->id = fpv_strdup(id);
    invite->organization_id = fpv_strdup(organization_id);
    invite->team_id = fpv_strdup(team_id);
    invite->email = normalized_email ? fpv_strdup(normalized_email) : NULL;
    invite->role = role;
    invite->created_at_ms = now_ms;
    invite->expires_at_ms = expiry;
    invite->accepted_at_ms = 0;
    invite->accepted_user_id = NULL;
    invite->created_by_user_id = fpv_strdup(created_by_user_id);
    if (!invite->id || !invite->organization_id ||
        (team_id && !invite->team_id) ||
        (normalized_email && !invite->email) ||
        (created_by_user_id && !invite->created_by_user_id)) {
      fpv_identity_invite_destroy(invite);
      fpv_free(id);
      fpv_free(normalized_email);
      fpv_free(token);
      fpv_free(token_hash);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    *out_invite = invite;
  }

  if (out_token) {
    *out_token = token;
  } else {
    fpv_free(token);
  }
  fpv_free(id);
  fpv_free(normalized_email);
  fpv_free(token_hash);
  return FPV_OK;
}

fpv_result_t fpv_identity_validate_invite(
    fpv_identity_store_t* store,
    const char* token,
    fpv_identity_invite_t** out_invite) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (!store || !store->db || !token) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_invite_t* invite = NULL;
  fpv_result_t result = fpv_identity_fetch_invite_by_hash(
      store,
      token_hash,
      &invite);
  fpv_free(token_hash);
  if (result != FPV_OK) {
    return result;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (invite->accepted_at_ms != 0 ||
      (invite->expires_at_ms != 0 && invite->expires_at_ms < now_ms)) {
    fpv_identity_invite_destroy(invite);
    return FPV_ERR_INVALID_STATE;
  }
  if (out_invite) {
    *out_invite = invite;
  } else {
    fpv_identity_invite_destroy(invite);
  }
  return FPV_OK;
}

fpv_result_t fpv_identity_accept_invite(
    fpv_identity_store_t* store,
    const char* token,
    const char* user_id,
    fpv_identity_invite_t** out_invite,
    fpv_user_role_t** out_role) {
  if (out_invite) {
    *out_invite = NULL;
  }
  if (out_role) {
    *out_role = NULL;
  }
  if (!store || !store->db || !token || !user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_user_t* user = NULL;
  fpv_result_t result = fpv_identity_get_user(store, user_id, &user);
  if (result != FPV_OK || !user) {
    fpv_user_destroy(user);
    return result != FPV_OK ? result : FPV_ERR_NOT_FOUND;
  }
  char* token_hash = fpv_identity_hash_token(token);
  if (!token_hash) {
    fpv_user_destroy(user);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_identity_invite_t* invite = NULL;
  result = fpv_identity_fetch_invite_by_hash(store, token_hash, &invite);
  fpv_free(token_hash);
  if (result != FPV_OK || !invite) {
    fpv_user_destroy(user);
    return result != FPV_OK ? result : FPV_ERR_NOT_FOUND;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (invite->accepted_at_ms != 0 ||
      (invite->expires_at_ms != 0 && invite->expires_at_ms < now_ms)) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_STATE;
  }
  if (invite->email && user->email &&
      strcmp(invite->email, user->email) != 0) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool exists = false;
  const char* role_params[] = {user_id, invite->organization_id, invite->team_id};
  result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_user_roles WHERE user_id = $1 AND organization_id = $2 "
      "AND ((team_id IS NULL AND $3 IS NULL) OR team_id = $3) LIMIT 1;",
      role_params,
      3,
      &exists);
  if (result != FPV_OK) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  if (exists) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_INVALID_STATE;
  }

  char role_buf[8];
  char assigned_buf[32];
  snprintf(role_buf, sizeof(role_buf), "%d", invite->role);
  snprintf(assigned_buf, sizeof(assigned_buf), "%llu",
           (unsigned long long)now_ms);
  const char* insert_params[] = {
      user_id,
      invite->organization_id,
      invite->team_id,
      role_buf,
      assigned_buf};
  const char* update_params[] = {assigned_buf, user_id, invite->id};

  result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_user_roles (user_id, organization_id, team_id, role, "
      "assigned_at_ms) VALUES ($1, $2, $3, $4, $5);",
      insert_params,
      5);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_exec_params(
      store->db,
      "UPDATE fpv_invites SET accepted_at_ms = $1, accepted_user_id = $2 "
      "WHERE id = $3;",
      update_params,
      3);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }
  result = fpv_db_commit(store->db);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return result;
  }

  invite->accepted_at_ms = now_ms;
  fpv_free(invite->accepted_user_id);
  invite->accepted_user_id = fpv_strdup(user_id);
  if (!invite->accepted_user_id) {
    fpv_identity_invite_destroy(invite);
    fpv_user_destroy(user);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (out_role) {
    *out_role = fpv_user_role_create(
        user_id,
        invite->organization_id,
        invite->team_id,
        invite->role,
        now_ms);
    if (!*out_role) {
      fpv_identity_invite_destroy(invite);
      fpv_user_destroy(user);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  if (out_invite) {
    *out_invite = invite;
  } else {
    fpv_identity_invite_destroy(invite);
  }
  fpv_user_destroy(user);
  return FPV_OK;
}

fpv_result_t fpv_identity_link_account(
    fpv_identity_store_t* store,
    const char* organization_id,
    const char* team_id,
    const char* funpay_user_id,
    const char* funpay_username,
    const char* display_name,
    const char* currency,
    fpv_account_t** out_account) {
  if (out_account) {
    *out_account = NULL;
  }
  if (!store || !store->db || !organization_id || !funpay_user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  bool org_exists = false;
  const char* org_params[] = {organization_id};
  fpv_result_t result = fpv_identity_db_exists(
      store,
      "SELECT 1 FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      org_params,
      1,
      &org_exists);
  if (result != FPV_OK) {
    return result;
  }
  if (!org_exists) {
    return FPV_ERR_NOT_FOUND;
  }
  if (team_id) {
    bool team_ok = false;
    const char* team_params[] = {team_id, organization_id};
    result = fpv_identity_db_exists(
        store,
        "SELECT 1 FROM fpv_teams WHERE id = $1 AND organization_id = $2 "
        "LIMIT 1;",
        team_params,
        2,
        &team_ok);
    if (result != FPV_OK) {
      return result;
    }
    if (!team_ok) {
      return FPV_ERR_NOT_FOUND;
    }
  }

  char* account_id = NULL;
  result = fpv_identity_fetch_account_id_for_org(
      store,
      organization_id,
      &account_id);
  bool exists = result == FPV_OK;
  if (result != FPV_OK && result != FPV_ERR_NOT_FOUND) {
    return result;
  }
  if (!exists) {
    account_id = fpv_identity_random_id(16);
    if (!account_id) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  uint64_t now_ms = fpv_time_now_ms();
  char linked_buf[32];
  snprintf(linked_buf, sizeof(linked_buf), "%llu",
           (unsigned long long)now_ms);

  if (exists) {
    const char* params[] = {
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        linked_buf,
        account_id};
    result = fpv_db_exec_params(
        store->db,
        "UPDATE fpv_accounts SET team_id = $1, funpay_user_id = $2, "
        "funpay_username = $3, display_name = $4, currency = $5, active = 1, "
        "linked_at_ms = $6, last_sync_at_ms = $6 WHERE id = $7;",
        params,
        7);
  } else {
    const char* params[] = {
        account_id,
        organization_id,
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        "1",
        linked_buf,
        linked_buf};
    result = fpv_db_exec_params(
        store->db,
        "INSERT INTO fpv_accounts (id, organization_id, team_id, funpay_user_id, "
        "funpay_username, display_name, currency, active, linked_at_ms, "
        "last_sync_at_ms) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10);",
        params,
        10);
  }
  if (result != FPV_OK) {
    fpv_free(account_id);
    return result;
  }

  if (out_account) {
    *out_account = fpv_account_create(
        account_id,
        organization_id,
        team_id,
        funpay_user_id,
        funpay_username,
        display_name,
        currency,
        true,
        now_ms,
        now_ms);
    if (!*out_account) {
      fpv_free(account_id);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }
  fpv_free(account_id);
  return FPV_OK;
}

fpv_result_t fpv_identity_get_user(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_user_t** out_user) {
  if (out_user) {
    *out_user = NULL;
  }
  if (!store || !store->db || !user_id || !out_user) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {user_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, email, display_name, email_verified, active, created_at_ms, "
      "last_login_ms FROM fpv_users WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_user_t* user = fpv_identity_user_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!user) {
    return FPV_ERR_INTERNAL;
  }
  *out_user = user;
  return FPV_OK;
}

fpv_result_t fpv_identity_get_organization(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_organization_t** out_organization) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (!store || !store->db || !organization_id || !out_organization) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, name, timezone, currency, retention_audit_log_days, "
      "retention_price_history_days, retention_competitor_listing_days, "
      "retention_order_history_days, created_at_ms, updated_at_ms "
      "FROM fpv_organizations WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_organization_t* org = fpv_identity_organization_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!org) {
    return FPV_ERR_INTERNAL;
  }
  *out_organization = org;
  return FPV_OK;
}

fpv_result_t fpv_identity_get_team(
    const fpv_identity_store_t* store,
    const char* team_id,
    fpv_team_t** out_team) {
  if (out_team) {
    *out_team = NULL;
  }
  if (!store || !store->db || !team_id || !out_team) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {team_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
      "FROM fpv_teams WHERE id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_team_t* team = fpv_identity_team_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!team) {
    return FPV_ERR_INTERNAL;
  }
  *out_team = team;
  return FPV_OK;
}

fpv_result_t fpv_identity_get_account_for_org(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_account_t** out_account) {
  if (out_account) {
    *out_account = NULL;
  }
  if (!store || !store->db || !organization_id || !out_account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_identity_db_query_single(
      store,
      "SELECT id, organization_id, team_id, funpay_user_id, funpay_username, "
      "display_name, currency, active, linked_at_ms, last_sync_at_ms "
      "FROM fpv_accounts WHERE organization_id = $1 LIMIT 1;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  fpv_account_t* account = fpv_identity_account_from_row(rows->rows[0]);
  fpv_db_result_destroy(rows);
  if (!account) {
    return FPV_ERR_INTERNAL;
  }
  *out_account = account;
  return FPV_OK;
}

fpv_result_t fpv_identity_list_teams(
    const fpv_identity_store_t* store,
    const char* organization_id,
    fpv_team_t*** out_teams,
    size_t* out_count) {
  if (out_teams) {
    *out_teams = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_teams || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(
      store->db,
      "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
      "FROM fpv_teams WHERE organization_id = $1 ORDER BY created_at_ms ASC;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }
  fpv_team_t** list =
      (fpv_team_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_identity_team_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_team_destroy(list[j]);
      }
      fpv_free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }
  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_teams = list;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_identity_resolve_user_context(
    const fpv_identity_store_t* store,
    const char* user_id,
    fpv_organization_t** out_organization,
    fpv_team_t** out_team,
    fpv_role_t* out_role) {
  if (out_organization) {
    *out_organization = NULL;
  }
  if (out_team) {
    *out_team = NULL;
  }
  if (out_role) {
    *out_role = FPV_ROLE_UNKNOWN;
  }
  if (!store || !store->db || !user_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* params[] = {user_id};
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = fpv_db_query(
      store->db,
      "SELECT organization_id, team_id, role FROM fpv_user_roles "
      "WHERE user_id = $1 ORDER BY role ASC, assigned_at_ms ASC;",
      params,
      1,
      &rows);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_NOT_FOUND;
  }
  char** row = rows->rows[0];
  char* org_id = row[0] ? fpv_strdup(row[0]) : NULL;
  char* team_id = row[1] ? fpv_strdup(row[1]) : NULL;
  int role_value = 0;
  bool role_ok = row[2] && fpv_identity_parse_int(row[2], &role_value);
  fpv_db_result_destroy(rows);
  if (!org_id || !role_ok || !fpv_identity_role_valid((fpv_role_t)role_value)) {
    fpv_free(org_id);
    fpv_free(team_id);
    return FPV_ERR_INTERNAL;
  }

  fpv_organization_t* org = NULL;
  result = fpv_identity_get_organization(store, org_id, &org);
  if (result != FPV_OK) {
    fpv_free(org_id);
    fpv_free(team_id);
    return result;
  }

  fpv_team_t* team = NULL;
  if (team_id) {
    result = fpv_identity_get_team(store, team_id, &team);
    if (result != FPV_OK && result != FPV_ERR_NOT_FOUND) {
      fpv_organization_destroy(org);
      fpv_free(org_id);
      fpv_free(team_id);
      return result;
    }
  }
  if (!team) {
    const char* team_params[] = {org_id};
    fpv_db_result_t* team_rows = NULL;
    result = fpv_identity_db_query_single(
        store,
        "SELECT id, organization_id, name, active, created_at_ms, updated_at_ms "
        "FROM fpv_teams WHERE organization_id = $1 ORDER BY created_at_ms ASC "
        "LIMIT 1;",
        team_params,
        1,
        &team_rows);
    if (result == FPV_OK) {
      team = fpv_identity_team_from_row(team_rows->rows[0]);
      fpv_db_result_destroy(team_rows);
    } else if (result != FPV_ERR_NOT_FOUND) {
      fpv_organization_destroy(org);
      fpv_free(org_id);
      fpv_free(team_id);
      return result;
    }
  }

  if (out_organization) {
    *out_organization = org;
  } else {
    fpv_organization_destroy(org);
  }
  if (out_team) {
    *out_team = team;
  } else {
    fpv_team_destroy(team);
  }
  if (out_role) {
    *out_role = (fpv_role_t)role_value;
  }

  fpv_free(org_id);
  fpv_free(team_id);
  return FPV_OK;
}

bool fpv_identity_user_has_role(
    const fpv_identity_store_t* store,
    const char* user_id,
    const char* organization_id,
    const char* team_id,
    fpv_role_t minimum_role) {
  if (!store || !store->db || !user_id || !organization_id) {
    return false;
  }
  fpv_db_result_t* rows = NULL;
  fpv_result_t result = FPV_OK;
  if (team_id) {
    const char* params[] = {user_id, organization_id, team_id};
    result = fpv_db_query(
        store->db,
        "SELECT role, team_id FROM fpv_user_roles WHERE user_id = $1 "
        "AND organization_id = $2 AND (team_id = $3 OR team_id IS NULL);",
        params,
        3,
        &rows);
  } else {
    const char* params[] = {user_id, organization_id};
    result = fpv_db_query(
        store->db,
        "SELECT role, team_id FROM fpv_user_roles WHERE user_id = $1 "
        "AND organization_id = $2 AND team_id IS NULL;",
        params,
        2,
        &rows);
  }
  if (result != FPV_OK) {
    fpv_db_result_destroy(rows);
    return false;
  }
  fpv_role_t best = FPV_ROLE_UNKNOWN;
  for (size_t i = 0; rows && i < rows->row_count; i++) {
    char** row = rows->rows[i];
    int role_value = 0;
    if (!row || !row[0] || !fpv_identity_parse_int(row[0], &role_value)) {
      continue;
    }
    fpv_role_t current = (fpv_role_t)role_value;
    if (!fpv_identity_role_valid(current)) {
      continue;
    }
    if (best == FPV_ROLE_UNKNOWN || current < best) {
      best = current;
    }
  }
  fpv_db_result_destroy(rows);
  return fpv_identity_role_at_least(best, minimum_role);
}


void fpv_identity_invite_destroy(fpv_identity_invite_t* invite) {
  if (!invite) {
    return;
  }
  fpv_free(invite->id);
  fpv_free(invite->organization_id);
  fpv_free(invite->team_id);
  fpv_free(invite->email);
  fpv_free(invite->accepted_user_id);
  fpv_free(invite->created_by_user_id);
  free(invite);
}

void fpv_identity_invite_list_destroy(
    fpv_identity_invite_t** invites,
    size_t count) {
  if (!invites) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_identity_invite_destroy(invites[i]);
  }
  fpv_free(invites);
}
