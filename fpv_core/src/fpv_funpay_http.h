/* FunPay Vertex FunPay HTTP helpers. */

#ifndef FPV_FUNPAY_HTTP_H
#define FPV_FUNPAY_HTTP_H

#include <stddef.h>

#include "fpv_core/fpv_funpay.h"

typedef struct fpv_funpay_http_header {
  const char* name;
  const char* value;
} fpv_funpay_http_header_t;

typedef struct fpv_funpay_http_response {
  long status;
  char* body;
  size_t body_size;
  char* phpsessid;
} fpv_funpay_http_response_t;

typedef struct fpv_funpay_http_form_part {
  const char* name;
  const char* value;
  const void* data;
  size_t data_size;
  const char* filename;
  const char* content_type;
} fpv_funpay_http_form_part_t;

typedef struct fpv_funpay_http_client fpv_funpay_http_client_t;

fpv_funpay_http_client_t* fpv_funpay_http_client_create(
    const char* user_agent,
    uint32_t timeout_ms,
    const fpv_funpay_proxy_config_t* proxy,
    fpv_funpay_error_t* error);
void fpv_funpay_http_client_destroy(fpv_funpay_http_client_t* client);

fpv_result_t fpv_funpay_http_request(
    fpv_funpay_http_client_t* client,
    const char* method,
    const char* url,
    const char* cookie,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const char* body,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error);
fpv_result_t fpv_funpay_http_request_multipart(
    fpv_funpay_http_client_t* client,
    const char* url,
    const char* cookie,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const fpv_funpay_http_form_part_t* parts,
    size_t part_count,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error);

void fpv_funpay_http_response_clear(fpv_funpay_http_response_t* response);

#endif
