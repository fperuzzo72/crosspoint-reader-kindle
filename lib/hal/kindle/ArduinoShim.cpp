// Bodies for the Arduino and ESP-IDF stand-in headers in arduino-shim/.
//
// Split from ArduinoCompat so the shim headers stay header-only from the
// tree's point of view: a file that includes <Print.h> gets a declaration and
// links against this, exactly as it would against the real core.

#include <sys/sysinfo.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "arduino-shim/HardwareSerial.h"
#include "arduino-shim/Print.h"
#include "arduino-shim/SPI.h"
#include "arduino-shim/Wire.h"
#include "arduino-shim/esp_heap_caps.h"
#include "arduino-shim/freertos/queue.h"
#include "arduino-shim/freertos/semphr.h"
#include "arduino-shim/freertos/task.h"

// ----------------------------------------------------------------- Print ---

size_t Print::write(const uint8_t* buffer, const size_t size) {
  size_t n = 0;
  for (size_t i = 0; i < size; ++i) {
    if (write(buffer[i]) == 0) {
      break;
    }
    ++n;
  }
  return n;
}

size_t Print::write(const char* s) {
  if (s == nullptr) {
    return 0;
  }
  return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s));
}

size_t Print::print(const char* s) { return write(s); }
size_t Print::print(const int v) { return printf("%d", v); }
size_t Print::print(const unsigned v) { return printf("%u", v); }
size_t Print::print(const long v) { return printf("%ld", v); }
size_t Print::print(const unsigned long v) { return printf("%lu", v); }
size_t Print::print(const double v, const int digits) { return printf("%.*f", digits, v); }

size_t Print::println() { return write(static_cast<uint8_t>('\n')); }
size_t Print::println(const char* s) { return print(s) + println(); }
size_t Print::println(const char c) { return print(c) + println(); }
size_t Print::println(const int v) { return print(v) + println(); }
size_t Print::println(const unsigned v) { return print(v) + println(); }
size_t Print::println(const long v) { return print(v) + println(); }
size_t Print::println(const unsigned long v) { return print(v) + println(); }
size_t Print::println(const double v, const int digits) { return print(v, digits) + println(); }

size_t Print::printf(const char* fmt, ...) {
  char stackBuf[256];
  va_list args;
  va_start(args, fmt);
  const int needed = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
  va_end(args);
  if (needed < 0) {
    return 0;
  }
  if (static_cast<size_t>(needed) < sizeof(stackBuf)) {
    return write(stackBuf);
  }
  // Longer than the stack buffer: format again into an exact allocation rather
  // than truncating. Log lines in this tree do occasionally run long.
  char* heapBuf = static_cast<char*>(malloc(static_cast<size_t>(needed) + 1));
  if (heapBuf == nullptr) {
    return write(stackBuf);
  }
  va_start(args, fmt);
  std::vsnprintf(heapBuf, static_cast<size_t>(needed) + 1, fmt, args);
  va_end(args);
  const size_t n = write(heapBuf);
  free(heapBuf);
  return n;
}

// --------------------------------------------------------- HardwareSerial ---

HardwareSerial Serial;
HardwareSerial Serial0;

size_t HardwareSerial::write(const uint8_t c) { return fwrite(&c, 1, 1, stderr); }
size_t HardwareSerial::write(const uint8_t* buffer, const size_t size) { return fwrite(buffer, 1, size, stderr); }
void HardwareSerial::flush() { fflush(stderr); }

// ------------------------------------------------------------- SPI / Wire ---

SPIClass SPI;
TwoWire Wire;

// -------------------------------------------------------------- FreeRTOS ---

namespace {

struct TaskThunk {
  TaskFunction_t fn;
  void* arg;
};

void* taskTrampoline(void* raw) {
  TaskThunk* thunk = static_cast<TaskThunk*>(raw);
  const TaskFunction_t fn = thunk->fn;
  void* arg = thunk->arg;
  delete thunk;
  fn(arg);
  return nullptr;
}

}  // namespace

BaseType_t xTaskCreate(const TaskFunction_t fn, const char*, const uint32_t, void* arg, const UBaseType_t,
                       TaskHandle_t* created) {
  pthread_t* thread = new pthread_t{};
  TaskThunk* thunk = new TaskThunk{fn, arg};
  if (pthread_create(thread, nullptr, taskTrampoline, thunk) != 0) {
    delete thunk;
    delete thread;
    return pdFAIL;
  }
  // FreeRTOS tasks are never joined; detach so the thread cleans itself up.
  pthread_detach(*thread);
  if (created != nullptr) {
    *created = thread;
  } else {
    // Nobody kept the handle, so nobody can delete it. Leak the handle rather
    // than the thread: freeing it here would leave vTaskDelete a dangling one.
  }
  return pdPASS;
}

BaseType_t xTaskCreatePinnedToCore(const TaskFunction_t fn, const char* name, const uint32_t stackDepth, void* arg,
                                   const UBaseType_t priority, TaskHandle_t* created, const BaseType_t) {
  // Core affinity is not honoured; see the note in FreeRTOS.h.
  return xTaskCreate(fn, name, stackDepth, arg, priority, created);
}

void vTaskDelete(const TaskHandle_t handle) {
  // FreeRTOS lets a task delete itself by passing nullptr. The pthread
  // equivalent is simply returning from the task function, so this only has to
  // handle the self case explicitly.
  if (handle == nullptr) {
    pthread_exit(nullptr);
  }
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
  auto* m = new pthread_mutex_t;
  if (pthread_mutex_init(m, nullptr) != 0) {
    delete m;
    return nullptr;
  }
  return m;
}

SemaphoreHandle_t xSemaphoreCreateBinary() { return xSemaphoreCreateMutex(); }

void vSemaphoreDelete(const SemaphoreHandle_t sem) {
  if (sem == nullptr) {
    return;
  }
  pthread_mutex_destroy(sem);
  delete sem;
}

BaseType_t xSemaphoreTake(const SemaphoreHandle_t sem, const TickType_t timeoutTicks) {
  if (sem == nullptr) {
    return pdFAIL;
  }
  if (timeoutTicks == 0) {
    return pthread_mutex_trylock(sem) == 0 ? pdTRUE : pdFALSE;
  }
  // A bounded wait is honoured as a bounded wait rather than silently becoming
  // an infinite one: code that passes a timeout is usually relying on getting
  // control back.
  if (timeoutTicks != portMAX_DELAY) {
    timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += static_cast<time_t>(timeoutTicks / 1000);
    deadline.tv_nsec += static_cast<long>(timeoutTicks % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_nsec -= 1000000000L;
      ++deadline.tv_sec;
    }
    return pthread_mutex_timedlock(sem, &deadline) == 0 ? pdTRUE : pdFALSE;
  }
  return pthread_mutex_lock(sem) == 0 ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(const SemaphoreHandle_t sem) {
  if (sem == nullptr) {
    return pdFAIL;
  }
  return pthread_mutex_unlock(sem) == 0 ? pdTRUE : pdFALSE;
}

// ------------------------------------------------------------- heap caps ---

namespace {

size_t systemFreeBytes() {
  struct sysinfo info {};
  if (sysinfo(&info) != 0) {
    return 0;
  }
  return static_cast<size_t>(info.freeram) * info.mem_unit;
}

}  // namespace

size_t heap_caps_get_free_size(uint32_t) { return systemFreeBytes(); }
size_t heap_caps_get_largest_free_block(uint32_t) { return systemFreeBytes(); }

// ---------------------------------------------------------------- queues ---

QueueHandle_t xQueueCreate(const UBaseType_t length, const UBaseType_t itemSize) {
  auto* q = new QueueDefinition{};
  q->storage = static_cast<uint8_t*>(std::calloc(length, itemSize));
  if (q->storage == nullptr) {
    delete q;
    return nullptr;
  }
  pthread_mutex_init(&q->lock, nullptr);
  pthread_cond_init(&q->notEmpty, nullptr);
  pthread_cond_init(&q->notFull, nullptr);
  q->itemSize = itemSize;
  q->capacity = length;
  return q;
}

void vQueueDelete(const QueueHandle_t q) {
  if (q == nullptr) {
    return;
  }
  pthread_mutex_destroy(&q->lock);
  pthread_cond_destroy(&q->notEmpty);
  pthread_cond_destroy(&q->notFull);
  std::free(q->storage);
  delete q;
}

namespace {

// Both send and receive need the same "wait until the queue changes, or give
// up" shape, and FreeRTOS's 0 / portMAX_DELAY / bounded triple has to survive.
bool waitOn(pthread_cond_t& cond, pthread_mutex_t& lock, const TickType_t timeoutTicks) {
  if (timeoutTicks == 0) {
    return false;
  }
  if (timeoutTicks == portMAX_DELAY) {
    return pthread_cond_wait(&cond, &lock) == 0;
  }
  timespec deadline{};
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += static_cast<time_t>(timeoutTicks / 1000);
  deadline.tv_nsec += static_cast<long>(timeoutTicks % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_nsec -= 1000000000L;
    ++deadline.tv_sec;
  }
  return pthread_cond_timedwait(&cond, &lock, &deadline) == 0;
}

}  // namespace

BaseType_t xQueueSend(const QueueHandle_t q, const void* item, const TickType_t timeoutTicks) {
  if (q == nullptr || item == nullptr) {
    return pdFAIL;
  }
  pthread_mutex_lock(&q->lock);
  // A loop, not an if: a condition variable can wake spuriously, and another
  // sender may have refilled the slot this one was woken for.
  while (q->count == q->capacity) {
    if (!waitOn(q->notFull, q->lock, timeoutTicks)) {
      pthread_mutex_unlock(&q->lock);
      return pdFAIL;
    }
  }
  std::memcpy(q->storage + q->tail * q->itemSize, item, q->itemSize);
  q->tail = (q->tail + 1) % q->capacity;
  ++q->count;
  pthread_cond_signal(&q->notEmpty);
  pthread_mutex_unlock(&q->lock);
  return pdPASS;
}

BaseType_t xQueueReceive(const QueueHandle_t q, void* item, const TickType_t timeoutTicks) {
  if (q == nullptr || item == nullptr) {
    return pdFAIL;
  }
  pthread_mutex_lock(&q->lock);
  while (q->count == 0) {
    if (!waitOn(q->notEmpty, q->lock, timeoutTicks)) {
      pthread_mutex_unlock(&q->lock);
      return pdFAIL;
    }
  }
  std::memcpy(item, q->storage + q->head * q->itemSize, q->itemSize);
  q->head = (q->head + 1) % q->capacity;
  --q->count;
  pthread_cond_signal(&q->notFull);
  pthread_mutex_unlock(&q->lock);
  return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t q) {
  if (q == nullptr) {
    return 0;
  }
  pthread_mutex_lock(&q->lock);
  const UBaseType_t n = static_cast<UBaseType_t>(q->count);
  pthread_mutex_unlock(&q->lock);
  return n;
}

void xQueueReset(const QueueHandle_t q) {
  if (q == nullptr) {
    return;
  }
  pthread_mutex_lock(&q->lock);
  q->count = q->head = q->tail = 0;
  pthread_cond_broadcast(&q->notFull);
  pthread_mutex_unlock(&q->lock);
}
