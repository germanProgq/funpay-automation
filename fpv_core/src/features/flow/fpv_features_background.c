#include "features/runtime/fpv_features_internal.h"


#include <limits.h>
#include <stdlib.h>
#include <string.h>

static fpv_raise_entry_t* fpv_raise_entry_get(
    fpv_feature_state_t* state,
    uint64_t category_id) {
  if (!state) {
    return NULL;
  }
  for (size_t i = 0; i < state->raise_count; i++) {
    if (state->raise_entries[i].category_id == category_id) {
      return &state->raise_entries[i];
    }
  }
  fpv_raise_entry_t* grown = (fpv_raise_entry_t*)realloc(
      state->raise_entries,
      (state->raise_count + 1) * sizeof(*grown));
  if (!grown) {
    return NULL;
  }
  state->raise_entries = grown;
  fpv_raise_entry_t* entry = &state->raise_entries[state->raise_count++];
  entry->category_id = category_id;
  entry->next_raise_ms = 0;
  return entry;
}


static void fpv_raise_task(void* context) {
  fpv_feature_state_t* state = (fpv_feature_state_t*)context;
  if (!state || !state->scheduler || !state->account) {
    return;
  }

  uint64_t now_ms = fpv_time_now_ms();
  uint32_t next_delay = 10000;
  if (!state->flags.auto_raise) {
    fpv_scheduler_schedule_delay(
        state->scheduler,
        next_delay,
        fpv_raise_task,
        state);
    return;
  }

  uint64_t* subcats = NULL;
  size_t subcat_count = 0;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_account_get_lot_subcategories(
      state->account,
      &subcats,
      &subcat_count,
      &error);
  fpv_funpay_error_clear(&error);
  if (result != FPV_OK || subcat_count == 0) {
    fpv_free(subcats);
    fpv_scheduler_schedule_delay(
        state->scheduler,
        next_delay,
        fpv_raise_task,
        state);
    return;
  }

  size_t target_count = 0;
  uint64_t* categories = NULL;
  uint64_t* counts = NULL;
  uint64_t** grouped = NULL;
  char** category_names = NULL;

  for (size_t i = 0; i < subcat_count; i++) {
    uint64_t category_id = 0;
    const char* category_name = NULL;
    if (!fpv_funpay_account_get_subcategory_category(
            state->account,
            subcats[i],
            &category_id,
            &category_name)) {
      continue;
    }
    size_t index = 0;
    for (; index < target_count; index++) {
      if (categories[index] == category_id) {
        break;
      }
    }
    if (index == target_count) {
      uint64_t* cat_grown = (uint64_t*)realloc(
          categories,
          (target_count + 1) * sizeof(*cat_grown));
      uint64_t* count_grown = (uint64_t*)realloc(
          counts,
          (target_count + 1) * sizeof(*count_grown));
      uint64_t** grouped_grown = (uint64_t**)realloc(
          grouped,
          (target_count + 1) * sizeof(*grouped_grown));
      char** names_grown = (char**)realloc(
          category_names,
          (target_count + 1) * sizeof(*names_grown));
      if (!cat_grown || !count_grown || !grouped_grown || !names_grown) {
        fpv_free(cat_grown);
        fpv_free(count_grown);
        fpv_free(grouped_grown);
        fpv_free(names_grown);
        break;
      }
      categories = cat_grown;
      counts = count_grown;
      grouped = grouped_grown;
      category_names = names_grown;
      categories[target_count] = category_id;
      counts[target_count] = 0;
      grouped[target_count] = NULL;
      category_names[target_count] =
          category_name ? fpv_strdup(category_name) : NULL;
      index = target_count++;
    }

    uint64_t* list = (uint64_t*)realloc(
        grouped[index],
        (counts[index] + 1) * sizeof(*list));
    if (!list) {
      continue;
    }
    grouped[index] = list;
    grouped[index][counts[index]++] = subcats[i];
  }

  fpv_free(subcats);

  uint64_t min_next_ms = 0;
  for (size_t i = 0; i < target_count; i++) {
    fpv_raise_entry_t* entry = fpv_raise_entry_get(state, categories[i]);
    if (!entry) {
      continue;
    }
    if (entry->next_raise_ms > now_ms) {
      if (min_next_ms == 0 || entry->next_raise_ms < min_next_ms) {
        min_next_ms = entry->next_raise_ms;
      }
      continue;
    }

    uint32_t wait_seconds = 0;
    fpv_funpay_error_t raise_error;
    memset(&raise_error, 0, sizeof(raise_error));
    fpv_result_t raise_result = fpv_funpay_account_raise_lots(
        state->account,
        categories[i],
        grouped[i],
        (size_t)counts[i],
        &wait_seconds,
        &raise_error);
    fpv_funpay_error_clear(&raise_error);

    if (raise_result == FPV_OK) {
      entry->next_raise_ms = now_ms + 3600ULL * 1000ULL;
      if (state->telegram && category_names &&
          category_names[i] && category_names[i][0]) {
        fpv_telegram_service_notify_lots_raised(
            state->telegram, category_names[i]);
      }
    } else if (wait_seconds > 0) {
      entry->next_raise_ms = now_ms + (uint64_t)wait_seconds * 1000ULL;
    } else {
      entry->next_raise_ms = now_ms + 10000ULL;
    }

    if (min_next_ms == 0 || entry->next_raise_ms < min_next_ms) {
      min_next_ms = entry->next_raise_ms;
    }
  }

  for (size_t i = 0; i < target_count; i++) {
    fpv_free(grouped[i]);
  }
  fpv_free(grouped);
  fpv_free(categories);
  fpv_free(counts);
  if (category_names) {
    for (size_t i = 0; i < target_count; i++) {
      fpv_free(category_names[i]);
    }
    fpv_free(category_names);
  }

  if (min_next_ms > now_ms) {
    uint64_t diff = min_next_ms - now_ms;
    next_delay = (uint32_t)(diff > UINT32_MAX ? UINT32_MAX : diff);
  }

  fpv_scheduler_schedule_delay(
      state->scheduler,
      next_delay,
      fpv_raise_task,
      state);
}


static void fpv_session_refresh_task(void* context) {
  fpv_feature_state_t* state = (fpv_feature_state_t*)context;
  if (!state || !state->scheduler || !state->account) {
    return;
  }
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_refresh(state->account, &error);
  fpv_funpay_error_clear(&error);
  uint32_t next_delay = result == FPV_OK ? 3600000U : 60000U;
  fpv_scheduler_schedule_delay(
      state->scheduler,
      next_delay,
      fpv_session_refresh_task,
      state);
}


static void fpv_sras_task(void* context) {
  fpv_feature_state_t* state = (fpv_feature_state_t*)context;
  if (!state || !state->scheduler || !state->account) {
    return;
  }

  uint32_t next_delay = 900000;
  if (!fpv_feature_mask_has(state->entitlements, FPV_FEATURE_SRAS_MONITOR)) {
    fpv_scheduler_schedule_delay(
        state->scheduler,
        next_delay,
        fpv_sras_task,
        state);
    return;
  }

  if (!fpv_funpay_account_is_initiated(state->account)) {
    fpv_funpay_error_t refresh_error;
    memset(&refresh_error, 0, sizeof(refresh_error));
    fpv_result_t refresh_result =
        fpv_funpay_account_refresh(state->account, &refresh_error);
    fpv_funpay_error_clear(&refresh_error);
    if (refresh_result != FPV_OK) {
      fpv_scheduler_schedule_delay(
          state->scheduler,
          next_delay,
          fpv_sras_task,
          state);
      return;
    }
  }

  fpv_funpay_balance_t balance;
  fpv_funpay_error_t error;
  memset(&balance, 0, sizeof(balance));
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_get_balance(state->account, &balance, &error);
  if (result == FPV_OK) {
    if (state->sras_active) {
      state->sras_active = false;
      if (state->telegram) {
        fpv_telegram_service_notify_sras(state->telegram, false);
      }
      fpv_features_log(
          state,
          FPV_LOG_INFO,
          "SRAS monitor: account stats restored.");
    }
  } else if (fpv_features_is_sras_error(&error)) {
    if (!state->sras_active) {
      state->sras_active = true;
      if (state->telegram) {
        fpv_telegram_service_notify_sras(state->telegram, true);
      }
      fpv_features_log(
          state,
          FPV_LOG_WARNING,
          "SRAS monitor: account stats unavailable.");
    }
  }
  fpv_funpay_error_clear(&error);

  fpv_scheduler_schedule_delay(
      state->scheduler,
      next_delay,
      fpv_sras_task,
      state);
}


void fpv_features_schedule_background(fpv_feature_state_t* state) {
  if (!state || !state->scheduler || state->background_scheduled) {
    return;
  }
  state->background_scheduled = true;
  fpv_scheduler_schedule_delay(
      state->scheduler,
      10000,
      fpv_raise_task,
      state);
  fpv_scheduler_schedule_delay(
      state->scheduler,
      3600000,
      fpv_session_refresh_task,
      state);
  fpv_scheduler_schedule_delay(
      state->scheduler,
      120000,
      fpv_sras_task,
      state);
}
