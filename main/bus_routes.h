#pragma once

// The buses (from esp32-s3-rtcquebec): departures.c's task and the saved favourite stops (bus_start, before ui_init
// fills the stop pages), and the settings page's routes /api/favs and /api/route (bus_routes_init, before web_start).
void bus_start(void);
void bus_routes_init(void);
