#pragma once
#include "esp_timer.h"
inline unsigned long millis() { return unsigned(esp_timer_get_time() / 1000); }
inline void delay(unsigned ms) { headless_time_us += ms * 1000; }
inline void taskYIELD() {}
