#include <stdio.h>
#include <string.h>
// ESP32-P4-Function-EV-Board: EK79007 1024x600 over MIPI-DSI (2 lanes) + LVGL v9 port. The panel scans a frame buffer
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
#include "esp_lcd_ek79007.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "display";

#define PIN_BL        26              // backlight (LEDC PWM), CONFIG_BSP_LCD_TYPE_1024_600 in Espressif's BSP
#define PIN_RST       27
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

static void lvgl_task(void *arg)
{
    while (1) {
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

void display_init(void)
{
    ESP_LOGI(TAG, "Init MIPI-DSI + EK79007 (%dx%d), LVGL screen %dx%d at %d,%d", PANEL_W, PANEL_H, DISP_W, DISP_H,
             DISP_OX, DISP_OY);
    ledc_timer_config_t lt = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
                               .timer_num = BL_TIMER, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));
    ledc_channel_config_t lc = { .gpio_num = PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_CH,
                                 .timer_sel = BL_TIMER, .duty = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&lc));

    static esp_ldo_channel_handle_t ldo;
    esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_LDO_CHAN, .voltage_mv = DSI_LDO_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo));

    esp_lcd_dsi_bus_handle_t dsi;
    esp_lcd_dsi_bus_config_t bc = { .bus_id = 0, .num_data_lanes = 2, .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                                    .lane_bit_rate_mbps = 1000 };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bc, &dsi));
    esp_lcd_panel_io_handle_t io;
    esp_lcd_dbi_io_config_t dbi = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi, &dbi, &io));
    esp_lcd_dpi_panel_config_t dpi = EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB565);
    dpi.num_fbs = 1;
    ek79007_vendor_config_t vc = { .mipi_config = { .dsi_bus = dsi, .dpi_config = &dpi } };
    esp_lcd_panel_dev_config_t dc = { .bits_per_pixel = 16, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                      .reset_gpio_num = PIN_RST, .vendor_config = &vc };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(io, &dc, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
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
