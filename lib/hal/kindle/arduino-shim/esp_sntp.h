#pragma once
// SNTP. The Kindle's clock is set by the system, which talks to Amazon's time
// servers on its own schedule. A reader process setting the clock would be
// fighting it, so these do nothing and the system's time is used as found.
inline void sntp_setoperatingmode(int) {}
inline void sntp_setservername(int, const char*) {}
inline void sntp_init() {}
inline void sntp_stop() {}
inline int sntp_get_sync_status() { return 0; }
