#pragma once

#include "FreeRTOS.h"

// A real mutex, not a stub: the tree uses these to guard shared state, and
// pretending to lock would turn a missing peripheral into a data race.
using SemaphoreHandle_t = pthread_mutex_t*;

SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateBinary();
void vSemaphoreDelete(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t timeoutTicks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
