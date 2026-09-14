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

  while (stopRequested == 0) {
    loop();
  }

  std::fprintf(stderr, "[kindle] stop requested, leaving the loop\n");
  return 0;
}
