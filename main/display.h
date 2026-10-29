#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
// Guition JC1060P470C: the LVGL screen is the whole 1024x600 panel (landscape). DISP_OX / DISP_OY: where the LVGL screen
// sits in the panel (0, 0 since the landscape layout; the round 466 UI was centred in it before).
#define PANEL_W 1024
#define PANEL_H 600
#define DISP_W  PANEL_W
#define DISP_H  PANEL_H
#define DISP_OX ((PANEL_W - DISP_W) / 2)
#define DISP_OY ((PANEL_H - DISP_H) / 2)
#define DISP_ROUND 0       // a rectangle: content may use the corners
#define DISP_SWAP  0       // raw frames (display_raw_area) in LVGL's own RGB565 byte order: the DPI frame buffer's
#else
#define DISP_W 466
#define DISP_H 466
#define DISP_ROUND 1       // the round AMOLED: the corners are outside the glass
#define DISP_SWAP  1       // raw frames in the panel's byte order (RGB565 byte-swapped, the CO5300's QSPI order)
#endif

void display_init(void);
bool display_lock(int timeout_ms);
void display_unlock(void);
void display_brightness(uint8_t level);
// The panel in sleep while presence says the screen is off (DISPOFF + SLPIN; SLPOUT, a whole frame, DISPON on the
// way back, ~200 ms). With the lock held. LVGL keeps running meanwhile, 10 rounds a second (the touch that wakes it).
// Asleep, it also lets the CPU idle at 80 MHz (power management's "screen" lock, held while the panel is awake).
void display_sleep(bool on);
bool display_asleep(void);
bool display_cpu_full_speed(void);       // the "screen" lock held

// Diagnostics (accumulated since the last reset)
typedef struct {
    uint32_t frames, render_us, render_max_us;     // rendered frames and their render time
    uint32_t anim_frames, anim_us, anim_gap_max_us; // back-to-back frames (animations): interval sum / worst gap
    uint32_t anim_gap_max_at_ms;                   // ...when it ended (ms since the reset; test console only)
    char anim_gap_max_kind[12];                    // ...between which frames: "lvgl>move", "move>move", ...
    uint64_t pixels;                               // pixels sent to the panel
    uint32_t lvgl_wait_max_us;                     // longest wait of the LVGL task for the lock
    uint32_t hold_max_us;                          // longest lock hold by another task...
    char hold_task[16];                            // ...and which task
} display_stats_t;
void display_get_stats(display_stats_t *out, bool reset);        // diag.c
void display_get_test_stats(display_stats_t *out, bool reset);   // the test console's "fps" (own counters)
void display_bench_no_panel(bool on);             // diag bench only

// A frame drawn without LVGL (slide.c): fill() writes rows y0..y0+n-1, full width, RGB565 in the panel's order
// (byte-swapped where DISP_SWAP), into dst; display_raw_frame() sends each band while the next one is filled. Display lock held; afterwards
// LVGL must redraw before it flushes again (lv_obj_invalidate).
typedef void (*display_fill_cb_t)(int y0, int n, void *dst, void *user);
void display_raw_frame(display_fill_cb_t fill, void *user);
// Only the area x0..x1, y0..y1 (widened to even/odd edges as the CO5300 needs): fill() writes n rows of w pixels
// starting at column x0 (dst: w * n pixels, panel order). Narrower than the screen: fewer bytes, faster (list scrolls).
// bottom_up: the bands from the bottom one up (a fill that moves rows down in its source, in place).
typedef void (*display_area_fill_cb_t)(int x0, int w, int y0, int n, void *dst, void *user);
void display_raw_area(int x0, int y0, int x1, int y1, bool bottom_up, display_area_fill_cb_t fill, void *user);
// Raw frames' time besides the fill since the last reset: waiting for a band's transfer, the window commands
typedef struct { int64_t wait_us, cmd_us; } display_raw_timing_t;
#if !DISP_SWAP
// Frames as block copies by the 2D engine (the P4's PPA, DMA: no CPU copy, no cache write-back): display_blit()
// copies a w x h block at sx, sy of a picture (RGB565, LVGL's order, pic_w x pic_h, rows of pic_w pixels) to dx, dy on
// screen; NULL = black. display_frame_done() after a frame's blocks (the diagnostics' frame count). Display lock held.
bool display_blit(const void *pic, int pic_w, int pic_h, int sx, int sy, int w, int h, int dx, int dy);
void display_frame_begin(void);                  // the blocks that follow make a new frame, shown at done (tear-free)
void display_frame_done(void);
void display_blit_bench(void);                    // console "blitbench": where a frame's time goes (log)
#endif
void display_raw_timing(display_raw_timing_t *out, bool reset);

// Called with every area LVGL sends to the panel, before the byte swap (RGB565 as LVGL draws it, w x h pixels), in the
// LVGL task: slide.c copies it into its picture of the screen shown, which then always matches the panel.
typedef void (*display_flush_hook_t)(const lv_area_t *a, const uint8_t *px);
void display_set_flush_hook(display_flush_hook_t hook);
