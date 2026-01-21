/* FunPay Vertex core shared library loader implementation. */

#include "core/infra/fpv_shared_lib.h"


#include <stddef.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

bool fpv_shared_lib_open(fpv_shared_lib_t* lib, const char* path) {
  if (!lib || !path) {
    return false;
  }
#if defined(_WIN32)
  lib->handle = (void*)LoadLibraryA(path);
#else
  lib->handle = dlopen(path, RTLD_NOW);
#endif
  return lib->handle != NULL;
}

void fpv_shared_lib_close(fpv_shared_lib_t* lib) {
  if (!lib || !lib->handle) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary((HMODULE)lib->handle);
#else
  dlclose(lib->handle);
#endif
  lib->handle = NULL;
}

void* fpv_shared_lib_symbol(fpv_shared_lib_t* lib, const char* name) {
  if (!lib || !lib->handle || !name) {
    return NULL;
  }
#if defined(_WIN32)
  return (void*)GetProcAddress((HMODULE)lib->handle, name);
#else
  return dlsym(lib->handle, name);
#endif
}
