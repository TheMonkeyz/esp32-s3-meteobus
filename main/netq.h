#pragma once
#include <stdbool.h>

// Who is downloading now, so the RTC's requests (departures.c) wait for the others: each TLS download holds 10-15 KB of
// internal RAM while it runs (CLAUDE.md, bug 27), and the weather display's low points (a place switch: the forecast,
// alerts, air quality and the radar at once) were already at the harness's floors before the buses came
// (docs/MERGE-PLAN.md, Memory). The weather loop and the radar set their flag around their downloads (the bus map's
// tiles are the radar task's own work); deps_step() asks nothing while another is set (netq_others_busy), nor while
// forge_net's download slot is held (net_dl_busy(): an update check or install, a service probe), and holds the slot
// itself for its request. Any task, no lock.
// Why not forge_net's slot (net_dl_begin, one at a time with a wait) for these two as well: the weather loop and the
// radar may download at the same time, as before the merge. Wi-Fi power save off while a map is open: forge_net's
// net_low_latency() since espforge v0.6.0 (netq_awake() here until MeteoBus v0.3.1).
typedef enum { NETQ_MAIN = 1, NETQ_RADAR = 2 } netq_who_t;

void netq_set(netq_who_t who, bool busy);
// Whether anyone but `self` is downloading now (no wait)
bool netq_others_busy(netq_who_t self);
