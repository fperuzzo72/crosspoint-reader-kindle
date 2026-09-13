#pragma once

// Kindle (i.MX6 EPDC) display backend.
//
// CrossPoint's whole app talks to HalDisplay and nothing else: no file outside
// lib/hal/ includes EInkDisplay.h or FreeInkDisplay.h. That makes the HAL the
// only seam this port has to cut, and this file is the display half of it.
//
// The ESP32 targets drive a raw panel over SPI/i80 through the FreeInk SDK's
// PanelDriver stack. A Kindle has none of that: the kernel's EPDC driver owns
// the panel, userspace gets an 8bpp grayscale framebuffer at /dev/fb0 plus a
// set of MXCFB_* ioctls to trigger waveforms. So FreeInkDisplay is bypassed
// entirely here rather than given an twelfth PanelDriver.
//
// The ioctls are reached through FBInk rather than hand-rolled. FBInk already
// carries the per-model quirk table for every Kindle ever shipped (rotation,
// viewport origin, bpp, which waveforms the panel actually honors), and the
// jailbreak installs it on the device anyway at /mnt/us/libkh/bin/fbink.
// Rediscovering that table from scratch would be weeks of work with a device
// in hand, and this port has no way to guess it wrong safely.
//
// Framebuffer contract, matching HalDisplay: CrossPoint composes into a 1bpp
// packed buffer, rows byte-aligned at widthBytes each, MSB = leftmost pixel,
// a SET bit meaning WHITE (clearScreen's default fill is 0xFF). The panel
// wants 8bpp gray, so every paint expands that buffer before handing it over.

#include <cstddef>
#include <cstdint>

namespace crosspoint::kindle {

// Kindle 8th generation (KT3, i.MX6SL, 6" 167ppi, no frontlight).
// Compile-time because the resolution of a given Kindle model never changes;
// begin() still checks these against what the kernel reports and refuses to
// run on a mismatch rather than scribbling past the end of the panel.
inline constexpr uint16_t KT3_WIDTH = 600;
inline constexpr uint16_t KT3_HEIGHT = 800;
inline constexpr uint16_t KT3_WIDTH_BYTES = KT3_WIDTH / 8;
inline constexpr uint32_t KT3_BUFFER_SIZE = static_cast<uint32_t>(KT3_WIDTH_BYTES) * KT3_HEIGHT;

// Gray levels the 1bpp expansion produces. Not 0/255 by accident: these are
// the values the EPDC's GC16/GL16 waveforms land on cleanly.
inline constexpr uint8_t GRAY_BLACK = 0x00;
inline constexpr uint8_t GRAY_WHITE = 0xFF;

// Expand a 1bpp packed frame into 8bpp grayscale, one output byte per pixel.
//
// Pure, allocation-free, and hardware-free precisely so it can be tested on
// the host: it is the one piece of this backend whose correctness does not
// need a Kindle on the desk. `src` is widthBytes * height bytes; `dst` is
// width * height bytes. Bits beyond `width` in the final byte of a row are
// padding and are not emitted, so a width that is not a multiple of 8 stays
// correct (no Kindle needs that today, but the EpdFont emitter shipped a bug
// of exactly this shape once).
void expand1bppToGray8(const uint8_t* src, uint8_t* dst, uint16_t width, uint16_t height, uint16_t srcRowBytes);

// How hard to drive the panel. Mirrors HalDisplay::RefreshMode so the Kindle
// HalDisplay can forward its argument straight through.
enum class Waveform : uint8_t {
  // GC16 with a black flash. Full 16-level repaint, clears accumulated ghost.
  Full,
  // GL16, no flash. The "text on white" waveform: what a page turn should use.
  Half,
  // DU, no flash. Two-level and quick; for UI that changes under a finger.
  // Deliberately not A2: A2 is faster still but leaves enough residue that a
  // reader ends up flashing more often to scrub it, which is a net loss.
  Fast,
};

class KindleFrameBuffer {
 public:
  KindleFrameBuffer() = default;
  ~KindleFrameBuffer();

  KindleFrameBuffer(const KindleFrameBuffer&) = delete;
  KindleFrameBuffer& operator=(const KindleFrameBuffer&) = delete;

  // Opens /dev/fb0 and initialises FBInk. Returns false and leaves the object
  // closed if the device geometry disagrees with KT3_WIDTH/KT3_HEIGHT.
  bool begin();
  void end();
  bool isOpen() const { return fbfd >= 0; }

  uint16_t width() const { return panelWidth; }
  uint16_t height() const { return panelHeight; }

  // Blocking paint: expand, blit, refresh, wait for the waveform to finish.
  void display(const uint8_t* frame, Waveform waveform);

  // Non-blocking paint. Returns true when a waveform is genuinely in flight
  // and waitComplete() must follow; false when it already finished inline.
  // The EPDC copies the frame out on submission, so unlike the SPI panels the
  // caller may reuse its framebuffer immediately after this returns.
  bool displayStart(const uint8_t* frame, Waveform waveform);
  void waitComplete();

  void deepSleep();

 private:
  void blit(const uint8_t* frame);

  int fbfd = -1;
  uint16_t panelWidth = 0;
  uint16_t panelHeight = 0;
  uint32_t pendingMarker = 0;
  bool hasPending = false;
  uint8_t* gray = nullptr;  // width * height scratch for the expansion
};

}  // namespace crosspoint::kindle
