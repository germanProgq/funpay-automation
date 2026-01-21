/* FunPay Vertex Telegram HTTP helpers. */

#include "telegram/core/fpv_telegram_internal.h"


#include <curl/curl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FPV_TG_API_BASE "https://api.telegram.org/bot"

#if defined(FPV_ENABLE_TEST_HOOKS)
static fpv_telegram_http_mock_fn fpv_tg_http_mock = NULL;
static fpv_telegram_http_multipart_mock_fn fpv_tg_http_multipart_mock = NULL;
static void* fpv_tg_http_mock_data = NULL;

void fpv_telegram_set_http_mock(
    fpv_telegram_http_mock_fn request,
    fpv_telegram_http_multipart_mock_fn multipart,
    void* user_data) {
  fpv_tg_http_mock = request;
  fpv_tg_http_multipart_mock = multipart;
  fpv_tg_http_mock_data = user_data;
}

void fpv_telegram_clear_http_mock(void) {
  fpv_tg_http_mock = NULL;
  fpv_tg_http_multipart_mock = NULL;
  fpv_tg_http_mock_data = NULL;
}
#endif

void fpv_tg_http_response_clear(fpv_tg_http_response_t* response) {
  if (!response) {
    return;
  }
  fpv_free(response->body);
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;
}

static size_t fpv_tg_write_callback(
    void* data,
    size_t size,
    size_t nmemb,
    void* userdata) {
  fpv_tg_http_response_t* response = (fpv_tg_http_response_t*)userdata;
  size_t total = size * nmemb;
  if (!response || total == 0) {
    return total;
  }
  char* grown = (char*)realloc(response->body, response->body_size + total + 1);
  if (!grown) {
    return 0;
  }
  response->body = grown;
  memcpy(response->body + response->body_size, data, total);
  response->body_size += total;
  response->body[response->body_size] = '\0';
  return total;
}

fpv_result_t fpv_tg_http_request(
    const char* method,
    const char* url,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_tg_http_response_t* response) {
#if defined(FPV_ENABLE_TEST_HOOKS)
  if (fpv_tg_http_mock) {
    return fpv_tg_http_mock(
        method,
        url,
        content_type,
        body,
        body_size,
        response,
        fpv_tg_http_mock_data);
  }
#endif
  if (!url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return FPV_ERR_INTERNAL;
  }
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fpv_tg_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

  struct curl_slist* headers = NULL;
  if (content_type) {
    char header[128];
    snprintf(header, sizeof(header), "Content-Type: %s", content_type);
    headers = curl_slist_append(headers, header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  if (method && strcmp(method, "POST") == 0) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    if (body && body_size > 0) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
    }
  } else if (method && strcmp(method, "GET") != 0) {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (body && body_size > 0) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_size);
    }
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return FPV_ERR_IO;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return FPV_OK;
}

fpv_result_t fpv_tg_http_request_multipart(
    const char* url,
    curl_mime* mime,
    fpv_tg_http_response_t* response) {
#if defined(FPV_ENABLE_TEST_HOOKS)
  if (fpv_tg_http_multipart_mock) {
    return fpv_tg_http_multipart_mock(
        url,
        mime,
        response,
        fpv_tg_http_mock_data);
  }
#endif
  if (!url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return FPV_ERR_INTERNAL;
  }
  response->body = NULL;
  response->body_size = 0;
  response->status = 0;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fpv_tg_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_easy_cleanup(curl);
    return FPV_ERR_IO;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response->status);
  curl_easy_cleanup(curl);
  return FPV_OK;
}

fpv_result_t fpv_tg_api_request(
    const fpv_telegram_service_t* service,
    const char* method,
    const char* endpoint,
    const char* content_type,
    const char* body,
    size_t body_size,
    fpv_json_value_t** out_root) {
  if (!service || !service->token || !endpoint) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/%s", FPV_TG_API_BASE, service->token, endpoint);

  fpv_tg_http_response_t response;
  fpv_result_t result =
      fpv_tg_http_request(method, url, content_type, body, body_size, &response);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  if (fpv_json_parse(response.body, response.body_size, &json, &error) != FPV_OK) {
    if (service && endpoint) {
      char log_buf[256];
      snprintf(log_buf, sizeof(log_buf),
               "Telegram API parse failed (%s).", endpoint);
      fpv_tg_log((fpv_telegram_service_t*)service, FPV_LOG_WARNING, log_buf);
    }
    fpv_tg_http_response_clear(&response);
    fpv_json_destroy(json);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* ok_val = fpv_json_object_get(json, "ok");
  bool ok = fpv_json_bool(ok_val, false);
  if (!ok) {
    const fpv_json_value_t* desc_val = fpv_json_object_get(json, "description");
    const fpv_json_value_t* code_val = fpv_json_object_get(json, "error_code");
    const char* desc = fpv_json_string(desc_val);
    int64_t code = 0;
    fpv_json_number_to_int64(code_val, &code);
    if (service && endpoint) {
      char log_buf[512];
      snprintf(log_buf, sizeof(log_buf),
               "Telegram API error (%s): %" PRId64 " %s",
               endpoint,
               code,
               desc ? desc : "unknown error");
      fpv_tg_log((fpv_telegram_service_t*)service, FPV_LOG_WARNING, log_buf);
    }
    fpv_json_destroy(json);
    fpv_tg_http_response_clear(&response);
    return FPV_ERR_INVALID_STATE;
  }

  if (out_root) {
    *out_root = json;
  } else {
    fpv_json_destroy(json);
  }
  fpv_tg_http_response_clear(&response);
  return FPV_OK;
}

char* fpv_tg_build_send_body(
    int64_t chat_id,
    const char* text,
    const char* reply_markup) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, text ? text : "");
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"parse_mode\":\"HTML\"",
                       strlen("\",\"parse_mode\":\"HTML\""));
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  return buffer;
}

char* fpv_tg_build_send_body_plain(
    int64_t chat_id,
    const char* text,
    const char* reply_markup) {
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, text ? text : "");
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"",
                       strlen("\""));
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  return buffer;
}

bool fpv_tg_send_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* text,
    const char* reply_markup,
    int* out_message_id) {
  if (out_message_id) {
    *out_message_id = 0;
  }
  if (!service || chat_id == 0 || !text) {
    return false;
  }
  char* safe_text = fpv_tg_make_valid_utf8(text);
  const char* send_text = safe_text ? safe_text : text;
  bool use_html = fpv_tg_html_is_balanced(send_text);
  char* stripped_text = NULL;
  const char* payload_text = send_text;
  if (!use_html) {
    stripped_text = fpv_tg_strip_html_tags(send_text);
    if (stripped_text) {
      payload_text = stripped_text;
    }
  }
  char* body = use_html
      ? fpv_tg_build_send_body(chat_id, payload_text, reply_markup)
      : fpv_tg_build_send_body_plain(chat_id, payload_text, reply_markup);
  if (!body) {
    fpv_free(stripped_text);
    fpv_free(safe_text);
    return false;
  }
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "sendMessage", "application/json",
                         body, strlen(body), &root);
  fpv_free(body);
  if (req != FPV_OK || !root) {
    fpv_json_destroy(root);
    if (use_html) {
      char* fallback_text = fpv_tg_strip_html_tags(send_text);
      const char* plain_text = fallback_text ? fallback_text : send_text;
      char* plain_body =
          fpv_tg_build_send_body_plain(chat_id, plain_text, reply_markup);
      if (!plain_body) {
        fpv_free(fallback_text);
        fpv_free(stripped_text);
        fpv_free(safe_text);
        return false;
      }
      fpv_json_value_t* plain_root = NULL;
      fpv_result_t plain_req =
          fpv_tg_api_request(service, "POST", "sendMessage",
                             "application/json", plain_body,
                             strlen(plain_body), &plain_root);
      fpv_free(plain_body);
      if (plain_req != FPV_OK || !plain_root) {
        fpv_json_destroy(plain_root);
        fpv_free(fallback_text);
        fpv_free(stripped_text);
        fpv_free(safe_text);
        return false;
      }
      if (out_message_id) {
        const fpv_json_value_t* result = fpv_json_object_get(plain_root, "result");
        const fpv_json_value_t* id_val =
            result ? fpv_json_object_get(result, "message_id") : NULL;
        int64_t msg_id = 0;
        if (fpv_json_number_to_int64(id_val, &msg_id)) {
          *out_message_id = (int)msg_id;
        }
      }
      fpv_json_destroy(plain_root);
      fpv_free(fallback_text);
      fpv_free(stripped_text);
      fpv_free(safe_text);
      return true;
    }
    fpv_free(stripped_text);
    fpv_free(safe_text);
    return false;
  }
  if (out_message_id) {
    const fpv_json_value_t* result = fpv_json_object_get(root, "result");
    const fpv_json_value_t* id_val =
        result ? fpv_json_object_get(result, "message_id") : NULL;
    int64_t msg_id = 0;
    if (fpv_json_number_to_int64(id_val, &msg_id)) {
      *out_message_id = (int)msg_id;
    }
  }
  fpv_json_destroy(root);
  fpv_free(stripped_text);
  fpv_free(safe_text);
  return true;
}

bool fpv_tg_edit_message_text(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* text,
    const char* reply_markup) {
  if (!service || chat_id == 0 || message_id == 0 || !text) {
    return false;
  }
  char* safe_text = fpv_tg_make_valid_utf8(text);
  const char* send_text = safe_text ? safe_text : text;
  bool use_html = fpv_tg_html_is_balanced(send_text);
  char* stripped_text = NULL;
  const char* payload_text = send_text;
  if (!use_html) {
    stripped_text = fpv_tg_strip_html_tags(send_text);
    if (stripped_text) {
      payload_text = stripped_text;
    }
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":",
                       strlen(",\"message_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                       strlen(",\"text\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, payload_text);
  if (use_html) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\",\"parse_mode\":\"HTML\"",
                         strlen("\",\"parse_mode\":\"HTML\""));
  } else {
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  }
  if (reply_markup && reply_markup[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                         strlen(",\"reply_markup\":"));
    fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "editMessageText",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  fpv_free(stripped_text);
  fpv_free(safe_text);
  fpv_json_destroy(root);
  return req == FPV_OK;
}

bool fpv_tg_edit_message_reply_markup(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id,
    const char* reply_markup) {
  if (!service || chat_id == 0 || message_id == 0 || !reply_markup) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":",
                       strlen("{\"chat_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":",
                       strlen(",\"message_id\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"reply_markup\":",
                       strlen(",\"reply_markup\":"));
  fpv_tg_buffer_append(&buffer, &length, &capacity, reply_markup, strlen(reply_markup));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "editMessageReplyMarkup",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  fpv_json_destroy(root);
  return req == FPV_OK;
}

void fpv_tg_answer_callback(
    fpv_telegram_service_t* service,
    const char* callback_id,
    const char* text,
    bool show_alert) {
  if (!service || !callback_id) {
    return;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"callback_query_id\":\"",
                       strlen("{\"callback_query_id\":\""));
  fpv_tg_json_escape_append(&buffer, &length, &capacity, callback_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  if (text && text[0]) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"text\":\"",
                         strlen(",\"text\":\""));
    fpv_tg_json_escape_append(&buffer, &length, &capacity, text);
    fpv_tg_buffer_append(&buffer, &length, &capacity, "\"", 1);
  }
  if (show_alert) {
    fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"show_alert\":true",
                         strlen(",\"show_alert\":true"));
  }
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_tg_api_request(service, "POST", "answerCallbackQuery",
                     "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
}

bool fpv_tg_delete_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id) {
  if (!service || chat_id == 0 || message_id == 0) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":", 11);
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":", 14);
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "deleteMessage",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
  return req == FPV_OK;
}

bool fpv_tg_pin_message(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    int message_id) {
  if (!service || chat_id == 0 || message_id == 0) {
    return false;
  }
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chat_buf[32];
  char msg_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  snprintf(msg_buf, sizeof(msg_buf), "%d", message_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"chat_id\":", 11);
  fpv_tg_buffer_append(&buffer, &length, &capacity, chat_buf, strlen(chat_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, ",\"message_id\":", 14);
  fpv_tg_buffer_append(&buffer, &length, &capacity, msg_buf, strlen(msg_buf));
  fpv_tg_buffer_append(&buffer, &length, &capacity, "}", 1);
  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "pinChatMessage",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_json_destroy(root);
  fpv_free(buffer);
  return req == FPV_OK;
}

bool fpv_tg_send_document(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const char* file_path,
    const char* caption,
    const char* reply_markup) {
  if (!service || chat_id == 0 || !file_path) {
    return false;
  }
  char* safe_caption = NULL;
  const char* caption_text = caption;
  char* caption_plain = NULL;
  if (caption && caption[0]) {
    safe_caption = fpv_tg_make_valid_utf8(caption);
    if (safe_caption) {
      caption_text = safe_caption;
    }
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/sendDocument", FPV_TG_API_BASE, service->token);
  CURL* curl = curl_easy_init();
  if (!curl) {
    fpv_free(safe_caption);
    return false;
  }
  curl_mime* mime = curl_mime_init(curl);
  if (!mime) {
    curl_easy_cleanup(curl);
    fpv_free(safe_caption);
    return false;
  }
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "chat_id");
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  curl_mime_data(part, chat_buf, CURL_ZERO_TERMINATED);

  part = curl_mime_addpart(mime);
  curl_mime_name(part, "document");
  curl_mime_filedata(part, file_path);

  if (caption_text && caption_text[0]) {
    bool use_html = fpv_tg_html_is_balanced(caption_text);
    if (!use_html) {
      caption_plain = fpv_tg_strip_html_tags(caption_text);
      if (caption_plain) {
        caption_text = caption_plain;
      }
    }
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption_text, CURL_ZERO_TERMINATED);
    if (use_html) {
      part = curl_mime_addpart(mime);
      curl_mime_name(part, "parse_mode");
      curl_mime_data(part, "HTML", CURL_ZERO_TERMINATED);
    }
  }
  if (reply_markup && reply_markup[0]) {
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "reply_markup");
    curl_mime_data(part, reply_markup, CURL_ZERO_TERMINATED);
  }

  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request_multipart(url, mime, &response);
  curl_mime_free(mime);
  curl_easy_cleanup(curl);
  fpv_tg_http_response_clear(&response);
  fpv_free(caption_plain);
  fpv_free(safe_caption);
  return result == FPV_OK;
}

bool fpv_tg_send_photo(
    fpv_telegram_service_t* service,
    int64_t chat_id,
    const void* data,
    size_t size,
    const char* caption,
    const char* reply_markup,
    int* out_message_id) {
  if (out_message_id) {
    *out_message_id = 0;
  }
  if (!service || chat_id == 0 || !data || size == 0) {
    return false;
  }
  char* safe_caption = NULL;
  const char* caption_text = caption;
  char* caption_plain = NULL;
  if (caption && caption[0]) {
    safe_caption = fpv_tg_make_valid_utf8(caption);
    if (safe_caption) {
      caption_text = safe_caption;
    }
  }
  char url[512];
  snprintf(url, sizeof(url), "%s%s/sendPhoto", FPV_TG_API_BASE, service->token);
  CURL* curl = curl_easy_init();
  if (!curl) {
    fpv_free(safe_caption);
    return false;
  }
  curl_mime* mime = curl_mime_init(curl);
  if (!mime) {
    curl_easy_cleanup(curl);
    fpv_free(safe_caption);
    return false;
  }
  curl_mimepart* part = curl_mime_addpart(mime);
  curl_mime_name(part, "chat_id");
  char chat_buf[32];
  snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, chat_id);
  curl_mime_data(part, chat_buf, CURL_ZERO_TERMINATED);

  part = curl_mime_addpart(mime);
  curl_mime_name(part, "photo");
  curl_mime_data(part, (const char*)data, (curl_off_t)size);
  curl_mime_filename(part, "photo.jpg");

  if (caption_text && caption_text[0]) {
    bool use_html = fpv_tg_html_is_balanced(caption_text);
    if (!use_html) {
      caption_plain = fpv_tg_strip_html_tags(caption_text);
      if (caption_plain) {
        caption_text = caption_plain;
      }
    }
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption_text, CURL_ZERO_TERMINATED);
    if (use_html) {
      part = curl_mime_addpart(mime);
      curl_mime_name(part, "parse_mode");
      curl_mime_data(part, "HTML", CURL_ZERO_TERMINATED);
    }
  }
  if (reply_markup && reply_markup[0]) {
    part = curl_mime_addpart(mime);
    curl_mime_name(part, "reply_markup");
    curl_mime_data(part, reply_markup, CURL_ZERO_TERMINATED);
  }

  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request_multipart(url, mime, &response);
  curl_mime_free(mime);
  curl_easy_cleanup(curl);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return false;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t error;
  if (fpv_json_parse(response.body, response.body_size, &json, &error) == FPV_OK) {
    const fpv_json_value_t* ok_val = fpv_json_object_get(json, "ok");
    if (fpv_json_bool(ok_val, false)) {
      const fpv_json_value_t* res_val = fpv_json_object_get(json, "result");
      const fpv_json_value_t* id_val = fpv_json_object_get(res_val, "message_id");
      int64_t msg_id = 0;
      if (fpv_json_number_to_int64(id_val, &msg_id) && out_message_id) {
        *out_message_id = (int)msg_id;
      }
    }
  }
  fpv_json_destroy(json);
  fpv_tg_http_response_clear(&response);
  fpv_free(caption_plain);
  fpv_free(safe_caption);
  return true;
}

fpv_result_t fpv_tg_get_file_path(
    fpv_telegram_service_t* service,
    const char* file_id,
    char** out_path) {
  if (!service || !file_id || !out_path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_path = NULL;
  char* buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  fpv_tg_buffer_append(&buffer, &length, &capacity, "{\"file_id\":\"", 12);
  fpv_tg_json_escape_append(&buffer, &length, &capacity, file_id);
  fpv_tg_buffer_append(&buffer, &length, &capacity, "\"}", 2);

  fpv_json_value_t* root = NULL;
  fpv_result_t req =
      fpv_tg_api_request(service, "POST", "getFile",
                         "application/json", buffer, strlen(buffer), &root);
  fpv_free(buffer);
  if (req != FPV_OK || !root) {
    fpv_json_destroy(root);
    return req;
  }
  const fpv_json_value_t* result = fpv_json_object_get(root, "result");
  const fpv_json_value_t* path_val =
      result ? fpv_json_object_get(result, "file_path") : NULL;
  const char* path = fpv_json_string(path_val);
  if (path && path[0]) {
    *out_path = fpv_strdup(path);
  }
  fpv_json_destroy(root);
  return *out_path ? FPV_OK : FPV_ERR_NOT_FOUND;
}

fpv_result_t fpv_tg_download_file(
    fpv_telegram_service_t* service,
    const char* file_path,
    void** out_data,
    size_t* out_size) {
  if (!service || !file_path || !out_data || !out_size) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_data = NULL;
  *out_size = 0;
  char url[512];
  snprintf(url, sizeof(url), "https://api.telegram.org/file/bot%s/%s",
           service->token, file_path);
  fpv_tg_http_response_t response;
  fpv_result_t result = fpv_tg_http_request("GET", url, NULL, NULL, 0, &response);
  if (result != FPV_OK) {
    fpv_tg_http_response_clear(&response);
    return result;
  }
  *out_data = response.body;
  *out_size = response.body_size;
  response.body = NULL;
  response.body_size = 0;
  fpv_tg_http_response_clear(&response);
  return FPV_OK;
}

fpv_result_t fpv_tg_download_tg_file(
    fpv_telegram_service_t* service,
    const char* file_id,
    void** out_data,
    size_t* out_size) {
  if (!service || !file_id || !out_data || !out_size) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  char* file_path = NULL;
  fpv_result_t result = fpv_tg_get_file_path(service, file_id, &file_path);
  if (result != FPV_OK) {
    fpv_free(file_path);
    return result;
  }
  result = fpv_tg_download_file(service, file_path, out_data, out_size);
  fpv_free(file_path);
  return result;
}
