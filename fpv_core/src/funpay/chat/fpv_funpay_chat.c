#include "funpay/core/fpv_funpay_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fpv_funpay_author {
  uint64_t id;
  char* name;
  char* avatar_url;
  char* badge;
} fpv_funpay_author_t;

fpv_result_t fpv_funpay_parse_chat_bookmarks(
    fpv_funpay_account_t* account,
    const char* html,
    fpv_chat_t*** out_chats,
    size_t* out_count) {
  if (out_chats) {
    *out_chats = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }

  fpv_html_doc_t* doc = fpv_html_parse(html, strlen(html));
  if (!doc || !doc->doc) {
    if (fpv_funpay_debug_messages(account)) {
      fpv_funpay_logf(account, FPV_LOG_WARNING,
                      "Failed to parse chat bookmarks HTML.");
      fpv_funpay_log_html_snippet(account, html);
    }
    fpv_html_destroy(doc);
    return FPV_ERR_PARSE;
  }

  xmlNode* root = xmlDocGetRootElement(doc->doc);
  fpv_html_node_list_t chats =
      fpv_html_find_all_by_class(root, "a", "contact-item");
  if (chats.count == 0) {
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&chats);
    return FPV_OK;
  }

  fpv_chat_t** list =
      (fpv_chat_t**)calloc(chats.count, sizeof(*list));
  if (!list) {
    fpv_html_destroy(doc);
    fpv_html_node_list_destroy(&chats);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t count = 0;
  for (size_t i = 0; i < chats.count; i++) {
    xmlNode* node = chats.nodes[i];
    char* id_attr = fpv_html_node_attr(node, "data-id");
    if (!id_attr) {
      continue;
    }

    xmlNode* msg_node =
        fpv_html_find_first_by_class(node->children, "div", "contact-item-message");
    if (!msg_node) {
      static const char* chat_candidates[] = {
          "contact-item-message",
          "contact-item__message",
          "chat-item-message",
          "chat-item__message",
          "message-text",
          "msg-text"};
      for (size_t c = 0; c < sizeof(chat_candidates) / sizeof(chat_candidates[0]); c++) {
        msg_node = fpv_funpay_find_first_by_class_substr(
            node->children,
            NULL,
            chat_candidates[c]);
        if (msg_node) {
          break;
        }
      }
    }
    if (!msg_node) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Chat bookmark missing message node (chat_id=%s).",
            id_attr ? id_attr : "?");
      }
      fpv_free(id_attr);
      continue;
    }
    char* last_message = fpv_html_node_text(msg_node);
    if (last_message) {
      fpv_funpay_trim(last_message);
      if (!last_message[0]) {
        fpv_free(last_message);
        last_message = NULL;
      }
    }
    if (!last_message) {
      static const char* attr_candidates[] = {
          "data-message",
          "data-text",
          "data-content",
          "data-last-message"};
      for (size_t c = 0;
           c < sizeof(attr_candidates) / sizeof(attr_candidates[0]);
           c++) {
        char* attr_text =
            fpv_funpay_find_first_attr_value(msg_node, NULL, attr_candidates[c]);
        if (attr_text) {
          fpv_funpay_trim(attr_text);
          if (attr_text[0]) {
            last_message = attr_text;
            break;
          }
          fpv_free(attr_text);
        }
      }
    }
    if (!last_message) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Chat bookmark message empty (chat_id=%s).",
            id_attr ? id_attr : "?");
      }
      fpv_free(id_attr);
      continue;
    }
    if (strncmp(last_message, FPV_FUNPAY_BOT_PREFIX, strlen(FPV_FUNPAY_BOT_PREFIX)) == 0) {
      memmove(
          last_message,
          last_message + strlen(FPV_FUNPAY_BOT_PREFIX),
          strlen(last_message) - strlen(FPV_FUNPAY_BOT_PREFIX) + 1);
    }

    xmlNode* time_node =
        fpv_html_find_first_by_class(node->children, "div", "contact-item-time");
    char* time_text = time_node ? fpv_html_node_text(time_node) : NULL;
    if (time_text) {
      fpv_funpay_trim(time_text);
    }

    xmlNode* name_node =
        fpv_html_find_first_by_class(node->children, "div", "media-user-name");
    char* title = name_node ? fpv_html_node_text(name_node) : NULL;
    if (title) {
      fpv_funpay_trim(title);
    }

    bool unread = fpv_html_node_has_class(node, "unread");
    fpv_chat_t* chat = fpv_chat_create(
        id_attr,
        title,
        last_message,
        time_text,
        unread ? 1U : 0U,
        NULL);
    fpv_free(id_attr);
    fpv_free(last_message);
    fpv_free(time_text);
    fpv_free(title);
    if (!chat) {
      continue;
    }
    list[count++] = chat;
    if (account) {
      fpv_funpay_account_store_chat(account, chat);
    }
  }

  fpv_html_destroy(doc);
  fpv_html_node_list_destroy(&chats);
  if (out_chats) {
    *out_chats = list;
  } else {
    for (size_t i = 0; i < count; i++) {
      fpv_chat_destroy(list[i]);
    }
    fpv_free(list);
  }
  if (out_count) {
    *out_count = count;
  }
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_request_chats(
    fpv_funpay_account_t* account,
    fpv_chat_t*** out_chats,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }

  char* tag = fpv_funpay_random_tag();
  if (!tag) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"chat_bookmarks\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false}",
      account->id,
      tag);
  fpv_funpay_sb_append(&objects, "]");

  char* objects_json = fpv_funpay_sb_detach(&objects);
  fpv_free(tag);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "runner/",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      true,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse chat bookmarks response",
        "runner/",
        "POST",
        response.status);
    return FPV_ERR_PARSE;
  }

  const fpv_json_value_t* objects_val =
      fpv_json_object_get(json, "objects");
  size_t count = fpv_json_array_size(objects_val);
  fpv_result_t parse_result = FPV_ERR_PARSE;
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects_val, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type || strcmp(type, "chat_bookmarks") != 0) {
      continue;
    }
    const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
    const fpv_json_value_t* html_val = fpv_json_object_get(data_val, "html");
    const char* html = fpv_json_string(html_val);
    if (!html) {
      continue;
    }
    parse_result = fpv_funpay_parse_chat_bookmarks(
        account,
        html,
        out_chats,
        out_count);
    break;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);
  return parse_result == FPV_ERR_PARSE ? FPV_ERR_PARSE : FPV_OK;
}


static fpv_funpay_author_t* fpv_funpay_author_find(
    fpv_funpay_author_t* authors,
    size_t count,
    uint64_t id) {
  for (size_t i = 0; i < count; i++) {
    if (authors[i].id == id) {
      return &authors[i];
    }
  }
  return NULL;
}


fpv_result_t fpv_funpay_parse_messages(
    fpv_funpay_account_t* account,
    const fpv_json_value_t* messages,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count) {
  if (out_messages) {
    *out_messages = NULL;
  }
  if (out_count) {
    *out_count = 0;
  }
  if (!messages || !fpv_json_is_type(messages, FPV_JSON_ARRAY)) {
    return FPV_ERR_PARSE;
  }

  size_t msg_count = fpv_json_array_size(messages);
  if (msg_count == 0) {
    return FPV_OK;
  }
  fpv_message_t** list =
      (fpv_message_t**)calloc(msg_count, sizeof(*list));
  if (!list) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_author_t* authors = NULL;
  size_t author_count = 0;

  size_t out_idx = 0;
  for (size_t i = 0; i < msg_count; i++) {
    const fpv_json_value_t* msg = fpv_json_array_get(messages, i);
    const fpv_json_value_t* id_val = fpv_json_object_get(msg, "id");
    const fpv_json_value_t* author_val = fpv_json_object_get(msg, "author");
    const fpv_json_value_t* html_val = fpv_json_object_get(msg, "html");
    if (!id_val || !author_val || !html_val) {
      continue;
    }

    uint64_t id_num = 0;
    uint64_t author_id = 0;
    fpv_json_number_to_uint64(id_val, &id_num);
    fpv_json_number_to_uint64(author_val, &author_id);
    const char* html = fpv_json_string(html_val);
    if (!html) {
      continue;
    }

    fpv_html_doc_t* doc = fpv_html_parse(html, strlen(html));
    if (!doc || !doc->doc) {
      if (fpv_funpay_debug_messages(account)) {
        fpv_funpay_logf(
            account,
            FPV_LOG_WARNING,
            "Failed to parse message HTML (id=%" PRIu64 ", chat=%" PRIu64 ").",
            id_num,
            chat_id);
        fpv_funpay_log_html_snippet(account, html);
      }
      fpv_html_destroy(doc);
      continue;
    }

    xmlNode* root = xmlDocGetRootElement(doc->doc);
    fpv_funpay_author_t* author = fpv_funpay_author_find(authors, author_count, author_id);
    if (!author) {
      fpv_funpay_author_t* grown = (fpv_funpay_author_t*)realloc(
          authors,
          (author_count + 1) * sizeof(*grown));
      if (!grown) {
        fpv_html_destroy(doc);
        continue;
      }
      authors = grown;
      author = &authors[author_count];
      author->id = author_id;
      author->name = NULL;
      author->badge = NULL;
      author_count++;
    }

    xmlNode* author_node =
        fpv_html_find_first_by_class(root, "div", "media-user-name");
    if (author_node) {
      xmlNode* badge_node =
          fpv_html_find_first_by_class(author_node->children, "span", NULL);
      if (badge_node && !author->badge) {
        author->badge = fpv_html_node_text(badge_node);
        if (author->badge) {
          fpv_funpay_trim(author->badge);
          if (author->badge[0] == '\0' || strcmp(author->badge, "0") == 0) {
            fpv_free(author->badge);
            author->badge = NULL;
          }
        }
      }
      xmlNode* author_link =
          fpv_html_find_first_by_class(author_node->children, "a", NULL);
      if (author_link && !author->name) {
        author->name = fpv_html_node_text(author_link);
        if (author->name) {
          fpv_funpay_trim(author->name);
        }
      }
    }

    char* message_text = NULL;
    char* image_url = NULL;
    bool by_bot = false;
    const char* text_source = NULL;

    xmlNode* image_node =
        fpv_html_find_first_by_class(root, "a", "chat-img-link");
    if (image_node) {
      image_url = fpv_html_node_attr(image_node, "href");
      if (image_url) {
        text_source = "image";
      }
    } else {
      if (author_id == 0) {
        xmlNode* alert_node =
            fpv_html_find_first_by_class(root, "div", "alert-info");
        message_text = alert_node ? fpv_html_node_text(alert_node) : NULL;
        if (message_text) {
          text_source = "class:alert-info";
        }
      } else {
        xmlNode* text_node =
            fpv_html_find_first_by_class(root, NULL, "chat-msg-text");
        message_text = text_node ? fpv_html_node_text(text_node) : NULL;
        if (message_text) {
          text_source = "class:chat-msg-text";
        }
      }
    }

    if (message_text) {
      fpv_funpay_trim(message_text);
      if (!message_text[0]) {
        fpv_free(message_text);
        message_text = NULL;
      }
    }

    if (!message_text && !image_url) {
      static const char* json_keys[] = {"text", "message", "content", "msg"};
      for (size_t k = 0; k < sizeof(json_keys) / sizeof(json_keys[0]); k++) {
        const fpv_json_value_t* txt_val = fpv_json_object_get(msg, json_keys[k]);
        const char* raw = fpv_json_string(txt_val);
        if (raw && raw[0]) {
          message_text = fpv_strdup(raw);
          if (message_text) {
            fpv_funpay_trim(message_text);
            if (message_text[0]) {
              text_source = json_keys[k];
              break;
            }
            fpv_free(message_text);
            message_text = NULL;
          }
        }
      }
    }

    if (!message_text && !image_url) {
      static const char* candidates[] = {
          "chat-msg-text",
          "chat-message-text",
          "chat-message__text",
          "chat-msg__text",
          "message-text",
          "msg-text",
          "chat-message",
          "chat-msg"};
      for (size_t c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
        xmlNode* text_node =
            fpv_funpay_find_first_by_class_substr(root, NULL, candidates[c]);
        if (text_node) {
          message_text = fpv_html_node_text(text_node);
          if (message_text) {
            fpv_funpay_trim(message_text);
            if (message_text[0]) {
              text_source = candidates[c];
              break;
            }
            fpv_free(message_text);
            message_text = NULL;
          }
        }
      }
    }

    if (!message_text && !image_url) {
      static const char* attr_candidates[] = {
          "data-text",
          "data-message",
          "data-content",
          "data-msg",
          "data-original",
          "data-original-title"};
      for (size_t c = 0;
           c < sizeof(attr_candidates) / sizeof(attr_candidates[0]);
           c++) {
        char* attr_text =
            fpv_funpay_find_first_attr_value(root, NULL, attr_candidates[c]);
        if (attr_text) {
          fpv_funpay_trim(attr_text);
          if (attr_text[0]) {
            message_text = attr_text;
            text_source = attr_candidates[c];
            break;
          }
          fpv_free(attr_text);
        }
      }
    }

    if (!message_text && !image_url) {
      message_text = fpv_html_node_text(root);
      if (message_text) {
        fpv_funpay_trim(message_text);
        if (message_text[0]) {
          text_source = "root-text";
        } else {
          fpv_free(message_text);
          message_text = NULL;
        }
      }
    }

    if (message_text) {
      if (strncmp(message_text, FPV_FUNPAY_BOT_PREFIX, strlen(FPV_FUNPAY_BOT_PREFIX)) == 0) {
        memmove(
            message_text,
            message_text + strlen(FPV_FUNPAY_BOT_PREFIX),
            strlen(message_text) - strlen(FPV_FUNPAY_BOT_PREFIX) + 1);
        by_bot = true;
        fpv_funpay_trim(message_text);
        if (!message_text[0]) {
          fpv_free(message_text);
          message_text = NULL;
        }
      }
    }

    if (fpv_funpay_debug_messages(account)) {
      size_t text_len = message_text ? strlen(message_text) : 0;
      const char* source = text_source ? text_source : (image_url ? "image" : "none");
      char* preview = NULL;
      if (message_text && message_text[0]) {
        preview = fpv_funpay_truncate_utf8(message_text, 120);
      }
      fpv_funpay_logf(
          account,
          (message_text || image_url) ? FPV_LOG_INFO : FPV_LOG_WARNING,
          "Message parse: id=%" PRIu64 " chat=%" PRIu64 " author=%" PRIu64
          " source=%s text_len=%zu image=%s preview=\"%s\"",
          id_num,
          chat_id,
          author_id,
          source,
          text_len,
          image_url ? "yes" : "no",
          preview ? preview : "");
      fpv_free(preview);
      if (!message_text && !image_url) {
        fpv_funpay_log_html_snippet(account, html);
      }
    }

    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%" PRIu64, id_num);
    char chat_buf[32];
    snprintf(chat_buf, sizeof(chat_buf), "%" PRIu64, chat_id);
    char author_buf[32];
    snprintf(author_buf, sizeof(author_buf), "%" PRIu64, author_id);

    fpv_message_direction_t direction =
        (account && account->id == author_id) ? FPV_MESSAGE_OUTBOUND : FPV_MESSAGE_INBOUND;
    const char* sender_name = author ? author->name : NULL;
    if (!sender_name && author_id == 0) {
      sender_name = "FunPay";
    }

    fpv_message_t* message = fpv_message_create(
        id_buf,
        chat_buf,
        chat_name,
        author_buf,
        sender_name,
        message_text,
        image_url,
        author ? author->badge : NULL,
        direction,
        by_bot,
        fpv_time_now_ms());

    fpv_free(message_text);
    fpv_free(image_url);
    fpv_html_destroy(doc);

    if (!message) {
      continue;
    }
    list[out_idx++] = message;
  }

  for (size_t i = 0; i < out_idx; i++) {
    fpv_message_t* message = list[i];
    if (!message || !message->sender_id) {
      continue;
    }
    uint64_t author_id = strtoull(message->sender_id, NULL, 10);
    fpv_funpay_author_t* author =
        fpv_funpay_author_find(authors, author_count, author_id);
    if (author) {
      if (!message->sender_name && author->name) {
        message->sender_name = fpv_strdup(author->name);
      }
      if (!message->badge && author->badge) {
        message->badge = fpv_strdup(author->badge);
      }
    }
    if (!message->sender_name && author_id == 0) {
      message->sender_name = fpv_strdup("FunPay");
    }
  }

  for (size_t i = 0; i < author_count; i++) {
    fpv_free(authors[i].name);
    fpv_free(authors[i].badge);
  }
  fpv_free(authors);

  if (out_messages) {
    *out_messages = list;
  } else {
    for (size_t i = 0; i < out_idx; i++) {
      fpv_message_destroy(list[i]);
    }
    fpv_free(list);
  }
  if (out_count) {
    *out_count = out_idx;
  }
  return FPV_OK;
}


fpv_result_t fpv_funpay_account_get_chat_histories(
    fpv_funpay_account_t* account,
    const fpv_funpay_chat_request_t* chats,
    size_t chat_count,
    fpv_funpay_chat_history_t** out_histories,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!out_histories || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return FPV_ERR_INVALID_STATE;
  }
  if (chat_count == 0) {
    *out_histories = NULL;
    *out_count = 0;
    return FPV_OK;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  for (size_t i = 0; i < chat_count; i++) {
    if (i > 0) {
      fpv_funpay_sb_append(&objects, ",");
    }
    fpv_funpay_sb_append_format(
        &objects,
        "{\"type\":\"chat_node\",\"id\":%" PRIu64 ",\"tag\":\"00000000\","
        "\"data\":{\"node\":%" PRIu64 ",\"last_message\":-1,\"content\":\"\"}}",
        chats[i].chat_id,
        chats[i].chat_id);
  }
  fpv_funpay_sb_append(&objects, "]");
  char* objects_json = fpv_funpay_sb_detach(&objects);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", account->csrf_token};
  char* form = fpv_funpay_form_encode(keys, values, 3);
  fpv_free(objects_json);
  if (!form) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_funpay_http_header_t headers[] = {
      fpv_funpay_header("accept", "*/*"),
      fpv_funpay_header(
          "content-type",
          "application/x-www-form-urlencoded; charset=UTF-8"),
      fpv_funpay_header("x-requested-with", "XMLHttpRequest")};

  fpv_funpay_http_response_t response;
  fpv_result_t result = fpv_funpay_account_request(
      account,
      "POST",
      "runner/",
      headers,
      sizeof(headers) / sizeof(headers[0]),
      form,
      false,
      true,
      &response,
      error);
  fpv_free(form);
  if (result != FPV_OK) {
    return result;
  }

  fpv_json_value_t* json = NULL;
  fpv_json_error_t json_error;
  if (fpv_json_parse(response.body, response.body_size, &json, &json_error) !=
      FPV_OK) {
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_PARSE;
  }

  fpv_funpay_chat_history_t* histories =
      (fpv_funpay_chat_history_t*)calloc(chat_count, sizeof(*histories));
  if (!histories) {
    fpv_json_destroy(json);
    fpv_funpay_http_response_clear(&response);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  size_t history_count = 0;
  const fpv_json_value_t* objects_val =
      fpv_json_object_get(json, "objects");
  size_t objects_count = fpv_json_array_size(objects_val);
  for (size_t i = 0; i < objects_count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects_val, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type || strcmp(type, "chat_node") != 0) {
      continue;
    }
    const fpv_json_value_t* id_val = fpv_json_object_get(obj, "id");
    uint64_t chat_id = 0;
    fpv_json_number_to_uint64(id_val, &chat_id);
    const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
    const fpv_json_value_t* msg_val = fpv_json_object_get(data_val, "messages");
    const char* name = NULL;
    for (size_t c = 0; c < chat_count; c++) {
      if (chats[c].chat_id == chat_id) {
        name = chats[c].chat_name;
        break;
      }
    }

    fpv_message_t** messages = NULL;
    size_t msg_count = 0;
    fpv_result_t parse_result = fpv_funpay_parse_messages(
        account,
        msg_val,
        chat_id,
        name,
        &messages,
        &msg_count);
    if (parse_result != FPV_OK) {
      continue;
    }
    histories[history_count].chat_id = chat_id;
    histories[history_count].chat_name = name ? fpv_strdup(name) : NULL;
    histories[history_count].messages = messages;
    histories[history_count].message_count = msg_count;
    history_count++;
  }

  fpv_json_destroy(json);
  fpv_funpay_http_response_clear(&response);

  *out_histories = histories;
  *out_count = history_count;
  return FPV_OK;
}


void fpv_funpay_chat_history_destroy(
    fpv_funpay_chat_history_t* histories,
    size_t count) {
  if (!histories) {
    return;
  }
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < histories[i].message_count; j++) {
      fpv_message_destroy(histories[i].messages[j]);
    }
    fpv_free(histories[i].messages);
    fpv_free(histories[i].chat_name);
  }
  free(histories);
}


fpv_result_t fpv_funpay_account_get_chat_history(
    fpv_funpay_account_t* account,
    uint64_t chat_id,
    const char* chat_name,
    fpv_message_t*** out_messages,
    size_t* out_count,
    fpv_funpay_error_t* error) {
  if (!out_messages || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  *out_messages = NULL;
  *out_count = 0;
  fpv_funpay_chat_request_t request = {chat_id, chat_name};
  fpv_funpay_chat_history_t* histories = NULL;
  size_t history_count = 0;
  fpv_result_t result = fpv_funpay_account_get_chat_histories(
      account,
      &request,
      1,
      &histories,
      &history_count,
      error);
  if (result != FPV_OK) {
    return result;
  }
  if (history_count == 0) {
    fpv_funpay_chat_history_destroy(histories, history_count);
    return FPV_OK;
  }

  fpv_message_t** messages = histories[0].messages;
  size_t count = histories[0].message_count;
  histories[0].messages = NULL;
  histories[0].message_count = 0;

  fpv_funpay_chat_history_destroy(histories, history_count);

  *out_messages = messages;
  *out_count = count;
  return FPV_OK;
}
