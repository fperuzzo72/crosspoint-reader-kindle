#pragma once
// SNTP. The Kindle's clock is set by the system, which talks to Amazon's time
// servers on its own schedule. A reader process setting the clock would be
// fighting it, so these do nothing and the system's time is used as found.
inline void sntp_setoperatingmode(int) {}
inline void sntp_setservername(int, const char*) {}
inline void sntp_init() {}
inline void sntp_stop() {}
#define SNTP_SYNC_STATUS_RESET 0
#define SNTP_SYNC_STATUS_COMPLETED 1
#define SNTP_SYNC_STATUS_IN_PROGRESS 2
#define SNTP_OPMODE_POLL 0

// Always RESET: this process never syncs, so claiming COMPLETED would tell the
// clock code its time came from a server when it came from the system.
inline int sntp_get_sync_status() { return SNTP_SYNC_STATUS_RESET; }

// The esp_-prefixed spellings of the same calls; newer IDF renamed them and
// the tree uses both.
inline bool esp_sntp_enabled() { return false; }
inline void esp_sntp_stop() {}
inline void esp_sntp_init() {}
inline void esp_sntp_setoperatingmode(int) {}
inline void esp_sntp_setservername(int, const char*) {}
