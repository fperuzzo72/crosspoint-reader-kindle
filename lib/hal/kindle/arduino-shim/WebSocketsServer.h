#pragma once

// WebSockets server.
//
// Four files could not compile without this header, ActivityManager among
// them, and ActivityManager defines RenderLock and the navigation the entire
// UI calls. That is why it is worth doing: it was the single biggest cluster
// of undefined references left.
//
// Implemented over real sockets, because sockets work here. RFC 6455 is not
// large for what CrossPoint uses it for: an upgrade handshake, text frames out
// to a browser, and a close. What is NOT implemented is the parts nothing in
// the tree exercises, and each one says so at the point of use rather than
// pretending:
//
//   - No frame fragmentation. A text frame longer than one frame's payload is
//     refused rather than silently truncated.
//   - No permessage-deflate. The handshake never negotiates it.
//   - No client-side role.
//
// The upload path this would serve is the fast-transfer WebSocket, and
// WebServer's multipart upload is not implemented either; see its header. So
// this compiles and serves, and the file-transfer feature stays incomplete
// until both are finished.

#include <cstdint>
#include <functional>
#include <vector>

#include "../ArduinoCompat.h"

enum WStype_t : uint8_t {
  WStype_ERROR,
  WStype_DISCONNECTED,
  WStype_CONNECTED,
  WStype_TEXT,
  WStype_BIN,
  WStype_FRAGMENT_TEXT_START,
  WStype_FRAGMENT_BIN_START,
  WStype_FRAGMENT,
  WStype_FRAGMENT_FIN,
  WStype_PING,
  WStype_PONG,
};

class WebSocketsServer {
 public:
  using WebSocketServerEvent = std::function<void(uint8_t num, WStype_t type, uint8_t* payload, size_t length)>;

  explicit WebSocketsServer(uint16_t port) : listenPort(port) {}
  ~WebSocketsServer();

  void begin();
  void close();
  void onEvent(WebSocketServerEvent handler) { eventHandler = handler; }

  // Drives everything: accepts, completes handshakes, reads frames, fires the
  // event handler. Called from the UI loop, so it never blocks.
  void loop();

  bool sendTXT(uint8_t num, const String& payload);
  bool sendTXT(uint8_t num, const char* payload);
  bool sendBIN(uint8_t num, const uint8_t* payload, size_t length);
  void disconnect(uint8_t num);

  uint8_t connectedClients() const;

 private:
  struct Client {
    int fd = -1;
    bool handshakeDone = false;
    std::string inbox;
  };

  bool acceptPending();
  void serviceClient(uint8_t num);
  bool completeHandshake(uint8_t num);
  bool readFrames(uint8_t num);
  bool sendFrame(uint8_t num, uint8_t opcode, const uint8_t* payload, size_t length);
  void dropClient(uint8_t num, bool notify);

  // Four is generous for a reader: the web UI is one browser tab.
  static constexpr uint8_t MAX_CLIENTS = 4;

  uint16_t listenPort;
  int listenFd = -1;
  Client clients[MAX_CLIENTS];
  WebSocketServerEvent eventHandler;
};
