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
#include "arduino-shim/Stream.h"
#include "arduino-shim/SPI.h"
#include "arduino-shim/Update.h"
#include "arduino-shim/Wire.h"
#include "arduino-shim/esp_heap_caps.h"
#include "arduino-shim/esp_rom_crc.h"
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
size_t Print::print(const String& s) { return write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }
size_t Print::println(const String& s) { return print(s) + println(); }
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

BaseType_t xQueuePeek(const QueueHandle_t q, void* item, const TickType_t timeoutTicks) {
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
  // Copy without consuming, and without signalling notFull: nothing left.
  std::memcpy(item, q->storage + q->head * q->itemSize, q->itemSize);
  pthread_mutex_unlock(&q->lock);
  return pdPASS;
}

BaseType_t xQueuePeek(const SemaphoreHandle_t sem, void*, const TickType_t) {
  // Peeking a mutex-as-queue asks "is a token available", i.e. is it free.
  // trylock answers that without blocking; releasing immediately leaves the
  // state exactly as found.
  //
  // One honest caveat: on a RECURSIVE mutex the holding thread can trylock its
  // own lock, so calling this from the holder reports "free". The tree's use
  // (RenderLock::peek asking whether a render is in flight) is a query from a
  // different thread, where this is correct.
  if (sem == nullptr) {
    return pdFAIL;
  }
  if (pthread_mutex_trylock(sem) != 0) {
    return pdFAIL;  // held
  }
  pthread_mutex_unlock(sem);
  return pdTRUE;
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

// ------------------------------------------------------------------- CRC ---

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, const uint32_t len) {
  // Standard reflected CRC-32 (the zlib/ESP-ROM polynomial), computed bitwise.
  // No table: this validates occasional stored blobs, not a hot path, and a
  // 1 KB table would cost more than it saves here.
  crc = ~crc;
  for (uint32_t i = 0; i < len; ++i) {
    crc ^= buf[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320U & (~((crc & 1U) - 1U)));
    }
  }
  return ~crc;
}

// --------------------------------------------------------------- Stream ---

String Stream::readStringUntil(const char terminator) {
  String out;
  for (;;) {
    const int c = read();
    if (c < 0 || static_cast<char>(c) == terminator) {
      break;
    }
    out += static_cast<char>(c);
  }
  return out;
}

String Stream::readString() { return readStringUntil('\0'); }

// ---------------------------------------------- recursive / ISR semaphores ---

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  auto* m = new pthread_mutex_t;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  // The point of the recursive variant: the tree takes some of these from a
  // thread that may already hold them, and a plain mutex deadlocks there.
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  const int rc = pthread_mutex_init(m, &attr);
  pthread_mutexattr_destroy(&attr);
  if (rc != 0) {
    delete m;
    return nullptr;
  }
  return m;
}

BaseType_t xSemaphoreTakeRecursive(const SemaphoreHandle_t sem, const TickType_t timeoutTicks) {
  return xSemaphoreTake(sem, timeoutTicks);
}

BaseType_t xSemaphoreGiveRecursive(const SemaphoreHandle_t sem) { return xSemaphoreGive(sem); }

BaseType_t xSemaphoreGiveFromISR(const SemaphoreHandle_t sem, BaseType_t* higherPriorityTaskWoken) {
  // No interrupt context exists here, so this is the ordinary give and no task
  // is ever reported woken.
  if (higherPriorityTaskWoken != nullptr) {
    *higherPriorityTaskWoken = pdFALSE;
  }
  return xSemaphoreGive(sem);
}

BaseType_t xSemaphoreTakeFromISR(const SemaphoreHandle_t sem, BaseType_t* higherPriorityTaskWoken) {
  if (higherPriorityTaskWoken != nullptr) {
    *higherPriorityTaskWoken = pdFALSE;
  }
  return xSemaphoreTake(sem, 0);
}

// ------------------------------------------------------- more heap caps ---

size_t heap_caps_get_total_size(uint32_t) {
  struct sysinfo info {};
  if (sysinfo(&info) != 0) {
    return 0;
  }
  return static_cast<size_t>(info.totalram) * info.mem_unit;
}

size_t heap_caps_get_minimum_free_size(uint32_t caps) {
  // The ESP32 tracks a low-water mark across the run. Nothing here does, so
  // this reports the CURRENT free size: an over-estimate of the minimum, which
  // is the safe direction for a caller deciding whether it once ran close to
  // the edge, since it will not falsely claim a shortage.
  return heap_caps_get_free_size(caps);
}

// --------------------------------------------------- task notifications ---
//
// FreeRTOS gives every task a built-in counting semaphore. There is no such
// per-thread slot here, so one is kept in a small table keyed by thread.

namespace {

struct Notification {
  pthread_t thread;
  uint32_t count;
  pthread_cond_t cond;
  bool inUse;
};

constexpr size_t MAX_NOTIFIED_TASKS = 8;
Notification g_notifications[MAX_NOTIFIED_TASKS];
pthread_mutex_t g_notifyLock = PTHREAD_MUTEX_INITIALIZER;

// Caller must hold g_notifyLock.
Notification* slotFor(const pthread_t thread, const bool create) {
  for (auto& n : g_notifications) {
    if (n.inUse && pthread_equal(n.thread, thread)) {
      return &n;
    }
  }
  if (!create) {
    return nullptr;
  }
  for (auto& n : g_notifications) {
    if (!n.inUse) {
      n.inUse = true;
      n.thread = thread;
      n.count = 0;
      pthread_cond_init(&n.cond, nullptr);
      return &n;
    }
  }
  // Out of slots. Returning null makes the take return 0 immediately, which
  // the caller reads as "nothing to do" rather than hanging forever.
  return nullptr;
}

}  // namespace

TaskHandle_t xTaskGetCurrentTaskHandle() {
  // The handle is only ever compared and passed back in, so a per-thread
  // pointer to its own id is enough.
  static thread_local pthread_t self = pthread_self();
  return &self;
}

uint32_t ulTaskNotifyTake(const BaseType_t clearOnExit, const TickType_t timeoutTicks) {
  pthread_mutex_lock(&g_notifyLock);
  Notification* n = slotFor(pthread_self(), true);
  if (n == nullptr) {
    pthread_mutex_unlock(&g_notifyLock);
    return 0;
  }

  while (n->count == 0) {
    if (timeoutTicks == 0) {
      break;
    }
    if (timeoutTicks == portMAX_DELAY) {
      pthread_cond_wait(&n->cond, &g_notifyLock);
      continue;
    }
    timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += static_cast<time_t>(timeoutTicks / 1000);
    deadline.tv_nsec += static_cast<long>(timeoutTicks % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_nsec -= 1000000000L;
      ++deadline.tv_sec;
    }
    if (pthread_cond_timedwait(&n->cond, &g_notifyLock, &deadline) != 0) {
      break;  // timed out
    }
  }

  const uint32_t taken = n->count;
  // FreeRTOS semantics: clearOnExit zeroes the counter, otherwise it decrements.
  if (clearOnExit != pdFALSE) {
    n->count = 0;
  } else if (n->count > 0) {
    --n->count;
  }
  pthread_mutex_unlock(&g_notifyLock);
  return taken;
}

BaseType_t xTaskNotifyGive(const TaskHandle_t task) {
  if (task == nullptr) {
    return pdFAIL;
  }
  pthread_mutex_lock(&g_notifyLock);
  Notification* n = slotFor(*task, true);
  if (n == nullptr) {
    pthread_mutex_unlock(&g_notifyLock);
    return pdFAIL;
  }
  ++n->count;
  pthread_cond_signal(&n->cond);
  pthread_mutex_unlock(&g_notifyLock);
  return pdPASS;
}

BaseType_t xTaskNotify(const TaskHandle_t task, const uint32_t value, const eNotifyAction action) {
  // Only eIncrement is used here, and it is exactly what Give does. Any other
  // action would need the full 32-bit notification value, which this table
  // does not carry, so it reports failure rather than silently doing the
  // wrong arithmetic.
  if (action != eIncrement) {
    return pdFAIL;
  }
  (void)value;
  return xTaskNotifyGive(task);
}

TaskHandle_t xSemaphoreGetMutexHolder(const SemaphoreHandle_t) {
  // pthreads has no portable owner query. Null means "free" to every caller
  // here, which makes the debug assertions vacuous rather than wrong.
  return nullptr;
}

// ----------------------------------------------------------------- OTA ---

UpdateClass Update;
