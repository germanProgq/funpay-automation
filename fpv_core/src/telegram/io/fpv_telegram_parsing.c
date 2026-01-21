/* FunPay Vertex Telegram message parsing helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <stdio.h>
#include <string.h>
#include <time.h>

void fpv_tg_message_clear(fpv_tg_message_t* message) {
  if (!message) {
    return;
  }
  fpv_free(message->chat_type);
  fpv_free(message->chat_username);
  fpv_free(message->from_username);
  fpv_free(message->text);
  fpv_free(message->document_file_id);
  fpv_free(message->document_file_name);
  fpv_free(message->photo_file_id);
  memset(message, 0, sizeof(*message));
}

void fpv_tg_callback_clear(fpv_tg_callback_t* cb) {
  if (!cb) {
    return;
  }
  fpv_free(cb->id);
  fpv_free(cb->data);
  fpv_free(cb->from_username);
  fpv_free(cb->chat_username);
  memset(cb, 0, sizeof(*cb));
}

bool fpv_tg_parse_message(const fpv_json_value_t* value, fpv_tg_message_t* out) {
  if (!value || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const fpv_json_value_t* msg_id = fpv_json_object_get(value, "message_id");
  int64_t mid = 0;
  if (!fpv_json_number_to_int64(msg_id, &mid)) {
    return false;
  }
  out->message_id = (int)mid;
  const fpv_json_value_t* chat = fpv_json_object_get(value, "chat");
  const fpv_json_value_t* chat_id = fpv_json_object_get(chat, "id");
  int64_t cid = 0;
  if (!fpv_json_number_to_int64(chat_id, &cid)) {
    return false;
  }
  out->chat_id = cid;
  const fpv_json_value_t* chat_type = fpv_json_object_get(chat, "type");
  out->chat_type = fpv_strdup(fpv_json_string(chat_type));
  const fpv_json_value_t* chat_username = fpv_json_object_get(chat, "username");
  const char* chat_user = fpv_json_string(chat_username);
  if (chat_user && chat_user[0]) {
    out->chat_username = fpv_strdup(chat_user);
  }
  const fpv_json_value_t* from = fpv_json_object_get(value, "from");
  const fpv_json_value_t* from_id = fpv_json_object_get(from, "id");
  int64_t uid = 0;
  fpv_json_number_to_int64(from_id, &uid);
  out->from_id = uid;
  const fpv_json_value_t* from_username = fpv_json_object_get(from, "username");
  const char* uname = fpv_json_string(from_username);
  if (uname && uname[0]) {
    out->from_username = fpv_strdup(uname);
  }
  const fpv_json_value_t* text = fpv_json_object_get(value, "text");
  const char* text_val = fpv_json_string(text);
  if (text_val && text_val[0]) {
    out->text = fpv_strdup(text_val);
  }
  const fpv_json_value_t* reply = fpv_json_object_get(value, "reply_to_message");
  if (reply) {
    const fpv_json_value_t* topic = fpv_json_object_get(reply, "forum_topic_created");
    out->reply_topic_created = fpv_json_bool(topic, false);
  }

  const fpv_json_value_t* document = fpv_json_object_get(value, "document");
  if (document) {
    const char* file_id = fpv_json_string(fpv_json_object_get(document, "file_id"));
    const char* file_name = fpv_json_string(fpv_json_object_get(document, "file_name"));
    int64_t file_size = 0;
    fpv_json_number_to_int64(fpv_json_object_get(document, "file_size"), &file_size);
    if (file_id && file_id[0]) {
      out->document_file_id = fpv_strdup(file_id);
      out->has_document = true;
    }
    if (file_name && file_name[0]) {
      out->document_file_name = fpv_strdup(file_name);
    }
    out->document_file_size = file_size;
  }

  const fpv_json_value_t* photo = fpv_json_object_get(value, "photo");
  if (photo && fpv_json_is_type(photo, FPV_JSON_ARRAY)) {
    size_t count = fpv_json_array_size(photo);
    if (count > 0) {
      const fpv_json_value_t* last = fpv_json_array_get(photo, count - 1);
      const char* file_id = fpv_json_string(fpv_json_object_get(last, "file_id"));
      int64_t file_size = 0;
      fpv_json_number_to_int64(fpv_json_object_get(last, "file_size"), &file_size);
      if (file_id && file_id[0]) {
        out->photo_file_id = fpv_strdup(file_id);
        out->photo_file_size = file_size;
        out->has_photo = true;
      }
    }
  }
  return true;
}

bool fpv_tg_parse_callback(const fpv_json_value_t* value, fpv_tg_callback_t* out) {
  if (!value || !out) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char* id = fpv_json_string(fpv_json_object_get(value, "id"));
  const char* data = fpv_json_string(fpv_json_object_get(value, "data"));
  if (!id || !data) {
    return false;
  }
  out->id = fpv_strdup(id);
  out->data = fpv_strdup(data);
  const fpv_json_value_t* from = fpv_json_object_get(value, "from");
  int64_t uid = 0;
  fpv_json_number_to_int64(fpv_json_object_get(from, "id"), &uid);
  out->from_id = uid;
  const char* uname = fpv_json_string(fpv_json_object_get(from, "username"));
  if (uname && uname[0]) {
    out->from_username = fpv_strdup(uname);
  }
  const fpv_json_value_t* message = fpv_json_object_get(value, "message");
  if (message) {
    int64_t chat_id = 0;
    fpv_json_number_to_int64(
        fpv_json_object_get(fpv_json_object_get(message, "chat"), "id"),
        &chat_id);
    out->chat_id = chat_id;
    int64_t msg_id = 0;
    fpv_json_number_to_int64(fpv_json_object_get(message, "message_id"), &msg_id);
    out->message_id = (int)msg_id;
    const char* chat_username =
        fpv_json_string(fpv_json_object_get(fpv_json_object_get(message, "chat"), "username"));
    if (chat_username && chat_username[0]) {
      out->chat_username = fpv_strdup(chat_username);
    }
  }
  return true;
}

bool fpv_tg_format_time_hms(uint64_t timestamp_ms, char* buffer, size_t buffer_len) {
  if (!buffer || buffer_len == 0) {
    return false;
  }
  time_t seconds = (time_t)(timestamp_ms / 1000ULL);
  struct tm tm_value;
#if defined(_WIN32)
  if (localtime_s(&tm_value, &seconds) != 0) {
    return false;
  }
#else
  if (!localtime_r(&seconds, &tm_value)) {
    return false;
  }
#endif
  int written = snprintf(
      buffer,
      buffer_len,
      "%02d:%02d:%02d",
      tm_value.tm_hour,
      tm_value.tm_min,
      tm_value.tm_sec);
  return written > 0 && (size_t)written < buffer_len;
}
