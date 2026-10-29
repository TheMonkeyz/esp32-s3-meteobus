#include <stdio.h>
#include <string.h>
// Guition JC1060P470C (ESP32-P4 + ESP32-C6, 7" 1024x600): JD9165 over MIPI-DSI (2 lanes) + LVGL v9 port. The panel scans a frame buffer
// in PSRAM on its own (DPI): a "transfer" here is a copy into that buffer and a cache write-back, done at once, so
// there is no transfer-done interrupt and nothing in flight (display_lvgl_inflight() is always 0). Same API as the
// AMOLED's display.c (display.h). The LVGL screen is DISP_W x DISP_H at (DISP_OX, DISP_OY) in the panel.
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9165.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "driver/ppa.h"
#include "esp_async_memcpy.h"
#include "esp_async_fbcpy.h"                 // esp_lcd's 2D-DMA copy (its priv_include: main/CMakeLists.txt)
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "display";

#define PIN_BL        23              // backlight (LEDC PWM, active high)
// The panel's reset also powers the GT911 touch controller (AVDD): it must be the real reset pin.
#if CONFIG_METEOBUS_P4_PANEL_V2
#define PANEL_V2      1
#define PIN_RST       0               // the 2026 "V2" panel
#else
#define PANEL_V2      0
#define PIN_RST       27
#endif
#define BL_CH         LEDC_CHANNEL_0
#define BL_TIMER      LEDC_TIMER_1
#define DSI_LDO_CHAN  3               // LDO_VO3 feeds the MIPI D-PHY
#define DSI_LDO_MV    2500
#define BUF_LINES     32

/* ---- diagnostics (as display.c) ---- */
static display_stats_t st, tst;
static bool last_raw;
static int64_t tst_t0;
static TaskHandle_t lvgl_th;
static int lock_depth;
static int64_t lock_t0, render_t0, last_render;
volatile int raw_phase, raw_band;     // breadcrumbs for the test console's "where"

static void count_frame(uint32_t render_us)
{
    display_stats_t *s[2] = {&st, &tst};
    for (int i = 0; i < 2; i++) {
        s[i]->frames++;
        s[i]->render_us += render_us;
        if (render_us > s[i]->render_max_us) s[i]->render_max_us = render_us;
    }
}

static void count_anim(uint32_t gap_us, bool test, bool raw)
{
    display_stats_t *s[2] = {&st, &tst};
    for (int i = 0; i < (test ? 2 : 1); i++) {
        s[i]->anim_frames++;
        s[i]->anim_us += gap_us;
        if (gap_us > s[i]->anim_gap_max_us) {
            s[i]->anim_gap_max_us = gap_us;
            s[i]->anim_gap_max_at_ms = (uint32_t)((esp_timer_get_time() - tst_t0) / 1000);
            snprintf(s[i]->anim_gap_max_kind, sizeof(s[i]->anim_gap_max_kind), "%s>%s", last_raw ? "move" : "lvgl",
                     raw ? "move" : "lvgl");
        }
    }
}

static esp_lcd_panel_handle_t panel;
static uint16_t *fb;                    // the frame buffer on screen (PSRAM, PANEL_W x PANEL_H RGB565): LVGL draws here
// Two frame buffers: a moving frame (display_frame_begin .. display_frame_done) is copied into the one not shown, which
// then takes over at the end of the panel's refresh: whole frames at the panel's 60 Hz, no tearing. LVGL's partial
// redraws and list scrolls go to the one on screen (fb).
static uint16_t *fbs[2], *tgt;          // tgt: the frame being built (NULL: none, blocks go to fb)
static int front;
static SemaphoreHandle_t vsync;         // given at the end of each refresh (on_refresh_done)
static SemaphoreHandle_t lvgl_mux;
static void *buf1, *buf2;               // LVGL's band buffers, also raw frames' bands
static display_raw_timing_t raw_t;

// Write the rows y0..y1 of the frame buffer back from the cache, so the DPI DMA sees them
static void fb_sync(int y0, int y1)
{
    uint8_t *p = (uint8_t *)(fb + (size_t)(DISP_OY + y0) * PANEL_W);
    size_t n = (size_t)(y1 - y0 + 1) * PANEL_W * 2;
    esp_cache_msync(p, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static inline uint16_t *fb_at(int x, int y) { return fb + (size_t)(DISP_OY + y) * PANEL_W + DISP_OX + x; }

void display_raw_timing(display_raw_timing_t *out, bool reset)
{
    if (out) *out = raw_t;
    if (reset) memset(&raw_t, 0, sizeof(raw_t));
}

void display_raw_area(int x0, int y0, int x1, int y1, bool bottom_up, display_area_fill_cb_t fill, void *user)
{
    // Same even/odd widening as the AMOLED's (callers rely on the area they get back being whole pairs)
    x0 &= ~1; y0 &= ~1; x1 |= 1; y1 |= 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DISP_W - 1) x1 = DISP_W - 1;
    if (y1 > DISP_H - 1) y1 = DISP_H - 1;
    const int w = x1 - x0 + 1;
    raw_phase = 1;
    int64_t tc = esp_timer_get_time();
    if (w == PANEL_W) {
        // Whole rows are contiguous in the frame buffer: the fill writes there directly, in one call (the order
        // DISP_SWAP 0 asks for), then one cache write-back. A band in between and a copy cost a frame ~45 ms (22 fps).
        raw_phase = 2;
        fill(0, w, y0, y1 - y0 + 1, fb_at(0, y0), user);
        raw_phase = 3;
        fb_sync(y0, y1);
    } else {
        const int rows = (DISP_W * BUF_LINES / w) & ~1;
        const int last = y0 + (y1 - y0) / rows * rows;
        for (int y = bottom_up ? last : y0; bottom_up ? y >= y0 : y <= y1; y += bottom_up ? -rows : rows) {
            int n = y1 + 1 - y < rows ? y1 + 1 - y : rows;
            raw_band = y;
            raw_phase = 2;
            fill(x0, w, y, n, buf1, user);
            raw_phase = 3;
            const uint16_t *s = buf1;
            for (int r = 0; r < n; r++, s += w) memcpy(fb_at(x0, y + r), s, w * 2);
            fb_sync(y, y + n - 1);
        }
    }
    raw_t.cmd_us += esp_timer_get_time() - tc;
    st.pixels += w * (y1 - y0 + 1);
    tst.pixels += w * (y1 - y0 + 1);
    raw_phase = 0;
    count_frame(0);
    int64_t now = esp_timer_get_time();
    if (last_render && now - last_render < 250000) count_anim(now - last_render, last_raw, true);
    last_render = now;
    last_raw = true;
}

/* Block copies (display.h) by the 2D-DMA, as esp_lcd's DPI driver does its draw_bitmap (esp_async_fbcpy: a strided
 * rectangle, source and destination offsets). Measured on a 1024x600 frame (console blitbench): the CPU 26-28 ms, the
 * PPA's scale engine at scale 1 26.5 ms (no faster: 39-40 fps either way), the PPA's fill (write only) 3.8 ms. The
 * PPA's fill paints black where a picture is missing. The source is written back from the cache first; the frame
 * buffer rows are invalidated after (the DMA wrote them behind the cache), as the PPA driver does. */
static ppa_client_handle_t ppa_fill;
static esp_async_fbcpy_handle_t fbcpy;
static int64_t blit_us, blit_px, frame_t0;       // timing: the blocks' time and pixels, for the "blit:" log line
static int blit_frames;
static SemaphoreHandle_t fbcpy_done;

static bool fbcpy_cb(esp_async_fbcpy_handle_t h, esp_async_fbcpy_event_data_t *e, void *arg)
{
    BaseType_t w = pdFALSE;
    xSemaphoreGiveFromISR(fbcpy_done, &w);
    return w == pdTRUE;
}

bool display_blit(const void *pic, int pic_w, int pic_h, int sx, int sy, int w, int h, int dx, int dy)
{
    if (w <= 0 || h <= 0) return true;
    int64_t t0 = esp_timer_get_time();
    if (!fbcpy) {
        esp_async_fbcpy_config_t c = {};
        ppa_client_config_t f = { .oper_type = PPA_OPERATION_FILL };
        fbcpy_done = xSemaphoreCreateBinary();
        if (!fbcpy_done || esp_async_fbcpy_install(&c, &fbcpy) != ESP_OK || ppa_register_client(&f, &ppa_fill) != ESP_OK) {
            ESP_LOGE(TAG, "2D-DMA copy not available");
            return false;
        }
    }
    esp_err_t e;
    if (!pic) {
        ppa_fill_oper_config_t f = {
            .out = { .buffer = tgt ? tgt : fb, .buffer_size = (uint32_t)PANEL_W * PANEL_H * 2, .pic_w = PANEL_W, .pic_h = PANEL_H,
                     .block_offset_x = DISP_OX + dx, .block_offset_y = DISP_OY + dy,
                     .fill_cm = PPA_FILL_COLOR_MODE_RGB565 },
            .fill_block_w = w, .fill_block_h = h, .fill_argb_color = { .val = 0xFF000000 }, .mode = PPA_TRANS_MODE_BLOCKING,
        };
        e = ppa_do_fill(ppa_fill, &f);
    } else {
        // the source rows the block is in, written back (clean lines cost nothing: 0.0 ms for a whole clean frame)
        esp_cache_msync((uint8_t *)pic + (size_t)sy * pic_w * 2, (size_t)h * pic_w * 2,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        esp_async_fbcpy_trans_desc_t t = {
            .src_buffer = pic, .dst_buffer = tgt ? tgt : fb,
            .src_buffer_size_x = pic_w, .src_buffer_size_y = pic_h,
            .dst_buffer_size_x = PANEL_W, .dst_buffer_size_y = PANEL_H,
            .src_offset_x = sx, .src_offset_y = sy, .dst_offset_x = DISP_OX + dx, .dst_offset_y = DISP_OY + dy,
            .copy_size_x = w, .copy_size_y = h,
            .pixel_format_unique_id = { .color_type_id = COLOR_TYPE_ID(COLOR_SPACE_RGB, COLOR_PIXEL_RGB565) },
        };
        e = esp_async_fbcpy(fbcpy, &t, fbcpy_cb, NULL);
        if (e == ESP_OK && xSemaphoreTake(fbcpy_done, pdMS_TO_TICKS(200)) != pdTRUE) e = ESP_ERR_TIMEOUT;
        if (e == ESP_OK)       // the rows written: the cache's copies of them are stale (rows are 2 KB: aligned)
            esp_cache_msync((tgt ? tgt : fb) + (size_t)(DISP_OY + dy) * PANEL_W, (size_t)h * PANEL_W * 2, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    if (e != ESP_OK) { ESP_LOGE(TAG, "2D %s: %s", pic ? "copy" : "fill", esp_err_to_name(e)); return false; }
    blit_us += esp_timer_get_time() - t0;
    blit_px += w * h;
    st.pixels += w * h;
    tst.pixels += w * h;
    return true;
}

static bool bench_dma_done(async_memcpy_handle_t m, async_memcpy_event_t *e, void *arg)
{
    BaseType_t w = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)arg, &w);
    return w == pdTRUE;
}

// Console "blitbench" (display lock held by nobody: it takes it): copies of a whole screen, timed one way at a time
void display_blit_bench(void)
{
    const size_t n = (size_t)PANEL_W * PANEL_H * 2;
    uint8_t *pic = heap_caps_aligned_alloc(128, n, MALLOC_CAP_SPIRAM), *pic2 = heap_caps_aligned_alloc(128, n, MALLOC_CAP_SPIRAM);
    uint8_t *in = heap_caps_malloc(PANEL_W * 2 * 32, MALLOC_CAP_INTERNAL);
    if (!pic || !pic2 || !in) { ESP_LOGW(TAG, "blitbench: no memory"); goto out; }
    memset(pic, 0x42, n); memset(pic2, 0x24, n);
    esp_cache_msync(pic, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    display_lock(-1);
    int64_t t[8];
    t[0] = esp_timer_get_time();
    display_blit(pic, PANEL_W, PANEL_H, 0, 0, PANEL_W, PANEL_H, 0, 0);            // PPA copy (with its cache syncs)
    t[1] = esp_timer_get_time();
    esp_cache_msync(pic, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);                         // a write-back of clean lines
    t[2] = esp_timer_get_time();
    memcpy(pic2, pic, n);                                                          // CPU, PSRAM to PSRAM
    t[3] = esp_timer_get_time();
    esp_cache_msync(pic2, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M);                        // ...and writing it back
    t[4] = esp_timer_get_time();
    display_blit(NULL, 0, 0, 0, 0, PANEL_W, PANEL_H, 0, 0);                       // PPA fill (write only)
    t[5] = esp_timer_get_time();
    for (size_t o = 0; o < n; o += PANEL_W * 2 * 32) memcpy(in, pic + o, PANEL_W * 2 * 32);   // CPU read only
    t[6] = esp_timer_get_time();
    for (size_t o = 0; o < n; o += PANEL_W * 2 * 32) memcpy(pic2 + o, in, PANEL_W * 2 * 32);  // CPU write only
    t[7] = esp_timer_get_time();
    // The AXI GDMA's memcpy: the whole frame in one, then row by row (2 KB rows: a swipe's pieces are rows)
    static async_memcpy_handle_t mcp;
    static SemaphoreHandle_t done;
    double t_dma = -1, t_rows = -1;
    if (!mcp) {
        async_memcpy_config_t c = ASYNC_MEMCPY_DEFAULT_CONFIG();
        c.backlog = 32;
        c.dma_burst_size = 64;
        if (esp_async_memcpy_install_gdma_axi(&c, &mcp) != ESP_OK) mcp = NULL;
        done = xSemaphoreCreateCounting(1024, 0);
    }
    if (mcp) {
        int64_t a = esp_timer_get_time();
        if (esp_async_memcpy(mcp, pic2, pic, n, bench_dma_done, done) == ESP_OK) xSemaphoreTake(done, pdMS_TO_TICKS(500));
        t_dma = (esp_timer_get_time() - a) / 1e3;
        a = esp_timer_get_time();
        int k = 0;
        for (int y = 0; y < PANEL_H; y++)
            if (esp_async_memcpy(mcp, pic2 + y * PANEL_W * 2, pic + y * PANEL_W * 2, PANEL_W * 2, bench_dma_done, done) == ESP_OK) k++;
            else { while (xSemaphoreTake(done, 0) == pdTRUE) k--; y--; }      // backlog full: take what finished
        while (k-- > 0) xSemaphoreTake(done, pdMS_TO_TICKS(500));
        t_rows = (esp_timer_get_time() - a) / 1e3;
    }
    display_unlock();
    ESP_LOGI(TAG, "blitbench: dma_copy=%.1f dma_rows=%.1f ms", t_dma, t_rows);
    ESP_LOGI(TAG, "blitbench %u B: ppa_copy=%.1f msync_clean=%.1f cpu_copy=%.1f msync_dirty=%.1f ppa_fill=%.1f "
                  "cpu_read=%.1f cpu_write=%.1f ms", (unsigned)n, (t[1] - t[0]) / 1e3, (t[2] - t[1]) / 1e3,
             (t[3] - t[2]) / 1e3, (t[4] - t[3]) / 1e3, (t[5] - t[4]) / 1e3, (t[6] - t[5]) / 1e3, (t[7] - t[6]) / 1e3);
    lv_obj_invalidate(lv_screen_active());
out:
    free(pic); free(pic2); free(in);
}

void display_frame_begin(void) { tgt = fbs[!front]; }

void display_frame_done(void)
{
    if (tgt) {                                       // show it: the DMA takes it at the end of the refresh under way
        while (xSemaphoreTake(vsync, 0) == pdTRUE) {}
        esp_lcd_panel_draw_bitmap(panel, 0, 0, PANEL_W, PANEL_H, tgt);
        xSemaphoreTake(vsync, pdMS_TO_TICKS(40));    // ...and after it, the other one is free to be written
        front = !front;
        fb = tgt;
        tgt = NULL;
    }
    int64_t t = esp_timer_get_time();
    if (frame_t0 && t - frame_t0 < 200000) blit_frames++;
    if (blit_frames >= 300) {                        // (a line per ~5 s of moving frames)
        ESP_LOGI(TAG, "blit: %d frames, %.1f ms of copies a frame (%.0f Mpx/s), %.1f ms between frames",
                 blit_frames, blit_us / 1000.0 / blit_frames, blit_px / (double)blit_us, 0.0);
        blit_frames = 0; blit_us = blit_px = 0;
    }
    frame_t0 = t;
    count_frame(0);
    int64_t now = esp_timer_get_time();
    if (last_render && now - last_render < 250000) count_anim(now - last_render, last_raw, true);
    last_render = now;
    last_raw = true;
}

typedef struct { display_fill_cb_t fill; void *user; } full_t;

static void full_fill(int x0, int w, int y0, int n, void *dst, void *user)
{
    const full_t *f = user;
    f->fill(y0, n, dst, f->user);
}

void display_raw_frame(display_fill_cb_t fill, void *user)
{
    full_t f = { fill, user };
    display_raw_area(0, 0, DISP_W - 1, DISP_H - 1, false, full_fill, &f);
}

static bool bench_no_panel;
static display_flush_hook_t flush_hook;
void display_set_flush_hook(display_flush_hook_t hook) { flush_hook = hook; }
void display_bench_no_panel(bool on) { bench_no_panel = on; }

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    if (!bench_no_panel) {
        int w = lv_area_get_width(a), h = lv_area_get_height(a);
        uint32_t n = (uint32_t)w * h;
        st.pixels += n;
        tst.pixels += n;
        if (flush_hook) flush_hook(a, px);
        const uint16_t *s = (const uint16_t *)px;
        for (int r = 0; r < h; r++, s += w) memcpy(fb_at(a->x1, a->y1 + r), s, w * 2);
        fb_sync(a->y1, a->y2);
    }
    lv_display_flush_ready(disp);
}

static uint32_t tick_cb(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

bool display_lock(int timeout_ms)
{
    int64_t t0 = esp_timer_get_time();
    bool ok = xSemaphoreTakeRecursive(lvgl_mux, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (ok && ++lock_depth == 1) {
        lock_t0 = esp_timer_get_time();
        if (xTaskGetCurrentTaskHandle() == lvgl_th && lock_t0 - t0 > st.lvgl_wait_max_us)
            st.lvgl_wait_max_us = lock_t0 - t0;
    }
    return ok;
}

void display_unlock(void)
{
    if (--lock_depth == 0 && xTaskGetCurrentTaskHandle() != lvgl_th) {
        int64_t held = esp_timer_get_time() - lock_t0;
        if (held > st.hold_max_us) {
            st.hold_max_us = held;
            strlcpy(st.hold_task, pcTaskGetName(NULL), sizeof(st.hold_task));
        }
    }
    xSemaphoreGiveRecursive(lvgl_mux);
}

static void render_evt(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        if (last_render && now - last_render < 250000) count_anim(now - last_render, true, false);
        last_render = render_t0 = now;
        last_raw = false;
    } else {
        count_frame(now - render_t0);
    }
}

int display_lvgl_inflight(void) { return 0; }

void display_get_stats(display_stats_t *out, bool reset)
{
    display_lock(-1);
    *out = st;
    if (reset) memset(&st, 0, sizeof(st));
    display_unlock();
}

void display_get_test_stats(display_stats_t *out, bool reset)
{
    display_lock(-1);
    *out = tst;
    if (reset) { memset(&tst, 0, sizeof(tst)); tst_t0 = esp_timer_get_time(); }
    display_unlock();
}

/* Brightness and sleep: the backlight. The panel itself stays on (its DPI stream never stops); "asleep" is the
 * backlight off and LVGL slowed down, as on the AMOLED. */
static volatile bool asleep;
static uint8_t level_set = 255;

static void bl_set(uint8_t level)
{
    uint32_t duty = (uint32_t)level * 1023 / 255;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CH, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CH);
}

void display_brightness(uint8_t level)
{
    level_set = level;
    if (!asleep) bl_set(level);
}

void display_sleep(bool on)
{
    if (on == asleep) return;
    asleep = on;
    bl_set(on ? 0 : level_set);
    if (!on) lv_obj_invalidate(lv_screen_active());
    ESP_LOGI(TAG, "panel %s", on ? "asleep" : "awake");
}

bool display_asleep(void) { return asleep; }
bool display_cpu_full_speed(void) { return true; }       // no power management on this board (yet)

static char init_note[96];             // repeated 8 s after start: the boot's first ~3 s miss the USB log

static void lvgl_task(void *arg)
{
    bool noted = false;
    while (1) {
        if (!noted && esp_timer_get_time() > 8000000) { noted = true; ESP_LOGI(TAG, "%s", init_note); }
        uint32_t wait = 10;
        if (display_lock(-1)) {
            wait = lv_timer_handler();
            display_unlock();
        }
        if (wait < 1) wait = 1;
        if (wait > 50) wait = 50;
        if (asleep && wait < 100) wait = 100;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

/* JD9165 init tables and timings for the two panels Guition has shipped on this board, from Gil Tal's
 * ESP32-P4-Internet-Radio (MIT licence, github.com/giltal/ESP32-P4-Internet-Radio, main/board.c), which took them from
 * Guition's reference drivers. V2: the 2026 batch, "V2" after the material number on the back label. */
#define CMD(c, ...) { (c), (uint8_t[]){ __VA_ARGS__ }, sizeof((uint8_t[]){ __VA_ARGS__ }), 0 }
#define CMD_DELAY(c, ms) { (c), (uint8_t[]){ 0x00 }, 0, (ms) }

#if CONFIG_METEOBUS_P4_PANEL_V2
static const jd9165_lcd_init_cmd_t s_panel_init[] = {
    CMD(0x30, 0x00), CMD(0xF7, 0x49, 0x61, 0x02, 0x00), CMD(0x30, 0x01), CMD(0x04, 0x0C), CMD(0x05, 0x08),
    CMD(0x0B, 0x11), CMD(0x20, 0x04), CMD(0x1F, 0x05), CMD(0x23, 0x38), CMD(0x28, 0x18), CMD(0x29, 0x29),
    CMD(0x2A, 0x01), CMD(0x2B, 0x29), CMD(0x2C, 0x01), CMD(0x30, 0x02), CMD(0x00, 0x05), CMD(0x01, 0x22),
    CMD(0x02, 0x08), CMD(0x03, 0x12), CMD(0x04, 0x16), CMD(0x05, 0x64), CMD(0x06, 0x00), CMD(0x07, 0x00),
    CMD(0x08, 0x78), CMD(0x09, 0x00), CMD(0x0A, 0x04),
    CMD(0x0B, 0x16, 0x17, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x07, 0x09),
    CMD(0x0C, 0x09, 0x1E, 0x1E, 0x1C, 0x1C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x0D, 0x0A, 0x05, 0x0B, 0x0D, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x06, 0x08),
    CMD(0x0E, 0x08, 0x1F, 0x1F, 0x1D, 0x1D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x0F, 0x0A, 0x05, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1D, 0x1D, 0x1F),
    CMD(0x10, 0x1F, 0x08, 0x08, 0x06, 0x06, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x11, 0x16, 0x17, 0x0D, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x1C, 0x1C, 0x1E),
    CMD(0x12, 0x1E, 0x09, 0x09, 0x07, 0x07, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x13, 0x00, 0x00, 0x00, 0x00), CMD(0x14, 0x00, 0x00, 0x41, 0x41), CMD(0x15, 0x00, 0x00, 0x00, 0x00),
    CMD(0x17, 0x00), CMD(0x18, 0x85), CMD(0x19, 0x06, 0x09), CMD(0x1A, 0x05, 0x08), CMD(0x1B, 0x0A, 0x04),
    CMD(0x26, 0x00), CMD(0x27, 0x00), CMD(0x30, 0x06),
    CMD(0x12, 0x3F, 0x25, 0x27, 0x35, 0x1D, 0x1B, 0x1B, 0x1A, 0x18, 0x0A, 0x2A, 0x21, 0x19, 0x30),
    CMD(0x13, 0x3F, 0x26, 0x27, 0x35, 0x1E, 0x1C, 0x1C, 0x1A, 0x18, 0x0B, 0x2A, 0x21, 0x19, 0x30),
    CMD(0x30, 0x0A), CMD(0x02, 0x4F), CMD(0x0B, 0x40), CMD(0x30, 0x0D), CMD(0x0D, 0x04), CMD(0x10, 0x05),
    CMD(0x11, 0x0C), CMD(0x12, 0x05), CMD(0x13, 0x0C), CMD(0x30, 0x00),
    CMD_DELAY(0x11, 120),   /* SLPOUT */
    CMD_DELAY(0x29, 20),    /* DISPON */
};
#define PANEL_PCLK_MHZ  52
#define PANEL_HPW       24
#define PANEL_HBP       136
#define PANEL_HFP       160
#define PANEL_VPW       2
#define PANEL_VBP       21
#define PANEL_VFP       12
#else
static const jd9165_lcd_init_cmd_t s_panel_init[] = {
    CMD(0x30, 0x00), CMD(0xF7, 0x49, 0x61, 0x02, 0x00), CMD(0x30, 0x01), CMD(0x04, 0x0C), CMD(0x05, 0x00),
    CMD(0x06, 0x00), CMD(0x0B, 0x11), CMD(0x17, 0x00), CMD(0x20, 0x04), CMD(0x1F, 0x05), CMD(0x23, 0x00),
    CMD(0x25, 0x19), CMD(0x28, 0x18), CMD(0x29, 0x04), CMD(0x2A, 0x01), CMD(0x2B, 0x04), CMD(0x2C, 0x01),
    CMD(0x30, 0x02), CMD(0x01, 0x22), CMD(0x03, 0x12), CMD(0x04, 0x00), CMD(0x05, 0x64), CMD(0x0A, 0x08),
    CMD(0x0B, 0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D),
    CMD(0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x0D, 0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C),
    CMD(0x0E, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x0F, 0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07),
    CMD(0x10, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x11, 0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06),
    CMD(0x12, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D),
    CMD(0x14, 0x00, 0x00, 0x11, 0x11), CMD(0x18, 0x99), CMD(0x30, 0x06),
    CMD(0x12, 0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29),
    CMD(0x13, 0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29),
    CMD(0x30, 0x0A), CMD(0x02, 0x4F), CMD(0x0B, 0x40), CMD(0x12, 0x3E), CMD(0x13, 0x78), CMD(0x30, 0x0D),
    CMD(0x0D, 0x04), CMD(0x10, 0x0C), CMD(0x11, 0x0C), CMD(0x12, 0x0C), CMD(0x13, 0x0C), CMD(0x30, 0x00),
    CMD_DELAY(0x11, 120),   /* SLPOUT */
    CMD_DELAY(0x29, 20),    /* DISPON */
};
#define PANEL_PCLK_MHZ  54
#define PANEL_HPW       40
#define PANEL_HBP       160
#define PANEL_HFP       160
#define PANEL_VPW       10
#define PANEL_VBP       23
#define PANEL_VFP       12
#endif


static IRAM_ATTR bool on_refresh(esp_lcd_panel_handle_t p, esp_lcd_dpi_panel_event_data_t *e, void *ctx)
{
    BaseType_t w = pdFALSE;
    xSemaphoreGiveFromISR(vsync, &w);
    return w == pdTRUE;
}

void display_init(void)
{
    ESP_LOGI(TAG, "Init MIPI-DSI + JD9165 (%dx%d), LVGL screen %dx%d at %d,%d", PANEL_W, PANEL_H, DISP_W, DISP_H,
             DISP_OX, DISP_OY);
    ledc_timer_config_t lt = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));
    ledc_channel_config_t lc = { .gpio_num = PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_CH,
                                 .timer_sel = BL_TIMER, .duty = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&lc));

    static esp_ldo_channel_handle_t ldo;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_LDO_CHAN, .voltage_mv = DSI_LDO_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo));

    esp_lcd_dsi_bus_handle_t dsi;
    esp_lcd_dsi_bus_config_t bc = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bc, &dsi));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi, &dbi, &io));
    esp_lcd_dpi_panel_config_t dpi = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = PANEL_PCLK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = PANEL_W, .v_size = PANEL_H,
            .hsync_pulse_width = PANEL_HPW, .hsync_back_porch = PANEL_HBP, .hsync_front_porch = PANEL_HFP,
            .vsync_pulse_width = PANEL_VPW, .vsync_back_porch = PANEL_VBP, .vsync_front_porch = PANEL_VFP,
        },
    };
    jd9165_vendor_config_t vc = {
        .init_cmds = s_panel_init,
        .init_cmds_size = sizeof(s_panel_init) / sizeof(s_panel_init[0]),
        .mipi_config = { .dsi_bus = dsi, .dpi_config = &dpi },
    };
    esp_lcd_panel_dev_config_t dc = { .bits_per_pixel = 16, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                      .reset_gpio_num = PIN_RST, .vendor_config = &vc };
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io, &dc, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    vTaskDelay(pdMS_TO_TICKS(120));                      // the JD9165 needs 120 ms after reset before its table
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    snprintf(init_note, sizeof(init_note), "JD9165 panel up (%s, reset GPIO %d, backlight GPIO %d)",
             PANEL_V2 ? "V2" : "V1", PIN_RST, PIN_BL);
    ESP_LOGI(TAG, "%s", init_note);
    void *f0 = NULL, *f1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &f0, &f1));
    fbs[0] = f0; fbs[1] = f1;
    fb = fbs[front = 0];
    for (int i = 0; i < 2; i++) {
        memset(fbs[i], 0, (size_t)PANEL_W * PANEL_H * 2);
        esp_cache_msync(fbs[i], (size_t)PANEL_W * PANEL_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    vsync = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_refresh_done = on_refresh };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL));

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(DISP_W, DISP_H);
    size_t buf_bytes = DISP_W * BUF_LINES * 2;
    buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf1 && buf2);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, render_evt, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, render_evt, LV_EVENT_RENDER_READY, NULL);

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, &lvgl_th, 1);
    bl_set(level_set);
    ESP_LOGI(TAG, "Display ready");
}
