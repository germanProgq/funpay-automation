#include "features/runtime/fpv_features_internal.h"


#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fpv_send_task {
  fpv_feature_state_t* state;
  uint64_t chat_id;
  char* chat_name;
  char* message_text;
  bool add_watermark;
  char** products;
  size_t products_count;
  char* products_path;
  bool restore_on_failure;
  bool update_lot_states;
  char* lot_update_tag;
  fpv_delivery_notice_t* delivery_notice;
} fpv_send_task_t;

void fpv_delivery_notice_destroy(fpv_delivery_notice_t* notice) {
  if (!notice) {
    return;
  }
  fpv_free(notice->order_id);
  fpv_free(notice->buyer_username);
  fpv_free(notice->delivery_text);
  fpv_free(notice);
}


static fpv_result_t fpv_send_message_entity(
    fpv_feature_state_t* state,
    const fpv_message_entity_t* entity,
    uint64_t chat_id,
    const char* chat_name) {
  if (!state || !entity || !state->account) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_message_t* sent = NULL;
  fpv_result_t result = FPV_ERR_INTERNAL;

  if (entity->type == FPV_ENTITY_TEXT) {
    result = fpv_funpay_account_send_message(
        state->account,
        chat_id,
        chat_name,
        entity->text,
        &sent,
        &error);
  } else if (entity->type == FPV_ENTITY_IMAGE) {
    result = fpv_funpay_account_send_image(
        state->account,
        chat_id,
        chat_name,
        entity->image_id,
        &sent,
        &error);
  } else if (entity->type == FPV_ENTITY_SLEEP) {
    uint32_t delay = (uint32_t)lrint(entity->sleep_seconds * 1000.0);
    fpv_sleep_ms(delay);
    return FPV_OK;
  }

  if (result == FPV_OK && sent) {
    if (state->runner && sent->id) {
      uint64_t msg_id = strtoull(sent->id, NULL, 10);
      if (msg_id > 0) {
        fpv_funpay_runner_mark_by_bot(state->runner, chat_id, msg_id);
      }
    }
    if (state->runner && sent->text) {
      fpv_funpay_runner_update_last_message(
          state->runner,
          chat_id,
          sent->text,
          NULL);
    }
  }
  fpv_message_destroy(sent);
  fpv_funpay_error_clear(&error);
  return result;
}


static void fpv_send_task_run(void* context) {
  fpv_send_task_t* task = (fpv_send_task_t*)context;
  if (!task || !task->state) {
    fpv_free(task);
    return;
  }

  fpv_feature_state_t* state = task->state;
  char* message_text = fpv_strdup(task->message_text ? task->message_text : "");
  if (!message_text) {
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task);
    return;
  }

  if (state->flags.watermark && state->flags.watermark[0] &&
      task->add_watermark && strncmp(message_text, "$photo=", 7) != 0) {
    char* joined = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        state->flags.watermark,
        strlen(state->flags.watermark));
    fpv_buffer_append_char(&joined, &length, &capacity, '\n');
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        message_text,
        strlen(message_text));
    fpv_free(message_text);
    message_text = joined;
  }

  fpv_message_entity_t* entities = NULL;
  size_t entity_count = 0;
  if (!fpv_parse_message_entities(message_text, &entities, &entity_count)) {
    fpv_free(message_text);
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task);
    return;
  }

  fpv_free(message_text);

  bool send_failed = false;
  for (size_t i = 0; i < entity_count; i++) {
    int attempts = 3;
    while (attempts-- > 0) {
      fpv_result_t result = fpv_send_message_entity(
          state,
          &entities[i],
          task->chat_id,
          task->chat_name);
      if (result == FPV_OK) {
        break;
      }
      fpv_sleep_ms(1000);
      if (attempts == 0) {
        fpv_features_log(state, FPV_LOG_ERROR, "Message send failed.");
        send_failed = true;
      }
    }
    if (send_failed) {
      break;
    }
  }

  if (send_failed && task->restore_on_failure &&
      task->products_path && task->products && task->products_count > 0) {
    fpv_products_restore(
        task->products_path,
        task->products,
        task->products_count);
  }

  if (task->update_lot_states) {
    fpv_features_queue_lot_update(state, task->lot_update_tag);
  }

  if (task->delivery_notice && state->telegram) {
    if (send_failed) {
      char* error_text =
          fpv_features_format_delivery_error(
              state, task->delivery_notice->order_id);
      const char* text = error_text
          ? error_text
          : (task->delivery_notice->delivery_text
              ? task->delivery_notice->delivery_text
              : "");
      fpv_telegram_service_notify_delivery(
          state->telegram,
          task->delivery_notice->order_id,
          task->delivery_notice->buyer_username,
          text,
          task->delivery_notice->goods_left,
          false);
      fpv_free(error_text);
    } else {
      fpv_telegram_service_notify_delivery(
          state->telegram,
          task->delivery_notice->order_id,
          task->delivery_notice->buyer_username,
          task->delivery_notice->delivery_text,
          task->delivery_notice->goods_left,
          true);
    }
  }

  fpv_free_message_entities(entities, entity_count);
  fpv_free(task->products_path);
  if (task->products) {
    fpv_products_free(task->products, task->products_count);
  }
  fpv_delivery_notice_destroy(task->delivery_notice);
  fpv_free(task->chat_name);
  fpv_free(task->message_text);
  fpv_free(task->lot_update_tag);
  fpv_free(task);
}


fpv_result_t fpv_features_send_message(
    fpv_feature_state_t* state,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark) {
  if (!state || !state->account || chat_id == 0 || !message_text) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* trim = message_text;
  while (*trim && isspace((unsigned char)*trim)) {
    trim++;
  }
  bool starts_photo = strncmp(trim, "$photo=", 7) == 0;
  char* text = fpv_strdup(message_text);
  if (!text) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (state->flags.watermark && state->flags.watermark[0] &&
      add_watermark && !starts_photo) {
    char* joined = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        state->flags.watermark,
        strlen(state->flags.watermark));
    fpv_buffer_append_char(&joined, &length, &capacity, '\n');
    fpv_buffer_append(
        &joined,
        &length,
        &capacity,
        text,
        strlen(text));
    fpv_free(text);
    text = joined;
  }
  if (!text) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_message_entity_t* entities = NULL;
  size_t entity_count = 0;
  if (!fpv_parse_message_entities(text, &entities, &entity_count)) {
    fpv_free(text);
    return FPV_ERR_PARSE;
  }
  fpv_free(text);
  if (entity_count == 0) {
    fpv_free_message_entities(entities, entity_count);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_result_t result = FPV_OK;
  for (size_t i = 0; i < entity_count; i++) {
    int attempts = 3;
    while (attempts-- > 0) {
      result = fpv_send_message_entity(
          state,
          &entities[i],
          chat_id,
          chat_name);
      if (result == FPV_OK) {
        break;
      }
      fpv_sleep_ms(1000);
    }
    if (result != FPV_OK) {
      fpv_free_message_entities(entities, entity_count);
      return result;
    }
  }
  fpv_free_message_entities(entities, entity_count);
  return FPV_OK;
}


bool fpv_features_queue_message(
    fpv_feature_state_t* state,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark,
    char** products,
    size_t products_count,
    const char* products_path,
    bool update_lot_states,
    const char* lot_update_tag,
    fpv_delivery_notice_t* delivery_notice) {
  if (!state || !state->scheduler || !message_text) {
    if (products_path && products && products_count > 0) {
      fpv_products_restore(products_path, products, products_count);
    }
    fpv_delivery_notice_destroy(delivery_notice);
    return false;
  }
  fpv_send_task_t* task = (fpv_send_task_t*)calloc(1, sizeof(*task));
  if (!task) {
    if (products_path && products && products_count > 0) {
      fpv_products_restore(products_path, products, products_count);
    }
    fpv_delivery_notice_destroy(delivery_notice);
    return false;
  }
  task->state = state;
  task->chat_id = chat_id;
  task->chat_name = chat_name ? fpv_strdup(chat_name) : NULL;
  task->message_text = fpv_strdup(message_text);
  task->add_watermark = add_watermark;
  task->products = products;
  task->products_count = products_count;
  task->products_path = products_path ? fpv_strdup(products_path) : NULL;
  task->restore_on_failure =
      products_path != NULL && products != NULL && products_count > 0;
  task->update_lot_states = update_lot_states;
  task->lot_update_tag = lot_update_tag ? fpv_strdup(lot_update_tag) : NULL;
  task->delivery_notice = delivery_notice;
  if (products_path && !task->products_path) {
    fpv_free(task->chat_name);
    fpv_free(task->message_text);
    fpv_free(task->lot_update_tag);
    fpv_delivery_notice_destroy(task->delivery_notice);
    if (products) {
      if (products_path && products_count > 0) {
        fpv_products_restore(products_path, products, products_count);
      }
    }
    fpv_free(task);
    return false;
  }
  if (!task->message_text) {
    fpv_free(task->chat_name);
    fpv_free(task->products_path);
    fpv_free(task->lot_update_tag);
    fpv_delivery_notice_destroy(task->delivery_notice);
    if (products) {
      if (products_path && products_count > 0) {
        fpv_products_restore(products_path, products, products_count);
      }
    }
    fpv_free(task);
    return false;
  }
  fpv_scheduler_enqueue(state->scheduler, fpv_send_task_run, task);
  return true;
}
