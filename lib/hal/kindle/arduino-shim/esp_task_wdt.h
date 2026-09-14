#pragma once
// The task watchdog exists on the ESP32 because a stuck task wedges the whole
// chip. A stuck Linux process is just a stuck process and the kernel keeps
// running, so there is nothing here to feed.
inline int esp_task_wdt_reset() { return 0; }
inline int esp_task_wdt_add(void*) { return 0; }
inline int esp_task_wdt_delete(void*) { return 0; }
// Never subscribed, because there is no watchdog to subscribe to.
inline int esp_task_wdt_status(void*) { return -1; }
