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

enum HTTPMethod : uint8_t {
  HTTP_ANY,
  HTTP_GET,
  HTTP_POST,
  HTTP_PUT,
  HTTP_DELETE,
  HTTP_OPTIONS,
  HTTP_HEAD,
  // WebDAV verbs. WebDAVHandler dispatches on these, so they are part of the
  // enum rather than an extension: a server that parsed only the common seven
  // would route every PROPFIND to the not-found handler.
  HTTP_PROPFIND,
  HTTP_PROPPATCH,
  HTTP_MKCOL,
  HTTP_MOVE,
  HTTP_COPY,
  HTTP_LOCK,
  HTTP_UNLOCK,
};

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

// Raw-body streaming, the counterpart to upload for a request whose body is
// not multipart. WebDAV's PUT arrives this way.
// Streaming with no length announced up front. A global rather than a class
// member, matching the Arduino library, because callers use it unqualified.
inline constexpr size_t CONTENT_LENGTH_UNKNOWN = SIZE_MAX;
inline constexpr size_t CONTENT_LENGTH_NOT_SET = SIZE_MAX - 1;

enum HTTPRawStatus : uint8_t { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };

struct HTTPRaw {
  HTTPRawStatus status = RAW_ABORTED;
  size_t totalSize = 0;
  size_t currentSize = 0;
  uint8_t* buf = nullptr;
};

class WebServer;

// A route handler object, as opposed to the lambda form on()-style routes take.
// WebDAVHandler is one: it claims whole URI subtrees and needs the raw body.
//
// Handler objects and on() routes share ONE registration order, and the first
// that claims a request gets it. That matters: the Arduino WebServer keeps both
// in a single chain, and the tree is written against that. See Registration.
class RequestHandler {
 public:
  virtual ~RequestHandler() = default;
  virtual bool canHandle(WebServer& server, HTTPMethod method, const String& uri) = 0;
  virtual bool handle(WebServer& server, HTTPMethod method, const String& uri) = 0;
  virtual bool canUpload(WebServer&, const String&) { return false; }
  virtual void upload(WebServer&, const String&, HTTPUpload&) {}
  virtual bool canRaw(WebServer&, const String&) { return false; }
  virtual void raw(WebServer&, const String&, HTTPRaw&) {}
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
  // The upload overload streams multipart/form-data: the upload handler is
  // called with UPLOAD_FILE_START, then UPLOAD_FILE_WRITE for each chunk, then
  // UPLOAD_FILE_END, and only afterwards does the main handler run to send the
  // response. That ordering is the Arduino original's, and CrossPoint's
  // handlers depend on it.
  void on(const String& uri, HTTPMethod method, THandlerFunction handler, THandlerFunction uploadHandler);
  void onNotFound(THandlerFunction handler) { notFoundHandler = handler; }
  // The server does NOT take ownership, matching the Arduino original.
  void addHandler(RequestHandler* handler);

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
  HTTPRaw& raw() { return currentRaw; }
  // The request's announced body size. WebDAV's MKCOL refuses a request that
  // carries one, so "absent" has to be distinguishable from zero.
  size_t clientContentLength() const { return requestContentLength; }

  // Percent-decoding, exposed because WebDAV decodes paths itself.
  static String urlDecode(const String& encoded);
  WiFiClient& client() { return activeClient; }

  // --- response ---
  void send(int code, const char* contentType = "text/plain", const String& content = String());
  void send(int code, const String& contentType, const String& content);
  void sendHeader(const String& name, const String& value, bool first = false);
  void setContentLength(size_t length) { plannedLength = length; }
  // The _P variants send from program memory on AVR. There is no separate
  // address space here, so they are the ordinary sends.
  void send_P(int code, const char* contentType, const char* content);
  void send_P(int code, const char* contentType, const char* content, size_t length);
  void sendContent(const String& content);
  void sendContent(const char* content, size_t length);
  void enableCORS(bool enable = true) { corsEnabled = enable; }

 private:
  // One entry per on() or addHandler() call, kept in the order they were made.
  //
  // Two lists, with handler objects consulted first, is the obvious shape and
  // it is wrong. The Arduino WebServer this stands in for keeps on() routes and
  // addHandler() objects in a single chain, so the tree can and does rely on
  // registration order — and exactly one case in it depends on the difference:
  // "/" is registered as a route near the top of setup, WebDAVHandler is added
  // at the bottom, and WebDAV claims GET for EVERY uri. Consulting handlers
  // first meant a browser asking for "/" got WebDAV's "405 Method Not Allowed"
  // for a directory instead of the file manager.
  struct Registration {
    // Set for addHandler(). Owned: deleted with the server, which is what
    // CrossPointWebServer says it expects when it hands one over.
    RequestHandler* handler = nullptr;

    // Set for on().
    String uri;
    HTTPMethod method = HTTP_ANY;
    THandlerFunction fn;
    // Runs while the request body is still being read, once per upload event,
    // and is what makes a browser's POST reach the filesystem in chunks
    // instead of through a buffer the size of the book.
    THandlerFunction uploadFn;
  };

  bool readRequest();
  void parseQuery(const std::string& query);
  void dispatch();
  void writeStatusLine(int code, const String& contentType);
  // Frames one piece of a chunked body: its size in hex, the bytes, CRLF. A
  // zero length writes the terminator that says the body is complete.
  void writeChunk(const char* data, size_t length);
  void finishChunked();

  uint16_t listenPort;
  int listenFd = -1;
  bool corsEnabled = false;
  // Boundary from the Content-Type of a multipart request, without the leading
  // dashes. Empty when the body is not multipart.
  std::string multipartBoundary;

  // Streams a multipart body: file parts go to `uploadHandler` chunk by chunk,
  // ordinary fields become args so arg("family") works the same as on a
  // urlencoded form. Returns false when the body is malformed or the peer
  // vanished mid-transfer.
  bool readMultipart(const THandlerFunction& uploadHandler);

  std::vector<Registration> registrations;
  THandlerFunction notFoundHandler;

  WiFiClient activeClient;
  String reqUri;
  HTTPMethod reqMethod = HTTP_GET;
  std::map<std::string, std::string> reqArgs;
  std::map<std::string, std::string> reqHeaders;
  std::vector<std::string> collectedHeaderNames;

  std::vector<std::pair<String, String>> pendingHeaders;
  size_t requestContentLength = 0;
  // Three states, and conflating two of them emitted "Content-Length: 0" for
  // every streamed response. NOT_SET means the handler said nothing, so the
  // body is whatever send() was handed. UNKNOWN means the handler explicitly
  // does not know yet and will stream, which is chunked transfer encoding.
  // Anything else is a promise to send exactly that many bytes.
  size_t plannedLength = CONTENT_LENGTH_NOT_SET;
  // Set once the response went out as chunked; cleared by the terminating
  // zero-length chunk, so an unterminated stream can be closed off.
  bool chunked = false;
  bool chunkedFinished = false;
  bool headersSent = false;

  HTTPUpload currentUpload;
  HTTPRaw currentRaw;
};

// The tree also spells it this way on newer cores.
using WebServerClass = WebServer;
