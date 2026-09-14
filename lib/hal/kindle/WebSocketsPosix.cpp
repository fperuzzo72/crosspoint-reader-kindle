// RFC 6455 server over sockets. See arduino-shim/WebSocketsServer.h for what
// is deliberately not implemented.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "arduino-shim/WebSocketsServer.h"
#include "arduino-shim/base64.h"

namespace {

// RFC 6455 section 1.3: this exact GUID is appended to the client key before
// hashing. It is not a secret and not configurable.
constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr uint8_t OPCODE_TEXT = 0x1;
constexpr uint8_t OPCODE_BINARY = 0x2;
constexpr uint8_t OPCODE_CLOSE = 0x8;
constexpr uint8_t OPCODE_PING = 0x9;
constexpr uint8_t OPCODE_PONG = 0xA;

std::string lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return s;
}

bool sendAll(const int fd, const void* data, const size_t len) {
  const auto* p = static_cast<const uint8_t*>(data);
  size_t sent = 0;
  while (sent < len) {
    // MSG_NOSIGNAL: a browser tab closing must not kill the reader.
    const ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

WebSocketsServer::~WebSocketsServer() { close(); }

void WebSocketsServer::begin() {
  close();
  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    return;
  }
  const int reuse = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(listenPort);
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 2) != 0) {
    ::close(listenFd);
    listenFd = -1;
    return;
  }
  // Non-blocking throughout: loop() runs from the UI loop and must never stall
  // it waiting on a browser.
  fcntl(listenFd, F_SETFL, fcntl(listenFd, F_GETFL, 0) | O_NONBLOCK);
}

void WebSocketsServer::close() {
  for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
    dropClient(i, false);
  }
  if (listenFd >= 0) {
    ::close(listenFd);
    listenFd = -1;
  }
}

void WebSocketsServer::dropClient(const uint8_t num, const bool notify) {
  if (num >= MAX_CLIENTS || clients[num].fd < 0) {
    return;
  }
  ::close(clients[num].fd);
  clients[num] = Client{};
  if (notify && eventHandler) {
    eventHandler(num, WStype_DISCONNECTED, nullptr, 0);
  }
}

bool WebSocketsServer::acceptPending() {
  if (listenFd < 0) {
    return false;
  }
  const int fd = accept(listenFd, nullptr, nullptr);
  if (fd < 0) {
    return false;
  }
  for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].fd < 0) {
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
      clients[i].fd = fd;
      clients[i].handshakeDone = false;
      clients[i].inbox.clear();
      return true;
    }
  }
  // Full. Closing immediately is better than holding a connection the server
  // will never service.
  ::close(fd);
  return false;
}

bool WebSocketsServer::completeHandshake(const uint8_t num) {
  Client& c = clients[num];

  char buf[1024];
  const ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
  if (n <= 0) {
    return false;
  }
  c.inbox.append(buf, static_cast<size_t>(n));

  // Wait for the blank line that ends the request headers.
  const size_t end = c.inbox.find("\r\n\r\n");
  if (end == std::string::npos) {
    return c.inbox.size() < 8192;  // still arriving, unless it is absurd
  }

  // Pull Sec-WebSocket-Key out of the headers.
  std::string key;
  size_t pos = 0;
  while (pos < end) {
    size_t eol = c.inbox.find("\r\n", pos);
    if (eol == std::string::npos || eol > end) {
      break;
    }
    const std::string line = c.inbox.substr(pos, eol - pos);
    const size_t colon = line.find(':');
    if (colon != std::string::npos && lower(line.substr(0, colon)) == "sec-websocket-key") {
      size_t vstart = colon + 1;
      while (vstart < line.size() && line[vstart] == ' ') {
        ++vstart;
      }
      key = line.substr(vstart);
    }
    pos = eol + 2;
  }

  if (key.empty()) {
    // Not a WebSocket upgrade. Answer rather than hanging up silently, so a
    // stray browser request gets something intelligible.
    static const char* kBadRequest =
        "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    sendAll(c.fd, kBadRequest, std::strlen(kBadRequest));
    return false;
  }

  const std::string combined = key + WS_GUID;
  uint8_t digest[20];
  sha1(reinterpret_cast<const uint8_t*>(combined.data()), combined.size(), digest);
  const String accept = base64::encode(digest, sizeof(digest));

  char response[256];
  const int len = std::snprintf(response, sizeof(response),
                                "HTTP/1.1 101 Switching Protocols\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Accept: %s\r\n\r\n",
                                accept.c_str());
  if (!sendAll(c.fd, response, static_cast<size_t>(len))) {
    return false;
  }

  c.handshakeDone = true;
  c.inbox.erase(0, end + 4);
  if (eventHandler) {
    eventHandler(num, WStype_CONNECTED, nullptr, 0);
  }
  return true;
}

bool WebSocketsServer::readFrames(const uint8_t num) {
  Client& c = clients[num];

  uint8_t buf[2048];
  const ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
  if (n == 0) {
    return false;  // peer closed
  }
  if (n < 0) {
    return errno == EAGAIN || errno == EWOULDBLOCK;
  }
  c.inbox.append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));

  for (;;) {
    if (c.inbox.size() < 2) {
      return true;
    }
    const auto* f = reinterpret_cast<const uint8_t*>(c.inbox.data());
    const uint8_t opcode = f[0] & 0x0F;
    const bool masked = (f[1] & 0x80) != 0;
    uint64_t payloadLen = f[1] & 0x7F;
    size_t offset = 2;

    if (payloadLen == 126) {
      if (c.inbox.size() < 4) {
        return true;
      }
      payloadLen = (static_cast<uint64_t>(f[2]) << 8) | f[3];
      offset = 4;
    } else if (payloadLen == 127) {
      if (c.inbox.size() < 10) {
        return true;
      }
      payloadLen = 0;
      for (int i = 0; i < 8; ++i) {
        payloadLen = (payloadLen << 8) | f[2 + i];
      }
      offset = 10;
    }

    // A client frame must be masked (RFC 6455 section 5.1); an unmasked one is
    // a protocol error, not something to tolerate.
    if (!masked) {
      return false;
    }
    if (c.inbox.size() < offset + 4 + payloadLen) {
      return true;  // frame still arriving
    }

    uint8_t mask[4];
    std::memcpy(mask, f + offset, 4);
    offset += 4;

    std::string payload(c.inbox, offset, static_cast<size_t>(payloadLen));
    for (size_t i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i & 3]);
    }
    c.inbox.erase(0, offset + static_cast<size_t>(payloadLen));

    switch (opcode) {
      case OPCODE_CLOSE:
        return false;
      case OPCODE_PING:
        sendFrame(num, OPCODE_PONG, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
        break;
      case OPCODE_PONG:
        break;
      case OPCODE_TEXT:
      case OPCODE_BINARY:
        if (eventHandler) {
          eventHandler(num, opcode == OPCODE_TEXT ? WStype_TEXT : WStype_BIN,
                       reinterpret_cast<uint8_t*>(payload.data()), payload.size());
        }
        break;
      default:
        // Continuation frames land here. Fragmentation is not implemented, and
        // dropping the connection is more honest than delivering half a
        // message as if it were whole.
        return false;
    }
  }
}

void WebSocketsServer::serviceClient(const uint8_t num) {
  Client& c = clients[num];
  if (c.fd < 0) {
    return;
  }
  const bool ok = c.handshakeDone ? readFrames(num) : completeHandshake(num);
  if (!ok) {
    dropClient(num, c.handshakeDone);
  }
}

void WebSocketsServer::loop() {
  acceptPending();
  for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
    serviceClient(i);
  }
}

bool WebSocketsServer::sendFrame(const uint8_t num, const uint8_t opcode, const uint8_t* payload,
                                 const size_t length) {
  if (num >= MAX_CLIENTS || clients[num].fd < 0 || !clients[num].handshakeDone) {
    return false;
  }

  uint8_t header[10];
  size_t headerLen = 2;
  header[0] = static_cast<uint8_t>(0x80 | opcode);  // FIN set: no fragmentation
  if (length < 126) {
    header[1] = static_cast<uint8_t>(length);
  } else if (length <= 0xFFFF) {
    header[1] = 126;
    header[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
    header[3] = static_cast<uint8_t>(length & 0xFF);
    headerLen = 4;
  } else {
    header[1] = 127;
    for (int i = 0; i < 8; ++i) {
      header[2 + i] = static_cast<uint8_t>((static_cast<uint64_t>(length) >> (8 * (7 - i))) & 0xFF);
    }
    headerLen = 10;
  }
  // Server frames are never masked.
  return sendAll(clients[num].fd, header, headerLen) &&
         (length == 0 || sendAll(clients[num].fd, payload, length));
}

bool WebSocketsServer::sendTXT(const uint8_t num, const String& payload) {
  return sendFrame(num, OPCODE_TEXT, reinterpret_cast<const uint8_t*>(payload.c_str()), payload.length());
}

bool WebSocketsServer::sendTXT(const uint8_t num, const char* payload) {
  if (payload == nullptr) {
    return false;
  }
  return sendFrame(num, OPCODE_TEXT, reinterpret_cast<const uint8_t*>(payload), std::strlen(payload));
}

bool WebSocketsServer::sendBIN(const uint8_t num, const uint8_t* payload, const size_t length) {
  return sendFrame(num, OPCODE_BINARY, payload, length);
}

void WebSocketsServer::disconnect(const uint8_t num) { dropClient(num, true); }

uint8_t WebSocketsServer::connectedClients() const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i].fd >= 0 && clients[i].handshakeDone) {
      ++n;
    }
  }
  return n;
}
