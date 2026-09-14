#pragma once

// FreeRTOS queues over a mutex and condition variable.
//
// Like the semaphores and unlike SPI, these do real work in the tree (the
// input manager's async poll hands samples across a thread boundary through
// one), so a stub would drop events rather than merely miss hardware.

#include <pthread.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "FreeRTOS.h"
#include "semphr.h"

struct QueueDefinition {
  pthread_mutex_t lock;
  pthread_cond_t notEmpty;
  pthread_cond_t notFull;
  uint8_t* storage;
  size_t itemSize;
  size_t capacity;
  size_t count;
  size_t head;
  size_t tail;
};

using QueueHandle_t = QueueDefinition*;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t timeoutTicks);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t timeoutTicks);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
// Read the head without removing it.
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t timeoutTicks);

// In FreeRTOS a semaphore IS a queue: SemaphoreHandle_t is a typedef of
// QueueHandle_t, and code peeks a mutex to ask whether it is currently free
// without taking it. This shim keeps the two as distinct types, so that usage
// needs its own overload rather than an implicit conversion that would be a
// lie about what the pointer is.
BaseType_t xQueuePeek(SemaphoreHandle_t sem, void* item, TickType_t timeoutTicks);
void xQueueReset(QueueHandle_t q);
