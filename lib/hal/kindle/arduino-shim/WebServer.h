#pragma once

// A real HTTP/1.1 server, not a stand-in.
//
// This one is implemented rather than stubbed because it carries a headline
// feature: the file-transfer web UI, the settings pages, the OPDS and Calibre
// flows. Sockets work here, so there was no reason to fake it.
//
// One honest exception. Multipart file UPLOAD is not implemented. The surface
// exists so the tree compiles, and it fails loudly at begin() time if a route
// registers an upload handler, rather than accepting a browser's POST and
// quietly dropping the book. Implementing it is the next piece of networking
// work; see docs/kindle-port.md.
//
// Single-threaded and one connection at a time, matching the Arduino original:
// handleClient() accepts, serves and closes. That is what the callers expect,
// and a reader serving one browser needs nothing more.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../ArduinoCompat.h"
#include "WiFiClient.h"

enum HTTPMethod : uint8_t { HTTP_ANY, HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE, HTTP_OPTIONS, HTTP_HEAD };

enum HTTPUploadStatus : uint8_t { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };

struct HTTPUpload {
  HTTPUploadStatus status = UPLOAD_FILE_ABORTED;
  String filename;
  String name;
  String type;
  size_t totalSize = 0;
  size_t currentSize = 0;
  uint8_t* buf = nullptr;
};

class WebServer {
 public:
  using THandlerFunction = std::function<void()>;

  explicit WebServer(uint16_t port = 80) : listenPort(port) {}
  ~WebServer();

  void begin();
  void begin(uint16_t port);
  void stop();
  void close() { stop(); }
  bool isRunning() const { return listenFd >= 0; }

  void on(const String& uri, THandlerFunction handler);
  void on(const String& uri, HTTPMethod method, THandlerFunction handler);
  // The upload overload is accepted so routes compile, and refused at begin().
  void on(const String& uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler);
  void onNotFound(THandlerFunction handler) { notFoundHandler = handler; }

  void handleClient();

  // --- request ---
  const String& uri() const { return reqUri; }
  HTTPMethod method() const { return reqMethod; }
  String arg(const String& name) const;
  String arg(int index) const;
  String argName(int index) const;
  int args() const { return static_cast<int>(reqArgs.size()); }
  bool hasArg(const String& name) const;
  String header(const String& name) const;
  bool hasHeader(const String& name) const;
  void collectHeaders(const char* headerKeys[], size_t count);
  HTTPUpload& upload() { return currentUpload; }
  WiFiClient& client() { return activeClient; }

  // --- response ---
  void send(int code, const char* contentType = "text/plain", const String& content = String());
  void send(int code, const String& contentType, const String& content);
  void sendHeader(const String& name, const String& value, bool first = false);
  void setContentLength(size_t length) { plannedLength = length; }
  void sendContent(const String& content);
  void sendContent(const char* content, size_t length);
  void enableCORS(bool enable = true) { corsEnabled = enable; }

 private:
  struct Route {
    String uri;
    HTTPMethod method;
    THandlerFunction handler;
  };

  bool readRequest();
  void parseQuery(const std::string& query);
  void dispatch();
  void writeStatusLine(int code, const String& contentType);

  uint16_t listenPort;
  int listenFd = -1;
  bool corsEnabled = false;
  bool uploadRouteRegistered = false;

  std::vector<Route> routes;
  THandlerFunction notFoundHandler;

  WiFiClient activeClient;
  String reqUri;
  HTTPMethod reqMethod = HTTP_GET;
  std::map<std::string, std::string> reqArgs;
  std::map<std::string, std::string> reqHeaders;
  std::vector<std::string> collectedHeaderNames;

  std::vector<std::pair<String, String>> pendingHeaders;
  size_t plannedLength = SIZE_MAX;  // SIZE_MAX = "not announced"
  bool headersSent = false;

  HTTPUpload currentUpload;
};

// The tree also spells it this way on newer cores.
using WebServerClass = WebServer;
