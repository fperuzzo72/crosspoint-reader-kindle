#pragma once

// UDP over a real socket. Used for the Calibre wireless-connect broadcast and
// for SSDP-style discovery, both of which work unchanged here.

#include <cstdint>

#include "IPAddress.h"
#include "Stream.h"

class NetworkUdp : public Stream {
 public:
  NetworkUdp() = default;
  ~NetworkUdp() override;

  uint8_t begin(uint16_t port);
  uint8_t beginMulticast(IPAddress group, uint16_t port);
  void stop();

  int beginPacket(const char* host, uint16_t port);
  int beginPacket(IPAddress ip, uint16_t port);
  int endPacket();

  // Without this, the overrides below HIDE Print's write(const char*) and
  // every write("literal") fails to compile against the uint8_t overload.
  using Print::write;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t size) override;

  int parsePacket();
  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t size) override;
  // Arduino's UDP offers both spellings and callers use the char one for text
  // payloads (the Calibre discovery handshake is one).
  int read(char* buf, size_t size) { return read(reinterpret_cast<uint8_t*>(buf), size); }
  int peek() override;
  void flush() override {}

  IPAddress remoteIP() const { return remoteAddr; }
  uint16_t remotePort() const { return remotePortNum; }

 private:
  int sock = -1;
  IPAddress remoteAddr;
  uint16_t remotePortNum = 0;

  // Outgoing datagram assembled between beginPacket and endPacket: UDP is
  // message-oriented, so bytes cannot go out as they are written the way a TCP
  // stream's can.
  uint8_t txBuf[1472] = {0};  // one Ethernet MTU minus IP and UDP headers
  size_t txLen = 0;
  uint32_t txAddr = 0;
  uint16_t txPort = 0;

  uint8_t rxBuf[1472] = {0};
  size_t rxLen = 0;
  size_t rxPos = 0;
};

using WiFiUDP = NetworkUdp;
// The ESP32 core ships both spellings and the tree uses both.
using NetworkUDP = NetworkUdp;
