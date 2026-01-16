/* FunPay Vertex core storage layout implementation. */

#include "fpv_core/fpv_storage.h"

#include <stdlib.h>
#include <string.h>

#include "fpv_fs.h"
#include "fpv_string.h"

static void fpv_storage_reset(fpv_storage_layout_t* layout) {
  if (!layout) {
    return;
  }

  fpv_free(layout->data_dir);
  fpv_free(layout->config_dir);
  fpv_free(layout->logs_dir);
  fpv_free(layout->cache_dir);
  fpv_free(layout->products_dir);
  fpv_free(layout->plugins_dir);
  memset(layout, 0, sizeof(*layout));
}

fpv_result_t fpv_storage_prepare(
    const char* data_dir,
    const char* config_dir,
    const char* logs_dir,
    const char* plugins_dir,
    fpv_storage_layout_t* layout) {
  fpv_result_t result = FPV_OK;
  char* old_path = NULL;
  char* new_path = NULL;

  if (!layout || !data_dir || !config_dir || !logs_dir) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  memset(layout, 0, sizeof(*layout));

  layout->data_dir = fpv_strdup(data_dir);
  layout->config_dir = fpv_strdup(config_dir);
  layout->logs_dir = fpv_strdup(logs_dir);
  if (!layout->data_dir || !layout->config_dir || !layout->logs_dir) {
    fpv_storage_reset(layout);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  if (plugins_dir && plugins_dir[0]) {
    layout->plugins_dir = fpv_strdup(plugins_dir);
    if (!layout->plugins_dir) {
      fpv_storage_reset(layout);
      return FPV_ERR_OUT_OF_MEMORY;
    }
  }

  result = fpv_fs_ensure_dir(layout->data_dir);
  if (result != FPV_OK) {
    fpv_storage_reset(layout);
    return result;
  }

  result = fpv_fs_ensure_dir(layout->config_dir);
  if (result != FPV_OK) {
    fpv_storage_reset(layout);
    return result;
  }

  result = fpv_fs_ensure_dir(layout->logs_dir);
  if (result != FPV_OK) {
    fpv_storage_reset(layout);
    return result;
  }

  if (layout->plugins_dir) {
    result = fpv_fs_ensure_dir(layout->plugins_dir);
    if (result != FPV_OK) {
      fpv_storage_reset(layout);
      return result;
    }
  }

  layout->cache_dir = fpv_path_join(layout->data_dir, "cache");
  layout->products_dir = fpv_path_join(layout->data_dir, "products");
  if (!layout->cache_dir || !layout->products_dir) {
    fpv_storage_reset(layout);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  result = fpv_fs_ensure_dir(layout->cache_dir);
  if (result != FPV_OK) {
    fpv_storage_reset(layout);
    return result;
  }

  result = fpv_fs_ensure_dir(layout->products_dir);
  if (result != FPV_OK) {
    fpv_storage_reset(layout);
    return result;
  }

  old_path = fpv_path_join(layout->cache_dir, "block_list.json");
  new_path = fpv_path_join(layout->cache_dir, "blacklist.json");
  if (old_path && new_path &&
      fpv_fs_exists(old_path) &&
      !fpv_fs_exists(new_path)) {
    fpv_fs_rename(old_path, new_path);
  }

  fpv_free(old_path);
  fpv_free(new_path);
  return FPV_OK;
}

void fpv_storage_layout_destroy(fpv_storage_layout_t* layout) {
  fpv_storage_reset(layout);
}
