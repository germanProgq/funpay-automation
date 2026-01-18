/* FunPay Vertex chat storage implementation. */

#include "fpv_core/fpv_chat_store.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_db.h"
#include "fpv_platform.h"
#include "fpv_time.h"

struct fpv_chat_store {
  fpv_db_t* db;
  fpv_mutex_t mutex;
};

static void fpv_chat_store_lock(const fpv_chat_store_t* store) {
  if (!store) {
    return;
  }
  fpv_mutex_lock((fpv_mutex_t*)&store->mutex);
}

static void fpv_chat_store_unlock(const fpv_chat_store_t* store) {
  if (!store) {
    return;
  }
  fpv_mutex_unlock((fpv_mutex_t*)&store->mutex);
}

static bool fpv_chat_store_parse_int(const char* text, int* out) {
  if (!text || !out) {
    return false;
  }
  char* end = NULL;
  long value = strtol(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (int)value;
  return true;
}

static bool fpv_chat_store_parse_uint32(const char* text, uint32_t* out) {
  if (!text || !out) {
    return false;
  }
  char* end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  if (!end || *end != '\0' || value > UINT32_MAX) {
    return false;
  }
  *out = (uint32_t)value;
  return true;
}

static bool fpv_chat_store_parse_uint64(const char* text, uint64_t* out) {
  if (!text || !out) {
    return false;
  }
  char* end = NULL;
  unsigned long long value = strtoull(text, &end, 10);
  if (!end || *end != '\0') {
    return false;
  }
  *out = (uint64_t)value;
  return true;
}

static int fpv_chat_store_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A' + 'a';
  }
  return value;
}

static int fpv_chat_store_ascii_strcasecmp(const char* left, const char* right) {
  if (!left || !right) {
    return left == right ? 0 : left ? 1 : -1;
  }
  for (size_t index = 0; left[index] && right[index]; index++) {
    int a = fpv_chat_store_ascii_tolower((unsigned char)left[index]);
    int b = fpv_chat_store_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
  }
  return fpv_chat_store_ascii_tolower((unsigned char)*left) -
         fpv_chat_store_ascii_tolower((unsigned char)*right);
}

static bool fpv_chat_store_parse_bool(const char* text, bool* out) {
  if (!text || !out) {
    return false;
  }
  if (strcmp(text, "1") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(text, "0") == 0) {
    *out = false;
    return true;
  }
  if (fpv_chat_store_ascii_strcasecmp(text, "true") == 0 ||
      fpv_chat_store_ascii_strcasecmp(text, "t") == 0) {
    *out = true;
    return true;
  }
  if (fpv_chat_store_ascii_strcasecmp(text, "false") == 0 ||
      fpv_chat_store_ascii_strcasecmp(text, "f") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static fpv_chat_t* fpv_chat_store_chat_from_row(char** row) {
  if (!row || !row[0]) {
    return NULL;
  }
  uint32_t unread = 0;
  if (!fpv_chat_store_parse_uint32(row[4], &unread)) {
    return NULL;
  }
  return fpv_chat_create(
      row[0],
      row[1],
      row[2],
      row[3],
      unread,
      row[5]);
}

static fpv_message_t* fpv_chat_store_message_from_row(char** row) {
  if (!row || !row[0] || !row[1]) {
    return NULL;
  }
  int direction_value = 0;
  bool by_bot = false;
  uint64_t created_at = 0;
  if (!fpv_chat_store_parse_int(row[7], &direction_value)) {
    return NULL;
  }
  if (direction_value < FPV_MESSAGE_INBOUND ||
      direction_value > FPV_MESSAGE_OUTBOUND) {
    return NULL;
  }
  if (!fpv_chat_store_parse_bool(row[8], &by_bot)) {
    return NULL;
  }
  if (!fpv_chat_store_parse_uint64(row[9], &created_at)) {
    return NULL;
  }

  return fpv_message_create(
      row[0],
      row[1],
      NULL,
      row[2],
      row[3],
      row[4],
      row[5],
      row[6],
      (fpv_message_direction_t)direction_value,
      by_bot,
      created_at);
}

static fpv_result_t fpv_chat_store_upsert_message_impl(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_message_t* message) {
  if (!store || !store->db || !organization_id || !message ||
      !message->id || !message->chat_id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  uint64_t created_at = message->created_at_ms;
  if (created_at == 0) {
    created_at = fpv_time_now_ms();
  }

  char direction_buf[8];
  char bot_buf[8];
  char created_buf[32];
  snprintf(direction_buf, sizeof(direction_buf), "%d", message->direction);
  snprintf(bot_buf, sizeof(bot_buf), "%d", message->by_bot ? 1 : 0);
  snprintf(created_buf, sizeof(created_buf), "%" PRIu64, created_at);

  const char* params[] = {
      organization_id,
      message->chat_id,
      message->id,
      message->sender_id,
      message->sender_name,
      message->text,
      message->image_url,
      message->badge,
      direction_buf,
      bot_buf,
      created_buf};

  return fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_messages (organization_id, chat_id, message_id, "
      "sender_id, sender_name, message_text, image_url, badge, direction, "
      "by_bot, created_at_ms) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, "
      "$10, $11) ON CONFLICT(organization_id, chat_id, message_id) DO UPDATE "
      "SET sender_id = excluded.sender_id, sender_name = excluded.sender_name, "
      "message_text = excluded.message_text, image_url = excluded.image_url, "
      "badge = excluded.badge, direction = excluded.direction, "
      "by_bot = excluded.by_bot, created_at_ms = excluded.created_at_ms;",
      params,
      11);
}

fpv_chat_store_t* fpv_chat_store_open(
    const char* data_dir,
    const char* db_url,
    fpv_result_t* out_result) {
  if (out_result) {
    *out_result = FPV_OK;
  }

  const char* resolved = db_url && db_url[0] ? db_url : getenv("FPV_DB_URL");
  fpv_db_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = resolved;
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

  fpv_chat_store_t* store =
      (fpv_chat_store_t*)calloc(1, sizeof(*store));
  if (!store) {
    if (out_result) {
      *out_result = FPV_ERR_OUT_OF_MEMORY;
    }
    fpv_db_close(db);
    return NULL;
  }
  if (!fpv_mutex_init(&store->mutex)) {
    if (out_result) {
      *out_result = FPV_ERR_INVALID_STATE;
    }
    fpv_db_close(db);
    free(store);
    return NULL;
  }
  store->db = db;
  return store;
}

void fpv_chat_store_destroy(fpv_chat_store_t* store) {
  if (!store) {
    return;
  }
  fpv_mutex_destroy(&store->mutex);
  fpv_db_close(store->db);
  free(store);
}

fpv_result_t fpv_chat_store_upsert_chat(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_chat_t* chat,
    uint64_t now_ms) {
  if (!store || !store->db || !organization_id || !chat || !chat->id) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  uint64_t updated_at = now_ms ? now_ms : fpv_time_now_ms();
  char unread_buf[16];
  char updated_buf[32];
  snprintf(unread_buf, sizeof(unread_buf), "%u", chat->unread_count);
  snprintf(updated_buf, sizeof(updated_buf), "%" PRIu64, updated_at);

  const char* params[] = {
      organization_id,
      chat->id,
      chat->title,
      chat->last_message_text,
      chat->last_message_time,
      chat->last_message_id,
      unread_buf,
      updated_buf};

  fpv_chat_store_lock(store);
  fpv_result_t result = fpv_db_exec_params(
      store->db,
      "INSERT INTO fpv_chats (organization_id, chat_id, title, "
      "last_message_text, last_message_time, last_message_id, unread_count, "
      "updated_at_ms) VALUES ($1, $2, $3, $4, $5, $6, $7, $8) ON "
      "CONFLICT(organization_id, chat_id) DO UPDATE SET title = excluded.title, "
      "last_message_text = excluded.last_message_text, "
      "last_message_time = excluded.last_message_time, "
      "last_message_id = excluded.last_message_id, "
      "unread_count = excluded.unread_count, "
      "updated_at_ms = excluded.updated_at_ms;",
      params,
      8);
  fpv_chat_store_unlock(store);
  return result;
}

fpv_result_t fpv_chat_store_upsert_message(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_message_t* message) {
  fpv_chat_store_lock(store);
  fpv_result_t result =
      fpv_chat_store_upsert_message_impl(store, organization_id, message);
  fpv_chat_store_unlock(store);
  return result;
}

fpv_result_t fpv_chat_store_upsert_messages(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_message_t* const* messages,
    size_t message_count) {
  if (!store || !store->db || !organization_id || !messages) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (message_count == 0) {
    return FPV_OK;
  }

  fpv_chat_store_lock(store);
  fpv_result_t result = fpv_db_begin(store->db);
  if (result != FPV_OK) {
    fpv_chat_store_unlock(store);
    return result;
  }

  for (size_t i = 0; i < message_count; i++) {
    result = fpv_chat_store_upsert_message_impl(
        store,
        organization_id,
        messages[i]);
    if (result != FPV_OK) {
      fpv_db_rollback(store->db);
      fpv_chat_store_unlock(store);
      return result;
    }
  }

  result = fpv_db_commit(store->db);
  if (result != FPV_OK) {
    fpv_db_rollback(store->db);
  }
  fpv_chat_store_unlock(store);
  return result;
}

fpv_result_t fpv_chat_store_load_chats(
    const fpv_chat_store_t* store,
    const char* organization_id,
    fpv_chat_t*** out_chats,
    size_t* out_count) {
  if (out_chats) {
    *out_chats = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !out_chats || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  const char* params[] = {organization_id};
  fpv_db_result_t* rows = NULL;
  fpv_chat_store_lock(store);
  fpv_result_t result = fpv_db_query(
      store->db,
      "SELECT chat_id, title, last_message_text, last_message_time, "
      "unread_count, last_message_id FROM fpv_chats WHERE organization_id = $1 "
      "ORDER BY updated_at_ms DESC;",
      params,
      1,
      &rows);
  fpv_chat_store_unlock(store);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }

  fpv_chat_t** list =
      (fpv_chat_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_chat_store_chat_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_chat_destroy(list[j]);
      }
      free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }

  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_chats = list;
  *out_count = row_count;
  return FPV_OK;
}

fpv_result_t fpv_chat_store_load_messages(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const char* chat_id,
    size_t limit,
    fpv_message_t*** out_messages,
    size_t* out_count) {
  if (out_messages) {
    *out_messages = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!store || !store->db || !organization_id || !chat_id || !out_messages ||
      !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (limit == 0) {
    return FPV_OK;
  }

  char limit_buf[16];
  snprintf(limit_buf, sizeof(limit_buf), "%zu", limit);
  const char* params[] = {organization_id, chat_id, limit_buf};
  fpv_db_result_t* rows = NULL;
  fpv_chat_store_lock(store);
  fpv_result_t result = fpv_db_query(
      store->db,
      "SELECT message_id, chat_id, sender_id, sender_name, message_text, "
      "image_url, badge, direction, by_bot, created_at_ms FROM ("
      "SELECT message_id, chat_id, sender_id, sender_name, message_text, "
      "image_url, badge, direction, by_bot, created_at_ms FROM fpv_messages "
      "WHERE organization_id = $1 AND chat_id = $2 ORDER BY created_at_ms DESC "
      "LIMIT $3) AS recent ORDER BY created_at_ms ASC;",
      params,
      3,
      &rows);
  fpv_chat_store_unlock(store);
  if (result != FPV_OK) {
    return result;
  }
  if (!rows || rows->row_count == 0) {
    fpv_db_result_destroy(rows);
    return FPV_OK;
  }

  fpv_message_t** list =
      (fpv_message_t**)calloc(rows->row_count, sizeof(*list));
  if (!list) {
    fpv_db_result_destroy(rows);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < rows->row_count; i++) {
    list[i] = fpv_chat_store_message_from_row(rows->rows[i]);
    if (!list[i]) {
      for (size_t j = 0; j < i; j++) {
        fpv_message_destroy(list[j]);
      }
      free(list);
      fpv_db_result_destroy(rows);
      return FPV_ERR_INTERNAL;
    }
  }

  size_t row_count = rows->row_count;
  fpv_db_result_destroy(rows);
  *out_messages = list;
  *out_count = row_count;
  return FPV_OK;
}
