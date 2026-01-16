/* FunPay Vertex core filesystem utilities. */

#ifndef FPV_FS_H
#define FPV_FS_H

#include <stdbool.h>

#include "fpv_core/fpv_result.h"

char* fpv_path_join(const char* left, const char* right);
bool fpv_path_has_extension(const char* path, const char* extension);

fpv_result_t fpv_fs_ensure_dir(const char* path);
bool fpv_fs_exists(const char* path);
bool fpv_fs_is_dir(const char* path);
fpv_result_t fpv_fs_rename(const char* from, const char* to);

#endif
