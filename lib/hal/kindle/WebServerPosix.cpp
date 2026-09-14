// HTTP/1.1 over sockets. See arduino-shim/WebServer.h for scope and for the
// one thing deliberately left unimplemented.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "arduino-shim/WebServer.h"

namespace detail {

std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size()) {
      const auto hex = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hex(in[i + 1]);
      const int lo = hex(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
      out += in[i];  // malformed escape: pass it through rather than losing it
    } else {
      out += in[i];
    }
  }
  return out;
}

std::string lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

HTTPMethod methodFromToken(const std::string& token) {
  if (token == "GET") return HTTP_GET;
  if (token == "POST") return HTTP_POST;
  if (token == "PUT") return HTTP_PUT;
  if (token == "DELETE") return HTTP_DELETE;
  if (token == "OPTIONS") return HTTP_OPTIONS;
  if (token == "HEAD") return HTTP_HEAD;
  // WebDAV.
  if (token == "PROPFIND") return HTTP_PROPFIND;
  if (token == "PROPPATCH") return HTTP_PROPPATCH;
  if (token == "MKCOL") return HTTP_MKCOL;
  if (token == "MOVE") return HTTP_MOVE;
  if (token == "COPY") return HTTP_COPY;
  if (token == "LOCK") return HTTP_LOCK;
  if (token == "UNLOCK") return HTTP_UNLOCK;
  return HTTP_ANY;
}

const char* reasonPhrase(const int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    default: return "";
  }
}

// Read a line ending in CRLF (or bare LF, which some clients send).
bool readLine(WiFiClient& c, std::string* out) {
  out->clear();
  for (;;) {
    const int ch = c.read();
    if (ch < 0) {
      return !out->empty();
    }
    if (ch == '\n') {
      if (!out->empty() && out->back() == '\r') {
        out->pop_back();
      }
      return true;
    }
    *out += static_cast<char>(ch);
    if (out->size() > 8192) {
      return false;  // a header line this long is an attack or a bug
    }
  }
}

}  // namespace detail

// Everything in detail is used unqualified below; only urlDecode needs the
// qualification, because WebServer has a member of the same name that would
// otherwise hide it and recurse.
using namespace detail;

WebServer::~WebServer() { stop(); }

void WebServer::begin() { begin(listenPort); }

void WebServer::begin(const uint16_t port) {
  stop();
  listenPort = port;

  if (uploadRouteRegistered) {
    // Refuse loudly rather than accept a browser's POST and drop the book.
    std::fprintf(stderr,
                 "[kindle] WebServer: a route registers an upload handler, and multipart\n"
                 "         upload is not implemented on this target. Refusing to start so\n"
                 "         the failure is here rather than halfway through a file.\n");
    return;
  }

  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    return;
  }
  const int reuse = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 4) != 0) {
    ::close(listenFd);
    listenFd = -1;
    return;
  }
  // Non-blocking accept: handleClient() is called from the UI loop and must
  // never stall it waiting for a browser that may never come.
  const int flags = fcntl(listenFd, F_GETFL, 0);
  fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);
}

void WebServer::stop() {
  if (listenFd >= 0) {
    ::close(listenFd);
    listenFd = -1;
  }
  activeClient.stop();
}

void WebServer::on(const String& u, THandlerFunction handler) { on(u, HTTP_ANY, handler); }

void WebServer::on(const String& u, const HTTPMethod m, THandlerFunction handler) {
  routes.push_back(Route{u, m, handler});
}

void WebServer::on(const String& u, const HTTPMethod m, THandlerFunction handler, THandlerFunction) {
  uploadRouteRegistered = true;
  routes.push_back(Route{u, m, handler});
}

void WebServer::collectHeaders(const char* headerKeys[], const size_t count) {
  collectedHeaderNames.clear();
  for (size_t i = 0; i < count; ++i) {
    collectedHeaderNames.push_back(lower(headerKeys[i]));
  }
}

void WebServer::handleClient() {
  if (listenFd < 0) {
    return;
  }
  const int fd = accept(listenFd, nullptr, nullptr);
  if (fd < 0) {
    return;  // nothing waiting
  }
  activeClient = WiFiClient(fd);

  reqArgs.clear();
  reqHeaders.clear();
  requestContentLength = 0;
  pendingHeaders.clear();
  plannedLength = SIZE_MAX;
  headersSent = false;

  if (readRequest()) {
    dispatch();
  }
  if (!headersSent) {
    // A handler that sent nothing still owes the browser a response.
    send(500, "text/plain", String("handler produced no response"));
  }
  activeClient.stop();
}

bool WebServer::readRequest() {
  std::string line;
  if (!readLine(activeClient, &line) || line.empty()) {
    return false;
  }

  const size_t sp1 = line.find(' ');
  const size_t sp2 = line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    return false;
  }
  reqMethod = methodFromToken(line.substr(0, sp1));
  std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);

  const size_t q = target.find('?');
  if (q != std::string::npos) {
    parseQuery(target.substr(q + 1));
    target = target.substr(0, q);
  }
  reqUri = String(detail::urlDecode(target));

  size_t contentLength = 0;
  bool urlencodedBody = false;
  while (readLine(activeClient, &line) && !line.empty()) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string name = lower(line.substr(0, colon));
    size_t vstart = colon + 1;
    while (vstart < line.size() && line[vstart] == ' ') {
      ++vstart;
    }
    const std::string value = line.substr(vstart);

    if (name == "content-length") {
      contentLength = static_cast<size_t>(std::strtoul(value.c_str(), nullptr, 10));
      requestContentLength = contentLength;
    } else if (name == "content-type" && value.find("application/x-www-form-urlencoded") != std::string::npos) {
      urlencodedBody = true;
    }
    reqHeaders[name] = value;
  }

  // Form bodies become args, exactly as the Arduino original does, so handlers
  // read POST fields through arg() without caring where they came from.
  if (contentLength > 0 && urlencodedBody && contentLength < (1u << 20)) {
    std::string body;
    body.resize(contentLength);
    size_t got = 0;
    while (got < contentLength) {
      const int n = activeClient.read(reinterpret_cast<uint8_t*>(&body[got]), contentLength - got);
      if (n <= 0) {
        break;
      }
      got += static_cast<size_t>(n);
    }
    body.resize(got);
    parseQuery(body);
  }
  return true;
}

void WebServer::parseQuery(const std::string& query) {
  size_t pos = 0;
  while (pos < query.size()) {
    size_t amp = query.find('&', pos);
    if (amp == std::string::npos) {
      amp = query.size();
    }
    const std::string pair = query.substr(pos, amp - pos);
    const size_t eq = pair.find('=');
    if (eq != std::string::npos) {
      reqArgs[detail::urlDecode(pair.substr(0, eq))] = detail::urlDecode(pair.substr(eq + 1));
    } else if (!pair.empty()) {
      reqArgs[detail::urlDecode(pair)] = "";
    }
    pos = amp + 1;
  }
}

String WebServer::urlDecode(const String& encoded) {
  // Qualified: inside a member, the bare name resolves to this very function
  // rather than the helper, which recurses forever.
  return String(detail::urlDecode(encoded.str()));
}

void WebServer::addHandler(RequestHandler* handler) {
  if (handler != nullptr) {
    handlers.push_back(handler);
  }
}

void WebServer::dispatch() {
  // Handler objects first, in registration order: they claim whole subtrees
  // (WebDAV does), which an exact-match route cannot express.
  for (RequestHandler* h : handlers) {
    if (h->canHandle(*this, reqMethod, reqUri) && h->handle(*this, reqMethod, reqUri)) {
      return;
    }
  }

  for (const Route& r : routes) {
    if (r.uri != reqUri) {
      continue;
    }
    if (r.method != HTTP_ANY && r.method != reqMethod) {
      continue;
    }
    if (r.handler) {
      r.handler();
      return;
    }
  }
  if (notFoundHandler) {
    notFoundHandler();
    return;
  }
  send(404, "text/plain", String("Not Found"));
}

String WebServer::arg(const String& name) const {
  const auto it = reqArgs.find(name.str());
  return it == reqArgs.end() ? String() : String(it->second);
}

String WebServer::arg(const int index) const {
  if (index < 0 || static_cast<size_t>(index) >= reqArgs.size()) {
    return String();
  }
  auto it = reqArgs.begin();
  std::advance(it, index);
  return String(it->second);
}

String WebServer::argName(const int index) const {
  if (index < 0 || static_cast<size_t>(index) >= reqArgs.size()) {
    return String();
  }
  auto it = reqArgs.begin();
  std::advance(it, index);
  return String(it->first);
}

bool WebServer::hasArg(const String& name) const { return reqArgs.find(name.str()) != reqArgs.end(); }

String WebServer::header(const String& name) const {
  const auto it = reqHeaders.find(lower(name.str()));
  return it == reqHeaders.end() ? String() : String(it->second);
}

bool WebServer::hasHeader(const String& name) const { return reqHeaders.count(lower(name.str())) > 0; }

void WebServer::sendHeader(const String& name, const String& value, const bool first) {
  if (first) {
    pendingHeaders.insert(pendingHeaders.begin(), {name, value});
  } else {
    pendingHeaders.push_back({name, value});
  }
}

void WebServer::writeStatusLine(const int code, const String& contentType) {
  char line[128];
  std::snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", code, reasonPhrase(code));
  activeClient.write(line);
  activeClient.write("Connection: close\r\n");

  if (!contentType.isEmpty()) {
    activeClient.write("Content-Type: ");
    activeClient.write(contentType.c_str());
    activeClient.write("\r\n");
  }
  if (corsEnabled) {
    activeClient.write("Access-Control-Allow-Origin: *\r\n");
  }
  for (const auto& h : pendingHeaders) {
    activeClient.write(h.first.c_str());
    activeClient.write(": ");
    activeClient.write(h.second.c_str());
    activeClient.write("\r\n");
  }
}

void WebServer::send(const int code, const char* contentType, const String& content) {
  send(code, String(contentType != nullptr ? contentType : "text/plain"), content);
}

void WebServer::send(const int code, const String& contentType, const String& content) {
  if (headersSent) {
    // A second send() on one request is a bug in the handler; treat it as more
    // body rather than emitting a second set of headers into the stream.
    sendContent(content);
    return;
  }
  writeStatusLine(code, contentType);

  // setContentLength() announces a body that arrives through later
  // sendContent() calls; without it the body is what is passed here.
  char lenLine[64];
  const size_t length = plannedLength != SIZE_MAX ? plannedLength : content.length();
  std::snprintf(lenLine, sizeof(lenLine), "Content-Length: %zu\r\n\r\n", length);
  activeClient.write(lenLine);
  headersSent = true;

  if (!content.isEmpty()) {
    activeClient.write(reinterpret_cast<const uint8_t*>(content.c_str()), content.length());
  }
}

void WebServer::send_P(const int code, const char* contentType, const char* content) {
  send(code, String(contentType != nullptr ? contentType : "text/plain"),
       String(content != nullptr ? content : ""));
}

void WebServer::send_P(const int code, const char* contentType, const char* content, const size_t length) {
  send(code, String(contentType != nullptr ? contentType : "text/plain"),
       String(std::string(content != nullptr ? content : "", length)));
}

void WebServer::sendContent(const String& content) {
  sendContent(content.c_str(), content.length());
}

void WebServer::sendContent(const char* content, const size_t length) {
  if (content == nullptr || length == 0) {
    return;
  }
  if (!headersSent) {
    // Streaming without a status line first: emit a bare 200 rather than
    // sending a naked body the browser cannot interpret.
    send(200, "text/plain", String());
  }
  activeClient.write(reinterpret_cast<const uint8_t*>(content), length);
}
