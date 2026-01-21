/* FunPay Vertex feature config models. */

#ifndef FPV_FEATURE_CONFIG_H
#define FPV_FEATURE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#include "fpv_core/fpv_result.h"

typedef struct fpv_auto_response_command {
  char* command;
  char* response;
  bool telegram_notification;
  char* notification_text;
} fpv_auto_response_command_t;

typedef struct fpv_auto_response_config {
  fpv_auto_response_command_t* commands;
  size_t count;
} fpv_auto_response_config_t;

typedef struct fpv_auto_delivery_lot {
  char* lot_name;
  char* response;
  char* products_file;
  bool disable;
  bool disable_auto_restore;
  bool disable_auto_disable;
  bool disable_auto_delivery;
  bool disable_multi_delivery;
} fpv_auto_delivery_lot_t;

typedef struct fpv_auto_delivery_config {
  fpv_auto_delivery_lot_t* lots;
  size_t count;
} fpv_auto_delivery_config_t;

fpv_result_t fpv_auto_response_config_load(
    const char* path,
    fpv_auto_response_config_t* config);
const fpv_auto_response_command_t* fpv_auto_response_find(
    const fpv_auto_response_config_t* config,
    const char* command);
void fpv_auto_response_config_destroy(fpv_auto_response_config_t* config);

fpv_result_t fpv_auto_delivery_config_load(
    const char* path,
    const char* products_dir,
    fpv_auto_delivery_config_t* config);
const fpv_auto_delivery_lot_t* fpv_auto_delivery_find(
    const fpv_auto_delivery_config_t* config,
    const char* lot_name);
void fpv_auto_delivery_config_destroy(fpv_auto_delivery_config_t* config);

#endif
