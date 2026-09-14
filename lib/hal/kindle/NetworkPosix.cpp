// Wi-Fi, TCP and UDP for the Kindle build.
//
// The division of labour is the point here, and it is stated at length in
// arduino-shim/WiFi.h: reading the truth about the connection is real, taking
// control of the radio is not ours to do and fails honestly rather than
// pretending. Sockets are entirely real.

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

#include "arduino-shim/NetworkUdp.h"
#include "arduino-shim/WiFi.h"
#include "arduino-shim/WiFiClient.h"

namespace {

// The first non-loopback interface carrying an IPv4 address. On a Kindle that
// is wlan0 in practice, but finding it by property rather than by name keeps
// this working over a USB network too, which is how a jailbroken device is
// often reached.
bool firstInetInterface(char* nameOut, const size_t nameLen, uint32_t* addrOut) {
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) {
    return false;
  }
  bool found = false;
  for (const ifaddrs* ifa = list; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || (ifa->ifa_flags & IFF_UP) == 0) {
      continue;
    }
    const auto* in = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
    if (nameOut != nullptr) {
      std::snprintf(nameOut, nameLen, "%s", ifa->ifa_name);
    }
    if (addrOut != nullptr) {
      *addrOut = in->sin_addr.s_addr;
    }
    found = true;
    break;
  }
  freeifaddrs(list);
  return found;
}

bool resolveHost(const char* host, const uint16_t port, sockaddr_in* out) {
  if (host == nullptr) {
    return false;
  }
  std::memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons(port);

  if (inet_pton(AF_INET, host, &out->sin_addr) == 1) {
    return true;
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &res) != 0 || res == nullptr) {
    return false;
  }
  out->sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
  freeaddrinfo(res);
  return true;
}

}  // namespace

// ---------------------------------------------------------------- WiFi ---

WiFiClass WiFi;

wl_status_t WiFiClass::status() {
  // "Connected" means the only thing this process can actually verify: an
  // interface is up and has an address. Whether the system considers itself
  // associated is not visible from here, and guessing would be worse.
  return firstInetInterface(nullptr, 0, nullptr) ? WL_CONNECTED : WL_DISCONNECTED;
}

IPAddress WiFiClass::localIP() {
  uint32_t addr = 0;
  if (!firstInetInterface(nullptr, 0, &addr)) {
    return IPAddress();
  }
  const auto* b = reinterpret_cast<const uint8_t*>(&addr);
  return IPAddress(b[0], b[1], b[2], b[3]);
}

String WiFiClass::macAddress() {
  char name[IFNAMSIZ] = {0};
  if (!firstInetInterface(name, sizeof(name), nullptr)) {
    return String("00:00:00:00:00:00");
  }
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    return String("00:00:00:00:00:00");
  }
  ifreq req{};
  std::snprintf(req.ifr_name, IFNAMSIZ, "%s", name);
  char out[18] = "00:00:00:00:00:00";
  if (ioctl(s, SIOCGIFHWADDR, &req) == 0) {
    const auto* m = reinterpret_cast<const uint8_t*>(req.ifr_hwaddr.sa_data);
    std::snprintf(out, sizeof(out), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  close(s);
  return String(out);
}

int32_t WiFiClass::RSSI() {
  // /proc/net/wireless reports link quality per interface. Third column is the
  // signal level in dBm on this kernel.
  FILE* f = std::fopen("/proc/net/wireless", "r");
  if (f == nullptr) {
    return 0;
  }
  char line[256];
  int32_t dbm = 0;
  // Two header lines precede the data.
  if (std::fgets(line, sizeof(line), f) != nullptr && std::fgets(line, sizeof(line), f) != nullptr) {
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      char iface[64];
      int status = 0;
      float quality = 0.0F;
      float level = 0.0F;
      if (std::sscanf(line, " %63[^:]: %d %f %f", iface, &status, &quality, &level) == 4) {
        dbm = static_cast<int32_t>(level);
        break;
      }
    }
  }
  std::fclose(f);
  return dbm;
}

String WiFiClass::SSID() {
  char name[IFNAMSIZ] = {0};
  if (!firstInetInterface(name, sizeof(name), nullptr)) {
    return String();
  }
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    return String();
  }
  // SIOCGIWESSID is the wireless-extensions ioctl; 0x8B1B is its value. The
  // Kindle's 3.x kernel still carries wext, so this works where the interface
  // is genuinely wireless and quietly returns empty where it is not.
  char essid[33] = {0};
  struct {
    char ifrn_name[IFNAMSIZ];
    struct {
      void* pointer;
      uint16_t length;
      uint16_t flags;
    } u;
  } wrq{};
  std::snprintf(wrq.ifrn_name, IFNAMSIZ, "%s", name);
  wrq.u.pointer = essid;
  wrq.u.length = sizeof(essid) - 1;
  String result;
  if (ioctl(s, 0x8B1B, &wrq) == 0) {
    result = String(essid);
  }
  close(s);
  return result;
}

String WiFiClass::getHostname() {
  char host[256] = {0};
  if (gethostname(host, sizeof(host) - 1) != 0) {
    return String();
  }
  return String(host);
}

wl_status_t WiFiClass::begin(const char*, const char*) {
  // Deliberately does nothing. See the header: the system owns association,
  // and a caller told "connecting" would wait forever.
  std::fprintf(stderr, "[kindle] WiFi.begin() ignored: the system owns the radio on this device\n");
  return WL_CONNECT_FAILED;
}

bool WiFiClass::disconnect(bool, bool) { return false; }
bool WiFiClass::softAP(const char*, const char*, int32_t, bool, int32_t) { return false; }
bool WiFiClass::softAPdisconnect(bool) { return false; }
int16_t WiFiClass::scanNetworks(bool) { return 0; }
String WiFiClass::SSID(uint8_t) { return String(); }  // no scan results to name
int32_t WiFiClass::RSSI(uint8_t) { return 0; }

// ----------------------------------------------------------- WiFiClient ---
//
// Copyable with shared ownership, like Arduino's: the tree passes clients by
// value and the socket must close when the LAST copy goes, not the first.

namespace {

// Closing happens here so every copy shares one lifetime.
std::shared_ptr<int> adoptFd(const int fd) {
  return std::shared_ptr<int>(new int(fd), [](int* p) {
    if (p != nullptr) {
      if (*p >= 0) {
        ::close(*p);
      }
      delete p;
    }
  });
}

}  // namespace

WiFiClient::WiFiClient(const int existingFd) : sockRef(adoptFd(existingFd)), peekRef(std::make_shared<int>(-1)) {}

int WiFiClient::connect(const char* host, const uint16_t port) {
  stop();
  sockaddr_in addr{};
  if (!resolveHost(host, port, &addr)) {
    return 0;
  }
  const int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return 0;
  }
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    return 0;
  }
  sockRef = adoptFd(s);
  peekRef = std::make_shared<int>(-1);
  return 1;
}

int WiFiClient::connect(const IPAddress ip, const uint16_t port) {
  char dotted[16];
  std::snprintf(dotted, sizeof(dotted), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return connect(dotted, port);
}

void WiFiClient::stop() {
  // Dropping the reference is the close; other copies, if any, keep it alive.
  sockRef.reset();
  peekRef.reset();
}

uint8_t WiFiClient::connected() {
  if (fd() < 0) {
    return 0;
  }
  // A socket whose peer has closed still exists; MSG_PEEK distinguishes "open
  // with nothing waiting" from "closed".
  char probe = 0;
  const ssize_t n = recv(fd(), &probe, 1, MSG_PEEK | MSG_DONTWAIT);
  if (n == 0) {
    return 0;  // orderly shutdown by the peer
  }
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    return 0;
  }
  return 1;
}

int WiFiClient::available() {
  const int held = peekRef && *peekRef >= 0 ? 1 : 0;
  if (fd() < 0) {
    return held;
  }
  int count = 0;
  if (ioctl(fd(), FIONREAD, &count) != 0) {
    count = 0;
  }
  return count + held;
}

int WiFiClient::read() {
  if (peekRef && *peekRef >= 0) {
    const int c = *peekRef;
    *peekRef = -1;
    return c;
  }
  uint8_t c = 0;
  return read(&c, 1) == 1 ? c : -1;
}

int WiFiClient::read(uint8_t* buf, const size_t size) {
  if (fd() < 0 || buf == nullptr || size == 0) {
    return -1;
  }
  size_t offset = 0;
  if (peekRef && *peekRef >= 0) {
    buf[0] = static_cast<uint8_t>(*peekRef);
    *peekRef = -1;
    offset = 1;
    if (size == 1) {
      return 1;
    }
  }
  const ssize_t n = recv(fd(), buf + offset, size - offset, 0);
  if (n < 0) {
    return offset > 0 ? static_cast<int>(offset) : -1;
  }
  return static_cast<int>(offset + static_cast<size_t>(n));
}

int WiFiClient::peek() {
  if (peekRef && *peekRef >= 0) {
    return *peekRef;
  }
  uint8_t c = 0;
  if (fd() < 0 || recv(fd(), &c, 1, 0) != 1) {
    return -1;
  }
  if (!peekRef) {
    peekRef = std::make_shared<int>(-1);
  }
  *peekRef = c;
  return *peekRef;
}

size_t WiFiClient::write(const uint8_t c) { return write(&c, 1); }

size_t WiFiClient::write(const uint8_t* buf, const size_t size) {
  if (fd() < 0 || buf == nullptr) {
    return 0;
  }
  size_t sent = 0;
  while (sent < size) {
    // MSG_NOSIGNAL: a write to a peer that went away must return an error, not
    // kill the process with SIGPIPE.
    const ssize_t n = send(fd(), buf + sent, size - sent, MSG_NOSIGNAL);
    if (n <= 0) {
      break;
    }
    sent += static_cast<size_t>(n);
  }
  return sent;
}

void WiFiClient::flush() {}

void WiFiClient::clear() {
  if (peekRef) {
    *peekRef = -1;
  }
  if (fd() < 0) {
    return;
  }
  // Drain without blocking: whatever is buffered goes, and the loop ends as
  // soon as the socket would wait.
  uint8_t scratch[1024];
  while (recv(fd(), scratch, sizeof(scratch), MSG_DONTWAIT) > 0) {
  }
}

void WiFiClient::setNoDelay(const bool enable) {
  if (fd() < 0) {
    return;
  }
  const int flag = enable ? 1 : 0;
  setsockopt(fd(), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

void WiFiClient::setConnectionTimeout(const uint32_t ms) {
  if (fd() < 0) {
    return;
  }
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(ms / 1000);
  tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
  setsockopt(fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// ----------------------------------------------------------- NetworkUdp ---

NetworkUdp::~NetworkUdp() { stop(); }

uint8_t NetworkUdp::begin(const uint16_t port) {
  stop();
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    return 0;
  }
  const int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  // Broadcast is needed for Calibre's wireless-connect discovery.
  const int broadcast = 1;
  setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(sock);
    sock = -1;
    return 0;
  }
  return 1;
}

uint8_t NetworkUdp::beginMulticast(const IPAddress group, const uint16_t port) {
  if (begin(port) == 0) {
    return 0;
  }
  ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = htonl((static_cast<uint32_t>(group[0]) << 24) | (static_cast<uint32_t>(group[1]) << 16) |
                                    (static_cast<uint32_t>(group[2]) << 8) | static_cast<uint32_t>(group[3]));
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
    stop();
    return 0;
  }
  return 1;
}

void NetworkUdp::stop() {
  if (sock >= 0) {
    close(sock);
    sock = -1;
  }
  txLen = rxLen = rxPos = 0;
}

int NetworkUdp::beginPacket(const char* host, const uint16_t port) {
  sockaddr_in addr{};
  if (!resolveHost(host, port, &addr)) {
    return 0;
  }
  txAddr = addr.sin_addr.s_addr;
  txPort = port;
  txLen = 0;
  return 1;
}

int NetworkUdp::beginPacket(const IPAddress ip, const uint16_t port) {
  char dotted[16];
  std::snprintf(dotted, sizeof(dotted), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return beginPacket(dotted, port);
}

int NetworkUdp::endPacket() {
  if (sock < 0) {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = txAddr;
  addr.sin_port = htons(txPort);
  const ssize_t n = sendto(sock, txBuf, txLen, MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  txLen = 0;
  return n > 0 ? 1 : 0;
}

size_t NetworkUdp::write(const uint8_t c) { return write(&c, 1); }

size_t NetworkUdp::write(const uint8_t* buf, const size_t size) {
  if (buf == nullptr) {
    return 0;
  }
  // Silently dropping the overflow would produce a truncated datagram that
  // looks valid; refusing the excess makes the caller's total come up short,
  // which is visible.
  const size_t room = sizeof(txBuf) - txLen;
  const size_t take = size < room ? size : room;
  std::memcpy(txBuf + txLen, buf, take);
  txLen += take;
  return take;
}

int NetworkUdp::parsePacket() {
  if (sock < 0) {
    return 0;
  }
  sockaddr_in from{};
  socklen_t fromLen = sizeof(from);
  const ssize_t n = recvfrom(sock, rxBuf, sizeof(rxBuf), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&from), &fromLen);
  if (n <= 0) {
    rxLen = rxPos = 0;
    return 0;
  }
  rxLen = static_cast<size_t>(n);
  rxPos = 0;
  const auto* b = reinterpret_cast<const uint8_t*>(&from.sin_addr.s_addr);
  remoteAddr = IPAddress(b[0], b[1], b[2], b[3]);
  remotePortNum = ntohs(from.sin_port);
  return static_cast<int>(rxLen);
}

int NetworkUdp::available() { return static_cast<int>(rxLen - rxPos); }

int NetworkUdp::read() { return rxPos < rxLen ? rxBuf[rxPos++] : -1; }

int NetworkUdp::read(uint8_t* buf, const size_t size) {
  if (buf == nullptr) {
    return -1;
  }
  const size_t left = rxLen - rxPos;
  const size_t take = size < left ? size : left;
  std::memcpy(buf, rxBuf + rxPos, take);
  rxPos += take;
  return static_cast<int>(take);
}

int NetworkUdp::peek() { return rxPos < rxLen ? rxBuf[rxPos] : -1; }

#include "arduino-shim/ESPmDNS.h"

MDNSResponder MDNS;

String IPAddress::toString() const {
  char buf[16];
  toCharArray(buf, sizeof(buf));
  return String(buf);
}
