#pragma once
#include <stdbool.h>

// Who is downloading now, so the RTC's requests (departures.c) wait for the others: each TLS download holds 10-15 KB of
// internal RAM while it runs (CLAUDE.md, bug 27), and the weather display's low points (a place switch: the forecast,
// alerts, air quality and the radar at once) were already at the harness's floors before the buses came
// (docs/MERGE-PLAN.md, Memory). The weather loop, the radar and the bus map set their flag around their downloads;
// departures.c waits until none is set (netq_wait_others) before each request. Any task, no lock.
typedef enum { NETQ_MAIN = 1, NETQ_RADAR = 2, NETQ_MAP = 4 } netq_who_t;

void netq_set(netq_who_t who, bool busy);
// Waits until the weather loop, the radar and the bus map are between downloads, up to max_ms; true if they were
bool netq_wait_others(int max_ms);
// Whether anyone but `self` is downloading now (no wait)
bool netq_others_busy(netq_who_t self);
