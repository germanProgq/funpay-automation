/* FunPay Vertex localization loader. */

#ifndef FPV_LOCALIZATION_H
#define FPV_LOCALIZATION_H

#include <stddef.h>

typedef struct fpv_localizer fpv_localizer_t;

fpv_localizer_t* fpv_localizer_create(
    const char* locales_dir,
    const char* language,
    const char* fallback_language);
void fpv_localizer_destroy(fpv_localizer_t* localizer);

const char* fpv_localizer_get(
    const fpv_localizer_t* localizer,
    const char* key);
char* fpv_localizer_format(
    const fpv_localizer_t* localizer,
    const char* key,
    const char* const* args,
    size_t arg_count);

#endif
