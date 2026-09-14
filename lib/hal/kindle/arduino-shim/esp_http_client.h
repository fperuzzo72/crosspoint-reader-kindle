#pragma once

// ESP-IDF's HTTP client, over sockets.
//
// This is the last dependency standing between the Kindle build and a linking
// binary, and the tree uses its streaming half: init, set_header, open,
// fetch_headers, get_status_code, read, close, cleanup, plus perform() for
// one-shot requests.
//
// HTTP/1.1 is implemented properly: request building, response header parsing,
// content-length and chunked bodies, and manual redirect stepping, because
// open()/read() does not auto-follow the way perform() does.
//
// TLS IS NOT. And the way it fails matters more than the fact that it does.
//
// An https:// URL is REFUSED, with ESP_ERR_NOT_SUPPORTED and a log line. It is
// not quietly retried over http://. The tree sends preemptive HTTP Basic
// credentials on these requests (OPDS servers, KOReader sync), so a silent
// downgrade would put a user's password on the wire in clear text to save a
// feature from failing. A feature that does not work is recoverable; a
// credential that leaked is not.
//
// What TLS would take here: the Kindle ships its own libraries, but which and
// at what version is not knowable from the host side, so it needs a shell on
// the device to settle. Failing that, vendoring mbedtls is the portable
// answer. Either way it is a deliberate piece of work, not something to
// approximate.
//
// The practical consequence: OPDS and Calibre over plain http work, and the
// GitHub-hosted OTA check does not. OTA does not apply to this target anyway
// (see Update.h), so nothing is lost that this target was going to have.

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED 0x106
#endif

typedef enum {
  HTTP_METHOD_GET = 0,
  HTTP_METHOD_POST,
  HTTP_METHOD_PUT,
  HTTP_METHOD_PATCH,
  HTTP_METHOD_DELETE,
  HTTP_METHOD_HEAD,
} esp_http_client_method_t;

typedef enum {
  HTTP_AUTH_TYPE_NONE = 0,
  HTTP_AUTH_TYPE_BASIC,
  HTTP_AUTH_TYPE_DIGEST,
} esp_http_client_auth_type_t;

typedef enum {
  HTTP_TRANSPORT_UNKNOWN = 0,
  HTTP_TRANSPORT_OVER_TCP,
  HTTP_TRANSPORT_OVER_SSL,
} esp_http_client_transport_t;

struct esp_http_client_config_t {
  const char* url = nullptr;
  const char* host = nullptr;
  int port = 0;
  const char* path = nullptr;
  const char* username = nullptr;
  const char* password = nullptr;
  esp_http_client_auth_type_t auth_type = HTTP_AUTH_TYPE_NONE;
  esp_http_client_method_t method = HTTP_METHOD_GET;
  int timeout_ms = 0;
  bool disable_auto_redirect = false;
  int max_redirection_count = 0;
  int buffer_size = 0;
  int buffer_size_tx = 0;
  bool keep_alive_enable = false;
  esp_http_client_transport_t transport_type = HTTP_TRANSPORT_UNKNOWN;
  // Accepted and ignored: there is no bundle to attach here, and the system
  // trust store is the right source. See esp_crt_bundle.h.
  esp_err_t (*crt_bundle_attach)(void*) = nullptr;
  const char* cert_pem = nullptr;
  bool skip_cert_common_name_check = false;
  void* user_data = nullptr;
};

struct esp_http_client;
using esp_http_client_handle_t = esp_http_client*;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key, const char* value);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char* url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t client);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int writeLen);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len);
int esp_http_client_write(esp_http_client_handle_t client, const char* buffer, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
// The Location header of the last response, for manual redirect stepping.
esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char* key, char** value);
