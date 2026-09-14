// Process entry point for the Kindle build.
//
// CrossPoint is written against the Arduino model: the core supplies main(),
// calls setup() once and then loop() forever. A Linux process has no such
// core, so this is it. Deliberately thin, because everything it does is a
// decision the Arduino core was making invisibly and those decisions should be
// visible here rather than assumed.
//
// Four things it adds that the ESP32 core did not have to think about:
//
//  - SIGPIPE is ignored. A socket write to a peer that vanished kills a Linux
//    process by default. On a chip there is no such signal, so the tree has
//    never had to defend against it, and a reader dying because a browser tab
//    closed would be baffling.
//
//  - SIGINT and SIGTERM set a flag rather than killing the process, so the
//    loop can leave the panel in a readable state instead of freezing whatever
//    half-drawn frame was up.
//
//  - stdout and stderr are unbuffered. The scriptlet redirects them into a log
//    on the card, and a crash with a full buffer loses exactly the lines that
//    explain it.
//
//  - The working directory moves to /mnt/us, which is what the reader means by
//    the root of its storage on this device.

#include <HalDisplay.h>
#include <HalSystem.h>

#include <csignal>
#include <cstdio>
#include <unistd.h>

// Defined in src/main.cpp, which is shared with every other target.
void setup();
void loop();

namespace {

volatile sig_atomic_t stopRequested = 0;

void onStop(int) { stopRequested = 1; }

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGINT, onStop);
  std::signal(SIGTERM, onStop);

  if (chdir("/mnt/us") != 0) {
    // Not fatal: a development run from elsewhere should still start, and
    // storage paths are absolute anyway. Worth saying, because a reader that
    // finds no books is otherwise a mystery.
    std::fprintf(stderr, "[kindle] could not chdir to /mnt/us; relative paths will resolve elsewhere\n");
  }

  setup();

  // Two ways out, and they mean different things. A signal is the launcher or
  // the system asking; the HAL flag is the user picking "Exit CrossPoint" in
  // the menu. Both leave through here so the panel is handed back in the same
  // state either way.
  while (stopRequested == 0 && !HalSystem::applicationExitRequested()) {
    loop();
  }

  std::fprintf(stderr, "[kindle] %s, leaving the loop\n",
               stopRequested != 0 ? "stop requested" : "exit chosen from the menu");

  // Leave the panel white rather than frozen on whatever was last drawn. The
  // Kindle's own UI has been running underneath this whole time and does not
  // know its screen was taken; it repaints on its next event, and until then a
  // stale CrossPoint frame reads as a hung device. A full refresh also scrubs
  // the ghosting this session accumulated, so the framework draws onto a clean
  // panel.
  display.clearScreen();
  display.displayBuffer(HalDisplay::FULL_REFRESH);

  // _exit, not return: returning runs the static destructors, and this tree
  // was written for a firmware that never shuts down. ~ActivityManager() has a
  // deliberate assert(false) saying exactly that, and a clean stop was ending
  // in SIGABRT because of it.
  //
  // Skipping the destructors is also the honest match for the model: on the
  // ESP32 the process does not unwind, it stops existing. Nothing here owns a
  // resource the kernel will not reclaim, and the display backend has already
  // drained its pending waveform by the time the loop exits.
  std::fflush(nullptr);
  _exit(0);
}
