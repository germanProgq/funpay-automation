/* FunPay Vertex FunPay HTTP helpers. */

#include "funpay/http/fpv_funpay_http.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "core/base/fpv_string.h"


typedef struct fpv_funpay_http_client {
  CURL* handle;
  char* user_agent;
  char* proxy;
  char* proxy_auth;
  long timeout_ms;
} fpv_funpay_http_client_t;

typedef struct fpv_funpay_http_buffer {
  char* data;
  size_t size;
} fpv_funpay_http_buffer_t;

static size_t fpv_funpay_http_refcount = 0;

#if defined(FPV_ENABLE_TEST_HOOKS)
static fpv_funpay_http_mock_fn fpv_funpay_http_mock = NULL;
static fpv_funpay_http_multipart_mock_fn fpv_funpay_http_multipart_mock = NULL;
static void* fpv_funpay_http_mock_data = NULL;

void fpv_funpay_http_set_mock(
    fpv_funpay_http_mock_fn request,
    fpv_funpay_http_multipart_mock_fn multipart,
    void* user_data) {
  fpv_funpay_http_mock = request;
  fpv_funpay_http_multipart_mock = multipart;
  fpv_funpay_http_mock_data = user_data;
}

void fpv_funpay_http_clear_mock(void) {
  fpv_funpay_http_mock = NULL;
  fpv_funpay_http_multipart_mock = NULL;
  fpv_funpay_http_mock_data = NULL;
}
#endif

static bool fpv_funpay_http_global_acquire(void) {
  if (fpv_funpay_http_refcount == 0) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      return false;
    }
  }
  fpv_funpay_http_refcount++;
  return true;
}

static void fpv_funpay_http_global_release(void) {
  if (fpv_funpay_http_refcount == 0) {
    return;
  }
  fpv_funpay_http_refcount--;
  if (fpv_funpay_http_refcount == 0) {
    curl_global_cleanup();
  }
}

static int fpv_ascii_tolower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value + ('a' - 'A');
  }
  return value;
}

static int fpv_ascii_strncasecmp(
    const char* left,
    const char* right,
    size_t count) {
  for (size_t i = 0; i < count; i++) {
    const unsigned char a = (unsigned char)left[i];
    const unsigned char b = (unsigned char)right[i];
    if (a == '\0' || b == '\0') {
      return a - b;
    }
    int lower_a = fpv_ascii_tolower(a);
    int lower_b = fpv_ascii_tolower(b);
    if (lower_a != lower_b) {
      return lower_a - lower_b;
    }
  }
  return 0;
}

static int fpv_ascii_strcasecmp(const char* left, const char* right) {
  size_t index = 0;
  while (left[index] != '\0' || right[index] != '\0') {
    int a = fpv_ascii_tolower((unsigned char)left[index]);
    int b = fpv_ascii_tolower((unsigned char)right[index]);
    if (a != b) {
      return a - b;
    }
    if (left[index] == '\0' || right[index] == '\0') {
      break;
    }
    index++;
  }
  return 0;
}

static size_t fpv_funpay_http_write_cb(
    void* ptr,
    size_t size,
    size_t nmemb,
    void* userdata) {
  fpv_funpay_http_buffer_t* buffer = (fpv_funpay_http_buffer_t*)userdata;
  size_t total = size * nmemb;
  if (!buffer || total == 0) {
    return total;
  }

  char* grown = (char*)realloc(buffer->data, buffer->size + total + 1);
  if (!grown) {
    return 0;
  }

  buffer->data = grown;
  memcpy(buffer->data + buffer->size, ptr, total);
  buffer->size += total;
  buffer->data[buffer->size] = '\0';
  return total;
}

static size_t fpv_funpay_http_header_cb(
    char* buffer,
    size_t size,
    size_t nitems,
    void* userdata) {
  fpv_funpay_http_response_t* response =
      (fpv_funpay_http_response_t*)userdata;
  size_t total = size * nitems;
  if (!response || total < 12) {
    return total;
  }

  if (fpv_ascii_strncasecmp(buffer, "Set-Cookie:", 11) != 0) {
    return total;
  }

  const char* start = buffer + 11;
  while (start < buffer + total && isspace((unsigned char)*start)) {
    start++;
  }

  const char* token = strstr(start, "PHPSESSID=");
  if (!token) {
    return total;
  }

  token += strlen("PHPSESSID=");
  const char* end = token;
  while (end < buffer + total && *end != ';' && *end != '\r' && *end != '\n') {
    end++;
  }

  size_t len = (size_t)(end - token);
  if (len == 0) {
    return total;
  }

  char* value = (char*)malloc(len + 1);
  if (!value) {
    return total;
  }
  memcpy(value, token, len);
  value[len] = '\0';
  fpv_free(response->phpsessid);
  response->phpsessid = value;
  return total;
}

static char* fpv_funpay_build_proxy(const fpv_funpay_proxy_config_t* proxy) {
  if (!proxy || !proxy->enabled || !proxy->host || !proxy->host[0]) {
    return NULL;
  }

  char port_buf[16];
  snprintf(port_buf, sizeof(port_buf), "%u", proxy->port);
  size_t host_len = strlen(proxy->host);
  size_t port_len = strlen(port_buf);

  char* buffer = (char*)malloc(host_len + port_len + 2);
  if (!buffer) {
    return NULL;
  }

  memcpy(buffer, proxy->host, host_len);
  buffer[host_len] = ':';
  memcpy(buffer + host_len + 1, port_buf, port_len);
  buffer[host_len + 1 + port_len] = '\0';
  return buffer;
}

static char* fpv_funpay_build_proxy_auth(
    const fpv_funpay_proxy_config_t* proxy) {
  if (!proxy || !proxy->enabled || !proxy->username || !proxy->username[0]) {
    return NULL;
  }

  const char* password = proxy->password ? proxy->password : "";
  size_t user_len = strlen(proxy->username);
  size_t pass_len = strlen(password);

  char* buffer = (char*)malloc(user_len + pass_len + 2);
  if (!buffer) {
    return NULL;
  }

  memcpy(buffer, proxy->username, user_len);
  buffer[user_len] = ':';
  memcpy(buffer + user_len + 1, password, pass_len);
  buffer[user_len + 1 + pass_len] = '\0';
  return buffer;
}

fpv_funpay_http_client_t* fpv_funpay_http_client_create(
    const char* user_agent,
    uint32_t timeout_ms,
    const fpv_funpay_proxy_config_t* proxy,
    fpv_funpay_error_t* error) {
  if (!fpv_funpay_http_global_acquire()) {
    if (error) {
      fpv_funpay_error_clear(error);
      error->code = FPV_FUNPAY_ERR_NETWORK;
      error->message = fpv_strdup("curl_global_init failed");
    }
    return NULL;
  }

  fpv_funpay_http_client_t* client =
      (fpv_funpay_http_client_t*)calloc(1, sizeof(*client));
  if (!client) {
    fpv_funpay_http_global_release();
    return NULL;
  }

  client->handle = curl_easy_init();
  if (!client->handle) {
    fpv_funpay_http_global_release();
    free(client);
    return NULL;
  }

  client->user_agent = fpv_strdup(user_agent);
  client->proxy = fpv_funpay_build_proxy(proxy);
  client->proxy_auth = fpv_funpay_build_proxy_auth(proxy);
  client->timeout_ms = timeout_ms > 0 ? (long)timeout_ms : 10000L;
  return client;
}

void fpv_funpay_http_client_destroy(fpv_funpay_http_client_t* client) {
  if (!client) {
    return;
  }
  if (client->handle) {
    curl_easy_cleanup(client->handle);
  }
  fpv_free(client->user_agent);
  fpv_free(client->proxy);
  fpv_free(client->proxy_auth);
  free(client);
  fpv_funpay_http_global_release();
}

static fpv_result_t fpv_funpay_http_apply_base(
    fpv_funpay_http_client_t* client) {
  CURL* handle = client->handle;
  curl_easy_reset(handle);
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, client->timeout_ms);
  curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, client->timeout_ms);
  if (client->user_agent && client->user_agent[0]) {
    curl_easy_setopt(handle, CURLOPT_USERAGENT, client->user_agent);
  }
  if (client->proxy && client->proxy[0]) {
    curl_easy_setopt(handle, CURLOPT_PROXY, client->proxy);
    if (client->proxy_auth && client->proxy_auth[0]) {
      curl_easy_setopt(handle, CURLOPT_PROXYUSERPWD, client->proxy_auth);
    }
  }
  return FPV_OK;
}

fpv_result_t fpv_funpay_http_request(
    fpv_funpay_http_client_t* client,
    const char* method,
    const char* url,
    const char* cookie,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const char* body,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error) {
#if defined(FPV_ENABLE_TEST_HOOKS)
  if (fpv_funpay_http_mock) {
    return fpv_funpay_http_mock(
        client,
        method,
        url,
        cookie,
        headers,
        header_count,
        body,
        response,
        error,
        fpv_funpay_http_mock_data);
  }
#endif
  if (!client || !method || !url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_funpay_http_response_t local_response;
  local_response.status = 0;
  local_response.body = NULL;
  local_response.body_size = 0;
  local_response.phpsessid = NULL;

  fpv_funpay_http_buffer_t buffer;
  buffer.data = NULL;
  buffer.size = 0;

  fpv_result_t result = fpv_funpay_http_apply_base(client);
  if (result != FPV_OK) {
    return result;
  }

  CURL* handle = client->handle;
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, fpv_funpay_http_write_cb);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, fpv_funpay_http_header_cb);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &local_response);

  if (cookie && cookie[0]) {
    curl_easy_setopt(handle, CURLOPT_COOKIE, cookie);
  }

  if (fpv_ascii_strcasecmp(method, "POST") == 0) {
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    if (body) {
      curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body);
      curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    } else {
      curl_easy_setopt(handle, CURLOPT_POSTFIELDS, "");
      curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, 0L);
    }
  } else {
    curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
  }

  struct curl_slist* header_list = NULL;
  for (size_t i = 0; i < header_count; i++) {
    if (!headers[i].name || !headers[i].value) {
      continue;
    }
    size_t name_len = strlen(headers[i].name);
    size_t value_len = strlen(headers[i].value);
    char* line = (char*)malloc(name_len + value_len + 3);
    if (!line) {
      curl_slist_free_all(header_list);
      fpv_funpay_http_response_clear(&local_response);
      free(buffer.data);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    memcpy(line, headers[i].name, name_len);
    line[name_len] = ':';
    line[name_len + 1] = ' ';
    memcpy(line + name_len + 2, headers[i].value, value_len);
    line[name_len + 2 + value_len] = '\0';
    header_list = curl_slist_append(header_list, line);
    free(line);
  }
  if (header_list) {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  }

  CURLcode code = curl_easy_perform(handle);
  if (header_list) {
    curl_slist_free_all(header_list);
  }

  if (code != CURLE_OK) {
    fpv_funpay_http_response_clear(&local_response);
    free(buffer.data);
    if (error) {
      fpv_funpay_error_clear(error);
      error->code = FPV_FUNPAY_ERR_NETWORK;
      error->http_status = 0;
      error->message = fpv_strdup(curl_easy_strerror(code));
      error->url = fpv_strdup(url);
      error->method = fpv_strdup(method);
    }
    return FPV_ERR_IO;
  }

  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &local_response.status);
  local_response.body = buffer.data;
  local_response.body_size = buffer.size;
  buffer.data = NULL;

  *response = local_response;
  return FPV_OK;
}

fpv_result_t fpv_funpay_http_request_multipart(
    fpv_funpay_http_client_t* client,
    const char* url,
    const char* cookie,
    const fpv_funpay_http_header_t* headers,
    size_t header_count,
    const fpv_funpay_http_form_part_t* parts,
    size_t part_count,
    fpv_funpay_http_response_t* response,
    fpv_funpay_error_t* error) {
#if defined(FPV_ENABLE_TEST_HOOKS)
  if (fpv_funpay_http_multipart_mock) {
    return fpv_funpay_http_multipart_mock(
        client,
        url,
        cookie,
        headers,
        header_count,
        parts,
        part_count,
        response,
        error,
        fpv_funpay_http_mock_data);
  }
#endif
  if (!client || !url || !response) {
    return FPV_ERR_INVALID_ARGUMENT;
  }

  fpv_funpay_http_response_t local_response;
  local_response.status = 0;
  local_response.body = NULL;
  local_response.body_size = 0;
  local_response.phpsessid = NULL;

  fpv_funpay_http_buffer_t buffer;
  buffer.data = NULL;
  buffer.size = 0;

  fpv_result_t result = fpv_funpay_http_apply_base(client);
  if (result != FPV_OK) {
    return result;
  }

  CURL* handle = client->handle;
  curl_easy_setopt(handle, CURLOPT_URL, url);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, fpv_funpay_http_write_cb);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, fpv_funpay_http_header_cb);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &local_response);

  if (cookie && cookie[0]) {
    curl_easy_setopt(handle, CURLOPT_COOKIE, cookie);
  }

  curl_mime* mime = curl_mime_init(handle);
  if (!mime) {
    free(buffer.data);
    return FPV_ERR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < part_count; i++) {
    if (!parts[i].name) {
      continue;
    }
    curl_mimepart* part = curl_mime_addpart(mime);
    if (!part) {
      curl_mime_free(mime);
      free(buffer.data);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    curl_mime_name(part, parts[i].name);
    if (parts[i].data) {
      curl_mime_data(
          part,
          (const char*)parts[i].data,
          (curl_off_t)parts[i].data_size);
      if (parts[i].filename) {
        curl_mime_filename(part, parts[i].filename);
      }
      if (parts[i].content_type) {
        curl_mime_type(part, parts[i].content_type);
      }
    } else if (parts[i].value) {
      curl_mime_data(part, parts[i].value, CURL_ZERO_TERMINATED);
    } else {
      curl_mime_data(part, "", 0);
    }
  }

  curl_easy_setopt(handle, CURLOPT_MIMEPOST, mime);

  struct curl_slist* header_list = NULL;
  for (size_t i = 0; i < header_count; i++) {
    if (!headers[i].name || !headers[i].value) {
      continue;
    }
    size_t name_len = strlen(headers[i].name);
    size_t value_len = strlen(headers[i].value);
    char* line = (char*)malloc(name_len + value_len + 3);
    if (!line) {
      curl_slist_free_all(header_list);
      curl_mime_free(mime);
      fpv_funpay_http_response_clear(&local_response);
      free(buffer.data);
      return FPV_ERR_OUT_OF_MEMORY;
    }
    memcpy(line, headers[i].name, name_len);
    line[name_len] = ':';
    line[name_len + 1] = ' ';
    memcpy(line + name_len + 2, headers[i].value, value_len);
    line[name_len + 2 + value_len] = '\0';
    header_list = curl_slist_append(header_list, line);
    free(line);
  }
  if (header_list) {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  }

  CURLcode code = curl_easy_perform(handle);
  if (header_list) {
    curl_slist_free_all(header_list);
  }
  curl_mime_free(mime);

  if (code != CURLE_OK) {
    fpv_funpay_http_response_clear(&local_response);
    free(buffer.data);
    if (error) {
      fpv_funpay_error_clear(error);
      error->code = FPV_FUNPAY_ERR_NETWORK;
      error->http_status = 0;
      error->message = fpv_strdup(curl_easy_strerror(code));
      error->url = fpv_strdup(url);
      error->method = fpv_strdup("POST");
    }
    return FPV_ERR_IO;
  }

  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &local_response.status);
  local_response.body = buffer.data;
  local_response.body_size = buffer.size;
  buffer.data = NULL;

  *response = local_response;
  return FPV_OK;
}

void fpv_funpay_http_response_clear(fpv_funpay_http_response_t* response) {
  if (!response) {
    return;
  }
  fpv_free(response->body);
  fpv_free(response->phpsessid);
  response->body = NULL;
  response->phpsessid = NULL;
  response->body_size = 0;
  response->status = 0;
}
