#include "features/runtime/fpv_features_internal.h"


#include <stdlib.h>
#include <string.h>

#include "core/io/fpv_fs.h"


fpv_result_t fpv_features_init(
    fpv_feature_state_t** out_state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir,
    fpv_logger_t* logger) {
  if (!out_state || !settings || !storage) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_feature_state_t* state =
      (fpv_feature_state_t*)calloc(1, sizeof(*state));
  if (!state) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_mutex_init(&state->config_mutex)) {
    fpv_features_destroy(state);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_mutex_init(&state->lot_update_mutex)) {
    fpv_features_destroy(state);
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (!fpv_mutex_init(&state->timed_mutex)) {
    fpv_features_destroy(state);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  state->logger = logger;
  fpv_result_t apply_result =
      fpv_features_apply_settings(state, settings, locales_dir);
  if (apply_result != FPV_OK) {
    fpv_features_destroy(state);
    return apply_result;
  }

  state->products_dir =
      storage->products_dir ? fpv_strdup(storage->products_dir) : NULL;
  if (storage->cache_dir) {
    state->blacklist_path =
        fpv_path_join(storage->cache_dir, "blacklist.json");
    state->old_users_path =
        fpv_path_join(storage->cache_dir, "old_users.json");
    state->adv_profile_path =
        fpv_path_join(storage->cache_dir, "advProfileStat.json");
  }

  if (storage->config_dir) {
    char* auto_response_path =
        fpv_path_join(storage->config_dir, "auto_response.cfg");
    if (auto_response_path) {
      fpv_result_t ar_result =
          fpv_auto_response_config_load(
              auto_response_path,
              &state->auto_response);
      if (ar_result != FPV_OK) {
        fpv_features_log(state, FPV_LOG_WARNING,
                         "Auto-response config load failed.");
      }
      fpv_free(auto_response_path);
    }

    char* auto_delivery_path =
        fpv_path_join(storage->config_dir, "auto_delivery.cfg");
    if (auto_delivery_path && state->products_dir) {
      fpv_result_t ad_result =
          fpv_auto_delivery_config_load(
              auto_delivery_path,
              state->products_dir,
              &state->auto_delivery);
      if (ad_result != FPV_OK) {
        fpv_features_log(state, FPV_LOG_WARNING,
                         "Auto-delivery config load failed.");
      }
      fpv_free(auto_delivery_path);
    } else {
      fpv_free(auto_delivery_path);
    }
  }

  if (state->blacklist_path) {
    fpv_cache_load_strings(
        state->blacklist_path,
        &state->blacklist);
  }
  if (state->old_users_path) {
    fpv_cache_load_uint64(
        state->old_users_path,
        &state->old_users,
        &state->old_user_count);
  }

  *out_state = state;
  return FPV_OK;
}


void fpv_features_destroy(fpv_feature_state_t* state) {
  if (!state) {
    return;
  }
  fpv_free(state->flags.watermark);
  fpv_free(state->flags.language);
  fpv_free(state->flags.greetings_text);
  fpv_free(state->flags.order_confirm_text);
  for (size_t i = 0; i < 5; i++) {
    fpv_free(state->flags.review_reply_texts[i]);
  }
  fpv_auto_response_config_destroy(&state->auto_response);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  fpv_localizer_destroy(state->localizer);
  fpv_string_list_destroy(&state->blacklist);
  fpv_free(state->old_users);
  fpv_free(state->blacklist_path);
  fpv_free(state->old_users_path);
  fpv_free(state->adv_profile_path);
  fpv_free(state->products_dir);
  fpv_free(state->raise_entries);
  fpv_free(state->last_lot_update_tag);
  fpv_free(state->pending_lot_update_tag);
  fpv_mutex_destroy(&state->config_mutex);
  fpv_mutex_destroy(&state->lot_update_mutex);
  fpv_mutex_destroy(&state->timed_mutex);
  if (state->timed_entries) {
    for (size_t i = 0; i < state->timed_count; i++) {
      fpv_free(state->timed_entries[i].response);
    }
    fpv_free(state->timed_entries);
  }
  free(state);
}


fpv_result_t fpv_features_update_settings(
    fpv_feature_state_t* state,
    const fpv_settings_t* settings,
    const fpv_storage_layout_t* storage,
    const char* locales_dir) {
  if (!state || !settings || !storage) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_result_t apply_result =
      fpv_features_apply_settings(state, settings, locales_dir);
  if (apply_result != FPV_OK) {
    return apply_result;
  }

  fpv_free(state->products_dir);
  state->products_dir =
      storage->products_dir ? fpv_strdup(storage->products_dir) : NULL;

  fpv_free(state->blacklist_path);
  fpv_free(state->old_users_path);
  fpv_free(state->adv_profile_path);
  state->blacklist_path = NULL;
  state->old_users_path = NULL;
  state->adv_profile_path = NULL;
  if (storage->cache_dir) {
    state->blacklist_path =
        fpv_path_join(storage->cache_dir, "blacklist.json");
    state->old_users_path =
        fpv_path_join(storage->cache_dir, "old_users.json");
    state->adv_profile_path =
        fpv_path_join(storage->cache_dir, "advProfileStat.json");
  }

  fpv_string_list_destroy(&state->blacklist);
  state->blacklist.items = NULL;
  state->blacklist.count = 0;
  fpv_free(state->old_users);
  state->old_users = NULL;
  state->old_user_count = 0;
  if (state->blacklist_path) {
    fpv_cache_load_strings(state->blacklist_path, &state->blacklist);
  }
  if (state->old_users_path) {
    fpv_cache_load_uint64(
        state->old_users_path,
        &state->old_users,
        &state->old_user_count);
  }

  fpv_auto_response_config_t next_response;
  fpv_auto_delivery_config_t next_delivery;
  memset(&next_response, 0, sizeof(next_response));
  memset(&next_delivery, 0, sizeof(next_delivery));

  if (storage->config_dir) {
    char* auto_response_path =
        fpv_path_join(storage->config_dir, "auto_response.cfg");
    if (auto_response_path) {
      fpv_auto_response_config_load(
          auto_response_path,
          &next_response);
      fpv_free(auto_response_path);
    }

    char* auto_delivery_path =
        fpv_path_join(storage->config_dir, "auto_delivery.cfg");
    if (auto_delivery_path && state->products_dir) {
      fpv_auto_delivery_config_load(
          auto_delivery_path,
          state->products_dir,
          &next_delivery);
      fpv_free(auto_delivery_path);
    } else {
      fpv_free(auto_delivery_path);
    }
  }

  fpv_features_config_lock(state);
  fpv_auto_response_config_destroy(&state->auto_response);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  state->auto_response = next_response;
  state->auto_delivery = next_delivery;
  fpv_features_config_unlock(state);

  if (state->runner) {
    fpv_funpay_runner_set_message_requests(
        state->runner,
        !settings->old_msg_mode);
  }

  return FPV_OK;
}


fpv_result_t fpv_features_reload_auto_response(
    fpv_feature_state_t* state,
    const char* path) {
  if (!state || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_auto_response_config_t next;
  memset(&next, 0, sizeof(next));
  fpv_result_t result = fpv_auto_response_config_load(path, &next);
  if (result != FPV_OK) {
    fpv_auto_response_config_destroy(&next);
    return result;
  }
  fpv_features_config_lock(state);
  fpv_auto_response_config_destroy(&state->auto_response);
  state->auto_response = next;
  fpv_features_config_unlock(state);
  return FPV_OK;
}


fpv_result_t fpv_features_reload_auto_delivery(
    fpv_feature_state_t* state,
    const char* path,
    const char* products_dir) {
  if (!state || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  const char* products = products_dir ? products_dir : state->products_dir;
  fpv_auto_delivery_config_t next;
  memset(&next, 0, sizeof(next));
  fpv_result_t result =
      fpv_auto_delivery_config_load(path, products, &next);
  if (result != FPV_OK) {
    fpv_auto_delivery_config_destroy(&next);
    return result;
  }
  fpv_features_config_lock(state);
  fpv_auto_delivery_config_destroy(&state->auto_delivery);
  state->auto_delivery = next;
  fpv_features_config_unlock(state);
  fpv_features_queue_timed_sync(state);
  fpv_features_queue_lot_secrets_sync(state);
  fpv_features_queue_lot_update(state, NULL);
  return FPV_OK;
}


fpv_result_t fpv_features_reload_blacklist(fpv_feature_state_t* state) {
  if (!state || !state->blacklist_path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_string_list_destroy(&state->blacklist);
  state->blacklist.items = NULL;
  state->blacklist.count = 0;
  return fpv_cache_load_strings(state->blacklist_path, &state->blacklist);
}


void fpv_features_set_telegram(
    fpv_feature_state_t* state,
    fpv_telegram_service_t* telegram) {
  if (!state) {
    return;
  }
  state->telegram = telegram;
}


void fpv_features_attach(
    fpv_feature_state_t* state,
    fpv_funpay_account_t* account,
    fpv_funpay_runner_t* runner,
    fpv_scheduler_t* scheduler,
    fpv_logger_t* logger,
    fpv_event_bus_t* bus) {
  if (!state) {
    return;
  }
  state->account = account;
  state->runner = runner;
  state->scheduler = scheduler;
  state->logger = logger;
  state->bus = bus;
  fpv_features_queue_timed_sync(state);
  fpv_features_queue_lot_secrets_sync(state);
  if (state->telegram) {
    fpv_telegram_service_attach_account(state->telegram, account);
    fpv_telegram_service_attach_runner(state->telegram, runner);
    fpv_telegram_service_update_init_messages(state->telegram);
  }
}
