#pragma once
// Backtrace printing from inside a crash handler. A Linux process gets a core
// file and gdb, which is strictly better, so this does nothing.
inline void esp_backtrace_print(int) {}
