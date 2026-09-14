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

// The two intermediate levels CrossPoint's gray planes can express. The EPDC
// resolves 16 through GC16/GL16; the renderer only ever asks for four, so these
// are the two that sit evenly between black and white.
inline constexpr uint8_t GRAY_DARK = 0x55;
inline constexpr uint8_t GRAY_LIGHT = 0xAA;

// Expand a 1bpp packed frame into 8bpp grayscale, one output byte per pixel.
//
// Pure, allocation-free, and hardware-free precisely so it can be tested on
// the host: it is the one piece of this backend whose correctness does not
// need a Kindle on the desk.
//
// Both sides carry their own row stride, and they genuinely differ: the KT3's
// framebuffer reports a scanline stride of 608 bytes for a 600px-wide panel
// (measured on device), so writing rows back to back would shear the image
// progressively down the screen. `dstRowBytes` is that stride, and the padding
// bytes past `width` are left untouched rather than cleared, since they are
// not on the panel.
//
// Bits beyond `width` in the final SOURCE byte of a row are padding and are
// not emitted, so a width that is not a multiple of 8 stays correct (no Kindle
// needs that today, but the EpdFont emitter shipped a bug of exactly this
// shape once).
void expand1bppToGray8(const uint8_t* src, uint8_t* dst, uint16_t width, uint16_t height, uint16_t srcRowBytes,
                       uint32_t dstRowBytes);

// Paint CrossPoint's two 1bpp grayscale overlay planes on top of an 8bpp frame
// that already holds the black-and-white base.
//
// The renderer expresses a gray pixel as a pair of bits, one from each plane,
// in the encoding the SDK calls OverlayMasks. Both planes start cleared and a
// plane SETS the bit where it claims the pixel, so, as (LSB, MSB):
//
//   (0,0)  this pixel is not gray; whatever the B/W base painted stands
//   (1,1)  dark
//   (0,1)  light
//   (1,0)  not produced by the encoding; left to the base rather than guessed
//
// Pixels the planes do not claim are not written at all, which is why `dst` is
// in-out: on this backend the base frame is already sitting in panel memory, so
// "keep the base" costs nothing and needs no second copy of it.
//
// Pure and hardware-free for the same reason as expand1bppToGray8: this is
// arithmetic that a host test can hold to account, and a wrong bit here is
// invisible in a photograph of a page of text.
void overlayGrayPlanesOnGray8(const uint8_t* lsbPlane, const uint8_t* msbPlane, uint8_t* dst, uint16_t width,
                              uint16_t height, uint16_t srcRowBytes, uint32_t dstRowBytes);

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

  // Blocking paint: expand into the mapped framebuffer, refresh, wait for the
  // waveform to finish. Returns false if the paint could not be issued.
  bool display(const uint8_t* frame, Waveform waveform);

  // Non-blocking paint. Returns true when a waveform is genuinely in flight
  // and waitComplete() must follow; false when it finished inline OR could not
  // be issued at all, so check isOpen() rather than reading false as success.
  // The EPDC copies the frame out on submission, so unlike the SPI panels the
  // caller may reuse its framebuffer immediately after this returns.
  bool displayStart(const uint8_t* frame, Waveform waveform);

  // Tear the connection to /dev/fb0 down and build it again.
  //
  // For after a system suspend. The mapping survives one as perfectly valid
  // memory, which is the problem: if the driver re-allocated the framebuffer
  // while the panel was powered down, writes still succeed and simply stop
  // reaching the panel, so a refresh shows whatever the driver left behind.
  // Nothing readable from userspace distinguishes that from a healthy mapping,
  // because reading back our own write proves only that the memory is memory.
  // So this does not test, it re-establishes, and reports the geometry the
  // driver comes back with in case it changed.
  bool reopen();

  // Expand a 1bpp frame into panel memory WITHOUT triggering a waveform.
  // Splitting the paint from the refresh is what lets a grayscale page reach
  // the panel in one waveform instead of two: the base lands here, the gray
  // planes are painted over it, and only then does the EPDC run.
  bool stageFrame(const uint8_t* frame);

  // Paint the gray overlay planes over whatever stageFrame() (or a previous
  // display()) left in panel memory. No waveform; call refresh() after.
  bool stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane);

  // Run a waveform over the whole panel using what is already in panel memory,
  // and wait for it. Returns false if it could not be issued.
  bool refresh(Waveform waveform);

  // Has something else painted over us?
  //
  // /dev/fb0 is one framebuffer shared with the Kindle's own UI, so when the
  // framework blanks the screen it replaces OUR pixels in the mapping we both
  // hold. That is directly observable, and it is the symptom actually reported:
  // the reader stays alive and correct, answers touches, and redraws properly
  // the moment it is asked, while the glass sits white.
  //
  // Note what this is not. Reading back our own write proves nothing about
  // whether the panel received it. This is the opposite question: whether what
  // we wrote is still what is there. A difference is positive evidence that a
  // second writer exists, which no amount of successful writing could show.
  bool panelContentWasReplaced() const;

  // Read one pixel back out of the mapped framebuffer. Only exists so the
  // smoke test can prove the pixels actually landed: the first run of this
  // backend reported success while every blit was silently failing, because
  // nothing ever looked at the panel memory afterwards.
  uint8_t peekPixel(uint16_t x, uint16_t y) const;
  void waitComplete();

  void deepSleep();

 private:
  void blit(const uint8_t* frame);
  // Snapshot the sample points after we paint, so a later comparison is
  // against what WE last put there rather than against anything older.
  void rememberPanelContent();

  // Spread thinly over the whole frame rather than clustered: a blanking pass
  // covers everything, but so would a single large white region in a book, and
  // only the spread tells them apart.
  static constexpr int PANEL_SAMPLES = 256;
  uint8_t contentSample[PANEL_SAMPLES] = {};
  bool haveContentSample = false;
  size_t sampleOffsetAt(int index) const;

  int fbfd = -1;
  uint16_t panelWidth = 0;
  uint16_t panelHeight = 0;
  uint32_t stride = 0;   // bytes per framebuffer row; 608 on the KT3, not 600
  size_t mapLen = 0;
  uint8_t* fbMem = nullptr;  // mmap'd /dev/fb0, written directly
  uint32_t pendingMarker = 0;
  bool hasPending = false;
};

}  // namespace crosspoint::kindle
