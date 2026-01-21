/* FunPay Vertex core runtime implementation. */

#include "fpv_core/fpv_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_funpay.h"
#include "fpv_core/fpv_log.h"
#include "fpv_core/fpv_plugin.h"
#include "fpv_core/fpv_scheduler.h"
#include "fpv_core/fpv_storage.h"
#include "features/runtime/fpv_features.h"

#include "core/io/fpv_fs.h"

#include "core/base/fpv_platform.h"

#include "core/app/fpv_settings.h"

#include "core/base/fpv_string.h"

#include "telegram/core/fpv_telegram.h"

#include "core/base/fpv_time.h"


typedef struct fpv_funpay_service {
  fpv_funpay_account_t* account;
  fpv_funpay_runner_t* runner;
  fpv_event_bus_t* bus;
  fpv_logger_t* logger;
  fpv_scheduler_t* scheduler;
  fpv_funpay_account_config_t account_config;
  fpv_funpay_runner_config_t runner_config;
  uint32_t delay_ms;
  bool running;
  fpv_feature_state_t* features;
  bool features_attached;
  fpv_mutex_t mutex;
} fpv_funpay_service_t;

struct fpv_core {
  char* data_dir;
  char* config_dir;
  char* logs_dir;
  char* plugins_dir;
  char* locale;
  char* locales_dir;
  fpv_event_bus_t* bus;
  fpv_logger_t* logger;
  fpv_storage_layout_t storage;
  fpv_scheduler_t* scheduler;
  fpv_plugin_manager_t* plugin_manager;
  struct fpv_funpay_service* funpay;
  fpv_feature_state_t* features;
  fpv_telegram_service_t* telegram;
  fpv_core_status_t status;
  uint64_t start_ms;
  uint64_t last_heartbeat_ms;
};

typedef struct fpv_core_control_task {
  fpv_core_t* core;
  bool restart;
} fpv_core_control_task_t;

static void* fpv_core_control_run(void* context) {
  fpv_core_control_task_t* task = (fpv_core_control_task_t*)context;
  if (!task) {
    return NULL;
  }
  fpv_core_t* core = task->core;
  bool restart = task->restart;
  free(task);
  if (!core) {
    return NULL;
  }
  fpv_core_stop(core, fpv_time_now_ms());
  if (restart) {
    fpv_core_start(core, fpv_time_now_ms());
  } else {
    exit(0);
  }
  return NULL;
}

static void fpv_core_schedule_control(fpv_core_t* core, bool restart) {
  if (!core) {
    return;
  }
  fpv_core_control_task_t* task =
      (fpv_core_control_task_t*)calloc(1, sizeof(*task));
  if (!task) {
    return;
  }
  task->core = core;
  task->restart = restart;
  fpv_thread_t thread;
  memset(&thread, 0, sizeof(thread));
  if (!fpv_thread_create(&thread, fpv_core_control_run, task)) {
    free(task);
    return;
  }
  fpv_thread_detach(&thread);
}

static void fpv_core_request_restart(void* context) {
  fpv_core_schedule_control((fpv_core_t*)context, true);
}

static void fpv_core_request_shutdown(void* context) {
  fpv_core_schedule_control((fpv_core_t*)context, false);
}

static size_t fpv_core_default_worker_count(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  if (info.dwNumberOfProcessors < 2) {
    return 2;
  }
  return info.dwNumberOfProcessors > 8 ? 8 : info.dwNumberOfProcessors;
#else
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  if (count < 2) {
    return 2;
  }
  return (size_t)(count > 8 ? 8 : count);
#endif
}

static fpv_result_t fpv_core_publish(fpv_core_t* core, fpv_event_t* event) {
  fpv_result_t result = FPV_ERR_INTERNAL;

  if (!core || !event) {
    fpv_event_destroy(event);
    return FPV_ERR_INVALID_ARGUMENT;
  }

  result = fpv_event_bus_publish(core->bus, event);
  if (result != FPV_OK) {
    fpv_event_destroy(event);
  }
  return result;
}

static fpv_result_t fpv_core_emit_status(
    fpv_core_t* core,
    fpv_core_status_t status,
    const char* detail,
    uint64_t now_ms) {
  fpv_event_t* event = fpv_event_create_core_status(status, detail, now_ms);
  if (!event) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  return fpv_core_publish(core, event);
}

static fpv_result_t fpv_core_emit_log(
    fpv_core_t* core,
    fpv_log_level_t level,
    const char* message,
    uint64_t now_ms) {
  fpv_event_t* event =
      fpv_event_create_log(level, "core", message, now_ms);
  if (!event) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  if (core->logger && message) {
    fpv_logger_log(core->logger, level, "core", message, now_ms);
  }
  return fpv_core_publish(core, event);
}

static void fpv_funpay_account_config_clear(
    fpv_funpay_account_config_t* config) {
  if (!config) {
    return;
  }
  fpv_free((char*)config->golden_key);
  fpv_free((char*)config->user_agent);
  fpv_free((char*)config->proxy.host);
  fpv_free((char*)config->proxy.username);
  fpv_free((char*)config->proxy.password);
  memset(config, 0, sizeof(*config));
}

static bool fpv_funpay_account_config_copy(
    fpv_funpay_account_config_t* dest,
    const fpv_funpay_account_config_t* src) {
  if (!dest || !src || !src->golden_key || !src->golden_key[0]) {
    return false;
  }
  memset(dest, 0, sizeof(*dest));
  dest->timeout_ms = src->timeout_ms;
  dest->data_dir = src->data_dir;
  dest->proxy.enabled = src->proxy.enabled;
  dest->proxy.port = src->proxy.port;
  dest->golden_key = fpv_strdup(src->golden_key);
  if (src->user_agent && src->user_agent[0]) {
    dest->user_agent = fpv_strdup(src->user_agent);
  }
  if (src->proxy.host && src->proxy.host[0]) {
    dest->proxy.host = fpv_strdup(src->proxy.host);
  }
  if (src->proxy.username && src->proxy.username[0]) {
    dest->proxy.username = fpv_strdup(src->proxy.username);
  }
  if (src->proxy.password && src->proxy.password[0]) {
    dest->proxy.password = fpv_strdup(src->proxy.password);
  }

  if (!dest->golden_key ||
      (src->user_agent && src->user_agent[0] && !dest->user_agent) ||
      (src->proxy.host && src->proxy.host[0] && !dest->proxy.host) ||
      (src->proxy.username && src->proxy.username[0] && !dest->proxy.username) ||
      (src->proxy.password && src->proxy.password[0] && !dest->proxy.password)) {
    fpv_funpay_account_config_clear(dest);
    return false;
  }
  return true;
}

static fpv_funpay_service_t* fpv_funpay_service_create(
    fpv_event_bus_t* bus,
    fpv_logger_t* logger,
    fpv_scheduler_t* scheduler,
    fpv_feature_state_t* features,
    const fpv_funpay_account_config_t* account_config,
    const fpv_funpay_runner_config_t* runner_config,
    uint32_t delay_ms) {
  if (!account_config || !scheduler) {
    return NULL;
  }
  fpv_funpay_service_t* service =
      (fpv_funpay_service_t*)calloc(1, sizeof(*service));
  if (!service) {
    return NULL;
  }
  service->bus = bus;
  service->logger = logger;
  service->scheduler = scheduler;
  service->features = features;
  service->features_attached = false;
  if (!fpv_funpay_account_config_copy(&service->account_config, account_config)) {
    free(service);
    return NULL;
  }
  service->runner_config =
      runner_config ? *runner_config : (fpv_funpay_runner_config_t){0};
  service->delay_ms = delay_ms > 0 ? delay_ms : 6000;
  service->running = true;
  if (!fpv_mutex_init(&service->mutex)) {
    fpv_funpay_account_config_clear(&service->account_config);
    free(service);
    return NULL;
  }
  return service;
}

static void fpv_funpay_service_destroy(fpv_funpay_service_t* service) {
  if (!service) {
    return;
  }
  fpv_mutex_lock(&service->mutex);
  service->running = false;
  fpv_funpay_runner_destroy(service->runner);
  fpv_funpay_account_destroy(service->account);
  service->runner = NULL;
  service->account = NULL;
  fpv_mutex_unlock(&service->mutex);
  fpv_mutex_destroy(&service->mutex);
  fpv_funpay_account_config_clear(&service->account_config);
  free(service);
}

static void fpv_funpay_service_log(
    fpv_funpay_service_t* service,
    fpv_log_level_t level,
    const char* message) {
  if (!service || !message) {
    return;
  }
  uint64_t now_ms = fpv_time_now_ms();
  if (service->logger) {
    fpv_logger_log(service->logger, level, "funpay", message, now_ms);
  }
  if (service->bus) {
    fpv_event_t* event =
        fpv_event_create_log(level, "funpay", message, now_ms);
    if (event) {
      if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
        fpv_event_destroy(event);
      }
    }
  }
}

static void fpv_funpay_publish_notification(
    fpv_funpay_service_t* service,
    const char* type,
    const char* message,
    fpv_notification_severity_t severity) {
  if (!service || !service->bus) {
    return;
  }
  fpv_notification_t* notice =
      fpv_notification_create(NULL, type, message, severity, fpv_time_now_ms());
  if (!notice) {
    return;
  }
  fpv_event_t* event =
      fpv_event_create_notification(notice, fpv_time_now_ms());
  fpv_notification_destroy(notice);
  if (!event) {
    return;
  }
  if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
    fpv_event_destroy(event);
  }
}

static void fpv_funpay_service_publish_events(
    fpv_funpay_service_t* service,
    fpv_funpay_event_batch_t* batch) {
  if (!service || !batch || !batch->events) {
    return;
  }
  for (size_t i = 0; i < batch->count; i++) {
    fpv_funpay_event_t* evt = batch->events[i];
    if (!evt) {
      continue;
    }
    if (service->features && service->features_attached) {
      fpv_features_handle_event(service->features, evt);
    }
    switch (evt->type) {
      case FPV_FUNPAY_EVENT_INITIAL_CHAT:
      case FPV_FUNPAY_EVENT_LAST_CHAT_MESSAGE_CHANGED: {
        if (!evt->chat || !service->bus) {
          break;
        }
        fpv_event_t* event =
            fpv_event_create_chat(evt->chat, evt->timestamp_ms);
        if (event) {
          if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
            fpv_event_destroy(event);
          }
        }
        break;
      }
      case FPV_FUNPAY_EVENT_CHATS_LIST_CHANGED: {
        if (evt->chats) {
          for (size_t c = 0; c < evt->chat_count; c++) {
            fpv_event_t* event =
                fpv_event_create_chat(evt->chats[c], evt->timestamp_ms);
            if (event) {
              if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
                fpv_event_destroy(event);
              }
            }
          }
        }
        fpv_funpay_publish_notification(
            service,
            "chats_list_changed",
            "Chats list updated.",
            FPV_NOTIFICATION_INFO);
        break;
      }
      case FPV_FUNPAY_EVENT_NEW_MESSAGE: {
        if (!evt->message || !service->bus) {
          break;
        }
        fpv_event_t* event =
            fpv_event_create_message(evt->message, evt->timestamp_ms);
        if (event) {
          if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
            fpv_event_destroy(event);
          }
        }
        break;
      }
      case FPV_FUNPAY_EVENT_INITIAL_ORDER:
      case FPV_FUNPAY_EVENT_NEW_ORDER:
      case FPV_FUNPAY_EVENT_ORDER_STATUS_CHANGED: {
        if (!evt->order || !service->bus) {
          break;
        }
        fpv_event_t* event =
            fpv_event_create_order(evt->order, evt->timestamp_ms);
        if (event) {
          if (fpv_event_bus_publish(service->bus, event) != FPV_OK) {
            fpv_event_destroy(event);
          }
        }
        break;
      }
      case FPV_FUNPAY_EVENT_ORDERS_LIST_CHANGED: {
        char message[96];
        snprintf(
            message,
            sizeof(message),
            "Orders updated: purchases %u, sales %u",
            evt->purchases,
            evt->sales);
        fpv_funpay_publish_notification(
            service,
            "orders_list_changed",
            message,
            FPV_NOTIFICATION_INFO);
        break;
      }
      default:
        break;
    }
  }
}

static void fpv_funpay_service_poll(void* context) {
  fpv_funpay_service_t* service = (fpv_funpay_service_t*)context;
  if (!service) {
    return;
  }

  fpv_mutex_lock(&service->mutex);
  bool running = service->running;
  fpv_funpay_runner_t* runner = service->runner;
  fpv_mutex_unlock(&service->mutex);
  if (!running || !runner) {
    return;
  }

  fpv_funpay_event_batch_t batch;
  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result = fpv_funpay_runner_poll(runner, &batch, &error);
  if (result == FPV_OK) {
    fpv_funpay_service_publish_events(service, &batch);
    fpv_funpay_event_batch_destroy(&batch);
  } else {
    fpv_funpay_service_log(
        service,
        FPV_LOG_WARNING,
        error.message ? error.message : "FunPay update failed.");
  }
  fpv_funpay_error_clear(&error);

  fpv_mutex_lock(&service->mutex);
  running = service->running;
  fpv_mutex_unlock(&service->mutex);
  if (running) {
    fpv_scheduler_schedule_delay(
        service->scheduler,
        service->delay_ms,
        fpv_funpay_service_poll,
        service);
  }
}

static void fpv_funpay_service_bootstrap(void* context) {
  fpv_funpay_service_t* service = (fpv_funpay_service_t*)context;
  if (!service) {
    return;
  }

  fpv_mutex_lock(&service->mutex);
  bool running = service->running;
  bool ready = service->runner != NULL;
  fpv_mutex_unlock(&service->mutex);
  if (!running || ready) {
    return;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_funpay_account_t* account =
      fpv_funpay_account_create(&service->account_config, &error);
  if (!account) {
    fpv_funpay_service_log(
        service,
        FPV_LOG_ERROR,
        error.message ? error.message : "FunPay account creation failed.");
    fpv_funpay_error_clear(&error);
    fpv_scheduler_schedule_delay(
        service->scheduler,
        service->delay_ms,
        fpv_funpay_service_bootstrap,
        service);
    return;
  }

  fpv_funpay_account_set_logger(
      account,
      service->logger,
      true,
      service->bus);

  fpv_result_t result = fpv_funpay_account_refresh(account, &error);
  if (result != FPV_OK) {
    fpv_funpay_service_log(
        service,
        FPV_LOG_ERROR,
        error.message ? error.message : "FunPay account refresh failed.");
    fpv_funpay_account_destroy(account);
    fpv_funpay_error_clear(&error);
    fpv_scheduler_schedule_delay(
        service->scheduler,
        service->delay_ms,
        fpv_funpay_service_bootstrap,
        service);
    return;
  }

  fpv_funpay_runner_t* runner =
      fpv_funpay_runner_create(account, &service->runner_config, &error);
  if (!runner) {
    fpv_funpay_service_log(
        service,
        FPV_LOG_ERROR,
        error.message ? error.message : "FunPay runner creation failed.");
    fpv_funpay_account_destroy(account);
    fpv_funpay_error_clear(&error);
    fpv_scheduler_schedule_delay(
        service->scheduler,
        service->delay_ms,
        fpv_funpay_service_bootstrap,
        service);
    return;
  }

  fpv_mutex_lock(&service->mutex);
  service->account = account;
  service->runner = runner;
  fpv_mutex_unlock(&service->mutex);

  if (service->features && !service->features_attached) {
    fpv_features_attach(
        service->features,
        account,
        runner,
        service->scheduler,
        service->logger,
        service->bus);
    fpv_features_schedule_background(service->features);
    service->features_attached = true;
  }

  fpv_funpay_service_log(service, FPV_LOG_INFO, "FunPay runner started.");
  fpv_scheduler_schedule_delay(
      service->scheduler,
      service->delay_ms,
      fpv_funpay_service_poll,
      service);
}

fpv_core_t* fpv_core_create(
    const fpv_core_config_t* config,
    fpv_event_bus_t* bus) {
  fpv_core_t* core = NULL;

  if (!config || !bus) {
    return NULL;
  }

  core = (fpv_core_t*)calloc(1, sizeof(*core));
  if (!core) {
    return NULL;
  }

  core->data_dir = fpv_strdup(config->data_dir);
  if (config->data_dir && !core->data_dir) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->config_dir = fpv_strdup(config->config_dir);
  if (config->config_dir && !core->config_dir) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->logs_dir = fpv_strdup(config->logs_dir);
  if (config->logs_dir && !core->logs_dir) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->plugins_dir = fpv_strdup(config->plugins_dir);
  if (config->plugins_dir && !core->plugins_dir) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->locales_dir = fpv_strdup(config->locales_dir);
  if (config->locales_dir && !core->locales_dir) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->locale = fpv_strdup(config->locale);
  if (config->locale && !core->locale) {
    fpv_core_destroy(core);
    return NULL;
  }

  core->bus = bus;
  core->status = FPV_CORE_STOPPED;
  if (fpv_storage_prepare(
          core->data_dir,
          core->config_dir,
          core->logs_dir,
          core->plugins_dir,
          &core->storage) != FPV_OK) {
    fpv_core_destroy(core);
    return NULL;
  }

  fpv_log_config_t log_config;
  log_config.logs_dir = core->logs_dir;
  log_config.base_name = "fpv_core";
  log_config.min_level = FPV_LOG_INFO;
  log_config.console = true;
  core->logger = fpv_logger_create(&log_config);
  if (!core->logger) {
    fpv_event_t* log_event = fpv_event_create_log(
        FPV_LOG_ERROR,
        "core",
        "Logger initialization failed.",
        fpv_time_now_ms());
    if (log_event) {
      if (fpv_event_bus_publish(core->bus, log_event) != FPV_OK) {
        fpv_event_destroy(log_event);
      }
    }
  }

  return core;
}

void fpv_core_destroy(fpv_core_t* core) {
  if (!core) {
    return;
  }

  fpv_free(core->data_dir);
  fpv_free(core->config_dir);
  fpv_free(core->logs_dir);
  fpv_free(core->plugins_dir);
  fpv_free(core->locales_dir);
  fpv_free(core->locale);
  fpv_plugin_manager_destroy(core->plugin_manager);
  if (core->telegram) {
    fpv_telegram_service_stop(core->telegram);
    fpv_telegram_service_destroy(core->telegram);
    core->telegram = NULL;
  }
  if (core->funpay) {
    fpv_mutex_lock(&core->funpay->mutex);
    core->funpay->running = false;
    fpv_mutex_unlock(&core->funpay->mutex);
  }
  fpv_scheduler_destroy(core->scheduler);
  fpv_funpay_service_destroy(core->funpay);
  fpv_features_destroy(core->features);
  fpv_logger_destroy(core->logger);
  fpv_storage_layout_destroy(&core->storage);
  free(core);
}

fpv_result_t fpv_core_start(fpv_core_t* core, uint64_t now_ms) {
  fpv_result_t result = FPV_OK;

  if (!core) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (core->status != FPV_CORE_STOPPED) {
    return FPV_ERR_INVALID_STATE;
  }

  core->status = FPV_CORE_STARTING;
  core->start_ms = now_ms;
  core->last_heartbeat_ms = now_ms;
  result = fpv_core_emit_status(core, core->status, "starting", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
    return result;
  }

  if (!core->scheduler) {
    const size_t worker_count = fpv_core_default_worker_count();
    core->scheduler = fpv_scheduler_create(worker_count);
    if (!core->scheduler) {
      core->status = FPV_CORE_ERROR;
      fpv_core_emit_log(
          core,
          FPV_LOG_ERROR,
          "Scheduler initialization failed.",
          now_ms);
      return FPV_ERR_INTERNAL;
    }
  }

  if (!core->plugin_manager && core->plugins_dir) {
    core->plugin_manager = fpv_plugin_manager_create(
        core->plugins_dir,
        core->bus,
        core->logger,
        core->scheduler,
        NULL);
    if (core->plugin_manager) {
      fpv_result_t load_result =
          fpv_plugin_manager_load_all(core->plugin_manager);
      if (load_result != FPV_OK &&
          load_result != FPV_ERR_NOT_FOUND) {
        fpv_core_emit_log(
            core,
            FPV_LOG_WARNING,
            "Plugin load reported errors.",
            now_ms);
      }
    } else {
      fpv_core_emit_log(
          core,
          FPV_LOG_WARNING,
          "Plugin manager initialization failed.",
          now_ms);
    }
  }

  if (!core->funpay) {
    char* config_path = fpv_path_join(core->config_dir, "_main.cfg");
    if (config_path && fpv_fs_exists(config_path)) {
      char path_log[512];
      snprintf(
          path_log,
          sizeof(path_log),
          "Loading config: %s",
          config_path);
      fpv_core_emit_log(core, FPV_LOG_INFO, path_log, now_ms);

      fpv_settings_t settings;
      memset(&settings, 0, sizeof(settings));
      fpv_result_t settings_result =
          fpv_settings_load(config_path, &settings);
      if (settings_result == FPV_OK) {
        if (settings.telegram_enabled) {
          const bool token_ok =
              settings.telegram_token && settings.telegram_token[0];
          const bool secret_ok =
              settings.telegram_secret && settings.telegram_secret[0];
          char tg_log[128];
          snprintf(
              tg_log,
              sizeof(tg_log),
              "Telegram enabled (token %s, secretKey %s).",
              token_ok ? "set" : "missing",
              secret_ok ? "set" : "missing");
          fpv_core_emit_log(core, FPV_LOG_INFO, tg_log, now_ms);
          if (!token_ok) {
            fpv_core_emit_log(
                core,
                FPV_LOG_WARNING,
                "Telegram token missing; service will not start.",
                now_ms);
          }
          if (!secret_ok) {
            fpv_core_emit_log(
                core,
                FPV_LOG_INFO,
                "Telegram secretKey missing; new users cannot authorize.",
                now_ms);
          }
        } else {
          fpv_core_emit_log(
              core,
              FPV_LOG_INFO,
              "Telegram disabled in config.",
              now_ms);
        }

        if (core->features) {
          fpv_features_destroy(core->features);
          core->features = NULL;
        }
        fpv_result_t features_result =
            fpv_features_init(
                &core->features,
                &settings,
                &core->storage,
                core->locales_dir,
                core->logger);
        if (features_result != FPV_OK) {
          core->features = NULL;
          fpv_core_emit_log(
              core,
              FPV_LOG_WARNING,
              "Feature initialization failed.",
              now_ms);
        }

        if (settings.telegram_enabled &&
            settings.telegram_token && settings.telegram_token[0]) {
          core->telegram = fpv_telegram_service_create(
              settings.telegram_token,
              settings.telegram_secret,
              &core->storage,
              core->locales_dir,
              settings.language,
              core->logger,
              core->bus,
              core,
              fpv_core_request_restart,
              fpv_core_request_shutdown);
          if (core->telegram) {
            if (core->features) {
              fpv_telegram_service_attach_features(core->telegram, core->features);
              fpv_features_set_telegram(core->features, core->telegram);
            }
            if (!fpv_telegram_service_start(core->telegram)) {
              fpv_core_emit_log(
                  core,
                  FPV_LOG_WARNING,
                  "Telegram service start failed.",
                  now_ms);
            } else {
              fpv_core_emit_log(
                  core,
                  FPV_LOG_INFO,
                  "Telegram service started.",
                  now_ms);
            }
          } else {
            fpv_core_emit_log(
                core,
                FPV_LOG_WARNING,
                "Telegram service initialization failed.",
                now_ms);
          }
        }

        settings.account.data_dir = core->data_dir;
        core->funpay = fpv_funpay_service_create(
            core->bus,
            core->logger,
            core->scheduler,
            core->features,
            &settings.account,
            &settings.runner,
            settings.requests_delay_ms);
        if (core->funpay) {
          fpv_scheduler_enqueue(
              core->scheduler,
              fpv_funpay_service_bootstrap,
              core->funpay);
        } else {
          fpv_core_emit_log(
              core,
              FPV_LOG_ERROR,
              "FunPay service initialization failed.",
              now_ms);
        }
      } else {
        char parse_log[128];
        snprintf(
            parse_log,
            sizeof(parse_log),
            "FunPay config parse failed (error %d).",
            (int)settings_result);
        fpv_core_emit_log(core, FPV_LOG_WARNING, parse_log, now_ms);
      }
      fpv_settings_destroy(&settings);
    } else {
      if (config_path) {
        char missing_log[512];
        snprintf(
            missing_log,
            sizeof(missing_log),
            "FunPay config not found: %s",
            config_path);
        fpv_core_emit_log(core, FPV_LOG_INFO, missing_log, now_ms);
      } else {
        fpv_core_emit_log(
            core,
            FPV_LOG_INFO,
            "FunPay config not found.",
            now_ms);
      }
    }
    fpv_free(config_path);
  }

  core->status = FPV_CORE_RUNNING;
  result = fpv_core_emit_status(core, core->status, "running", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
    return result;
  }

  result = fpv_core_emit_log(core, FPV_LOG_INFO, "Core started.", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
  }
  return result;
}

fpv_result_t fpv_core_stop(fpv_core_t* core, uint64_t now_ms) {
  fpv_result_t result = FPV_OK;

  if (!core) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (core->status != FPV_CORE_RUNNING) {
    return FPV_ERR_INVALID_STATE;
  }

  core->status = FPV_CORE_STOPPING;
  result = fpv_core_emit_status(core, core->status, "stopping", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
    return result;
  }

  if (core->plugin_manager) {
    fpv_plugin_manager_destroy(core->plugin_manager);
    core->plugin_manager = NULL;
  }

  if (core->telegram) {
    fpv_telegram_service_stop(core->telegram);
    fpv_telegram_service_destroy(core->telegram);
    core->telegram = NULL;
  }

  if (core->funpay) {
    fpv_mutex_lock(&core->funpay->mutex);
    core->funpay->running = false;
    fpv_mutex_unlock(&core->funpay->mutex);
  }

  if (core->scheduler) {
    fpv_scheduler_destroy(core->scheduler);
    core->scheduler = NULL;
  }

  if (core->funpay) {
    fpv_funpay_service_destroy(core->funpay);
    core->funpay = NULL;
  }
  if (core->features) {
    fpv_features_destroy(core->features);
    core->features = NULL;
  }

  core->status = FPV_CORE_STOPPED;
  result = fpv_core_emit_status(core, core->status, "stopped", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
    return result;
  }

  result = fpv_core_emit_log(core, FPV_LOG_INFO, "Core stopped.", now_ms);
  if (result != FPV_OK) {
    core->status = FPV_CORE_ERROR;
  }
  return result;
}

fpv_result_t fpv_core_tick(fpv_core_t* core, uint64_t now_ms) {
  char message[96];
  uint64_t uptime_seconds = 0;

  if (!core) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (core->status != FPV_CORE_RUNNING) {
    return FPV_ERR_INVALID_STATE;
  }

  if (now_ms < core->last_heartbeat_ms) {
    core->last_heartbeat_ms = now_ms;
    return FPV_OK;
  }

  if (now_ms - core->last_heartbeat_ms < 10000) {
    return FPV_OK;
  }

  uptime_seconds = (now_ms - core->start_ms) / 1000;
  snprintf(
      message,
      sizeof(message),
      "Core heartbeat: uptime %" PRIu64 "s",
      uptime_seconds);

  core->last_heartbeat_ms = now_ms;
  return fpv_core_emit_log(core, FPV_LOG_DEBUG, message, now_ms);
}

fpv_core_status_t fpv_core_status(const fpv_core_t* core) {
  if (!core) {
    return FPV_CORE_ERROR;
  }
  return core->status;
}

fpv_result_t fpv_core_send_message(
    fpv_core_t* core,
    uint64_t chat_id,
    const char* chat_name,
    const char* message_text,
    bool add_watermark) {
  if (!core || !message_text) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->features || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  bool attached = service->features_attached;
  fpv_mutex_unlock(&service->mutex);
  if (!attached) {
    return FPV_ERR_INVALID_STATE;
  }

  return fpv_features_send_message(
      core->features,
      chat_id,
      chat_name,
      message_text,
      add_watermark);
}

fpv_result_t fpv_core_fetch_chat_history(
    fpv_core_t* core,
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
  if (!core || !out_messages || !out_count) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }
  if (!core->features ||
      !fpv_features_has_entitlement(core->features, FPV_FEATURE_STATUS_MANAGER)) {
    return FPV_ERR_UNSUPPORTED;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  bool running = service->running;
  if (!account || !running) {
    fpv_mutex_unlock(&service->mutex);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_get_chat_history(
          account,
          chat_id,
          chat_name,
          out_messages,
          out_count,
          &error);
  fpv_mutex_unlock(&service->mutex);

  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        error.message ? error.message : "Chat history request failed.",
        fpv_time_now_ms());
  }
  fpv_funpay_error_clear(&error);
  return result;
}

fpv_result_t fpv_core_reload_auto_response(fpv_core_t* core) {
  if (!core || !core->features) {
    return FPV_ERR_INVALID_STATE;
  }

  char* path = fpv_path_join(core->config_dir, "auto_response.cfg");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_result_t result = fpv_features_reload_auto_response(core->features, path);
  fpv_free(path);
  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        "Auto-response reload failed.",
        fpv_time_now_ms());
  } else {
    fpv_core_emit_log(
        core,
        FPV_LOG_INFO,
        "Auto-response config reloaded.",
        fpv_time_now_ms());
  }
  return result;
}

fpv_result_t fpv_core_reload_auto_delivery(fpv_core_t* core) {
  if (!core || !core->features) {
    return FPV_ERR_INVALID_STATE;
  }

  char* path = fpv_path_join(core->config_dir, "auto_delivery.cfg");
  if (!path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  fpv_result_t result = fpv_features_reload_auto_delivery(
      core->features,
      path,
      core->storage.products_dir);
  fpv_free(path);
  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        "Auto-delivery reload failed.",
        fpv_time_now_ms());
  } else {
    fpv_core_emit_log(
        core,
        FPV_LOG_INFO,
        "Auto-delivery config reloaded.",
        fpv_time_now_ms());
  }
  return result;
}

fpv_result_t fpv_core_refresh_lots(fpv_core_t* core) {
  if (!core) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  bool running = service->running;
  if (!account || !running) {
    fpv_mutex_unlock(&service->mutex);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_funpay_lot_section_t* sections = NULL;
  size_t section_count = 0;
  fpv_result_t result =
      fpv_funpay_account_get_lot_sections(
          account,
          &sections,
          &section_count,
          &error);
  if (result != FPV_OK) {
    fpv_mutex_unlock(&service->mutex);
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        error.message ? error.message : "Lot section request failed.",
        fpv_time_now_ms());
    fpv_funpay_error_clear(&error);
    return result;
  }
  fpv_funpay_error_clear(&error);

  char log_buf[512];
  snprintf(log_buf, sizeof(log_buf),
           "Lots refresh: sections=%zu.", section_count);
  fpv_core_emit_log(core, FPV_LOG_INFO, log_buf, fpv_time_now_ms());

  fpv_result_t final_result = FPV_OK;
  for (size_t i = 0; i < section_count; i++) {
    snprintf(log_buf, sizeof(log_buf),
             "Lots refresh: section id=%" PRIu64 " currency=%d.",
             sections[i].id,
             sections[i].is_currency ? 1 : 0);
    fpv_core_emit_log(core, FPV_LOG_INFO, log_buf, fpv_time_now_ms());

    fpv_lot_t** lots = NULL;
    size_t lot_count = 0;
    fpv_funpay_error_t lot_error;
    memset(&lot_error, 0, sizeof(lot_error));
    fpv_result_t lot_result =
        fpv_funpay_account_get_trade_lots(
            account,
            sections[i].id,
            sections[i].is_currency,
            &lots,
            &lot_count,
            &lot_error);
    if (lot_result != FPV_OK) {
      final_result = lot_result;
      fpv_core_emit_log(
          core,
          FPV_LOG_WARNING,
          lot_error.message ? lot_error.message : "Lot refresh failed.",
          fpv_time_now_ms());
      fpv_funpay_error_clear(&lot_error);
      continue;
    }
    fpv_funpay_error_clear(&lot_error);

    snprintf(log_buf, sizeof(log_buf),
             "Lots refresh: section id=%" PRIu64 " lots=%zu.",
             sections[i].id,
             lot_count);
    fpv_core_emit_log(core, FPV_LOG_INFO, log_buf, fpv_time_now_ms());

    size_t log_limit = 50;
    size_t log_count = lot_count < log_limit ? lot_count : log_limit;
    for (size_t j = 0; j < log_count; j++) {
      fpv_lot_t* lot = lots[j];
      if (!lot) {
        continue;
      }
      const char* lot_id = lot->id ? lot->id : "";
      const char* title = lot->title ? lot->title : "";
      size_t title_len = strlen(title);
      size_t title_clip = title_len > 120 ? 120 : title_len;
      snprintf(log_buf, sizeof(log_buf),
               "Lots refresh: lot id=%s title=%.*s active=%d stock=%u.",
               lot_id,
               (int)title_clip,
               title,
               lot->active ? 1 : 0,
               lot->stock);
      fpv_core_emit_log(core, FPV_LOG_INFO, log_buf, fpv_time_now_ms());
    }
    if (lot_count > log_limit) {
      snprintf(log_buf, sizeof(log_buf),
               "Lots refresh: section id=%" PRIu64 " truncated %zu lots.",
               sections[i].id,
               lot_count - log_limit);
      fpv_core_emit_log(core, FPV_LOG_INFO, log_buf, fpv_time_now_ms());
    }

    for (size_t j = 0; j < lot_count; j++) {
      if (!lots[j]) {
        continue;
      }
      fpv_event_t* event =
          fpv_event_create_lot(lots[j], fpv_time_now_ms());
      if (event) {
        if (fpv_event_bus_publish(core->bus, event) != FPV_OK) {
          fpv_event_destroy(event);
        }
      }
    }
    for (size_t j = 0; j < lot_count; j++) {
      fpv_lot_destroy(lots[j]);
    }
    fpv_free(lots);
  }

  fpv_free(sections);
  fpv_mutex_unlock(&service->mutex);

  if (final_result == FPV_OK) {
    fpv_core_emit_log(core, FPV_LOG_INFO, "Lots refreshed.", fpv_time_now_ms());
  }
  return final_result;
}

fpv_result_t fpv_core_set_lot_active(
    fpv_core_t* core,
    uint64_t lot_id,
    bool active) {
  if (!core || lot_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  bool running = service->running;
  if (!account || !running) {
    fpv_mutex_unlock(&service->mutex);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_set_lot_active(account, lot_id, active, &error);
  fpv_mutex_unlock(&service->mutex);

  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        error.message ? error.message : "Lot update failed.",
        fpv_time_now_ms());
  }
  fpv_funpay_error_clear(&error);
  return result;
}

fpv_result_t fpv_core_clone_lot(
    fpv_core_t* core,
    uint64_t lot_id,
    const char* title,
    const char* original_title,
    uint64_t* out_lot_id) {
  if (out_lot_id) {
    *out_lot_id = 0;
  }
  if (!core || lot_id == 0) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  bool running = service->running;
  if (!account || !running) {
    fpv_mutex_unlock(&service->mutex);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_clone_lot(
          account,
          lot_id,
          title,
          original_title,
          out_lot_id,
          &error);
  fpv_mutex_unlock(&service->mutex);

  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        error.message ? error.message : "Lot clone failed.",
        fpv_time_now_ms());
  }
  fpv_funpay_error_clear(&error);
  return result;
}

fpv_result_t fpv_core_clone_lot_from_url(
    fpv_core_t* core,
    const char* lot_url,
    const char* title,
    uint64_t* out_lot_id) {
  if (out_lot_id) {
    *out_lot_id = 0;
  }
  if (!core || !lot_url || !lot_url[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (core->status != FPV_CORE_RUNNING || !core->funpay) {
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_service_t* service = core->funpay;
  fpv_mutex_lock(&service->mutex);
  fpv_funpay_account_t* account = service->account;
  bool running = service->running;
  if (!account || !running) {
    fpv_mutex_unlock(&service->mutex);
    return FPV_ERR_INVALID_STATE;
  }

  fpv_funpay_error_t error;
  memset(&error, 0, sizeof(error));
  fpv_result_t result =
      fpv_funpay_account_clone_lot_from_url(
          account,
          lot_url,
          title,
          out_lot_id,
          &error);
  fpv_mutex_unlock(&service->mutex);

  if (result != FPV_OK) {
    fpv_core_emit_log(
        core,
        FPV_LOG_WARNING,
        error.message ? error.message : "Lot clone failed.",
        fpv_time_now_ms());
  }
  fpv_funpay_error_clear(&error);
  return result;
}
