#include <string.h>
// Guition JC1060P470C: GT911 capacitive touch (I2C 0x5D or 0x14, SDA 7 / SCL 8) -> LVGL pointer input. Its RST and
// INT lines aren't the P4's to drive (its power comes with the panel's reset, display_p4.c), and it reports in its own
// 800x480 (read from its config, then scaled to the panel).
// Same API and behaviour as the AMOLED's touch.c (CST9217): the panel's 1024x600 points are mapped into the LVGL
// screen (DISP_W x DISP_H at DISP_OX, DISP_OY; outside it, clamped to its edge).
#include "touch.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "presence.h"

static const char *TAG = "touch";

#define PIN_SDA   7
#define PIN_SCL   8

static i2c_master_dev_handle_t dev;
static i2c_master_bus_handle_t bus;
static bool ok;
static int n_ok, n_err, max_x = PANEL_W, max_y = PANEL_H;
static volatile bool inj_on, inj_down;
static volatile int inj_x, inj_y;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void touch_inject(bool down, int x, int y)
{
    inj_x = clampi(x, 0, DISP_W - 1);
    inj_y = clampi(y, 0, DISP_H - 1);
    inj_down = down;
    inj_on = true;
}

void touch_inject_end(void) { inj_down = false; inj_on = false; }

static esp_err_t gt_read(uint16_t reg, uint8_t *d, size_t n)
{
    uint8_t r[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_transmit_receive(dev, r, 2, d, n, 20);
}

static esp_err_t gt_write8(uint16_t reg, uint8_t v)
{
    uint8_t w[3] = { reg >> 8, reg & 0xFF, v };
    return i2c_master_transmit(dev, w, 3, 20);
}

/* The GT911 reports every 5 + N ms (its config's 0x8056, low 4 bits). Shipped here at a rate that gave a new point
 * only every ~35-40 ms during drags: at 64 frames a second the page moved in steps of 2-3 frames (the user's
 * "stuttering", 2026-10-29). Set N = 0 (5 ms): the whole config read, that byte changed, the checksum (0x80FF: two's
 * complement of the sum of 0x8047..0x80FE) and the "new config" flag (0x8100) written. Once: the chip keeps it. */
static int gt_period = -1;                       // ms between reports (the 15 s log line: start-up lines are lost)
static char gt_note[64] = "config not read";

static void gt_fast_reports(void)
{
    uint8_t cfg[186];                                    // 0x8047..0x80FE, the checksum, the flag
    if (gt_read(0x8047, cfg, 185) != ESP_OK) { ESP_LOGW(TAG, "GT911 config not read"); return; }
    uint8_t sum = 0;
    for (int i = 0; i < 184; i++) sum += cfg[i];
    int n = cfg[0x8056 - 0x8047] & 0x0F;
    gt_period = 5 + n;
    snprintf(gt_note, sizeof(gt_note), "config v%d, checksum %02X vs %02X", cfg[0], cfg[184], (uint8_t)(~sum + 1));
    ESP_LOGI(TAG, "GT911 %s: a report every %d ms", gt_note, 5 + n);
    if (!n || (uint8_t)(~sum + 1) != cfg[184]) return;    // already fast, or a config we don't understand: leave it
    cfg[0x8056 - 0x8047] &= 0xF0;
    sum = 0;
    for (int i = 0; i < 184; i++) sum += cfg[i];
    cfg[184] = (uint8_t)(~sum + 1);
    cfg[185] = 1;
    uint8_t w[2 + 186] = { 0x80, 0x47 };
    memcpy(w + 2, cfg, 186);
    esp_err_t e = i2c_master_transmit(dev, w, sizeof(w), 100);
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "GT911: a report every 5 ms now (%s)", esp_err_to_name(e));
    if (e == ESP_OK) gt_period = 5;
    strlcat(gt_note, e == ESP_OK ? ", set to 5 ms" : ", write failed", sizeof(gt_note));
}

void touch_init(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_1, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &bus) != ESP_OK) { ESP_LOGE(TAG, "I2C bus init failed"); return; }
    // No bare address probe: right after the panel's reset it upsets the controller (Gil Tal's board.c). Read its
    // product id at either address instead.
    uint8_t addr = 0, id[4] = {0}, cfg[4] = {0};
    for (int i = 0; i < 2 && !addr; i++) {
        uint8_t a = i ? 0x14 : 0x5D;
        i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = a, .scl_speed_hz = 400000 };
        if (i2c_master_bus_add_device(bus, &dc, &dev) != ESP_OK) continue;
        if (gt_read(0x8140, id, 4) == ESP_OK) addr = a;
        else { i2c_master_bus_rm_device(dev); dev = NULL; }
    }
    if (!addr) { ESP_LOGE(TAG, "GT911 NOT found (0x5D, 0x14)"); return; }
    if (gt_read(0x8048, cfg, 4) == ESP_OK) {             // the resolution the controller reports in
        int x = cfg[0] | cfg[1] << 8, y = cfg[2] | cfg[3] << 8;
        if (x > 0 && y > 0) { max_x = x; max_y = y; }
    }
    ok = true;
    ESP_LOGI(TAG, "GT%.4s found at 0x%02X, %dx%d", (char *)id, addr, max_x, max_y);
    gt_fast_reports();
}

static volatile int64_t last_down_us;
uint32_t touch_idle_ms(void) { return (uint32_t)((esp_timer_get_time() - last_down_us) / 1000); }

static int n_nofinger, n_notready;
static int lr_state, lr_x, lr_y;        // the last report: the GT911 says "not ready" between its ~100 Hz reports
static bool got_new;                    // the last read brought a new report (touch_fresh)

static int touch_read_chip(int *x, int *y)
{
    got_new = true;
    if (inj_on) { *x = inj_x; *y = inj_y; return inj_down; }
    if (!ok) return 0;
    uint8_t s;
    if (gt_read(0x814E, &s, 1) != ESP_OK) { n_err++; return -1; }
    n_ok++;
    if (!(s & 0x80)) { n_notready++; got_new = false; *x = lr_x; *y = lr_y; return lr_state; }   // no new report yet
    int n = s & 0x0F;
    if (n == 0) {
        n_nofinger++;
        lr_state = 0;
    } else {
        uint8_t d[4];
        if (gt_read(0x8150, d, 4) != ESP_OK) { n_err++; gt_write8(0x814E, 0); return -1; }
        int rx = d[0] | d[1] << 8, ry = d[2] | d[3] << 8;
        rx = rx * PANEL_W / max_x;
        ry = ry * PANEL_H / max_y;
        lr_x = clampi(rx - DISP_OX, 0, DISP_W - 1);
        lr_y = clampi(ry - DISP_OY, 0, DISP_H - 1);
        lr_state = 1;
    }
    gt_write8(0x814E, 0);                                // report consumed
    *x = lr_x; *y = lr_y;
    return lr_state;
}

static int touch_read(int *x, int *y)
{
    int r = touch_read_chip(x, y);
    if (r > 0) last_down_us = esp_timer_get_time();
    return r;
}

static volatile bool forget;
static volatile uint32_t forgotten;
void touch_forget(void) { forget = true; forgotten++; }
uint32_t touch_forgotten(void) { return forgotten; }

static void read_core(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int lx, ly;
    static bool was;
    static int errs;
    static uint32_t last_log;
    if (forget) { forget = false; was = false; errs = 0; }
    uint32_t now = lv_tick_get();
    if (now - last_log > 15000) {
        ESP_LOGI(TAG, "reads ok=%d err=%d, no finger %d, not ready %d (a report every %d ms; %s)", n_ok, n_err,
                 n_nofinger, n_notready, gt_period, gt_note);
        last_log = now;
    }
    int x, y;
    int r = touch_read(&x, &y);
    if (r < 0 && was && ++errs < 6) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = lx; data->point.y = ly;
        return;
    }
    errs = 0;
    static bool swallow;                    // the touch that wakes a dark screen does nothing else
    if (r > 0 && !was && presence_touch()) swallow = true;
    if (swallow) {
        if (r == 0) swallow = false;
        was = r > 0;
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = lx; data->point.y = ly;
        return;
    }
    if (r > 0) {
        lx = x; ly = y;
        data->state = LV_INDEV_STATE_PRESSED;
        if (!was) ESP_LOGI(TAG, "down %d,%d", x, y);
        was = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        if (was) ESP_LOGI(TAG, "up %d,%d", lx, ly);
        was = false;
    }
    data->point.x = lx;
    data->point.y = ly;
}

static bool fresh;
bool touch_fresh(void) { return fresh; }

int touch_get(int *x, int *y)
{
    static int64_t last;
    static int lr, lx, ly;
    int64_t now = esp_timer_get_time();
    fresh = inj_on || !last || now - last >= 10000;
    if (!fresh) { *x = lx; *y = ly; return lr; }
    last = now;
    lr = touch_read(x, y);
    fresh = got_new;
    lx = *x;
    ly = *y;
    return lr;
}

static touch_read_hook_t read_hook;
void touch_set_read_hook(touch_read_hook_t hook) { read_hook = hook; }

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    read_core(indev, data);
    if (read_hook) read_hook(indev, data);
}

void touch_register_lvgl(void)
{
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, read_cb);
}

i2c_master_bus_handle_t touch_i2c_bus(void) { return bus; }
