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
