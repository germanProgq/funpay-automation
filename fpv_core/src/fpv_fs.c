/* FunPay Vertex core filesystem utilities. */

#include "fpv_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpv_string.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if defined(_WIN32)
#define FPV_PATH_SEP '\\'
#else
#define FPV_PATH_SEP '/'
#endif

static bool fpv_fs_create_dir(const char* path) {
#if defined(_WIN32)
  if (CreateDirectoryA(path, NULL)) {
    return true;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return fpv_fs_is_dir(path);
  }
  return false;
#else
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  if (errno == EEXIST) {
    return fpv_fs_is_dir(path);
  }
  return false;
#endif
}

bool fpv_fs_exists(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
#if defined(_WIN32)
  return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
  struct stat info;
  return stat(path, &info) == 0;
#endif
}

bool fpv_fs_is_dir(const char* path) {
  if (!path || !path[0]) {
    return false;
  }
#if defined(_WIN32)
  const DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
  struct stat info;
  if (stat(path, &info) != 0) {
    return false;
  }
  return S_ISDIR(info.st_mode);
#endif
}

fpv_result_t fpv_fs_ensure_dir(const char* path) {
  char* mutable_path = NULL;
  size_t length = 0;
  size_t start = 0;

  if (!path || !path[0]) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  mutable_path = fpv_strdup(path);
  if (!mutable_path) {
    return FPV_ERR_OUT_OF_MEMORY;
  }

  length = strlen(mutable_path);

  while (length > 1 &&
         (mutable_path[length - 1] == '/' ||
          mutable_path[length - 1] == '\\')) {
#if defined(_WIN32)
    if (length == 3 && mutable_path[1] == ':') {
      break;
    }
#endif
    mutable_path[length - 1] = '\0';
    length--;
  }

#if defined(_WIN32)
  if (length >= 2 && mutable_path[1] == ':') {
    start = 2;
    if (length >= 3 &&
        (mutable_path[2] == '/' || mutable_path[2] == '\\')) {
      start = 3;
    }
  }
#else
  if (mutable_path[0] == '/') {
    start = 1;
  }
#endif

  for (size_t i = start; i < length; i++) {
    if (mutable_path[i] == '/' || mutable_path[i] == '\\') {
      char saved = mutable_path[i];
      mutable_path[i] = '\0';
      if (mutable_path[0] != '\0' &&
          !fpv_fs_create_dir(mutable_path)) {
        fpv_free(mutable_path);
        return FPV_ERR_IO;
      }
      mutable_path[i] = saved;
    }
  }

  if (!fpv_fs_create_dir(mutable_path)) {
    fpv_free(mutable_path);
    return FPV_ERR_IO;
  }

  fpv_free(mutable_path);
  return FPV_OK;
}

fpv_result_t fpv_fs_rename(const char* from, const char* to) {
  if (!from || !to) {
    return FPV_ERR_INVALID_ARGUMENT;
  }
  if (rename(from, to) != 0) {
    return FPV_ERR_IO;
  }
  return FPV_OK;
}

char* fpv_path_join(const char* left, const char* right) {
  size_t left_len = 0;
  size_t right_len = 0;
  size_t offset = 0;
  bool needs_sep = false;
  char* joined = NULL;

  if (!left || !right) {
    return NULL;
  }

  left_len = strlen(left);
  right_len = strlen(right);

  while (right_len > 0 &&
         (right[0] == '/' || right[0] == '\\')) {
    right++;
    right_len--;
  }

  if (left_len > 0) {
    const char tail = left[left_len - 1];
    needs_sep = tail != '/' && tail != '\\';
  }

  joined = (char*)malloc(left_len + right_len + (needs_sep ? 1 : 0) + 1);
  if (!joined) {
    return NULL;
  }

  if (left_len > 0) {
    memcpy(joined, left, left_len);
    offset = left_len;
  }

  if (needs_sep) {
    joined[offset++] = FPV_PATH_SEP;
  }

  if (right_len > 0) {
    memcpy(joined + offset, right, right_len);
    offset += right_len;
  }

  joined[offset] = '\0';
  return joined;
}

static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

bool fpv_path_has_extension(const char* path, const char* extension) {
  size_t path_len = 0;
  size_t ext_len = 0;

  if (!path || !extension) {
    return false;
  }

  path_len = strlen(path);
  ext_len = strlen(extension);
  if (ext_len == 0 || path_len < ext_len) {
    return false;
  }

  for (size_t i = 0; i < ext_len; i++) {
    const int a = fpv_ascii_tolower(
        (unsigned char)path[path_len - ext_len + i]);
    const int b = fpv_ascii_tolower((unsigned char)extension[i]);
    if (a != b) {
      return false;
    }
  }

  return true;
}
