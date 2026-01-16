/* FunPay Vertex core string utilities. */

#ifndef FPV_STRING_H
#define FPV_STRING_H

#include <stddef.h>

char* fpv_strdup(const char* value);
char* fpv_strdup_n(const char* value, size_t max_len);
void fpv_free(void* ptr);

#endif
