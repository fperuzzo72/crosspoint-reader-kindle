#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>
#include <esp_wifi.h>

#include <functional>
#include <HalStorage.h>
#include <string>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
#else
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#endif

namespace {
#if !defined(FREEINK_NET_WOLFSSL)
// RX holds the response headers. Smaller buffers leave enough contiguous heap
// for mbedTLS on redirect-heavy OPDS feeds while still preserving the headers
// we read directly (Location, Content-Length).
constexpr int HTTP_RX_BUF = 2048;
constexpr int HTTP_TX_BUF = 512;
#endif
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room. esp_http_client's timeout_ms is uint32, so unlike Arduino
// HTTPClient's uint16 setTimeout it doesn't silently truncate.
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr size_t READ_CHUNK = 1024;
constexpr int MAX_REDIRECTS = 5;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
};

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// OtaUpdater.cpp already disables WiFi power-save for firmware downloads, but
// OPDS feed/book fetches never did despite being able to run just as long for
// a large category. Modem sleep periodically powers the radio down between
// DTIM beacon intervals, which can drop or stall packets mid-transfer -- more
// likely to be hit the longer a transfer takes, so small feeds mostly get
// away with it while a large category consistently doesn't.
struct WifiPowerSaveGuard {
  WifiPowerSaveGuard() {
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) LOG_ERR("HTTP", "Failed to disable WiFi power-save: %d", err);
  }
  ~WifiPowerSaveGuard() {
    esp_err_t err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) LOG_ERR("HTTP", "Failed to restore WiFi power-save: %d", err);
  }
};

#if defined(FREEINK_NET_WOLFSSL) && FREEINK_DEVICE_KINDLE
// The trust anchors, read once from the card and kept.
//
// Read rather than compiled in: the set expires and gets revoked, and a file
// the user can replace beats a binary they would have to wait for. Kept rather
// than re-read because wolfSSL wants the whole PEM as one buffer and a reading
// session may do many requests.
#include <wolfssl/ssl.h>

constexpr const char* KINDLE_CA_BUNDLE_PATH = "/crosspoint/cacert.pem";

// Keep only the certificates this wolfSSL build can actually parse.
//
// wolfSSL_CTX_load_verify_buffer walks a multi-certificate PEM in order and
// STOPS at the first one it cannot handle, and SecureClient does not look at
// its return value. So one certificate the build has no algorithm for silently
// discards every certificate after it in the file. A Mozilla bundle is 121
// certificates of mixed RSA-2048, RSA-4096, P-256, P-384 and one P-521, in no
// order this cares about, and the symptom is ASN_NO_SIGNER_E on a perfectly
// ordinary site.
//
// So each one is offered on its own to a throwaway context and kept only if it
// is accepted. Costs a parse per certificate, once per session, and turns a
// silent truncation into a number in the log.
std::string filterLoadableCAs(const std::string& bundle) {
  static const char* const BEGIN = "-----BEGIN CERTIFICATE-----";
  static const char* const END = "-----END CERTIFICATE-----";

  std::string kept;
  kept.reserve(bundle.size());
  size_t accepted = 0;
  size_t rejected = 0;
  size_t index = 0;
  // Which ones, not just how many. The index is enough: the same bundle file is
  // on the machine that built this, so naming the index names the certificate.
  std::string rejectedList;

  size_t pos = 0;
  while (true) {
    const size_t b = bundle.find(BEGIN, pos);
    if (b == std::string::npos) break;
    const size_t e = bundle.find(END, b);
    if (e == std::string::npos) break;
    const size_t end = e + std::strlen(END);
    const std::string one = bundle.substr(b, end - b) + "\n";
    pos = end;

    WOLFSSL_CTX* probe = wolfSSL_CTX_new(wolfTLS_client_method());
    if (probe == nullptr) {
      // Cannot test; keep it rather than throw away a trust anchor over a
      // failure that is ours.
      kept += one;
      ++accepted;
      continue;
    }
    const int rc = wolfSSL_CTX_load_verify_buffer(probe, reinterpret_cast<const unsigned char*>(one.data()),
                                                  static_cast<long>(one.size()), WOLFSSL_FILETYPE_PEM);
    wolfSSL_CTX_free(probe);
    if (rc == WOLFSSL_SUCCESS) {
      kept += one;
      ++accepted;
    } else {
      ++rejected;
      if (rejectedList.size() < 200) {
        char note[32];
        std::snprintf(note, sizeof(note), "%zu(%d) ", index, rc);
        rejectedList += note;
      }
    }
    ++index;
  }

  std::fprintf(stderr, "[kindle] CA bundle: %zu usable, %zu this build cannot parse\n", accepted, rejected);
  if (rejected > 0) {
    std::fprintf(stderr, "[kindle] rejected index(err): %s\n", rejectedList.c_str());
  }
  return kept;
}

const char* kindleRootCAs() {
  static std::string pem;
  static bool tried = false;
  if (tried) {
    return pem.empty() ? nullptr : pem.c_str();
  }
  tried = true;

  HalFile f;
  if (!Storage.openFileForRead("HTTP", KINDLE_CA_BUNDLE_PATH, f)) {
    return nullptr;
  }
  const size_t size = f.size();
  // A bundle is a couple of hundred KB. Anything wildly outside that is not a
  // bundle, and load_verify_buffer would be handed nonsense.
  if (size == 0 || size > 4u * 1024u * 1024u) {
    f.close();
    return nullptr;
  }
  pem.resize(size);
  const int got = f.read(reinterpret_cast<uint8_t*>(&pem[0]), size);
  f.close();
  if (got <= 0 || static_cast<size_t>(got) != size) {
    pem.clear();
    return nullptr;
  }
  std::fprintf(stderr, "[kindle] CA bundle loaded: %zu bytes from %s\n", size, KINDLE_CA_BUNDLE_PATH);
  pem = filterLoadableCAs(pem);
  if (pem.empty()) {
    return nullptr;
  }
  return pem.c_str();
}
#endif

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolf(const std::string& startUrl, const std::string& username,
                                         const std::string& password, Sink& sink, bool downgradeRedirectsToHttp) {
  WifiPowerSaveGuard psGuard;
  std::string url = startUrl;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
#if FREEINK_DEVICE_KINDLE
    // Verify the peer, and refuse rather than fall back if there is nothing to
    // verify against.
    //
    // setInsecure() is the ESP32 choice and defensible there: a CA bundle is
    // real flash on a microcontroller. It is not defensible here. These
    // requests carry preemptive HTTP Basic credentials, so unverified TLS hands
    // the password to whoever answers the connection — encrypted, and to the
    // wrong party. That is the same hazard this port refused when it made https
    // fail outright instead of retrying in the clear, and turning TLS on would
    // be a poor moment to start accepting it.
    {
      const char* rootCA = kindleRootCAs();
      if (rootCA == nullptr) {
        // stderr, not LOG_ERR: this build defines no ENABLE_SERIAL_LOG, so
        // every LOG_* macro compiles to nothing. A refusal nobody can see is
        // just a download that failed for no reason.
        std::fprintf(stderr,
                     "[kindle] no CA bundle at %s; refusing https rather than skipping verification\n",
                     KINDLE_CA_BUNDLE_PATH);
        return HttpDownloader::HTTP_ERROR;
      }
      http.setCACert(rootCA);
    }
#else
    http.setInsecure();
#endif
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL bad URL: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
    // append a second User-Agent header, which strict servers reject (aiohttp
    // answers 400 "Duplicate 'User-Agent' header found").
    http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (!username.empty() && !password.empty()) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", url.c_str());
    const int status = http.GET(
        [&http, &sink](const uint8_t* data, size_t len) {
          if (http.getStatus() != 200) return true;
          if (sink.total == 0 && http.hasContentLength()) sink.total = http.getContentLength();
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
          return true;
        },
        [&sink]() { return sink.cancelFlag && *sink.cancelFlag; });

    if (http.aborted()) return HttpDownloader::ABORTED;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    if (isRedirect(status)) {
      const std::string location = http.getHeader("location");
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(url, location, url)) {
        LOG_ERR("HTTP", "wolfSSL bad redirect: %d", status);
        return HttpDownloader::HTTP_ERROR;
      }
      if (downgradeRedirectsToHttp && url.rfind("https://", 0) == 0) {
        // Fetch the redirect target over plain HTTP. GitHub's release-asset
        // CDN serves its signed URLs on both schemes, and skipping the second
        // TLS session removes its ~17KB record buffer — the MEMORY_E /
        // OOM-abort site on C3 heaps that sit near 45KB free.
        url.replace(0, 8, "http://");
      }
      continue;
    }
    if (status != 200) {
      LOG_ERR("HTTP", "wolfSSL unexpected status: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) return HttpDownloader::FILE_ERROR;
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }
    return HttpDownloader::OK;
  }
  LOG_ERR("HTTP", "too many redirects");
  return HttpDownloader::HTTP_ERROR;
}
#endif

#if !defined(FREEINK_NET_WOLFSSL)
// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink) {
  WifiPowerSaveGuard psGuard;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  // Verify HTTPS against the bundled CA roots. This build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified TLS handshake can't be set
  // up at all; the model is public servers over verified https and local
  // servers over plain http (esp_http_client picks the transport from the URL
  // scheme, so http:// needs no cert config). The prior setInsecure() worked
  // only because Arduino's ssl_client drives mbedtls directly.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!username.empty() && !password.empty()) {
    // Preemptive Basic auth, like the prior addHeader; don't wait for a 401.
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }

  // open()/read() does not auto-follow redirects (only perform() does), so step
  // 30x responses manually. OPDS download endpoints and the GitHub release CDN
  // both redirect.
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }
  int64_t contentLength = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  for (int hop = 0; isRedirect(status) && hop < MAX_REDIRECTS; ++hop) {
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
  }

  if (status != 200) {
    LOG_ERR("HTTP", "unexpected status: %d", status);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  // fetch_headers returns 0 for a chunked response (no Content-Length); leave
  // total at 0 so progress stays silent and the size check is skipped.
  sink.total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("HTTP", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  while (true) {
    if (sink.cancelFlag && *sink.cancelFlag) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (read < 0) {
      LOG_ERR("HTTP", "read error after %zu bytes", sink.downloaded);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif  // !FREEINK_NET_WOLFSSL

// All HTTP(S) fetches go through wolfSSL when it is the active TLS stack: it
// speaks TLS 1.3 and reads large bodies from servers where the esp_http_client/
// mbedTLS path fails to connect or stalls mid-stream. Plain-http URLs still use a
// WiFiClient inside runGetWolf, so this is safe for non-TLS targets too.
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password, Sink& sink,
                                           bool downgradeRedirectsToHttp = false) {
#if defined(FREEINK_NET_WOLFSSL)
  return runGetWolf(url, username, password, sink, downgradeRedirectsToHttp);
#else
  // esp_http_client follows redirects internally; the downgrade only exists on
  // the wolfSSL path, where the manual hop loop exposes the Location URL.
  (void)downgradeRedirectsToHttp;
  return runGet(url, username, password, sink);
#endif
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             bool downgradeRedirectsToHttp) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };

  const DownloadError result = runGetSecure(url, username, password, sink, downgradeRedirectsToHttp);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  file.close();

  if (result != OK) {
    Storage.remove(destPath.c_str());
    return result;
  }
  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
