/* FunPay Vertex chat storage API. */

#ifndef FPV_CHAT_STORE_H
#define FPV_CHAT_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "fpv_export.h"
#include "fpv_models.h"
#include "fpv_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fpv_chat_store fpv_chat_store_t;

FPV_CORE_API fpv_chat_store_t* fpv_chat_store_open(
    const char* data_dir,
    const char* db_url,
    fpv_result_t* out_result);
FPV_CORE_API void fpv_chat_store_destroy(fpv_chat_store_t* store);

FPV_CORE_API fpv_result_t fpv_chat_store_upsert_chat(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_chat_t* chat,
    uint64_t now_ms);
FPV_CORE_API fpv_result_t fpv_chat_store_upsert_message(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_message_t* message);
FPV_CORE_API fpv_result_t fpv_chat_store_upsert_messages(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const fpv_message_t* const* messages,
    size_t message_count);

FPV_CORE_API fpv_result_t fpv_chat_store_load_chats(
    const fpv_chat_store_t* store,
    const char* organization_id,
    fpv_chat_t*** out_chats,
    size_t* out_count);
FPV_CORE_API fpv_result_t fpv_chat_store_load_messages(
    const fpv_chat_store_t* store,
    const char* organization_id,
    const char* chat_id,
    size_t limit,
    fpv_message_t*** out_messages,
    size_t* out_count);

#ifdef __cplusplus
}
#endif

#endif
