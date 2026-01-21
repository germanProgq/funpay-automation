#include "features/runtime/fpv_features_internal.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/io/fpv_fs.h"


typedef struct fpv_lot_update_task {
  fpv_feature_state_t* state;
  char* runner_tag;
} fpv_lot_update_task_t;

static size_t fpv_count_products(const char* path) {
  if (!path || !fpv_fs_exists(path)) {
    return 0;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return 0;
  }
  size_t count = 0;
  bool has_content = false;
  int ch = 0;
  while ((ch = fgetc(file)) != EOF) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      if (has_content) {
        count++;
      }
      has_content = false;
      continue;
    }
    has_content = true;
  }
  if (has_content) {
    count++;
  }
  fclose(file);
  return count;
}


static void fpv_features_update_lot_states(fpv_feature_state_t* state) {
  if (!state || !state->account) {
    return;
  }
  if (!state->flags.auto_restore && !state->flags.auto_disable) {
    return;
  }

  char* activated_list = NULL;
  size_t activated_len = 0;
  size_t activated_cap = 0;
  size_t activated_count = 0;
  char* deactivated_list = NULL;
  size_t deactivated_len = 0;
  size_t deactivated_cap = 0;
  size_t deactivated_count = 0;

  fpv_funpay_lot_section_t* sections = NULL;
  size_t section_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_sections(
      state->account,
      &sections,
      &section_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || section_count == 0) {
    fpv_free(sections);
    return;
  }

  for (size_t i = 0; i < section_count; i++) {
    fpv_lot_t** lots = NULL;
    size_t lot_count = 0;
    memset(&error, 0, sizeof(error));
    result = fpv_funpay_account_get_trade_lots(
        state->account,
        sections[i].id,
        sections[i].is_currency,
        &lots,
        &lot_count,
        &error);
    fpv_funpay_error_clear(&error);
    if (result != FPV_OK || lot_count == 0) {
      if (lots) {
        for (size_t j = 0; j < lot_count; j++) {
          fpv_lot_destroy(lots[j]);
        }
        fpv_free(lots);
      }
      continue;
    }

    for (size_t j = 0; j < lot_count; j++) {
      fpv_lot_t* lot = lots[j];
      if (!lot || !lot->id) {
        continue;
      }
      uint64_t lot_id = strtoull(lot->id, NULL, 10);
      if (lot_id == 0) {
        continue;
      }
      const char* title = lot->title ? lot->title : "";
      const char* lot_name =
          (title && title[0]) ? title : (lot->id ? lot->id : "");
      bool config_found = false;
      bool disable_restore = false;
      bool disable_disable = false;
      char* products_file = NULL;
      fpv_features_config_lock(state);
      const fpv_auto_delivery_lot_t* config =
          fpv_auto_delivery_find(&state->auto_delivery, title);
      if (config) {
        config_found = true;
        disable_restore = config->disable_auto_restore;
        disable_disable = config->disable_auto_disable;
        if (config->products_file && config->products_file[0]) {
          products_file = fpv_strdup(config->products_file);
        }
      }
      fpv_features_config_unlock(state);
      bool has_products =
          products_file && products_file[0] && state->products_dir;
      size_t products_count = 1;
      if (has_products) {
        char* path = fpv_path_join(state->products_dir, products_file);
        if (path) {
          products_count = fpv_count_products(path);
          fpv_free(path);
        } else {
          products_count = 0;
        }
      }
      fpv_free(products_file);

      bool should_activate = false;
      if (!lot->active && state->flags.auto_restore) {
        if (!config_found) {
          should_activate = true;
        } else if (!disable_restore) {
          if (!state->flags.auto_disable) {
            should_activate = true;
          } else if (!has_products || products_count > 0) {
            should_activate = true;
          }
        }
      }

      if (should_activate) {
        fpv_funpay_error_t lot_error;
        memset(&lot_error, 0, sizeof(lot_error));
        fpv_result_t change_result = fpv_funpay_account_set_lot_active(
            state->account,
            lot_id,
            true,
            &lot_error);
        if (change_result == FPV_OK) {
          char message[256];
          snprintf(message, sizeof(message), "Lot activated: %s", title);
          fpv_features_log(state, FPV_LOG_INFO, message);
          if (lot_name && lot_name[0]) {
            bool ok = true;
            if (activated_count > 0) {
              ok = fpv_buffer_append_char(
                  &activated_list, &activated_len, &activated_cap, '\n');
            }
            if (ok) {
              ok = fpv_buffer_append(
                  &activated_list, &activated_len, &activated_cap,
                  lot_name, strlen(lot_name));
            }
            if (!ok) {
              fpv_free(activated_list);
              activated_list = NULL;
              activated_len = 0;
              activated_cap = 0;
            } else {
              activated_count++;
            }
          }
        } else {
          fpv_features_log(
              state,
              FPV_LOG_WARNING,
              lot_error.message ? lot_error.message : "Lot activation failed.");
        }
        fpv_funpay_error_clear(&lot_error);
      }

      bool should_deactivate = false;
      if (lot->active && state->flags.auto_disable && config_found &&
          !disable_disable && has_products &&
          products_count == 0) {
        should_deactivate = true;
      }

      if (should_deactivate) {
        fpv_funpay_error_t lot_error;
        memset(&lot_error, 0, sizeof(lot_error));
        fpv_result_t change_result = fpv_funpay_account_set_lot_active(
            state->account,
            lot_id,
            false,
            &lot_error);
        if (change_result == FPV_OK) {
          char message[256];
          snprintf(message, sizeof(message), "Lot deactivated: %s", title);
          fpv_features_log(state, FPV_LOG_INFO, message);
          if (lot_name && lot_name[0]) {
            bool ok = true;
            if (deactivated_count > 0) {
              ok = fpv_buffer_append_char(
                  &deactivated_list, &deactivated_len, &deactivated_cap, '\n');
            }
            if (ok) {
              ok = fpv_buffer_append(
                  &deactivated_list, &deactivated_len, &deactivated_cap,
                  lot_name, strlen(lot_name));
            }
            if (!ok) {
              fpv_free(deactivated_list);
              deactivated_list = NULL;
              deactivated_len = 0;
              deactivated_cap = 0;
            } else {
              deactivated_count++;
            }
          }
        } else {
          fpv_features_log(
              state,
              FPV_LOG_WARNING,
              lot_error.message ? lot_error.message : "Lot deactivation failed.");
        }
        fpv_funpay_error_clear(&lot_error);
      }
    }

    for (size_t j = 0; j < lot_count; j++) {
      fpv_lot_destroy(lots[j]);
    }
    fpv_free(lots);
  }

  if (state->telegram) {
    if (activated_list && activated_list[0]) {
      fpv_telegram_service_notify_lots_activated(
          state->telegram, activated_list);
    }
    if (deactivated_list && deactivated_list[0]) {
      fpv_telegram_service_notify_lots_deactivated(
          state->telegram, deactivated_list);
    }
  }
  fpv_free(activated_list);
  fpv_free(deactivated_list);
  fpv_free(sections);
}


static void fpv_features_lot_update_task(void* context) {
  fpv_lot_update_task_t* task = (fpv_lot_update_task_t*)context;
  if (!task) {
    return;
  }
  fpv_feature_state_t* state = task->state;
  if (state) {
    fpv_features_update_lot_states(state);
  }

  if (!state || !state->lot_update_mutex.initialized) {
    fpv_free(task->runner_tag);
    fpv_free(task);
    return;
  }

  fpv_mutex_lock(&state->lot_update_mutex);
  if (task->runner_tag) {
    fpv_free(state->last_lot_update_tag);
    state->last_lot_update_tag = task->runner_tag;
    task->runner_tag = NULL;
  }
  bool pending = state->lot_update_pending;
  char* pending_tag = state->pending_lot_update_tag;
  state->pending_lot_update_tag = NULL;
  state->lot_update_pending = false;
  state->lot_update_running = false;
  fpv_mutex_unlock(&state->lot_update_mutex);

  if (pending) {
    fpv_features_queue_lot_update(state, pending_tag);
  }
  fpv_free(pending_tag);
  fpv_free(task->runner_tag);
  fpv_free(task);
}


void fpv_features_queue_lot_update(
    fpv_feature_state_t* state,
    const char* runner_tag) {
  if (!state || !state->scheduler) {
    return;
  }
  if (!state->flags.auto_restore && !state->flags.auto_disable) {
    return;
  }
  if (!state->lot_update_mutex.initialized) {
    return;
  }

  fpv_mutex_lock(&state->lot_update_mutex);
  if (runner_tag && state->last_lot_update_tag &&
      strcmp(runner_tag, state->last_lot_update_tag) == 0) {
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  if (state->lot_update_running) {
    state->lot_update_pending = true;
    if (runner_tag && runner_tag[0]) {
      fpv_free(state->pending_lot_update_tag);
      state->pending_lot_update_tag = fpv_strdup(runner_tag);
    }
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  state->lot_update_running = true;
  fpv_mutex_unlock(&state->lot_update_mutex);

  fpv_lot_update_task_t* task =
      (fpv_lot_update_task_t*)calloc(1, sizeof(*task));
  if (!task) {
    fpv_mutex_lock(&state->lot_update_mutex);
    state->lot_update_running = false;
    fpv_mutex_unlock(&state->lot_update_mutex);
    return;
  }
  task->state = state;
  if (runner_tag && runner_tag[0]) {
    task->runner_tag = fpv_strdup(runner_tag);
  }
  fpv_scheduler_enqueue(state->scheduler, fpv_features_lot_update_task, task);
}
