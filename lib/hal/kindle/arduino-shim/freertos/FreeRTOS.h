#pragma once

// FreeRTOS stand-in backed by pthreads.
//
// Ten files reach for this. Unlike SPI and Wire, these are NOT no-ops: the
// tree uses tasks and mutexes for real work (the activity manager's background
// jobs, the storage lock), and faking them would produce races rather than
// merely missing hardware. pthreads provides the same primitives honestly, so
// this maps onto them.
//
// What does NOT carry across is priority and core affinity. FreeRTOS scheduling
// is cooperative-ish and pinned; Linux is neither. The priority argument is
// accepted and ignored, which is safe for work that is merely backgrounded and
// would NOT be safe for anything relying on priority for correctness.

#include <pthread.h>
#include <time.h>

#include <cstdint>

using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
using TickType_t = uint32_t;

#ifndef pdTRUE
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#endif

// One tick per millisecond keeps the arithmetic in the tree correct as written.
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1U
#endif
#ifndef portMAX_DELAY
#define portMAX_DELAY 0xFFFFFFFFU
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))
#endif

// Critical sections. On the ESP32 these are spinlocks that also disable
// interrupts on the current core; here there are no interrupts to disable and
// a mutex is the honest equivalent. Recursive, because the tree nests them.
struct portMUX_TYPE {
  pthread_mutex_t lock;
};
#define portMUX_INITIALIZER_UNLOCKED {PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP}
#ifndef portMUX_FREE_VAL
#define portMUX_FREE_VAL 0
#endif

inline void portENTER_CRITICAL(portMUX_TYPE* mux) {
  if (mux != nullptr) {
    pthread_mutex_lock(&mux->lock);
  }
}
inline void portEXIT_CRITICAL(portMUX_TYPE* mux) {
  if (mux != nullptr) {
    pthread_mutex_unlock(&mux->lock);
  }
}
// The task-level spellings; identical here, since there is no interrupt
// context to distinguish them from.
#define taskENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#define taskEXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)

inline void vTaskDelay(const TickType_t ticks) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(ticks / 1000);
  ts.tv_nsec = static_cast<long>(ticks % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) == -1) {
  }
}

inline TickType_t xTaskGetTickCount() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<TickType_t>(static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000);
}
