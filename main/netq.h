#pragma once
#include <stdbool.h>

// Who is downloading now, so the RTC's requests (departures.c) wait for the others: each TLS download holds 10-15 KB of
// internal RAM while it runs (CLAUDE.md, bug 27), and the weather display's low points (a place switch: the forecast,
// alerts, air quality and the radar at once) were already at the harness's floors before the buses came
// (docs/MERGE-PLAN.md, Memory). The weather loop and the radar set their flag around their downloads (the bus map's
// tiles are the radar task's own work); deps_step() asks nothing while another is set (netq_others_busy). Any task, no
// lock. NETQ_MAP only names the bus map for netq_awake.
typedef enum { NETQ_MAIN = 1, NETQ_RADAR = 2, NETQ_MAP = 4 } netq_who_t;

void netq_set(netq_who_t who, bool busy);
// Whether anyone but `self` is downloading now (no wait)
bool netq_others_busy(netq_who_t self);

// Wi-Fi power save off while a map screen is open (`on`: NETQ_RADAR or NETQ_MAP), back on when none is. With ESP-IDF's
// default for a station (WIFI_PS_MIN_MODEM) each reply waited for the router's next beacon, ~100 ms a request: the
// radar's 14 past frames (28 GeoMet requests) took 4.9-6.6 s, 3.1-3.7 s with it off (2026-10-10; the PC: ~90 ms a
// request). Left alone while Easy Connect runs (forge_net sets its own). LVGL task (screen open / unloaded).
void netq_awake(netq_who_t who, bool on);
