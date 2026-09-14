#pragma once

#include "FreeRTOS.h"
#include "task.h"

// A real mutex, not a stub: the tree uses these to guard shared state, and
// pretending to lock would turn a missing peripheral into a data race.
using SemaphoreHandle_t = pthread_mutex_t*;

SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
// Recursive: the tree takes some of these from a thread that may already hold
// them, which a plain mutex would deadlock on.
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t timeoutTicks);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);
// From an ISR: there are no interrupt handlers here, so these are the ordinary
// calls and the "higher priority task woken" flag is never set.
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t* higherPriorityTaskWoken);
BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t sem, BaseType_t* higherPriorityTaskWoken);
void vSemaphoreDelete(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeoutTicks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
// Who holds this mutex, or null if free. Used for debug assertions; pthreads
// exposes no portable way to ask, so this reports null and the assertions it
// feeds become vacuous rather than wrong.
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t sem);
