/* FunPay Vertex core shared library loader. */

#ifndef FPV_SHARED_LIB_H
#define FPV_SHARED_LIB_H

#include <stdbool.h>

typedef struct fpv_shared_lib {
  void* handle;
} fpv_shared_lib_t;

bool fpv_shared_lib_open(fpv_shared_lib_t* lib, const char* path);
void fpv_shared_lib_close(fpv_shared_lib_t* lib);
void* fpv_shared_lib_symbol(fpv_shared_lib_t* lib, const char* name);

#endif
