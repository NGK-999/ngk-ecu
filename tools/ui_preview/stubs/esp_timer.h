#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
extern int64_t headless_time_us;
static inline int64_t esp_timer_get_time(void) { return headless_time_us; }
#ifdef __cplusplus
}
#endif
