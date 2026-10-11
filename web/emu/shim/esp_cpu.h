#pragma once
#include <stdint.h>
// slide.c's zoom times its overlay blend in CPU cycles (a log line only): no cycle counter in the browser
static inline uint32_t esp_cpu_get_cycle_count(void) { return 0; }
