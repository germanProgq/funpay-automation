/* FunPay Vertex identity cryptography helpers. */

#include "identity/core/fpv_identity_internal.h"


#include <ctype.h>
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

void fpv_secure_zero(void* data, size_t len) {
  volatile unsigned char* ptr = (volatile unsigned char*)data;
  while (len--) {
    *ptr++ = 0;
  }
}

bool fpv_random_bytes(void* data, size_t len) {
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

char* fpv_hex_encode(const uint8_t* data, size_t len) {
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

bool fpv_hex_decode(
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
typedef struct fpv_sha256_ctx {
  uint32_t state[8];
  uint64_t bitcount;
  uint8_t buffer[64];
  size_t buffer_len;
} fpv_sha256_ctx_t;

uint32_t fpv_sha256_rotr(uint32_t value, uint32_t shift) {
  return (value >> shift) | (value << (32 - shift));
}

uint32_t fpv_sha256_load_be(const uint8_t* data) {
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         ((uint32_t)data[3]);
}

void fpv_sha256_store_be(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value >> 24);
  out[1] = (uint8_t)(value >> 16);
  out[2] = (uint8_t)(value >> 8);
  out[3] = (uint8_t)(value);
}

const uint32_t fpv_sha256_k[64] = {
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

void fpv_sha256_transform(uint32_t state[8], const uint8_t block[64]) {
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

void fpv_sha256_init(fpv_sha256_ctx_t* ctx) {
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

void fpv_sha256_update(
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

void fpv_sha256_final(fpv_sha256_ctx_t* ctx, uint8_t out[32]) {
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

void fpv_sha256_digest(
    const uint8_t* data,
    size_t len,
    uint8_t out[32]) {
  fpv_sha256_ctx_t ctx;
  fpv_sha256_init(&ctx);
  fpv_sha256_update(&ctx, data, len);
  fpv_sha256_final(&ctx, out);
}

void fpv_hmac_sha256(
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

bool fpv_pbkdf2_sha256(
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

bool fpv_secure_equal(const uint8_t* left, const uint8_t* right, size_t len) {
  if (!left || !right) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= (uint8_t)(left[i] ^ right[i]);
  }
  return diff == 0;
}

fpv_data_retention_policy_t fpv_identity_default_retention(void) {
  fpv_data_retention_policy_t policy;
  policy.audit_log_days = 365;
  policy.price_history_days = 365;
  policy.competitor_listing_days = 90;
  policy.order_history_days = 365;
  return policy;
}

char* fpv_identity_random_id(size_t bytes) {
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

char* fpv_identity_hash_password(
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

char* fpv_identity_hash_token(const char* token) {
  uint8_t digest[32];
  fpv_sha256_digest((const uint8_t*)token, strlen(token), digest);
  char* hex = fpv_hex_encode(digest, sizeof(digest));
  fpv_secure_zero(digest, sizeof(digest));
  return hex;
}
