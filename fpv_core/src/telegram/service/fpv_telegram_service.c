/* FunPay Vertex Telegram service lifecycle and notifications. */

#include "telegram/core/fpv_telegram_internal.h"


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpv_telegram_service_t* fpv_telegram_service_create(
    const char* token,
    const char* secret,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    const char* language,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus,
    void* control_context,
    fpv_telegram_control_fn restart_fn,
    fpv_telegram_control_fn shutdown_fn) {
  if (!token || !token[0] || !storage) {
    return NULL;
  }
  fpv_telegram_service_t* service =
      (fpv_telegram_service_t*)calloc(1, sizeof(*service));
  if (!service) {
    return NULL;
  }
  if (!fpv_mutex_init(&service->mutex)) {
    free(service);
    return NULL;
  }
  service->token = fpv_strdup(token);
  service->secret = secret ? fpv_strdup(secret) : NULL;
  service->logger = logger;
  service->bus = bus;
  service->locales_dir = locales_dir ? fpv_strdup(locales_dir) : NULL;
  service->language = language ? fpv_strdup(language) : fpv_strdup("ru");
  service->localizer =
      fpv_localizer_create(locales_dir, service->language, "ru");
  if (!service->token || !service->language || !service->localizer) {
    fpv_telegram_service_destroy(service);
    return NULL;
  }
  service->storage = *storage;
  if (storage->data_dir) {
    service->storage.data_dir = fpv_strdup(storage->data_dir);
  }
  if (storage->config_dir) {
    service->storage.config_dir = fpv_strdup(storage->config_dir);
  }
  if (storage->cache_dir) {
    service->storage.cache_dir = fpv_strdup(storage->cache_dir);
  }
  if (storage->products_dir) {
    service->storage.products_dir = fpv_strdup(storage->products_dir);
  }
  if (storage->logs_dir) {
    service->storage.logs_dir = fpv_strdup(storage->logs_dir);
  }
  if (storage->plugins_dir) {
    service->storage.plugins_dir = fpv_strdup(storage->plugins_dir);
  }

  fpv_result_t identity_result = FPV_OK;
  service->identity =
      fpv_identity_store_open(service->storage.data_dir, &identity_result);
  if (!service->identity) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "Identity store initialization failed (error %d).",
             (int)identity_result);
    fpv_tg_log(service, FPV_LOG_WARNING, msg);
  }

  fpv_result_t session_result = fpv_tg_open_session_db(service);
  if (session_result != FPV_OK) {
    char msg[128];
    snprintf(msg, sizeof(msg),
             "Telegram session DB initialization failed (error %d).",
             (int)session_result);
    fpv_tg_log(service, FPV_LOG_WARNING, msg);
  }

  service->control_context = control_context;
  service->restart_fn = restart_fn;
  service->shutdown_fn = shutdown_fn;

  fpv_tg_load_notification_settings(service);
  fpv_tg_load_sessions(service);
  fpv_tg_load_answer_templates(service);
  return service;
}

void fpv_telegram_service_destroy(fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  fpv_telegram_service_stop(service);
  fpv_free(service->token);
  fpv_free(service->secret);
  fpv_free(service->locales_dir);
  fpv_free(service->language);
  fpv_localizer_destroy(service->localizer);
  fpv_free(service->notifications);
  for (size_t i = 0; i < service->session_count; i++) {
    fpv_free(service->sessions[i].user_id);
  }
  fpv_free(service->sessions);
  fpv_identity_store_destroy(service->identity);
  fpv_db_close(service->session_db);
  fpv_string_list_destroy(&service->answer_templates);
  for (size_t i = 0; i < service->user_state_count; i++) {
    fpv_tg_state_data_clear(&service->user_states[i].data);
  }
  fpv_free(service->user_states);
  fpv_free(service->attempts);
  fpv_free(service->init_messages);
  for (size_t i = 0; i < service->delivery_test_count; i++) {
    fpv_free(service->delivery_tests[i].key);
    fpv_free(service->delivery_tests[i].lot_name);
  }
  fpv_free(service->delivery_tests);
  for (size_t i = 0; i < service->profile_lot_count; i++) {
    fpv_lot_destroy(service->profile_lots[i]);
  }
  fpv_free(service->profile_lots);
  fpv_free(service->storage.data_dir);
  fpv_free(service->storage.config_dir);
  fpv_free(service->storage.cache_dir);
  fpv_free(service->storage.products_dir);
  fpv_free(service->storage.logs_dir);
  fpv_free(service->storage.plugins_dir);
  fpv_mutex_destroy(&service->mutex);
  free(service);
}

bool fpv_telegram_service_start(fpv_telegram_service_t* service) {
  if (!service || service->running) {
    return false;
  }
  service->start_ms = fpv_time_now_ms();
  srand((unsigned int)service->start_ms);
  service->instance_id =
      ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ service->start_ms;
  fpv_tg_set_my_commands(service);
  service->running = true;
  if (!fpv_thread_create(&service->thread, fpv_tg_thread_main, service)) {
    service->running = false;
    return false;
  }
  return true;
}

void fpv_telegram_service_stop(fpv_telegram_service_t* service) {
  if (!service || !service->running) {
    return;
  }
  service->running = false;
  fpv_thread_join(&service->thread);
}

void fpv_telegram_service_attach_account(
    fpv_telegram_service_t* service,
    fpv_funpay_account_t* account) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->account = account;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_attach_runner(
    fpv_telegram_service_t* service,
    fpv_funpay_runner_t* runner) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->runner = runner;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_attach_features(
    fpv_telegram_service_t* service,
    fpv_feature_state_t* features) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->features = features;
  fpv_mutex_unlock(&service->mutex);
}

void fpv_telegram_service_update_language(
    fpv_telegram_service_t* service,
    const char* locales_dir,
    const char* language) {
  if (!service || !language || !language[0]) {
    return;
  }
  char* language_copy = fpv_strdup(language);
  char* locales_copy = locales_dir ? fpv_strdup(locales_dir) : NULL;
  fpv_mutex_lock(&service->mutex);
  fpv_free(service->language);
  service->language = language_copy;
  fpv_free(service->locales_dir);
  service->locales_dir = locales_copy;
  fpv_localizer_destroy(service->localizer);
  service->localizer =
      fpv_localizer_create(service->locales_dir, service->language, "ru");
  fpv_mutex_unlock(&service->mutex);
  fpv_tg_set_my_commands(service);
}

void fpv_telegram_service_update_init_messages(
    fpv_telegram_service_t* service) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  size_t msg_count = service->init_message_count;
  fpv_tg_init_message_t* messages = NULL;
  if (msg_count > 0) {
    messages = (fpv_tg_init_message_t*)calloc(msg_count, sizeof(*messages));
    if (messages) {
      memcpy(messages, service->init_messages, msg_count * sizeof(*messages));
    }
  }
  fpv_mutex_unlock(&service->mutex);

  if (!account || !messages) {
    fpv_free(messages);
    return;
  }

  fpv_funpay_balance_t balance;
  memset(&balance, 0, sizeof(balance));
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_funpay_account_get_balance(account, &balance, &error);
  fpv_funpay_error_clear(&error);

  const char* username = fpv_funpay_account_username(account);
  uint64_t account_id = fpv_funpay_account_id(account);
  uint32_t sales = fpv_funpay_account_active_sales(account);

  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char id_buf[32];
  char rub_buf[32];
  char usd_buf[32];
  char eur_buf[32];
  char sales_buf[32];
  snprintf(id_buf, sizeof(id_buf), "%" PRIu64, account_id);
  snprintf(rub_buf, sizeof(rub_buf), "%.2f", balance.total_rub);
  snprintf(usd_buf, sizeof(usd_buf), "%.2f", balance.total_usd);
  snprintf(eur_buf, sizeof(eur_buf), "%.2f", balance.total_eur);
  snprintf(sales_buf, sizeof(sales_buf), "%u", sales);
  char* text = fpv_tg_loc_format(
      service,
      "fpv_init",
      (const char*[]){
          FPV_VERSION,
          escaped_user ? escaped_user : "",
          id_buf,
          rub_buf,
          usd_buf,
          eur_buf,
          sales_buf},
      7);
  fpv_free(escaped_user);
  if (!text) {
    fpv_free(messages);
    return;
  }

  for (size_t i = 0; i < msg_count; i++) {
    if (messages[i].chat_id == 0 || messages[i].message_id == 0) {
      continue;
    }
    fpv_tg_edit_message_text(
        service, messages[i].chat_id, messages[i].message_id, text, NULL);
  }
  fpv_free(text);
  fpv_free(messages);
}

bool fpv_tg_send_notification(
    fpv_telegram_service_t* service,
    const char* text,
    const char* reply_markup,
    const char* notification_type,
    const void* photo_data,
    size_t photo_size,
    bool pin,
    bool capture_init) {
  if (!service || !text || !notification_type) {
    return false;
  }
  int type_index = fpv_tg_notification_index(notification_type);
  if (type_index < 0) {
    return false;
  }
  fpv_mutex_lock(&service->mutex);
  size_t count = service->notification_count;
  fpv_tg_chat_settings_t* settings =
      (fpv_tg_chat_settings_t*)calloc(count, sizeof(*settings));
  if (settings) {
    memcpy(settings, service->notifications, count * sizeof(*settings));
  }
  fpv_mutex_unlock(&service->mutex);
  if (!settings) {
    return false;
  }
  bool any_sent = false;
  for (size_t i = 0; i < count; i++) {
    if (!settings[i].enabled[type_index]) {
      continue;
    }
    int message_id = 0;
    bool needs_id = capture_init || pin;
    bool sent = false;
    if (photo_data && photo_size > 0) {
      sent = fpv_tg_send_photo(
          service, settings[i].chat_id, photo_data, photo_size, text,
          reply_markup, needs_id ? &message_id : NULL);
    } else {
      sent = fpv_tg_send_message(
          service, settings[i].chat_id, text, reply_markup,
          needs_id ? &message_id : NULL);
    }
    if (!sent) {
      char chat_buf[32];
      snprintf(chat_buf, sizeof(chat_buf), "%" PRId64, settings[i].chat_id);
      char* msg = fpv_tg_loc_format(
          service,
          "log_tg_notification_error",
          (const char*[]){chat_buf},
          1);
      fpv_tg_log(service, FPV_LOG_WARNING,
                 msg ? msg : "Telegram send error.");
      fpv_free(msg);
      continue;
    }
    if (capture_init && message_id > 0) {
      fpv_mutex_lock(&service->mutex);
      fpv_tg_init_message_t* grown =
          (fpv_tg_init_message_t*)realloc(
              service->init_messages,
              (service->init_message_count + 1) * sizeof(*grown));
      if (grown) {
        service->init_messages = grown;
        service->init_messages[service->init_message_count].chat_id =
            settings[i].chat_id;
        service->init_messages[service->init_message_count].message_id =
            message_id;
        service->init_message_count++;
      }
      fpv_mutex_unlock(&service->mutex);
    }
    if (pin && message_id > 0) {
      fpv_tg_pin_message(service, settings[i].chat_id, message_id);
    }
    any_sent = true;
  }
  fpv_free(settings);
  return any_sent;
}

void fpv_telegram_service_notify_delivery(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    const char* delivery_text,
    int64_t goods_left,
    bool success) {
  if (!service || !order_id) {
    return;
  }
  char* escaped_delivery = fpv_tg_escape_html(delivery_text ? delivery_text : "");
  char* text = NULL;
  if (!success) {
    const char* fmt = "\xE2\x9D\x8C <code>%s</code>";
    size_t size = strlen(fmt) + (escaped_delivery ? strlen(escaped_delivery) : 0) + 16;
    text = (char*)malloc(size);
    if (text) {
      snprintf(text, size, fmt, escaped_delivery ? escaped_delivery : "");
    }
  } else {
    const char* amount =
        goods_left < 0 ? "<b>\xE2\x88\x9E</b>" : NULL;
    char left_buf[32];
    if (!amount) {
      snprintf(left_buf, sizeof(left_buf), "<code>%" PRId64 "</code>", goods_left);
      amount = left_buf;
    }
    const char* fmt =
        "\xE2\x9C\x85 \xD0\xA3\xD1\x81\xD0\xBF\xD0\xB5\xD1\x88\xD0\xBD\xD0\xBE \xD0\xB2\xD1\x8B\xD0\xB4\xD0\xB0\xD0\xBB \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80 \xD0\xB4\xD0\xBB\xD1\x8F \xD0\xBE\xD1\x80\xD0\xB4\xD0\xB5\xD1\x80\xD0\xB0 <code>%s</code>.\n\n"
        "\xF0\x9F\x9B\x92 <b><i>\xD0\xA2\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80:</i></b>\n"
        "<code>%s</code>\n\n"
        "\xF0\x9F\x93\x8B <b><i>\xD0\x9E\xD1\x81\xD1\x82\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD1\x82\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x80\xD0\xBE\xD0\xB2: </i></b>%s";
    size_t size = strlen(fmt) + strlen(order_id) +
        (escaped_delivery ? strlen(escaped_delivery) : 0) + strlen(amount) + 32;
    text = (char*)malloc(size);
    if (text) {
      snprintf(text, size, fmt, order_id, escaped_delivery ? escaped_delivery : "", amount);
    }
  }
  fpv_free(escaped_delivery);
  if (!text) {
    return;
  }
  fpv_tg_send_notification(
      service, text, NULL, "8", NULL, 0, false, false);
  fpv_free(text);
}

void fpv_telegram_service_notify_new_message(
    fpv_telegram_service_t* service,
    const fpv_message_t* const* messages,
    size_t message_count,
    uint64_t chat_id,
    const char* chat_name,
    uint64_t account_id) {
  if (!service || !messages || message_count == 0 || chat_id == 0) {
    return;
  }
  char* text = fpv_tg_build_messages_text(
      service, messages, message_count, chat_name, account_id, 0);
  if (!text) {
    return;
  }

  char* reply_markup =
      fpv_tg_build_reply_keyboard(service, chat_id, chat_name, false, true);
  fpv_tg_send_notification(service, text, reply_markup, "2", NULL, 0, false, false);
  fpv_free(reply_markup);
  fpv_free(text);
}

void fpv_telegram_service_notify_command(
    fpv_telegram_service_t* service,
    uint64_t chat_id,
    const char* chat_name,
    const char* username,
    const char* command,
    const char* text) {
  if (!service || !command) {
    return;
  }
  char* escaped_user = fpv_tg_escape_html(username ? username : "");
  char* message_text = NULL;
  if (text && text[0]) {
    char* escaped_text = fpv_tg_escape_html(text);
    message_text = escaped_text ? escaped_text : fpv_strdup("");
  } else {
    char* escaped_cmd = fpv_tg_escape_html(command);
    char buffer[512];
    snprintf(
        buffer,
        sizeof(buffer),
        "\xF0\x9F\xA7\x91\xE2\x80\x8D\xF0\x9F\x92\xBB \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C <b><i>%s</i></b> \xD0\xB2\xD0\xB2\xD0\xB5\xD0\xBB \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD1\x83 <code>%s</code>.",
        escaped_user ? escaped_user : "",
        escaped_cmd ? escaped_cmd : "");
    fpv_free(escaped_cmd);
    message_text = fpv_strdup(buffer);
  }
  fpv_free(escaped_user);
  if (!message_text) {
    return;
  }
  fpv_tg_send_notification(service, message_text, NULL, "3", NULL, 0, false, false);
  fpv_free(message_text);
}

void fpv_telegram_service_notify_new_order(
    fpv_telegram_service_t* service,
    const char* order_title,
    const char* order_id,
    const char* buyer_username,
    double price,
    const char* price_text,
    uint64_t chat_id,
    const char* delivery_info) {
  if (!service || !order_id || !buyer_username) {
    return;
  }
  char price_buf[32];
  if (price_text && price_text[0]) {
    snprintf(price_buf, sizeof(price_buf), "%s", price_text);
  } else {
    snprintf(price_buf, sizeof(price_buf), "%.2f", price);
  }
  char* escaped_title = fpv_tg_escape_html(order_title ? order_title : "");
  char* escaped_user = fpv_tg_escape_html(buyer_username);
  char* escaped_info = fpv_tg_escape_html(delivery_info ? delivery_info : "");
  char* formatted = fpv_tg_loc_format(
      service,
      "ntfc_new_order",
      (const char*[]){
          escaped_title ? escaped_title : "",
          escaped_user ? escaped_user : "",
          price_buf,
          order_id,
          escaped_info ? escaped_info : ""},
      5);
  char* text = formatted ? fpv_strdup(formatted) : NULL;
  fpv_free(formatted);
  fpv_free(escaped_title);
  fpv_free(escaped_user);
  fpv_free(escaped_info);
  if (!text) {
    return;
  }
  char* reply_markup =
      fpv_tg_build_order_keyboard(service, order_id, buyer_username, chat_id, false, false);
  fpv_tg_send_notification(service, text, reply_markup, "4", NULL, 0, false, false);
  fpv_free(reply_markup);
  fpv_free(text);
}

void fpv_telegram_service_notify_order_confirmed(
    fpv_telegram_service_t* service,
    const char* order_id,
    const char* buyer_username,
    uint64_t chat_id) {
  if (!service || !order_id || !buyer_username) {
    return;
  }
  char* escaped_user = fpv_tg_escape_html(buyer_username);
  char buffer[512];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\xAA\x99 \xD0\x9F\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C <a href=\"https://funpay.com/chat/?node=%" PRIu64 "\">%s</a> "
      "\xD0\xBF\xD0\xBE\xD0\xB4\xD1\x82\xD0\xB2\xD0\xB5\xD1\x80\xD0\xB4\xD0\xB8\xD0\xBB \xD0\xB2\xD1\x8B\xD0\xBF\xD0\xBE\xD0\xBB\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7\xD0\xB0 <code>%s</code>.",
      chat_id,
      escaped_user ? escaped_user : "",
      order_id);
  fpv_free(escaped_user);
  fpv_tg_send_notification(service, buffer, NULL, "5", NULL, 0, false, false);
}

void fpv_telegram_service_notify_review(
    fpv_telegram_service_t* service,
    const char* order_id,
    int stars,
    const char* review_text,
    const char* reply_text,
    uint64_t chat_id,
    const char* buyer_username) {
  if (!service || !order_id || stars <= 0) {
    return;
  }
  char star_buf[16];
  if (stars > 5) {
    stars = 5;
  }
  memset(star_buf, 0, sizeof(star_buf));
  for (int i = 0; i < stars && i < 5; i++) {
    strcat(star_buf, "\xE2\xAD\x90");
  }
  char* escaped_review = fpv_tg_escape_html(review_text ? review_text : "");
  char* escaped_reply = reply_text ? fpv_tg_escape_html(reply_text) : NULL;
  char buffer[2048];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x94\xAE \xD0\x92\xD1\x8B \xD0\xBF\xD0\xBE\xD0\xBB\xD1\x83\xD1\x87\xD0\xB8\xD0\xBB\xD0\xB8 %s \xD0\xB7\xD0\xB0 \xD0\xB7\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xB7 <code>%s</code>!\n\n"
      "\xF0\x9F\x92\xAC<b>\xD0\x9E\xD1\x82\xD0\xB7\xD1\x8B\xD0\xB2:</b>\n<code>%s</code>",
      star_buf,
      order_id,
      escaped_review ? escaped_review : "");
  fpv_free(escaped_review);
  if (escaped_reply) {
    char* merged = NULL;
    size_t length = 0;
    size_t capacity = 0;
    fpv_tg_buffer_append(&merged, &length, &capacity, buffer, strlen(buffer));
    fpv_tg_buffer_append(&merged, &length, &capacity,
                         "\n\n\xF0\x9F\x97\xA8\xEF\xB8\x8F<b>\xD0\x9E\xD1\x82\xD0\xB2\xD0\xB5\xD1\x82:</b>\n<code>",
                         45);
    fpv_tg_buffer_append(&merged, &length, &capacity,
                         escaped_reply, strlen(escaped_reply));
    fpv_tg_buffer_append(&merged, &length, &capacity, "</code>", 7);
    fpv_tg_send_notification(service, merged, NULL, "5r", NULL, 0, false, false);
    fpv_free(merged);
  } else {
    fpv_tg_send_notification(service, buffer, NULL, "5r", NULL, 0, false, false);
  }
  fpv_free(escaped_reply);
}

void fpv_telegram_service_notify_refund(
    fpv_telegram_service_t* service,
    const char* order_id,
    bool success) {
  if (!service || !order_id || !order_id[0]) {
    return;
  }
  const char* key = success ? "refund_complete" : "refund_error";
  char* text = fpv_tg_loc_format(
      service,
      key,
      (const char*[]){order_id},
      1);
  if (!text) {
    return;
  }
  fpv_tg_send_notification(service, text, NULL, "5r", NULL, 0, false, false);
  fpv_free(text);
}

void fpv_telegram_service_notify_lots_activated(
    fpv_telegram_service_t* service,
    const char* lot_list) {
  if (!service || !lot_list) {
    return;
  }
  char* escaped = fpv_tg_escape_html(lot_list);
  char buffer[1024];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x9F\xA2 <b>\xD0\x90\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBB \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B:</b>\n\n<code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "6", NULL, 0, false, false);
}

void fpv_telegram_service_notify_lots_deactivated(
    fpv_telegram_service_t* service,
    const char* lot_list) {
  if (!service || !lot_list) {
    return;
  }
  char* escaped = fpv_tg_escape_html(lot_list);
  char buffer[1024];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xF0\x9F\x94\xB4 <b>\xD0\x94\xD0\xB5\xD0\xB0\xD0\xBA\xD1\x82\xD0\xB8\xD0\xB2\xD0\xB8\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBB \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B:</b>\n\n<code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "7", NULL, 0, false, false);
}

void fpv_telegram_service_notify_lots_raised(
    fpv_telegram_service_t* service,
    const char* category_name) {
  if (!service || !category_name) {
    return;
  }
  char* escaped = fpv_tg_escape_html(category_name);
  char buffer[512];
  snprintf(
      buffer,
      sizeof(buffer),
      "\xE2\xA4\xB4\xEF\xB8\x8F<b><i>\xD0\x9F\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F\xD0\xBB \xD0\xB2\xD1\x81\xD0\xB5 \xD0\xBB\xD0\xBE\xD1\x82\xD1\x8B \xD0\xBA\xD0\xB0\xD1\x82\xD0\xB5\xD0\xB3\xD0\xBE\xD1\x80\xD0\xB8\xD0\xB8</i></b> <code>%s</code>",
      escaped ? escaped : "");
  fpv_free(escaped);
  fpv_tg_send_notification(service, buffer, NULL, "9", NULL, 0, false, false);
}

void fpv_telegram_service_notify_sras(
    fpv_telegram_service_t* service,
    bool active) {
  if (!service) {
    return;
  }
  const char* text = active
      ? "SRAS monitor: account stats are unavailable. Possible hidden ban."
        " Check FunPay profile."
      : "SRAS monitor: account stats are available again.";
  fpv_tg_send_notification(service, text, NULL, "14", NULL, 0, false, false);
}

char* fpv_telegram_service_take_delivery_test(
    fpv_telegram_service_t* service,
    const char* key) {
  if (!service || !key || !key[0]) {
    return NULL;
  }
  fpv_mutex_lock(&service->mutex);
  for (size_t i = 0; i < service->delivery_test_count; i++) {
    fpv_tg_delivery_test_t* entry = &service->delivery_tests[i];
    if (entry->key && strcmp(entry->key, key) == 0) {
      char* lot_name =
          entry->lot_name ? fpv_strdup(entry->lot_name) : NULL;
      fpv_free(entry->key);
      fpv_free(entry->lot_name);
      service->delivery_tests[i] =
          service->delivery_tests[service->delivery_test_count - 1];
      service->delivery_test_count--;
      fpv_mutex_unlock(&service->mutex);
      return lot_name;
    }
  }
  fpv_mutex_unlock(&service->mutex);
  return NULL;
}
