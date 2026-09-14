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

#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>

// Defined in src/main.cpp, which is shared with every other target.
void setup();
void loop();

namespace {

volatile sig_atomic_t stopRequested = 0;

void onStop(int) { stopRequested = 1; }

// Say where it died.
//
// A crash here produces no core file and no console, and the process simply
// vanishes: the launcher reports "killed by signal 11" and that is the whole of
// the evidence. The binary is linked EXEC rather than PIE, so the program
// counter at the fault is an address in the binary itself, and addr2line
// against the unstripped build turns it into a file and a line. That is the
// difference between a bug report and a guess.
//
// Written with write(2) and a fixed buffer rather than fprintf: this runs on a
// corrupted process and must not take a lock or an allocator with it.
void onCrash(const int sig, siginfo_t* const info, void* const context) {
  char buf[256];
  const char* name = sig == SIGSEGV   ? "SIGSEGV"
                     : sig == SIGBUS  ? "SIGBUS"
                     : sig == SIGILL  ? "SIGILL"
                     : sig == SIGFPE  ? "SIGFPE"
                     : sig == SIGABRT ? "SIGABRT"
                                      : "signal";
  unsigned long pc = 0;
  unsigned long lr = 0;
  unsigned long sp = 0;
#if defined(__arm__)
  if (context != nullptr) {
    const auto* uc = static_cast<const ucontext_t*>(context);
    pc = uc->uc_mcontext.arm_pc;
    lr = uc->uc_mcontext.arm_lr;
    sp = uc->uc_mcontext.arm_sp;
  }
#else
  (void)context;
#endif
  int n = std::snprintf(buf, sizeof(buf),
                        "\n[kindle] CRASH %s at fault address %p\n"
                        "[kindle]   pc=0x%08lx lr=0x%08lx sp=0x%08lx\n"
                        "[kindle]   resolve with: arm-kindlepw2-linux-gnueabi-addr2line -Cfe "
                        "build/kindle/link/crosspoint 0x%08lx 0x%08lx\n",
                        name, info != nullptr ? info->si_addr : nullptr, pc, lr, sp, pc, lr);
  if (n > 0) {
    ssize_t ignored = write(STDERR_FILENO, buf, static_cast<size_t>(n));
    (void)ignored;
  }
  // Default handling, so the launcher still reports death by signal and the
  // exit status keeps meaning what it always meant.
  std::signal(sig, SIG_DFL);
  raise(sig);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  std::signal(SIGPIPE, SIG_IGN);
  std::signal(SIGINT, onStop);
  std::signal(SIGTERM, onStop);

  struct sigaction crashAction{};
  crashAction.sa_sigaction = onCrash;
  crashAction.sa_flags = SA_SIGINFO;
  sigemptyset(&crashAction.sa_mask);
  for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
    sigaction(sig, &crashAction, nullptr);
  }

  // Seed the C library generator, because the shim's random() is rand() and an
  // unseeded rand() returns the SAME sequence on every run. On the ESP32 this
  // never came up: random() there is backed by the hardware generator and needs
  // no seeding, so nothing in the tree calls randomSeed(). The visible symptom
  // would have been a "random" sleep wallpaper that is the same one every time.
  std::srand(static_cast<unsigned>(::time(nullptr)) ^ static_cast<unsigned>(::getpid()));

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
