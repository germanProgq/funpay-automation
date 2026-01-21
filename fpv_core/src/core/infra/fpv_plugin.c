/* FunPay Vertex core plugin manager implementation. */

#include "fpv_core/fpv_plugin.h"

#include <stdlib.h>
#include <string.h>

#include "fpv_core/fpv_event_bus.h"
#include "fpv_core/fpv_models.h"
#include "core/io/fpv_fs.h"

#include "core/infra/fpv_shared_lib.h"

#include "core/base/fpv_string.h"

#include "core/base/fpv_time.h"


#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct fpv_plugin_record {
  fpv_shared_lib_t lib;
  const fpv_plugin_descriptor_t* descriptor;
  char* path;
  bool initialized;
} fpv_plugin_record_t;

struct fpv_plugin_manager {
  char* plugins_dir;
  fpv_plugin_context_t context;
  fpv_plugin_record_t* records;
  size_t count;
  size_t capacity;
};

static const char* fpv_plugin_extension(void) {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

static void fpv_plugin_log(
    const fpv_plugin_manager_t* manager,
    fpv_log_level_t level,
    const char* message) {
  if (manager && manager->context.logger) {
    fpv_logger_log(
        manager->context.logger,
        level,
        "plugin",
        message,
        fpv_time_now_ms());
  }
}

static bool fpv_plugin_manager_reserve(
    fpv_plugin_manager_t* manager,
    size_t capacity) {
  fpv_plugin_record_t* records = NULL;

  if (manager->capacity >= capacity) {
    return true;
  }

  records = (fpv_plugin_record_t*)realloc(
      manager->records,
      capacity * sizeof(*records));
  if (!records) {
    return false;
  }

  manager->records = records;
  manager->capacity = capacity;
  return true;
}

static bool fpv_plugin_id_exists(
    const fpv_plugin_manager_t* manager,
    const char* id) {
  if (!manager || !id) {
    return false;
  }

  for (size_t i = 0; i < manager->count; i++) {
    const fpv_plugin_descriptor_t* descriptor =
        manager->records[i].descriptor;
    if (descriptor && descriptor->id &&
        strcmp(descriptor->id, id) == 0) {
      return true;
    }
  }

  return false;
}

static fpv_result_t fpv_plugin_publish(
    fpv_plugin_manager_t* manager,
    const fpv_plugin_descriptor_t* descriptor,
    bool enabled) {
  fpv_plugin_t* plugin = NULL;
  fpv_event_t* event = NULL;

  if (!manager || !descriptor || !manager->context.bus) {
    return FPV_OK;
  }

  plugin = fpv_plugin_create(
      descriptor->id,
      descriptor->name,
      descriptor->version,
      enabled);
  if (!plugin) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  event = fpv_event_create_plugin(
      plugin,
      fpv_time_now_ms());
  fpv_plugin_destroy(plugin);
  if (!event) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (fpv_event_bus_publish(manager->context.bus, event) != FPV_OK) {
    fpv_event_destroy(event);
  }

  return FPV_OK;
}

fpv_plugin_manager_t* fpv_plugin_manager_create(
    const char* plugins_dir,
    fpv_event_bus_t* bus,
    fpv_logger_t* logger,
    fpv_scheduler_t* scheduler,
    fpv_ini_t* config) {
  fpv_plugin_manager_t* manager = NULL;

  if (!plugins_dir || !plugins_dir[0]) {
    return NULL;
  }

  manager = (fpv_plugin_manager_t*)calloc(1, sizeof(*manager));
  if (!manager) {
    return NULL;
  }

  manager->plugins_dir = fpv_strdup(plugins_dir);
  if (!manager->plugins_dir) {
    free(manager);
    return NULL;
  }

  manager->context.bus = bus;
  manager->context.logger = logger;
  manager->context.scheduler = scheduler;
  manager->context.config = config;
  return manager;
}

void fpv_plugin_manager_destroy(fpv_plugin_manager_t* manager) {
  if (!manager) {
    return;
  }

  fpv_plugin_manager_unload_all(manager);
  fpv_free(manager->plugins_dir);
  free(manager->records);
  free(manager);
}

fpv_result_t fpv_plugin_manager_load(
    fpv_plugin_manager_t* manager,
    const char* path) {
  fpv_shared_lib_t lib = {0};
  fpv_plugin_descriptor_fn descriptor_fn = NULL;
  const fpv_plugin_descriptor_t* descriptor = NULL;
  fpv_result_t result = FPV_OK;
  fpv_plugin_record_t record;

  if (!manager || !path) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  if (!fpv_shared_lib_open(&lib, path)) {
    return FPV_ERR_IO;
  }

  descriptor_fn = (fpv_plugin_descriptor_fn)fpv_shared_lib_symbol(
      &lib,
      FPV_PLUGIN_DESCRIPTOR_SYMBOL);
  if (!descriptor_fn) {
    fpv_shared_lib_close(&lib);
    return FPV_ERR_NOT_FOUND;
  }

  descriptor = descriptor_fn();
  if (!descriptor ||
      descriptor->abi_version != FPV_PLUGIN_ABI_VERSION ||
      !descriptor->id || !descriptor->name || !descriptor->version) {
    fpv_shared_lib_close(&lib);
    return FPV_ERR_UNSUPPORTED;
  }

  if (fpv_plugin_id_exists(manager, descriptor->id)) {
    fpv_shared_lib_close(&lib);
    return FPV_ERR_INVALID_STATE;
  }

  if (descriptor->initialize) {
    result = descriptor->initialize(&manager->context);
    if (result != FPV_OK) {
      fpv_shared_lib_close(&lib);
      return result;
    }
  }

  memset(&record, 0, sizeof(record));
  record.lib = lib;
  record.descriptor = descriptor;
  record.path = fpv_strdup(path);
  record.initialized = true;

  if (!record.path) {
    if (descriptor->shutdown) {
      descriptor->shutdown();
    }
    fpv_shared_lib_close(&lib);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (!fpv_plugin_manager_reserve(manager, manager->count + 1)) {
    fpv_free(record.path);
    if (descriptor->shutdown) {
      descriptor->shutdown();
    }
    fpv_shared_lib_close(&lib);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  manager->records[manager->count++] = record;
  fpv_plugin_publish(manager, descriptor, true);
  return FPV_OK;
}

fpv_result_t fpv_plugin_manager_load_all(fpv_plugin_manager_t* manager) {
  const char* extension = NULL;
  fpv_result_t result = FPV_OK;
  fpv_result_t last_error = FPV_OK;

  if (!manager || !manager->plugins_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  extension = fpv_plugin_extension();
  if (!fpv_fs_exists(manager->plugins_dir)) {
    return FPV_ERR_NOT_FOUND;
  }
  if (!fpv_fs_is_dir(manager->plugins_dir)) {
    return FPV_ERR_INVALID_STATE;
  }

#if defined(_WIN32)
  {
    char* pattern = fpv_path_join(manager->plugins_dir, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle = INVALID_HANDLE_VALUE;

    if (!pattern) {
      return FPV_ERR_OUT_OF_MEMORY;
    }

    handle = FindFirstFileA(pattern, &data);
    fpv_free(pattern);

    if (handle == INVALID_HANDLE_VALUE) {
      return FPV_ERR_IO;
    }

    do {
      if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        continue;
      }

      if (!fpv_path_has_extension(data.cFileName, extension)) {
        continue;
      }

      char* full_path = fpv_path_join(manager->plugins_dir, data.cFileName);
      if (!full_path) {
        last_error = FPV_ERR_OUT_OF_MEMORY;
        continue;
      }

      result = fpv_plugin_manager_load(manager, full_path);
      fpv_free(full_path);
      if (result != FPV_OK) {
        last_error = result;
        fpv_plugin_log(manager, FPV_LOG_WARNING, "Plugin load failed.");
      }
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
  }
#else
  {
    DIR* dir = opendir(manager->plugins_dir);
    if (!dir) {
      return FPV_ERR_IO;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      if (entry->d_name[0] == '.') {
        continue;
      }

      if (!fpv_path_has_extension(entry->d_name, extension)) {
        continue;
      }

      char* full_path = fpv_path_join(manager->plugins_dir, entry->d_name);
      if (!full_path) {
        last_error = FPV_ERR_OUT_OF_MEMORY;
        continue;
      }

      result = fpv_plugin_manager_load(manager, full_path);
      fpv_free(full_path);
      if (result != FPV_OK) {
        last_error = result;
        fpv_plugin_log(manager, FPV_LOG_WARNING, "Plugin load failed.");
      }
    }

    closedir(dir);
  }
#endif

  return last_error == FPV_OK ? FPV_OK : last_error;
}

void fpv_plugin_manager_unload_all(fpv_plugin_manager_t* manager) {
  if (!manager) {
    return;
  }

  for (size_t i = 0; i < manager->count; i++) {
    fpv_plugin_record_t* record = &manager->records[i];
    if (record->descriptor && record->descriptor->shutdown &&
        record->initialized) {
      record->descriptor->shutdown();
      record->initialized = false;
    }
    fpv_plugin_publish(manager, record->descriptor, false);
    fpv_shared_lib_close(&record->lib);
    fpv_free(record->path);
  }

  manager->count = 0;
}

size_t fpv_plugin_manager_count(const fpv_plugin_manager_t* manager) {
  if (!manager) {
    return 0;
  }
  return manager->count;
}
