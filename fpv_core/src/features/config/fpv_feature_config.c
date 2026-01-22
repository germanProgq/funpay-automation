/* FunPay Vertex feature config implementation. */

#include "features/config/fpv_feature_config.h"


#include <stdlib.h>
#include <string.h>

#include "fpv_core/fpv_ini.h"
#include "core/io/fpv_fs.h"

#include "core/base/fpv_string.h"


static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static int fpv_ascii_strcasecmp(const char* left, const char* right) {
  size_t index = 0;
  while (left[index] != '\0' || right[index] != '\0') {
    int a = fpv_ascii_tolower((unsigned char)left[index]);
    int b = fpv_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
    if (left[index] == '\0' || right[index] == '\0') {
      break;
    }
    index++;
  }
  return 0;
}

static const char* fpv_trim_start(const char* value) {
  while (value && *value && (*value == ' ' || *value == '\t')) {
    value++;
  }
  return value;
}

static char* fpv_trim_copy(const char* value) {
  const char* start = fpv_trim_start(value);
  if (!start) {
    return NULL;
  }
  const char* end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
    end--;
  }
  return fpv_strdup_n(start, (size_t)(end - start));
}

static bool fpv_parse_bool(const char* value, bool* out) {
  if (!value || !out) {
    return false;
  }
  if (value[0] == '1' && value[1] == '\0') {
    *out = true;
    return true;
  }
  if (value[0] == '0' && value[1] == '\0') {
    *out = false;
    return true;
  }
  if (fpv_ascii_strcasecmp(value, "true") == 0) {
    *out = true;
    return true;
  }
  if (fpv_ascii_strcasecmp(value, "false") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static char* fpv_lower_ascii_copy(const char* value) {
  if (!value) {
    return NULL;
  }
  size_t length = strlen(value);
  char* copy = (char*)malloc(length + 1);
  if (!copy) {
    return NULL;
  }
  for (size_t i = 0; i < length; i++) {
    copy[i] = (char)fpv_ascii_tolower((unsigned char)value[i]);
  }
  copy[length] = '\0';
  return copy;
}

static const fpv_auto_response_command_t* fpv_auto_response_find_exact(
    const fpv_auto_response_config_t* config,
    const char* command) {
  if (!config || !command) {
    return NULL;
  }
  for (size_t i = 0; i < config->count; i++) {
    if (config->commands[i].command &&
        strcmp(config->commands[i].command, command) == 0) {
      return &config->commands[i];
    }
  }
  return NULL;
}

static fpv_result_t fpv_auto_response_append(
    fpv_auto_response_config_t* config,
    const char* command,
    const char* response,
    bool telegram_notification,
    const char* notification_text) {
  if (!config || !command || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  fpv_auto_response_command_t* grown = (fpv_auto_response_command_t*)realloc(
      config->commands,
      (config->count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  config->commands = grown;
  fpv_auto_response_command_t* entry = &config->commands[config->count];
  memset(entry, 0, sizeof(*entry));
  entry->command = fpv_lower_ascii_copy(command);
  entry->response = fpv_strdup(response);
  entry->telegram_notification = telegram_notification;
  if (notification_text && notification_text[0]) {
    entry->notification_text = fpv_strdup(notification_text);
  }
  if (!entry->command || !entry->response) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  config->count++;
  return FPV_OK;
}

fpv_result_t fpv_auto_response_config_load(
    const char* path,
    fpv_auto_response_config_t* config) {
  if (!path || !config) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  memset(config, 0, sizeof(*config));

  fpv_ini_error_t ini_error;
  fpv_ini_t* ini = fpv_ini_load(path, &ini_error);
  if (!ini) {
    return ini_error.code == FPV_OK ? FPV_ERR_PARSE : ini_error.code;
  }

  size_t section_count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < section_count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (!section || !section[0]) {
      fpv_auto_response_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    const char* response = fpv_ini_get(ini, section, "response");
    if (!response || !response[0]) {
      fpv_auto_response_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    const char* telegram_value =
        fpv_ini_get(ini, section, "telegramNotification");
    bool telegram_notification = false;
    if (telegram_value && telegram_value[0] &&
        !fpv_parse_bool(telegram_value, &telegram_notification)) {
      fpv_auto_response_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    const char* notification_text =
        fpv_ini_get(ini, section, "notificationText");
    if (notification_text && !notification_text[0]) {
      fpv_auto_response_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    const char* splitter = strchr(section, '|');
    if (!splitter) {
      if (fpv_auto_response_find_exact(config, section)) {
        fpv_auto_response_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
      fpv_result_t append_result =
          fpv_auto_response_append(
              config,
              section,
              response,
              telegram_notification,
              notification_text);
      if (append_result != FPV_OK) {
        fpv_auto_response_config_destroy(config);
        fpv_ini_destroy(ini);
        return append_result;
      }
      continue;
    }

    char* section_copy = fpv_strdup(section);
    if (!section_copy) {
      fpv_auto_response_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    char* token = strtok(section_copy, "|");
    while (token) {
      char* trimmed = fpv_trim_copy(token);
      if (!trimmed) {
        fpv_free(section_copy);
        fpv_auto_response_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_OUT_OF_MEMORY;
      }
      if (trimmed[0] != '\0') {
        if (fpv_auto_response_find_exact(config, trimmed)) {
          fpv_free(trimmed);
          fpv_free(section_copy);
          fpv_auto_response_config_destroy(config);
          fpv_ini_destroy(ini);
          return FPV_ERR_PARSE;
        }
        fpv_result_t append_result =
            fpv_auto_response_append(
                config,
                trimmed,
                response,
                telegram_notification,
                notification_text);
        fpv_free(trimmed);
        if (append_result != FPV_OK) {
          fpv_free(section_copy);
          fpv_auto_response_config_destroy(config);
          fpv_ini_destroy(ini);
          return append_result;
        }
      } else {
        fpv_free(trimmed);
      }
      token = strtok(NULL, "|");
    }
    fpv_free(section_copy);
  }

  fpv_ini_destroy(ini);
  return FPV_OK;
}

const fpv_auto_response_command_t* fpv_auto_response_find(
    const fpv_auto_response_config_t* config,
    const char* command) {
  if (!config || !command) {
    return NULL;
  }
  char* lowered = fpv_lower_ascii_copy(command);
  if (!lowered) {
    return NULL;
  }
  const fpv_auto_response_command_t* found =
      fpv_auto_response_find_exact(config, lowered);
  fpv_free(lowered);
  return found;
}

void fpv_auto_response_config_destroy(fpv_auto_response_config_t* config) {
  if (!config) {
    return;
  }
  for (size_t i = 0; i < config->count; i++) {
    fpv_free(config->commands[i].command);
    fpv_free(config->commands[i].response);
    fpv_free(config->commands[i].notification_text);
  }
  fpv_free(config->commands);
  config->commands = NULL;
  config->count = 0;
}

static fpv_result_t fpv_auto_delivery_append(
    fpv_auto_delivery_config_t* config,
    const char* lot_name,
    const char* response,
    const char* products_file,
    bool timed,
    uint32_t timer_hours,
    uint32_t multi_delivery_count,
    bool disable,
    bool disable_auto_restore,
    bool disable_auto_disable,
    bool disable_auto_delivery,
    bool disable_multi_delivery) {
  fpv_auto_delivery_lot_t* grown = (fpv_auto_delivery_lot_t*)realloc(
      config->lots,
      (config->count + 1) * sizeof(*grown));
  if (!grown) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  config->lots = grown;
  fpv_auto_delivery_lot_t* entry = &config->lots[config->count];
  memset(entry, 0, sizeof(*entry));
  entry->lot_name = fpv_strdup(lot_name);
  entry->response = fpv_strdup(response);
  if (products_file && products_file[0]) {
    entry->products_file = fpv_strdup(products_file);
  }
  entry->timed = timed;
  entry->timer_hours = timer_hours;
  entry->multi_delivery_count = multi_delivery_count;
  entry->disable = disable;
  entry->disable_auto_restore = disable_auto_restore;
  entry->disable_auto_disable = disable_auto_disable;
  entry->disable_auto_delivery = disable_auto_delivery;
  entry->disable_multi_delivery = disable_multi_delivery;
  if (!entry->lot_name || !entry->response) {
    return FPV_ERR_OUT_OF_MEMORY;
  }
  config->count++;
  return FPV_OK;
}

fpv_result_t fpv_auto_delivery_config_load(
    const char* path,
    const char* products_dir,
    fpv_auto_delivery_config_t* config) {
  if (!path || !config || !products_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  memset(config, 0, sizeof(*config));

  fpv_ini_error_t ini_error;
  fpv_ini_t* ini = fpv_ini_load(path, &ini_error);
  if (!ini) {
    return ini_error.code == FPV_OK ? FPV_ERR_PARSE : ini_error.code;
  }

  size_t section_count = fpv_ini_section_count(ini);
  for (size_t i = 0; i < section_count; i++) {
    const char* section = fpv_ini_section_name(ini, i);
    if (!section || !section[0]) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    const char* response = fpv_ini_get(ini, section, "response");
    if (!response || !response[0]) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    const char* timed_value = fpv_ini_get(ini, section, "timed");
    bool timed = false;
    if (timed_value && timed_value[0] &&
        !fpv_parse_bool(timed_value, &timed)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    const char* timer_value = fpv_ini_get(ini, section, "timerHours");
    uint32_t timer_hours = 0;
    if (timer_value && timer_value[0]) {
      char* end = NULL;
      unsigned long parsed = strtoul(timer_value, &end, 10);
      if (!end || *end != '\0') {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
      if (parsed > UINT32_MAX) {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
      timer_hours = (uint32_t)parsed;
    }

    const char* multi_delivery_count_value =
        fpv_ini_get(ini, section, "multiDeliveryCount");
    uint32_t multi_delivery_count = 0;
    if (multi_delivery_count_value && multi_delivery_count_value[0]) {
      char* end = NULL;
      unsigned long parsed = strtoul(multi_delivery_count_value, &end, 10);
      if (!end || *end != '\0' || parsed == 0 || parsed > UINT32_MAX) {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
      multi_delivery_count = (uint32_t)parsed;
    }

    const char* products_file =
        fpv_ini_get(ini, section, "productsFileName");
    if (products_file && !products_file[0]) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    if (timed) {
      if (!products_file || !products_file[0]) {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
      if (timer_hours == 0) {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
    }

    if (products_file && products_file[0]) {
      char* full_path = fpv_path_join(products_dir, products_file);
      if (!full_path || !fpv_fs_exists(full_path)) {
        fpv_free(full_path);
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_NOT_FOUND;
      }
      fpv_free(full_path);
      if (!timed &&
          !strstr(response, "$product") &&
          !strstr(response, "$products")) {
        fpv_auto_delivery_config_destroy(config);
        fpv_ini_destroy(ini);
        return FPV_ERR_PARSE;
      }
    }

    const char* disable_value = fpv_ini_get(ini, section, "disable");
    const char* disable_restore_value =
        fpv_ini_get(ini, section, "disableAutoRestore");
    const char* disable_disable_value =
        fpv_ini_get(ini, section, "disableAutoDisable");
    const char* disable_delivery_value =
        fpv_ini_get(ini, section, "disableAutoDelivery");
    const char* disable_multi_value =
        fpv_ini_get(ini, section, "disableMultiDelivery");

    bool disable = false;
    bool disable_auto_restore = false;
    bool disable_auto_disable = false;
    bool disable_auto_delivery = false;
    bool disable_multi_delivery = false;
    if (disable_value && disable_value[0] &&
        !fpv_parse_bool(disable_value, &disable)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    if (disable_restore_value && disable_restore_value[0] &&
        !fpv_parse_bool(disable_restore_value, &disable_auto_restore)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    if (disable_disable_value && disable_disable_value[0] &&
        !fpv_parse_bool(disable_disable_value, &disable_auto_disable)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    if (disable_delivery_value && disable_delivery_value[0] &&
        !fpv_parse_bool(disable_delivery_value, &disable_auto_delivery)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }
    if (disable_multi_value && disable_multi_value[0] &&
        !fpv_parse_bool(disable_multi_value, &disable_multi_delivery)) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return FPV_ERR_PARSE;
    }

    fpv_result_t append_result =
        fpv_auto_delivery_append(
            config,
            section,
            response,
            products_file,
            timed,
            timer_hours,
            multi_delivery_count,
            disable,
            disable_auto_restore,
            disable_auto_disable,
            disable_auto_delivery,
            disable_multi_delivery);
    if (append_result != FPV_OK) {
      fpv_auto_delivery_config_destroy(config);
      fpv_ini_destroy(ini);
      return append_result;
    }
  }

  fpv_ini_destroy(ini);
  return FPV_OK;
}

const fpv_auto_delivery_lot_t* fpv_auto_delivery_find(
    const fpv_auto_delivery_config_t* config,
    const char* lot_name) {
  if (!config || !lot_name) {
    return NULL;
  }
  for (size_t i = 0; i < config->count; i++) {
    if (config->lots[i].lot_name &&
        strstr(lot_name, config->lots[i].lot_name)) {
      return &config->lots[i];
    }
  }
  return NULL;
}

void fpv_auto_delivery_config_destroy(fpv_auto_delivery_config_t* config) {
  if (!config) {
    return;
  }
  for (size_t i = 0; i < config->count; i++) {
    fpv_free(config->lots[i].lot_name);
    fpv_free(config->lots[i].response);
    fpv_free(config->lots[i].products_file);
  }
  fpv_free(config->lots);
  config->lots = NULL;
  config->count = 0;
}
