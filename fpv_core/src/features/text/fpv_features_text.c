#include "features/runtime/fpv_features_internal.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char* fpv_replace_all(
    const char* text,
    const char* key,
    const char* value) {
  if (!text || !key || !key[0]) {
    return text ? fpv_strdup(text) : NULL;
  }
  const char* pos = strstr(text, key);
  if (!pos) {
    return fpv_strdup(text);
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const char* cursor = text;
  size_t key_len = strlen(key);
  size_t value_len = value ? strlen(value) : 0;
  while ((pos = strstr(cursor, key)) != NULL) {
    size_t prefix_len = (size_t)(pos - cursor);
    fpv_buffer_append(&buffer, &length, &capacity, cursor, prefix_len);
    if (value) {
      fpv_buffer_append(&buffer, &length, &capacity, value, value_len);
    }
    cursor = pos + key_len;
  }
  if (*cursor) {
    fpv_buffer_append(&buffer, &length, &capacity, cursor, strlen(cursor));
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}


static void fpv_format_date_parts(
    struct tm* tm_value,
    char* date,
    size_t date_len,
    char* time_short,
    size_t time_short_len,
    char* time_full,
    size_t time_full_len,
    char* date_text,
    size_t date_text_len,
    char* full_date_text,
    size_t full_date_text_len) {
  static const char* months[] = {
      "\xD1\x8F\xD0\xBD\xD0\xB2\xD0\xB0\xD1\x80\xD1\x8F",
      "\xD1\x84\xD0\xB5\xD0\xB2\xD1\x80\xD0\xB0\xD0\xBB\xD1\x8F",
      "\xD0\xBC\xD0\xB0\xD1\x80\xD1\x82\xD0\xB0",
      "\xD0\xB0\xD0\xBF\xD1\x80\xD0\xB5\xD0\xBB\xD1\x8F",
      "\xD0\xBC\xD0\xB0\xD1\x8F",
      "\xD0\xB8\xD1\x8E\xD0\xBD\xD1\x8F",
      "\xD0\xB8\xD1\x8E\xD0\xBB\xD1\x8F",
      "\xD0\xB0\xD0\xB2\xD0\xB3\xD1\x83\xD1\x81\xD1\x82\xD0\xB0",
      "\xD1\x81\xD0\xB5\xD0\xBD\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xBE\xD0\xBA\xD1\x82\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xBD\xD0\xBE\xD1\x8F\xD0\xB1\xD1\x80\xD1\x8F",
      "\xD0\xB4\xD0\xB5\xD0\xBA\xD0\xB0\xD0\xB1\xD1\x80\xD1\x8F"};

  const char* month = "";
  if (tm_value->tm_mon >= 0 && tm_value->tm_mon < 12) {
    month = months[tm_value->tm_mon];
  }

  snprintf(
      date,
      date_len,
      "%02d.%02d.%04d",
      tm_value->tm_mday,
      tm_value->tm_mon + 1,
      tm_value->tm_year + 1900);
  snprintf(
      time_short,
      time_short_len,
      "%02d:%02d",
      tm_value->tm_hour,
      tm_value->tm_min);
  snprintf(
      time_full,
      time_full_len,
      "%02d:%02d:%02d",
      tm_value->tm_hour,
      tm_value->tm_min,
      tm_value->tm_sec);
  snprintf(
      date_text,
      date_text_len,
      "%d %s",
      tm_value->tm_mday,
      month);
  snprintf(
      full_date_text,
      full_date_text_len,
      "%d %s %d \xD0\xB3\xD0\xBE\xD0\xB4\xD0\xB0",
      tm_value->tm_mday,
      month,
      tm_value->tm_year + 1900);
}


char* fpv_format_message_text(
    const fpv_message_t* message,
    const fpv_chat_t* chat,
    const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username = NULL;
  const char* chat_id = NULL;
  const char* message_text = NULL;

  if (message) {
    username = message->sender_name ? message->sender_name : message->chat_name;
    chat_id = message->chat_id;
    if (message->text) {
      message_text = message->text;
    } else if (message->image_url) {
      message_text = message->image_url;
    }
  } else if (chat) {
    username = chat->title;
    chat_id = chat->id;
    message_text = chat->last_message_text;
  }

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username ? username : "");
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$message_text", message_text ? message_text : "");
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$chat_id", chat_id ? chat_id : "");
  fpv_free(output);
  output = temp;
  return output;
}


char* fpv_format_order_text(const fpv_order_t* order, const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username = order && order->buyer_username ? order->buyer_username : "";
  const char* order_title = order && order->title ? order->title : "";
  const char* order_id = order && order->id ? order->id : "";

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_desc", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_title", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_id", order_id);
  fpv_free(output);
  output = temp;
  return output;
}


char* fpv_format_order_detail_text(
    const fpv_funpay_order_detail_t* detail,
    const char* text) {
  time_t now = time(NULL);
  struct tm tm_value;
#if defined(_WIN32)
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif

  char date[16];
  char time_short[16];
  char time_full[16];
  char date_text[64];
  char full_date_text[80];
  fpv_format_date_parts(
      &tm_value,
      date,
      sizeof(date),
      time_short,
      sizeof(time_short),
      time_full,
      sizeof(time_full),
      date_text,
      sizeof(date_text),
      full_date_text,
      sizeof(full_date_text));

  const char* username =
      detail && detail->buyer_username ? detail->buyer_username : "";
  const char* order_title =
      detail && detail->title ? detail->title : "";
  const char* order_id = detail && detail->id ? detail->id : "";

  char* output = fpv_strdup(text ? text : "");
  char* temp = NULL;

  temp = fpv_replace_all(output, "$full_date_text", full_date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date_text", date_text);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$date", date);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$time", time_short);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$full_time", time_full);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$username", username);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_desc", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_title", order_title);
  fpv_free(output);
  output = temp;
  temp = fpv_replace_all(output, "$order_id", order_id);
  fpv_free(output);
  output = temp;
  return output;
}


static char* fpv_normalize_text_lines(const char* text) {
  if (!text) {
    return fpv_strdup("");
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  const char* line_start = text;
  for (const char* ptr = text;; ptr++) {
    if (*ptr == '\n' || *ptr == '\0') {
      const char* end = ptr;
      while (end > line_start && isspace((unsigned char)end[-1]) &&
             end[-1] != '\n') {
        end--;
      }
      const char* start = line_start;
      while (start < end && isspace((unsigned char)*start)) {
        start++;
      }
      if (length > 0) {
        fpv_buffer_append_char(&buffer, &length, &capacity, '\n');
      }
      if (end > start) {
        fpv_buffer_append(
            &buffer,
            &length,
            &capacity,
            start,
            (size_t)(end - start));
      }
      line_start = ptr + 1;
      if (*ptr == '\0') {
        break;
      }
    }
  }
  if (!buffer) {
    buffer = fpv_strdup("");
  }
  return buffer;
}


static char* fpv_replace_blank_lines(const char* text) {
  char* result = fpv_strdup(text ? text : "");
  if (!result) {
    return NULL;
  }
  while (strstr(result, "\n\n") != NULL) {
    char* replaced = fpv_replace_all(result, "\n\n", "\n[a][/a]\n");
    fpv_free(result);
    result = replaced;
    if (!result) {
      return NULL;
    }
  }
  return result;
}


static bool fpv_split_text_lines(
    const char* text,
    fpv_message_entity_t** out_entities,
    size_t* out_count) {
  char* copy = fpv_strdup(text ? text : "");
  if (!copy) {
    return false;
  }
  size_t line_count = 0;
  for (char* ptr = copy; *ptr; ptr++) {
    if (*ptr == '\n') {
      line_count++;
    }
  }
  line_count++;

  char** lines = (char**)calloc(line_count, sizeof(*lines));
  if (!lines) {
    fpv_free(copy);
    return false;
  }
  size_t count = 0;
  char* token = strtok(copy, "\n");
  while (token) {
    lines[count++] = token;
    token = strtok(NULL, "\n");
  }

  size_t index = 0;
  while (index < count) {
    size_t chunk_end = index + 20;
    if (chunk_end > count) {
      chunk_end = count;
    }
    char* buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    for (size_t i = index; i < chunk_end; i++) {
      if (i > index) {
        fpv_buffer_append_char(&buffer, &length, &capacity, '\n');
      }
      fpv_buffer_append(
          &buffer,
          &length,
          &capacity,
          lines[i],
          strlen(lines[i]));
    }
    if (buffer) {
      char* trimmed = fpv_trim_copy(buffer);
      bool keep = trimmed && trimmed[0] != '\0' &&
          strcmp(trimmed, "[a][/a]") != 0;
      fpv_free(trimmed);
      if (keep) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(buffer);
          fpv_free(lines);
          fpv_free(copy);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_TEXT;
        entity->text = buffer;
      } else {
        fpv_free(buffer);
      }
    }
    index = chunk_end;
  }

  fpv_free(lines);
  fpv_free(copy);
  return true;
}


bool fpv_parse_message_entities(
    const char* text,
    fpv_message_entity_t** out_entities,
    size_t* out_count) {
  if (!out_entities || !out_count) {
    return false;
  }
  *out_entities = NULL;
  *out_count = 0;

  char* normalized = fpv_normalize_text_lines(text ? text : "");
  if (!normalized) {
    return false;
  }
  char* expanded = fpv_replace_blank_lines(normalized);
  fpv_free(normalized);
  if (!expanded) {
    return false;
  }

  const char* ptr = expanded;
  while (*ptr) {
    const char* next = strstr(ptr, "$photo=");
    const char* next_sleep = strstr(ptr, "$sleep=");
    const char* next_new = strstr(ptr, "$new");
    const char* marker = NULL;
    if (next && (!marker || next < marker)) {
      marker = next;
    }
    if (next_sleep && (!marker || next_sleep < marker)) {
      marker = next_sleep;
    }
    if (next_new && (!marker || next_new < marker)) {
      marker = next_new;
    }

    if (!marker) {
      fpv_split_text_lines(ptr, out_entities, out_count);
      break;
    }

    if (marker > ptr) {
      char* segment = fpv_strdup_n(ptr, (size_t)(marker - ptr));
      if (!segment) {
        fpv_free(expanded);
        return false;
      }
      fpv_split_text_lines(segment, out_entities, out_count);
      fpv_free(segment);
    }

    if (marker == next) {
      const char* id_start = marker + strlen("$photo=");
      uint64_t id = 0;
      while (*id_start && isdigit((unsigned char)*id_start)) {
        id = id * 10 + (uint64_t)(*id_start - '0');
        id_start++;
      }
      if (id > 0) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(expanded);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_IMAGE;
        entity->image_id = id;
      }
      ptr = id_start;
      continue;
    }

    if (marker == next_sleep) {
      const char* value_start = marker + strlen("$sleep=");
      char* end = NULL;
      double value = strtod(value_start, &end);
      if (end && end > value_start) {
        fpv_message_entity_t* grown = (fpv_message_entity_t*)realloc(
            *out_entities,
            (*out_count + 1) * sizeof(**out_entities));
        if (!grown) {
          fpv_free(expanded);
          return false;
        }
        *out_entities = grown;
        fpv_message_entity_t* entity = &(*out_entities)[(*out_count)++];
        memset(entity, 0, sizeof(*entity));
        entity->type = FPV_ENTITY_SLEEP;
        entity->sleep_seconds = value;
        ptr = end;
        continue;
      }
      ptr = marker + 1;
      continue;
    }

    if (marker == next_new) {
      ptr = marker + strlen("$new");
      continue;
    }
  }

  fpv_free(expanded);
  return true;
}


void fpv_free_message_entities(
    fpv_message_entity_t* entities,
    size_t count) {
  if (!entities) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    fpv_free(entities[i].text);
  }
  fpv_free(entities);
}
