#pragma once

// A TCP client over a real socket.
//
// Unlike WiFi.h's control half, there is nothing to fake here: the Kindle has
// a working network stack and BSD sockets do exactly what Arduino's WiFiClient
// promises. So this is an implementation, not a stand-in, and code above it
// (OPDS, OTA checks, KOReader sync) behaves as written.

#include <cstdint>

#include "Client.h"
#include "IPAddress.h"

class WiFiClient : public Client {
 public:
  WiFiClient() = default;
  explicit WiFiClient(int existingFd) : sock(existingFd) {}
  ~WiFiClient() override;

  WiFiClient(const WiFiClient&) = delete;
  WiFiClient& operator=(const WiFiClient&) = delete;
  WiFiClient(WiFiClient&& other) noexcept;
  WiFiClient& operator=(WiFiClient&& other) noexcept;

  int connect(const char* host, uint16_t port) override;
  int connect(IPAddress ip, uint16_t port) override;
  void stop() override;
  uint8_t connected() override;
  explicit operator bool() { return connected() != 0; }

  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t size);
  int peek() override;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t size) override;
  void flush() override;

  void setNoDelay(bool enable);

 private:
  int sock = -1;
  // peek() has to look one byte ahead without consuming it, and a socket has
  // no ungetc, so the byte is held here.
  int peeked = -1;
};

// Newer ESP32 cores renamed this; the tree uses both spellings.
using NetworkClient = WiFiClient;
