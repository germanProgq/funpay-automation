/* FunPay Vertex core plugin ABI and manager API. */

#ifndef FPV_PLUGIN_H
#define FPV_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_event_bus.h"
#include "fpv_export.h"
#include "fpv_ini.h"
#include "fpv_log.h"
#include "fpv_result.h"
#include "fpv_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FPV_PLUGIN_ABI_VERSION 1u
#define FPV_PLUGIN_DESCRIPTOR_SYMBOL "fpv_plugin_get_descriptor"

typedef struct fpv_plugin_context {
  fpv_event_bus_t* bus;
  fpv_logger_t* logger;
  fpv_scheduler_t* scheduler;
  fpv_ini_t* config;
} fpv_plugin_context_t;

typedef struct fpv_plugin_descriptor {
  uint32_t abi_version;
  const char* id;
  const char* name;
  const char* version;
  uint32_t flags;
  fpv_result_t (*initialize)(const fpv_plugin_context_t* context);
  void (*shutdown)(void);
} fpv_plugin_descriptor_t;

typedef const fpv_plugin_descriptor_t* (*fpv_plugin_descriptor_fn)(void);

typedef struct fpv_plugin_manager fpv_plugin_manager_t;

FPV_CORE_API fpv_plugin_manager_t* fpv_plugin_manager_create(
    const char* plugins_dir,
    fpv_event_bus_t* bus,
    fpv_logger_t* logger,
    fpv_scheduler_t* scheduler,
    fpv_ini_t* config);
FPV_CORE_API void fpv_plugin_manager_destroy(fpv_plugin_manager_t* manager);

FPV_CORE_API fpv_result_t fpv_plugin_manager_load_all(
    fpv_plugin_manager_t* manager);
FPV_CORE_API fpv_result_t fpv_plugin_manager_load(
    fpv_plugin_manager_t* manager,
    const char* path);
FPV_CORE_API void fpv_plugin_manager_unload_all(
    fpv_plugin_manager_t* manager);
FPV_CORE_API size_t fpv_plugin_manager_count(
    const fpv_plugin_manager_t* manager);

#ifdef __cplusplus
}
#endif

#endif
