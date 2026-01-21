#include "funpay/core/fpv_funpay_internal.h"


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

void fpv_funpay_event_destroy(fpv_funpay_event_t* event) {
  if (!event) {
    return;
  }
  fpv_free(event->runner_tag);
  fpv_chat_destroy(event->chat);
  if (event->chats) {
    for (size_t i = 0; i < event->chat_count; i++) {
      fpv_chat_destroy(event->chats[i]);
    }
    fpv_free(event->chats);
  }
  fpv_message_destroy(event->message);
  fpv_order_destroy(event->order);
  free(event);
}


void fpv_funpay_event_batch_destroy(fpv_funpay_event_batch_t* batch) {
  if (!batch || !batch->events) {
    return;
  }
  for (size_t i = 0; i < batch->count; i++) {
    fpv_funpay_event_destroy(batch->events[i]);
  }
  fpv_free(batch->events);
  batch->events = NULL;
  batch->count = 0;
}


static fpv_funpay_event_t* fpv_funpay_event_create(
    fpv_funpay_event_type_t type,
    const char* tag) {
  fpv_funpay_event_t* event =
      (fpv_funpay_event_t*)calloc(1, sizeof(*event));
  if (!event) {
    return NULL;
  }
  event->type = type;
  event->timestamp_ms = fpv_time_now_ms();
  event->runner_tag = tag ? fpv_strdup(tag) : NULL;
  return event;
}


static fpv_funpay_order_state_t* fpv_funpay_runner_find_order(
    fpv_funpay_runner_t* runner,
    const char* order_id) {
  if (!runner || !order_id) {
    return NULL;
  }
  for (size_t i = 0; i < runner->order_count; i++) {
    if (runner->orders[i].id &&
        strcmp(runner->orders[i].id, order_id) == 0) {
      return &runner->orders[i];
    }
  }
  return NULL;
}


static fpv_result_t fpv_funpay_runner_update_order(
    fpv_funpay_runner_t* runner,
    const fpv_order_t* order) {
  fpv_funpay_order_state_t* state =
      fpv_funpay_runner_find_order(runner, order->id);
  if (state) {
    state->status = order->status;
    return FPV_OK;
  }

  fpv_funpay_order_state_t* grown = (fpv_funpay_order_state_t*)realloc(
      runner->orders,
      (runner->order_count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  runner->orders = grown;
  runner->orders[runner->order_count].id = fpv_strdup(order->id);
  runner->orders[runner->order_count].status = order->status;
  if (!runner->orders[runner->order_count].id) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  runner->order_count++;
  return FPV_OK;
}


static fpv_funpay_last_message_t* fpv_funpay_runner_find_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->last_message_count; i++) {
    if (runner->last_messages[i].chat_id == chat_id) {
      return &runner->last_messages[i];
    }
  }
  return NULL;
}


static fpv_result_t fpv_funpay_runner_set_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* text,
    const char* time_text) {
  fpv_funpay_last_message_t* state =
      fpv_funpay_runner_find_last_message(runner, chat_id);
  if (!state) {
    fpv_funpay_last_message_t* grown = (fpv_funpay_last_message_t*)realloc(
        runner->last_messages,
        (runner->last_message_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->last_messages = grown;
    state = &runner->last_messages[runner->last_message_count++];
    state->chat_id = chat_id;
    state->text = NULL;
    state->time_text = NULL;
  }
  fpv_free(state->text);
  fpv_free(state->time_text);
  state->text = text ? fpv_strdup(text) : NULL;
  state->time_text = time_text ? fpv_strdup(time_text) : NULL;
  if ((text && !state->text) || (time_text && !state->time_text)) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return FPV_OK;
}


static fpv_funpay_last_message_id_t* fpv_funpay_runner_find_last_message_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->last_message_id_count; i++) {
    if (runner->last_message_ids[i].chat_id == chat_id) {
      return &runner->last_message_ids[i];
    }
  }
  return NULL;
}


static fpv_result_t fpv_funpay_runner_set_last_message_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  fpv_funpay_last_message_id_t* state =
      fpv_funpay_runner_find_last_message_id(runner, chat_id);
  if (!state) {
    fpv_funpay_last_message_id_t* grown =
        (fpv_funpay_last_message_id_t*)realloc(
            runner->last_message_ids,
            (runner->last_message_id_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->last_message_ids = grown;
    state = &runner->last_message_ids[runner->last_message_id_count++];
    state->chat_id = chat_id;
  }
  state->message_id = message_id;
  return FPV_OK;
}


static fpv_funpay_init_message_t* fpv_funpay_runner_find_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->init_message_count; i++) {
    if (runner->init_messages[i].chat_id == chat_id) {
      return &runner->init_messages[i];
    }
  }
  return NULL;
}


static fpv_result_t fpv_funpay_runner_set_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* text) {
  fpv_funpay_init_message_t* state =
      fpv_funpay_runner_find_init_message(runner, chat_id);
  if (!state) {
    fpv_funpay_init_message_t* grown = (fpv_funpay_init_message_t*)realloc(
        runner->init_messages,
        (runner->init_message_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->init_messages = grown;
    state = &runner->init_messages[runner->init_message_count++];
    state->chat_id = chat_id;
    state->text = NULL;
  }
  fpv_free(state->text);
  state->text = text ? fpv_strdup(text) : NULL;
  return (text && !state->text) ? FPV_ERR_OUT_OF_MEMORY : FPV_OK;
}


static void fpv_funpay_runner_clear_init_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->init_message_count; i++) {
    if (runner->init_messages[i].chat_id == chat_id) {
      fpv_free(runner->init_messages[i].text);
      runner->init_messages[i] = runner->init_messages[runner->init_message_count - 1];
      runner->init_message_count--;
      return;
    }
  }
}


static fpv_funpay_bot_ids_t* fpv_funpay_runner_find_bot_ids(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id) {
  for (size_t i = 0; i < runner->bot_id_count; i++) {
    if (runner->bot_ids[i].chat_id == chat_id) {
      return &runner->bot_ids[i];
    }
  }
  return NULL;
}


static fpv_result_t fpv_funpay_runner_add_bot_id(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  fpv_funpay_bot_ids_t* state =
      fpv_funpay_runner_find_bot_ids(runner, chat_id);
  if (!state) {
    fpv_funpay_bot_ids_t* grown = (fpv_funpay_bot_ids_t*)realloc(
        runner->bot_ids,
        (runner->bot_id_count + 1) * sizeof(*grown));
    if (!grown) {
      return FPV_ERR_OUT_OF_MEMORY;
    }
    runner->bot_ids = grown;
    state = &runner->bot_ids[runner->bot_id_count++];
    state->chat_id = chat_id;
    state->ids = NULL;
    state->count = 0;
  }
  uint64_t* ids = (uint64_t*)realloc(
      state->ids,
      (state->count + 1) * sizeof(*ids));
  if (!ids) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  state->ids = ids;
  state->ids[state->count++] = message_id;
  return FPV_OK;
}


static bool fpv_funpay_bot_id_contains(
    const fpv_funpay_bot_ids_t* state,
    uint64_t message_id) {
  if (!state) {
    return false;
  }
  for (size_t i = 0; i < state->count; i++) {
    if (state->ids[i] == message_id) {
      return true;
    }
  }
  return false;
}


static void fpv_funpay_bot_id_cleanup(
    fpv_funpay_bot_ids_t* state,
    uint64_t last_seen_id) {
  if (!state) {
    return;
  }
  size_t write = 0;
  for (size_t i = 0; i < state->count; i++) {
    if (state->ids[i] > last_seen_id) {
      state->ids[write++] = state->ids[i];
    }
  }
  state->count = write;
  if (state->count == 0) {
    fpv_free(state->ids);
    state->ids = NULL;
  }
}


fpv_funpay_runner_t* fpv_funpay_runner_create(
    fpv_funpay_account_t* account,
    const fpv_funpay_runner_config_t* config,
    fpv_funpay_error_t* error) {
  if (!account || !account->initiated) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_ACCOUNT_NOT_INITIATED,
        "Account is not initiated",
        NULL,
        NULL,
        0);
    return NULL;
  }
  if (account->runner) {
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_RUNNER_ALREADY_ATTACHED,
        "Runner already attached",
        NULL,
        NULL,
        0);
    return NULL;
  }

  fpv_funpay_runner_t* runner =
      (fpv_funpay_runner_t*)calloc(1, sizeof(*runner));
  if (!runner) {
    return NULL;
  }

  runner->account = account;
  runner->make_msg_requests = config ? !config->disable_message_requests : true;
  runner->make_order_requests = config ? !config->disable_order_requests : true;
  runner->first_request = true;
  runner->last_msg_tag = fpv_funpay_random_tag();
  runner->last_order_tag = fpv_funpay_random_tag();

  if (!runner->last_msg_tag || !runner->last_order_tag) {
    fpv_funpay_runner_destroy(runner);
    return NULL;
  }

  account->runner = runner;
  return runner;
}


void fpv_funpay_runner_destroy(fpv_funpay_runner_t* runner) {
  if (!runner) {
    return;
  }
  if (runner->account && runner->account->runner == runner) {
    runner->account->runner = NULL;
  }
  fpv_free(runner->last_msg_tag);
  fpv_free(runner->last_order_tag);
  for (size_t i = 0; i < runner->order_count; i++) {
    fpv_free(runner->orders[i].id);
  }
  fpv_free(runner->orders);
  for (size_t i = 0; i < runner->last_message_count; i++) {
    fpv_free(runner->last_messages[i].text);
    fpv_free(runner->last_messages[i].time_text);
  }
  fpv_free(runner->last_messages);
  fpv_free(runner->last_message_ids);
  for (size_t i = 0; i < runner->init_message_count; i++) {
    fpv_free(runner->init_messages[i].text);
  }
  fpv_free(runner->init_messages);
  for (size_t i = 0; i < runner->bot_id_count; i++) {
    fpv_free(runner->bot_ids[i].ids);
  }
  fpv_free(runner->bot_ids);
  free(runner);
}


fpv_result_t fpv_funpay_runner_mark_by_bot(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    uint64_t message_id) {
  if (!runner) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  return fpv_funpay_runner_add_bot_id(runner, chat_id, message_id);
}


fpv_result_t fpv_funpay_runner_update_last_message(
    fpv_funpay_runner_t* runner,
    uint64_t chat_id,
    const char* message_text,
    const char* message_time) {
  if (!runner) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* text = message_text ? message_text : FPV_FUNPAY_IMAGE_TEXT;
  char* truncated = fpv_funpay_truncate_utf8(text, 250);
  if (!truncated) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  fpv_result_t result = fpv_funpay_runner_set_last_message(
      runner,
      chat_id,
      truncated,
      message_time);
  fpv_free(truncated);
  return result;
}


void fpv_funpay_runner_set_message_requests(
    fpv_funpay_runner_t* runner,
    bool enabled) {
  if (!runner) {
    return;
  }
  runner->make_msg_requests = enabled;
  fpv_free(runner->last_message_ids);
  runner->last_message_ids = NULL;
  runner->last_message_id_count = 0;
}


static fpv_result_t fpv_funpay_runner_get_updates(
    fpv_funpay_runner_t* runner,
    fpv_json_value_t** out_json,
    fpv_funpay_error_t* error) {
  if (!runner || !out_json) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_string_builder_t objects;
  fpv_funpay_sb_reset(&objects);
  fpv_funpay_sb_append(&objects, "[");
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"orders_counters\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false},",
      runner->account->id,
      runner->last_order_tag);
  fpv_funpay_sb_append_format(
      &objects,
      "{\"type\":\"chat_bookmarks\",\"id\":%" PRIu64 ",\"tag\":\"%s\",\"data\":false}",
      runner->account->id,
      runner->last_msg_tag);
  fpv_funpay_sb_append(&objects, "]");
  char* objects_json = fpv_funpay_sb_detach(&objects);
  if (!objects_json) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  const char* keys[] = {"objects", "request", "csrf_token"};
  const char* values[] = {objects_json, "false", runner->account->csrf_token};
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
      runner->account,
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
  result = fpv_json_parse(response.body, response.body_size, &json, &json_error);
  fpv_funpay_http_response_clear(&response);
  if (result != FPV_OK) {
    fpv_json_destroy(json);
    fpv_funpay_error_set(
        error,
        FPV_FUNPAY_ERR_PARSE,
        "Failed to parse updates JSON",
        "runner/",
        "POST",
        0);
    return FPV_ERR_PARSE;
  }

  *out_json = json;
  return FPV_OK;
}


static fpv_result_t fpv_funpay_event_list_append(
    fpv_funpay_event_t*** events,
    size_t* count,
    fpv_funpay_event_t* event) {
  fpv_funpay_event_t** grown = (fpv_funpay_event_t**)realloc(
      *events,
      (*count + 1) * sizeof(**events));
  if (!grown) {
    fpv_funpay_event_destroy(event);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  *events = grown;
  (*events)[(*count)++] = event;
  return FPV_OK;
}


static fpv_result_t fpv_funpay_runner_parse_chat_updates(
    fpv_funpay_runner_t* runner,
    const fpv_json_value_t* obj,
    fpv_funpay_event_t*** events,
    size_t* event_count) {
  const fpv_json_value_t* tag_val = fpv_json_object_get(obj, "tag");
  const char* tag = fpv_json_string(tag_val);
  if (tag) {
    fpv_free(runner->last_msg_tag);
    runner->last_msg_tag = fpv_strdup(tag);
  }

  const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
  const fpv_json_value_t* html_val = fpv_json_object_get(data_val, "html");
  const char* html = fpv_json_string(html_val);
  if (!html) {
    return FPV_ERR_PARSE;
  }

  fpv_chat_t** parsed_chats = NULL;
  size_t parsed_count = 0;
  fpv_result_t parse_result = fpv_funpay_parse_chat_bookmarks(
      runner->account,
      html,
      &parsed_chats,
      &parsed_count);
  if (parse_result != FPV_OK) {
    return parse_result;
  }

  fpv_chat_t** lcmc_chats = NULL;
  size_t lcmc_count = 0;

  for (size_t i = 0; i < parsed_count; i++) {
    fpv_chat_t* chat = parsed_chats[i];
    uint64_t chat_id = chat->id ? strtoull(chat->id, NULL, 10) : 0;
    fpv_funpay_last_message_t* last =
        fpv_funpay_runner_find_last_message(runner, chat_id);
    bool skip = false;
    if (last && last->text && chat->last_message_text &&
        strcmp(last->text, chat->last_message_text) == 0) {
      if (last->time_text && chat->last_message_time) {
        if (!fpv_funpay_time_is_tag(chat->last_message_time) ||
            strcmp(last->time_text, chat->last_message_time) == 0) {
          skip = true;
        }
      } else {
        skip = true;
      }
    }

    if (skip) {
      fpv_chat_destroy(chat);
      continue;
    }

    fpv_funpay_runner_set_last_message(
        runner,
        chat_id,
        chat->last_message_text,
        chat->last_message_time);

    if (runner->first_request) {
      fpv_funpay_event_t* event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_INITIAL_CHAT, runner->last_msg_tag);
      if (!event) {
        fpv_chat_destroy(chat);
        continue;
      }
      event->chat = chat;
      fpv_funpay_event_list_append(events, event_count, event);
      fpv_funpay_runner_set_init_message(
          runner,
          chat_id,
          chat->last_message_text);
    } else {
      fpv_chat_t** grown = (fpv_chat_t**)realloc(
          lcmc_chats,
          (lcmc_count + 1) * sizeof(*grown));
      if (!grown) {
        fpv_chat_destroy(chat);
        continue;
      }
      lcmc_chats = grown;
      lcmc_chats[lcmc_count++] = chat;
    }
  }

  if (lcmc_count > 0) {
    if (runner->account->chats.count == 0) {
      fpv_funpay_account_request_chats(
          runner->account,
          NULL,
          NULL,
          NULL);
    }
    fpv_funpay_event_t* list_event =
        fpv_funpay_event_create(FPV_FUNPAY_EVENT_CHATS_LIST_CHANGED, runner->last_msg_tag);
    if (list_event) {
      list_event->chats =
          fpv_funpay_account_clone_chats(runner->account, &list_event->chat_count);
      fpv_funpay_event_list_append(events, event_count, list_event);
    }
  }

  if (!runner->make_msg_requests) {
    for (size_t i = 0; i < lcmc_count; i++) {
      fpv_funpay_event_t* event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED, runner->last_msg_tag);
      if (!event) {
        fpv_chat_destroy(lcmc_chats[i]);
        continue;
      }
      event->chat = lcmc_chats[i];
      fpv_funpay_event_list_append(events, event_count, event);
    }
    fpv_free(lcmc_chats);
    fpv_free(parsed_chats);
    return FPV_OK;
  }

  size_t index = 0;
  while (index < lcmc_count) {
    size_t batch_size = lcmc_count - index;
    if (batch_size > 10) {
      batch_size = 10;
    }
    fpv_funpay_chat_request_t* batch =
        (fpv_funpay_chat_request_t*)calloc(batch_size, sizeof(*batch));
    if (!batch) {
      break;
    }
    for (size_t i = 0; i < batch_size; i++) {
      fpv_chat_t* chat = lcmc_chats[index + i];
      batch[i].chat_id = chat->id ? strtoull(chat->id, NULL, 10) : 0;
      batch[i].chat_name = chat->title;
    }

    fpv_funpay_chat_history_t* histories = NULL;
    size_t history_count = 0;
    fpv_result_t hist_result = fpv_funpay_account_get_chat_histories(
        runner->account,
        batch,
        batch_size,
        &histories,
        &history_count,
        NULL);

    for (size_t i = 0; i < batch_size; i++) {
      fpv_chat_t* chat = lcmc_chats[index + i];
      fpv_funpay_event_t* lcmc_event =
          fpv_funpay_event_create(FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED, runner->last_msg_tag);
      if (lcmc_event) {
        lcmc_event->chat = chat;
        fpv_funpay_event_list_append(events, event_count, lcmc_event);
      } else {
        fpv_chat_destroy(chat);
      }

      if (hist_result != FPV_OK) {
        continue;
      }

      fpv_funpay_chat_history_t* history = NULL;
      for (size_t h = 0; h < history_count; h++) {
        if (histories[h].chat_id == batch[i].chat_id) {
          history = &histories[h];
          break;
        }
      }
      if (!history || history->message_count == 0) {
        continue;
      }

      fpv_funpay_last_message_id_t* last_id =
          fpv_funpay_runner_find_last_message_id(runner, history->chat_id);
      size_t filtered_count = 0;
      fpv_message_t** filtered =
          (fpv_message_t**)calloc(history->message_count, sizeof(*filtered));
      if (!filtered) {
        continue;
      }

      for (size_t m = 0; m < history->message_count; m++) {
        fpv_message_t* msg = history->messages[m];
        uint64_t msg_id = msg->id ? strtoull(msg->id, NULL, 10) : 0;
        if (last_id && msg_id <= last_id->message_id) {
          continue;
        }
        filtered[filtered_count++] = msg;
      }

      if (filtered_count == 0) {
        fpv_free(filtered);
        continue;
      }

      fpv_funpay_bot_ids_t* bot_state =
          fpv_funpay_runner_find_bot_ids(runner, history->chat_id);
      if (bot_state) {
        for (size_t m = 0; m < filtered_count; m++) {
          fpv_message_t* msg = filtered[m];
          uint64_t msg_id = msg->id ? strtoull(msg->id, NULL, 10) : 0;
          if (fpv_funpay_bot_id_contains(bot_state, msg_id)) {
            msg->by_bot = true;
          }
        }
      }

      if (!last_id) {
        fpv_funpay_init_message_t* init =
            fpv_funpay_runner_find_init_message(runner, history->chat_id);
        if (init && init->text) {
          size_t temp_count = 0;
          fpv_message_t** temp =
              (fpv_message_t**)calloc(filtered_count, sizeof(*temp));
          if (temp) {
            for (size_t m = filtered_count; m-- > 0;) {
              fpv_message_t* msg = filtered[m];
              const char* text = msg->text;
              if (!text && msg->image_url) {
                text = FPV_FUNPAY_IMAGE_TEXT;
              }
              if (text && strcmp(text, init->text) == 0) {
                break;
              }
              temp[temp_count++] = msg;
            }
            if (temp_count > 0) {
              fpv_free(filtered);
              filtered = NULL;
              filtered_count = 0;
              filtered = (fpv_message_t**)calloc(temp_count, sizeof(*filtered));
              if (filtered) {
                for (size_t m = 0; m < temp_count; m++) {
                  filtered[m] = temp[temp_count - 1 - m];
                }
                filtered_count = temp_count;
              }
            }
            fpv_free(temp);
          }
          fpv_funpay_runner_clear_init_message(runner, history->chat_id);
        } else {
          fpv_message_t* last_msg = filtered[filtered_count - 1];
          fpv_free(filtered);
          filtered = (fpv_message_t**)calloc(1, sizeof(*filtered));
          if (filtered) {
            filtered[0] = last_msg;
            filtered_count = 1;
          } else {
            filtered_count = 0;
          }
        }
      }

      if (filtered_count == 0) {
        fpv_free(filtered);
        continue;
      }

      fpv_message_t** dedup =
          (fpv_message_t**)calloc(filtered_count, sizeof(*dedup));
      size_t dedup_count = 0;
      if (dedup) {
        dedup[dedup_count++] = filtered[0];
        for (size_t m = 1; m < filtered_count; m++) {
          fpv_message_t* prev = filtered[m - 1];
          fpv_message_t* current = filtered[m];
          const char* prev_text = prev->text ? prev->text : "";
          const char* curr_text = current->text ? current->text : "";
          if (strcmp(prev_text, curr_text) != 0 ||
              strcmp(prev->sender_id ? prev->sender_id : "",
                     current->sender_id ? current->sender_id : "") != 0) {
            dedup[dedup_count++] = current;
          }
        }
      }

      uint64_t last_seen = 0;
      fpv_message_t* last_message = dedup ? dedup[dedup_count - 1] : filtered[filtered_count - 1];
      if (last_message && last_message->id) {
        last_seen = strtoull(last_message->id, NULL, 10);
      }
      fpv_funpay_runner_set_last_message_id(
          runner,
          history->chat_id,
          last_seen);
      if (bot_state) {
        fpv_funpay_bot_id_cleanup(bot_state, last_seen);
      }

      fpv_message_t** final_list = dedup ? dedup : filtered;
      size_t final_count = dedup ? dedup_count : filtered_count;
      for (size_t m = 0; m < final_count; m++) {
        fpv_funpay_event_t* msg_event =
            fpv_funpay_event_create(FPV_FUNPAY_EVENT_NEW_MESSAGE, runner->last_msg_tag);
        if (!msg_event) {
          continue;
        }
        fpv_message_t* cloned = fpv_message_clone(final_list[m]);
        if (!cloned) {
          fpv_funpay_event_destroy(msg_event);
          continue;
        }
        msg_event->message = cloned;
        fpv_funpay_event_list_append(events, event_count, msg_event);
      }
      fpv_free(dedup);
      fpv_free(filtered);
    }

    fpv_free(batch);
    fpv_funpay_chat_history_destroy(histories, history_count);
    index += batch_size;
  }

  fpv_free(lcmc_chats);
  fpv_free(parsed_chats);
  return FPV_OK;
}


static fpv_result_t fpv_funpay_runner_parse_order_updates(
    fpv_funpay_runner_t* runner,
    const fpv_json_value_t* obj,
    fpv_funpay_event_t*** events,
    size_t* event_count) {
  const fpv_json_value_t* tag_val = fpv_json_object_get(obj, "tag");
  const char* tag = fpv_json_string(tag_val);
  if (tag) {
    fpv_free(runner->last_order_tag);
    runner->last_order_tag = fpv_strdup(tag);
  }

  const fpv_json_value_t* data_val = fpv_json_object_get(obj, "data");
  if (!runner->first_request) {
    const fpv_json_value_t* buyer_val = fpv_json_object_get(data_val, "buyer");
    const fpv_json_value_t* seller_val = fpv_json_object_get(data_val, "seller");
    uint64_t buyer = 0;
    uint64_t seller = 0;
    fpv_json_number_to_uint64(buyer_val, &buyer);
    fpv_json_number_to_uint64(seller_val, &seller);

    fpv_funpay_event_t* list_event =
        fpv_funpay_event_create(FPV_FUNPAY_EVENT_ORDERS_LIST_CHANGED, runner->last_order_tag);
    if (list_event) {
      list_event->purchases = (uint32_t)buyer;
      list_event->sales = (uint32_t)seller;
      fpv_funpay_event_list_append(events, event_count, list_event);
    }
  }

  if (!runner->make_order_requests) {
    return FPV_OK;
  }

  fpv_order_t** orders = NULL;
  size_t order_count = 0;
  fpv_result_t result = fpv_funpay_account_get_orders(
      runner->account,
      &orders,
      &order_count,
      NULL);
  if (result != FPV_OK) {
    return result;
  }

  for (size_t i = 0; i < order_count; i++) {
    fpv_order_t* order = orders[i];
    fpv_funpay_order_state_t* state =
        fpv_funpay_runner_find_order(runner, order->id);
    if (!state) {
      fpv_order_status_t status = order->status;
      fpv_funpay_event_t* event = NULL;
      if (runner->first_request) {
        event = fpv_funpay_event_create(FPV_FUNPAY_EVENT_INITIAL_ORDER, runner->last_order_tag);
      } else {
        event = fpv_funpay_event_create(FPV_FUNPAY_EVENT_NEW_ORDER, runner->last_order_tag);
      }
      if (event) {
        event->order = order;
        fpv_funpay_event_list_append(events, event_count, event);
      }
      fpv_funpay_runner_update_order(runner, order);
      if (!runner->first_request && status == FPV_ORDER_DELIVERED) {
        fpv_funpay_event_t* status_event =
            fpv_funpay_event_create(
                FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED,
                runner->last_order_tag);
        if (status_event) {
          status_event->order = fpv_order_clone(order);
          fpv_funpay_event_list_append(events, event_count, status_event);
        }
      }
      if (!event) {
        fpv_order_destroy(order);
      }
      continue;
    }

    if (state->status != order->status) {
      fpv_funpay_event_t* status_event =
          fpv_funpay_event_create(
              FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED,
              runner->last_order_tag);
      if (status_event) {
        status_event->order = order;
        fpv_funpay_event_list_append(events, event_count, status_event);
      }
      fpv_funpay_runner_update_order(runner, order);
      if (!status_event) {
        fpv_order_destroy(order);
      }
    } else {
      fpv_order_destroy(order);
    }
  }

  fpv_free(orders);
  return FPV_OK;
}


fpv_result_t fpv_funpay_runner_poll(
    fpv_funpay_runner_t* runner,
    fpv_funpay_event_batch_t* batch,
    fpv_funpay_error_t* error) {
  if (!runner || !batch) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  batch->events = NULL;
  batch->count = 0;

  fpv_json_value_t* updates = NULL;
  fpv_result_t result = fpv_funpay_runner_get_updates(
      runner,
      &updates,
      error);
  if (result != FPV_OK) {
    return result;
  }

  fpv_funpay_event_t** events = NULL;
  size_t event_count = 0;

  const fpv_json_value_t* objects = fpv_json_object_get(updates, "objects");
  size_t count = fpv_json_array_size(objects);
  for (size_t i = 0; i < count; i++) {
    const fpv_json_value_t* obj = fpv_json_array_get(objects, i);
    const fpv_json_value_t* type_val = fpv_json_object_get(obj, "type");
    const char* type = fpv_json_string(type_val);
    if (!type) {
      continue;
    }
    if (strcmp(type, "chat_bookmarks") == 0) {
      fpv_funpay_runner_parse_chat_updates(
          runner,
          obj,
          &events,
          &event_count);
    } else if (strcmp(type, "orders_counters") == 0) {
      fpv_funpay_runner_parse_order_updates(
          runner,
          obj,
          &events,
          &event_count);
    }
  }

  runner->first_request = false;
  fpv_json_destroy(updates);

  batch->events = events;
  batch->count = event_count;
  return FPV_OK;
}
