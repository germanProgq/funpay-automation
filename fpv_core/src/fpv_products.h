/* FunPay Vertex product storage helpers. */

#ifndef FPV_PRODUCTS_H
#define FPV_PRODUCTS_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_core/fpv_result.h"

fpv_result_t fpv_products_take(
    const char* path,
    uint32_t amount,
    char*** out_products,
    size_t* out_count,
    size_t* out_remaining);
fpv_result_t fpv_products_restore(
    const char* path,
    char** products,
    size_t count);
void fpv_products_free(char** products, size_t count);

#endif
