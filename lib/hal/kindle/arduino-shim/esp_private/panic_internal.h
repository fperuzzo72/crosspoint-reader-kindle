#pragma once
// Panic-handler internals. A Linux process crashes into a core file and the
// kernel keeps running, so there is no panic handler to hook.
struct panic_info_t {
  int core;
  const char* reason;
};
