/* FunPay Vertex Telegram chat actions. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fpv_tg_update_reply_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username,
    bool again,
    bool extend) {
  if (!service || !callback) {
    return;
  }
  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, username, again, extend);
  if (reply_markup) {
    fpv_tg_edit_message_reply_markup(
        service,
        callback->chat_id,
        callback->message_id,
        reply_markup);
    fpv_free(reply_markup);
  }
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_update_order_keyboard(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username,
    bool confirmation,
    bool no_refund) {
  if (!service || !callback || !order_id || !order_id[0]) {
    return;
  }
  char* reply_markup = fpv_tg_build_order_keyboard(
      service, order_id, username, chat_id, confirmation, no_refund);
  if (reply_markup) {
    fpv_tg_edit_message_reply_markup(
        service,
        callback->chat_id,
        callback->message_id,
        reply_markup);
    fpv_free(reply_markup);
  }
}

void fpv_tg_extend_chat_history(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    uint64_t chat_id,
    const char* username) {
  if (!service || !callback || chat_id == 0) {
    return;
  }
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  if (!account) {
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }
  fpv_message_t** messages = NULL;
  size_t message_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_chat_history(
      account,
      chat_id,
      username,
      &messages,
      &message_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || message_count == 0) {
    if (messages) {
      for (size_t i = 0; i < message_count; i++) {
        fpv_message_destroy(messages[i]);
      }
      fpv_free(messages);
    }
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  uint64_t account_id = fpv_funpay_account_id(account);
  char* text = fpv_tg_build_messages_text(
      service,
      (const fpv_message_t* const*)messages,
      message_count,
      username,
      account_id,
      10);
  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, username, false, false);
  if (text) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        callback->message_id,
        text,
        reply_markup);
  } else {
    fpv_tg_send_message(service, callback->chat_id,
                        fpv_tg_loc(service, "get_chat_error"),
                        NULL, NULL);
  }
  fpv_free(reply_markup);
  fpv_free(text);
  for (size_t i = 0; i < message_count; i++) {
    fpv_message_destroy(messages[i]);
  }
  fpv_free(messages);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

void fpv_tg_confirm_refund(
    fpv_telegram_service_t* service,
    const fpv_tg_callback_t* callback,
    const char* order_id,
    uint64_t chat_id,
    const char* username) {
  if (!service || !callback || !order_id || !order_id[0]) {
    if (service && callback) {
      fpv_tg_answer_callback(service, callback->id, NULL, false);
    }
    return;
  }
  fpv_funpay_account_t* account = fpv_tg_get_account(service);
  int status_id = 0;
  int attempts = 3;
  bool success = false;
  if (account) {
    while (attempts > 0) {
      fpv_funpay_error_t error;
      memset(&error, 0, sizeof(error));
      fpv_result_t result =
          fpv_funpay_account_refund(account, order_id, &error);
      fpv_funpay_error_clear(&error);
      if (result == FPV_OK) {
        success = true;
        break;
      }
      char attempts_buf[32];
      snprintf(attempts_buf, sizeof(attempts_buf), "%d", attempts);
      char* text = fpv_tg_loc_format(
          service,
          "refund_attempt",
          (const char*[]){order_id, attempts_buf},
          2);
      if (status_id == 0) {
        fpv_tg_send_message(
            service,
            callback->chat_id,
            text ? text : "",
            NULL,
            &status_id);
      } else {
        fpv_tg_edit_message_text(
            service,
            callback->chat_id,
            status_id,
            text ? text : "",
            NULL);
      }
      fpv_free(text);
      attempts--;
      if (attempts > 0) {
        fpv_tg_sleep_ms(1000);
      }
    }
  }

  if (!success) {
    char* text = fpv_tg_loc_format(
        service,
        "refund_error",
        (const char*[]){order_id},
        1);
    if (status_id > 0) {
      fpv_tg_edit_message_text(
          service,
          callback->chat_id,
          status_id,
          text ? text : "",
          NULL);
    } else {
      fpv_tg_send_message(
          service,
          callback->chat_id,
          text ? text : "",
          NULL,
          NULL);
    }
    fpv_free(text);
    fpv_tg_update_order_keyboard(
        service, callback, order_id, chat_id, username, false, false);
    fpv_tg_answer_callback(service, callback->id, NULL, false);
    return;
  }

  char* text = fpv_tg_loc_format(
      service,
      "refund_complete",
      (const char*[]){order_id},
      1);
  if (status_id > 0) {
    fpv_tg_edit_message_text(
        service,
        callback->chat_id,
        status_id,
        text ? text : "",
        NULL);
  } else {
    fpv_tg_send_message(
        service,
        callback->chat_id,
        text ? text : "",
        NULL,
        NULL);
  }
  fpv_free(text);
  fpv_tg_update_order_keyboard(
      service, callback, order_id, chat_id, username, false, true);
  fpv_tg_answer_callback(service, callback->id, NULL, false);
}

