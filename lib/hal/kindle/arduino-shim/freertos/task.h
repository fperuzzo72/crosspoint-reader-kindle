#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = pthread_t*;
using TaskFunction_t = void (*)(void*);

// Creates a detached pthread. The priority and core arguments are accepted and
// ignored: see the note in FreeRTOS.h about what does not carry across.
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg, UBaseType_t priority,
                       TaskHandle_t* created);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg,
                                   UBaseType_t priority, TaskHandle_t* created, BaseType_t core);
void vTaskDelete(TaskHandle_t handle);
inline void taskYIELD() { sched_yield(); }

// Task notifications: FreeRTOS's lightweight per-task counting semaphore, used
// in this tree to park a worker until there is something to do.
//
// Implemented rather than stubbed, and for the usual reason: a stub that
// returned immediately would turn a blocking wait into a busy loop, and one
// that never returned would deadlock. Both are worse than the real thing,
// which is a counter and a condition variable per task.
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t timeoutTicks);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle();
