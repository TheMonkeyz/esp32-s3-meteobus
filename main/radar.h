#pragma once
#include <stdbool.h>
#include "lvgl.h"

lv_obj_t *radar_create(lv_font_t *f_title, lv_font_t *f_small, lv_font_t *f_micro);
void radar_set_visible(bool visible);
void radar_relocate(void);   // location changed: new map + radar
void radar_zoom(int step);   // +1 zoom in, -1 zoom out (call from the LVGL task)
void radar_units_changed(void);   // clock format / distance unit changed: redraw the labels

// The radar panel (the landscape weather page): a w x h window on the radar picture's centre, as a child of parent.
// A tap plays / stops the last 3 h as the full screen does; its corner control calls expand_cb (open the full
// screen); dirty_cb says the picture changed while the panel's screen isn't the one shown (slide.c's cached picture).
// While shown, the latest frame is kept fresh; ui.c's drag_read turns its vertical swipes into radar_zoom(). Create it
// after radar_create().
lv_obj_t *radar_panel_create(lv_obj_t *parent, int w, int h, lv_font_t *f_small, lv_font_t *f_micro,
                             void (*expand_cb)(void), void (*dirty_cb)(void));
void radar_panel_set_shown(bool shown);   // the panel's screen is the one shown (and the panel isn't hidden)
void radar_panel_stop(void);              // stop a playback in the panel (a drag started; display lock held)

// Download every zoom level's map that isn't cached yet, in the background.
// While it runs, the radar screen shows a "Preparing maps" panel.
void radar_preload_start(void);
// Other downloads run in the radar task while the radar isn't on screen (MeteoBus: the buses' requests and the bus
// map's picture): work() every second, and at once after radar_side_wake(). It shares the task's internal stack
// and never downloads at the same time as the radar.
void radar_set_side_work(bool (*work)(void));
void radar_side_wake(void);

// For other screens (alert map): a w x h RGB565 window at (x0, y0) of the cached basemap for zoom z (4..10), the map
// whose top-left corner is (ox, oy) in world pixels at that zoom (W x H, centred on a location). False if that level
// isn't cached for that map.
#define RADAR_ZOOM_MIN 4
#define RADAR_ZOOM_MAX 10
bool radar_basemap_crop(int z, double ox, double oy, int x0, int y0, int w, int h, uint16_t *dst);
bool radar_osm_render(int z, double ox, double oy, uint16_t *dst, int w, int h);   // dimmed OSM tiles for any window
