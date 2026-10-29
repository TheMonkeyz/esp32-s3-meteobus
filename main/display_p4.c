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
static uint16_t *fb;                    // the panel's frame buffer (PSRAM, PANEL_W x PANEL_H RGB565)
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
    const int w = x1 - x0 + 1, rows = (DISP_W * BUF_LINES / w) & ~1;
    const int last = y0 + (y1 - y0) / rows * rows;
    raw_phase = 1;
    for (int y = bottom_up ? last : y0; bottom_up ? y >= y0 : y <= y1; y += bottom_up ? -rows : rows) {
        int n = y1 + 1 - y < rows ? y1 + 1 - y : rows;
        raw_band = y;
        raw_phase = 2;
        fill(x0, w, y, n, buf1, user);                   // panel order: byte-swapped RGB565 (the AMOLED's)
        raw_phase = 3;
        int64_t tc = esp_timer_get_time();
        const uint16_t *s = buf1;
        for (int r = 0; r < n; r++) {
            uint16_t *d = fb_at(x0, y + r);
            for (int i = 0; i < w; i++) d[i] = __builtin_bswap16(s[i]);
            s += w;
        }
        fb_sync(y, y + n - 1);
        raw_t.cmd_us += esp_timer_get_time() - tc;
        st.pixels += w * n;
        tst.pixels += w * n;
    }
    raw_phase = 0;
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
        .num_fbs = 1,
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
    void *f = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &f));
    fb = f;
    memset(fb, 0, (size_t)PANEL_W * PANEL_H * 2);
    esp_cache_msync(fb, (size_t)PANEL_W * PANEL_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

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
