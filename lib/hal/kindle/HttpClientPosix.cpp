// HTTP/1.1 over sockets, shaped like ESP-IDF's esp_http_client.
// See arduino-shim/esp_http_client.h for what TLS does and does not do here.

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "arduino-shim/esp_http_client.h"

namespace {

std::string lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

const char* methodName(const esp_http_client_method_t m) {
  switch (m) {
    case HTTP_METHOD_POST: return "POST";
    case HTTP_METHOD_PUT: return "PUT";
    case HTTP_METHOD_PATCH: return "PATCH";
    case HTTP_METHOD_DELETE: return "DELETE";
    case HTTP_METHOD_HEAD: return "HEAD";
    case HTTP_METHOD_GET:
    default: return "GET";
  }
}

}  // namespace

struct esp_http_client {
  std::string scheme;
  std::string host;
  int port = 80;
  std::string path;
  esp_http_client_method_t method = HTTP_METHOD_GET;
  std::map<std::string, std::string> requestHeaders;
  int timeoutMs = 10000;

  int sock = -1;
  std::string pending;  // bytes read past the headers, still owed to read()
  std::map<std::string, std::string> responseHeaders;
  int statusCode = 0;
  int64_t contentLength = -1;
  bool chunked = false;
  int64_t bodyConsumed = 0;
  // Held so get_header can hand out a stable pointer, as IDF does.
  std::string headerScratch;

  bool parseUrl(const std::string& url);
  bool connectSocket();
  void closeSocket();
  bool readLine(std::string* line);
  int readChunked(char* buffer, int len);
};

bool esp_http_client::parseUrl(const std::string& url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) {
    return false;
  }
  scheme = lower(url.substr(0, schemeEnd));

  size_t hostStart = schemeEnd + 3;
  size_t pathStart = url.find('/', hostStart);
  std::string hostPort = pathStart == std::string::npos ? url.substr(hostStart)
                                                        : url.substr(hostStart, pathStart - hostStart);
  path = pathStart == std::string::npos ? "/" : url.substr(pathStart);

  port = scheme == "https" ? 443 : 80;
  const size_t colon = hostPort.rfind(':');
  if (colon != std::string::npos) {
    port = std::atoi(hostPort.c_str() + colon + 1);
    hostPort = hostPort.substr(0, colon);
  }
  host = hostPort;
  return !host.empty();
}

bool esp_http_client::connectSocket() {
  closeSocket();

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  char portStr[8];
  std::snprintf(portStr, sizeof(portStr), "%d", port);
  if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr) {
    return false;
  }

  sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(res);
    return false;
  }

  // A read with no timeout hangs the UI loop forever on a server that accepts
  // and then says nothing.
  timeval tv{};
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  const bool ok = ::connect(sock, res->ai_addr, res->ai_addrlen) == 0;
  freeaddrinfo(res);
  if (!ok) {
    closeSocket();
    return false;
  }
  return true;
}

void esp_http_client::closeSocket() {
  if (sock >= 0) {
    ::close(sock);
    sock = -1;
  }
}

bool esp_http_client::readLine(std::string* line) {
  line->clear();
  for (;;) {
    // Anything already buffered comes first: fetch_headers reads ahead.
    if (!pending.empty()) {
      const size_t eol = pending.find('\n');
      if (eol != std::string::npos) {
        *line = pending.substr(0, eol);
        pending.erase(0, eol + 1);
        if (!line->empty() && line->back() == '\r') {
          line->pop_back();
        }
        return true;
      }
    }
    char buf[1024];
    const ssize_t n = recv(sock, buf, sizeof(buf), 0);
    if (n <= 0) {
      return false;
    }
    pending.append(buf, static_cast<size_t>(n));
  }
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
  if (config == nullptr || config->url == nullptr) {
    return nullptr;
  }
  auto* client = new esp_http_client();
  if (!client->parseUrl(config->url)) {
    delete client;
    return nullptr;
  }
  client->method = config->method;
  if (config->timeout_ms > 0) {
    client->timeoutMs = config->timeout_ms;
  }
  return client;
}

esp_err_t esp_http_client_cleanup(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->closeSocket();
  delete client;
  return ESP_OK;
}

esp_err_t esp_http_client_set_header(const esp_http_client_handle_t client, const char* key, const char* value) {
  if (client == nullptr || key == nullptr) {
    return ESP_FAIL;
  }
  if (value == nullptr) {
    client->requestHeaders.erase(key);
  } else {
    client->requestHeaders[key] = value;
  }
  return ESP_OK;
}

esp_err_t esp_http_client_set_url(const esp_http_client_handle_t client, const char* url) {
  if (client == nullptr || url == nullptr || !client->parseUrl(url)) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t esp_http_client_set_method(const esp_http_client_handle_t client, const esp_http_client_method_t method) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->method = method;
  return ESP_OK;
}

esp_err_t esp_http_client_set_redirection(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  const auto it = client->responseHeaders.find("location");
  if (it == client->responseHeaders.end()) {
    return ESP_FAIL;
  }
  std::string target = it->second;
  // A relative Location keeps the current scheme, host and port.
  if (target.find("://") == std::string::npos) {
    char base[512];
    std::snprintf(base, sizeof(base), "%s://%s:%d", client->scheme.c_str(), client->host.c_str(), client->port);
    target = std::string(base) + (target.empty() || target[0] == '/' ? target : "/" + target);
  }
  return client->parseUrl(target) ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_http_client_open(const esp_http_client_handle_t client, const int writeLen) {
  if (client == nullptr) {
    return ESP_FAIL;
  }

  if (client->scheme == "https") {
    // Refused, never downgraded. The tree sends preemptive Basic credentials
    // on these requests; retrying in the clear to save the feature would put a
    // password on the wire. See the header.
    std::fprintf(stderr, "[kindle] https is not supported on this target yet; refusing %s://%s (no silent downgrade)\n",
                 client->scheme.c_str(), client->host.c_str());
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (!client->connectSocket()) {
    return ESP_FAIL;
  }

  client->pending.clear();
  client->responseHeaders.clear();
  client->statusCode = 0;
  client->contentLength = -1;
  client->chunked = false;
  client->bodyConsumed = 0;

  std::string request;
  request.reserve(512);
  request += methodName(client->method);
  request += " ";
  request += client->path;
  request += " HTTP/1.1\r\nHost: ";
  request += client->host;
  if (client->port != 80) {
    char portPart[16];
    std::snprintf(portPart, sizeof(portPart), ":%d", client->port);
    request += portPart;
  }
  // Close rather than keep-alive: this client makes one request per open() and
  // a persistent connection would only complicate the body-end detection.
  request += "\r\nConnection: close\r\n";
  for (const auto& h : client->requestHeaders) {
    request += h.first + ": " + h.second + "\r\n";
  }
  if (writeLen > 0) {
    char lengthLine[48];
    std::snprintf(lengthLine, sizeof(lengthLine), "Content-Length: %d\r\n", writeLen);
    request += lengthLine;
  }
  request += "\r\n";

  const ssize_t sent = send(client->sock, request.data(), request.size(), MSG_NOSIGNAL);
  return sent == static_cast<ssize_t>(request.size()) ? ESP_OK : ESP_FAIL;
}

int64_t esp_http_client_fetch_headers(const esp_http_client_handle_t client) {
  if (client == nullptr || client->sock < 0) {
    return -1;
  }

  std::string line;
  if (!client->readLine(&line)) {
    return -1;
  }
  // "HTTP/1.1 200 OK"
  const size_t firstSpace = line.find(' ');
  if (firstSpace == std::string::npos) {
    return -1;
  }
  client->statusCode = std::atoi(line.c_str() + firstSpace + 1);

  while (client->readLine(&line) && !line.empty()) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    size_t vstart = colon + 1;
    while (vstart < line.size() && line[vstart] == ' ') {
      ++vstart;
    }
    const std::string name = lower(line.substr(0, colon));
    const std::string value = line.substr(vstart);
    client->responseHeaders[name] = value;

    if (name == "content-length") {
      client->contentLength = std::strtoll(value.c_str(), nullptr, 10);
    } else if (name == "transfer-encoding" && lower(value).find("chunked") != std::string::npos) {
      client->chunked = true;
    }
  }

  // Chunked responses have no length up front; IDF reports -1 there too.
  return client->chunked ? -1 : client->contentLength;
}

int esp_http_client_get_status_code(const esp_http_client_handle_t client) {
  return client != nullptr ? client->statusCode : 0;
}

int64_t esp_http_client_get_content_length(const esp_http_client_handle_t client) {
  return client != nullptr ? client->contentLength : -1;
}

int esp_http_client::readChunked(char* buffer, const int len) {
  // One chunk per call at most: the caller loops anyway, and this keeps the
  // size-line parsing simple.
  std::string sizeLine;
  if (!readLine(&sizeLine)) {
    return -1;
  }
  const long chunkSize = std::strtol(sizeLine.c_str(), nullptr, 16);
  if (chunkSize <= 0) {
    contentLength = bodyConsumed;  // terminating chunk: the body is complete
    return 0;
  }

  int produced = 0;
  long remaining = chunkSize;
  while (remaining > 0 && produced < len) {
    if (pending.empty()) {
      char buf[4096];
      const ssize_t n = recv(sock, buf, sizeof(buf), 0);
      if (n <= 0) {
        return produced > 0 ? produced : -1;
      }
      pending.append(buf, static_cast<size_t>(n));
    }
    const size_t take = std::min({pending.size(), static_cast<size_t>(remaining), static_cast<size_t>(len - produced)});
    std::memcpy(buffer + produced, pending.data(), take);
    pending.erase(0, take);
    produced += static_cast<int>(take);
    remaining -= static_cast<long>(take);
  }
  // Swallow the CRLF that terminates the chunk.
  std::string crlf;
  if (remaining == 0) {
    readLine(&crlf);
  }
  bodyConsumed += produced;
  return produced;
}

int esp_http_client_read(const esp_http_client_handle_t client, char* buffer, const int len) {
  if (client == nullptr || buffer == nullptr || len <= 0 || client->sock < 0) {
    return -1;
  }
  if (client->chunked) {
    return client->readChunked(buffer, len);
  }

  if (client->contentLength >= 0 && client->bodyConsumed >= client->contentLength) {
    return 0;
  }

  int produced = 0;
  if (!client->pending.empty()) {
    const size_t take = std::min(client->pending.size(), static_cast<size_t>(len));
    std::memcpy(buffer, client->pending.data(), take);
    client->pending.erase(0, take);
    produced = static_cast<int>(take);
  }
  if (produced < len) {
    const ssize_t n = recv(client->sock, buffer + produced, static_cast<size_t>(len - produced), 0);
    if (n > 0) {
      produced += static_cast<int>(n);
    } else if (produced == 0) {
      // No length header and the peer closed: that IS the end of the body.
      if (client->contentLength < 0) {
        client->contentLength = client->bodyConsumed;
      }
      return n == 0 ? 0 : -1;
    }
  }
  client->bodyConsumed += produced;
  return produced;
}

int esp_http_client_write(const esp_http_client_handle_t client, const char* buffer, const int len) {
  if (client == nullptr || buffer == nullptr || client->sock < 0) {
    return -1;
  }
  const ssize_t n = send(client->sock, buffer, static_cast<size_t>(len), MSG_NOSIGNAL);
  return static_cast<int>(n);
}

esp_err_t esp_http_client_perform(const esp_http_client_handle_t client) {
  const esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    return err;
  }
  if (esp_http_client_fetch_headers(client) < 0 && !client->chunked) {
    return ESP_FAIL;
  }
  // perform() is the one-shot form: drain the body so the caller can read the
  // status and be done.
  char scratch[1024];
  while (esp_http_client_read(client, scratch, sizeof(scratch)) > 0) {
  }
  return ESP_OK;
}

bool esp_http_client_is_complete_data_received(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return false;
  }
  if (client->contentLength < 0) {
    return false;  // still chunked and unterminated
  }
  return client->bodyConsumed >= client->contentLength;
}

esp_err_t esp_http_client_close(const esp_http_client_handle_t client) {
  if (client == nullptr) {
    return ESP_FAIL;
  }
  client->closeSocket();
  return ESP_OK;
}

esp_err_t esp_http_client_get_header(const esp_http_client_handle_t client, const char* key, char** value) {
  if (client == nullptr || key == nullptr || value == nullptr) {
    return ESP_FAIL;
  }
  const auto it = client->responseHeaders.find(lower(key));
  if (it == client->responseHeaders.end()) {
    *value = nullptr;
    return ESP_OK;  // IDF reports "absent" as success with a null value
  }
  client->headerScratch = it->second;
  *value = client->headerScratch.data();
  return ESP_OK;
}
