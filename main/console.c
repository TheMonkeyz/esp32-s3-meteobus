// The display's own test console commands and diagnostics lines, registered with espforge's forge_core (testcon.h,
// diag.h): forge_core's built-ins are ping, help, heap, where, memspeed, reboot; forge_net adds wifi, key, portal.
// Every answer is a log line starting with "test: " (docs/TESTING.md, "Harness"). Moved from testcon.c and diag.c,
// October 5 (v1.14.0).
#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"              // esp_clk_cpu_freq(): "power"
#include "sdkconfig.h"
#if CONFIG_PM_PROFILING
#include "esp_pm.h"                                   // esp_pm_dump_locks(): "power"
#endif
#include "testcon.h"
#include "diag.h"
#include "display.h"
#include "slide.h"
#include "touch.h"
#include "ui.h"
#include "config.h"
#include "driver/usb_serial_jtag.h"
#if CONFIG_LWIP_STATS
#include "lwip/stats.h"
#endif
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#endif

static const char *TAG = "test";

#define CX (DISP_W / 2)
#define CY (DISP_H / 2)

// Finger moves in steps of ~16 ms (LVGL reads the touch every 30 ms, so every read sees a new point)
static void finger_path(int x0, int y0, int x1, int y1, int ms)
{
    int steps = ms / 16 < 2 ? 2 : ms / 16;
    for (int i = 0; i <= steps; i++) {
        touch_inject(true, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

static void finger_up(int x, int y)
{
    touch_inject(false, x, y);
    vTaskDelay(pdMS_TO_TICKS(100));          // LVGL must read the release before the real controller takes over
    touch_inject_end();
}

static void press(int x, int y, int ms)
{
    touch_inject(true, x, y);
    vTaskDelay(pdMS_TO_TICKS(ms));
    finger_up(x, y);
}

static int ms_arg(const char *s)                          // 0..10000 ms
{
    int v = atoi(s);
    return v < 0 ? 0 : v > 10000 ? 10000 : v;
}

/* ---------- commands ---------- */

// "power": the panel's state and display.c's "screen" lock (held: 240 MHz even when idle; free: an idle core may
// drop to 80 MHz). cpu_mhz is the clock while this command runs: always the maximum, a running task holds ESP-IDF's
// own lock (harness presence.dim_off_wake; with CONFIG_PM_PROFILING, the time spent in each mode follows)
static void cmd_power(int argc, char **argv)
{
    ESP_LOGI(TAG, "power cpu_mhz=%d panel=%s screen_lock=%d", esp_clk_cpu_freq() / 1000000,
             display_asleep() ? "asleep" : "awake", display_cpu_full_speed());
#if CONFIG_PM_PROFILING
    esp_pm_dump_locks(stdout);
#endif
}

static void cmd_screen(int argc, char **argv)
{
    if (!display_lock(2000)) {                           // the console must stay free for "where"
        ESP_LOGW(TAG, "error screen: display busy for 2 s (send 'where')");
        return;
    }
    const char *n = ui_screen_name();
    display_unlock();
    ESP_LOGI(TAG, "screen %s", n);
}

static void cmd_page(int argc, char **argv)
{
    if (!display_lock(2000)) { ESP_LOGW(TAG, "error page: display busy for 2 s (send 'where')"); return; }
    int place, day, places, days, ix, iy, bx, by;
    ui_pages(&place, &day, &places, &days);
    ui_hero_icon(&ix, &iy);
    ui_route_badge(&bx, &by);
    display_unlock();
    ESP_LOGI(TAG, "page place=%d places=%d day=%d days=%d icon=%d,%d badge=%d,%d", place, places, day, days, ix, iy, bx, by);
}

static void cmd_tap(int argc, char **argv)
{
    if (argc != 3) { ESP_LOGW(TAG, "error tap X Y"); return; }
    press(atoi(argv[1]), atoi(argv[2]), 120);   // a real tap is ~80-150 ms; 60 ms fitted inside one full redraw
    ESP_LOGI(TAG, "ok tap");
}

static void cmd_press(int argc, char **argv)            // long-press: press X Y [ms, default 1200]
{
    if (argc < 3) { ESP_LOGW(TAG, "error press X Y [ms]"); return; }
    press(atoi(argv[1]), atoi(argv[2]), argc > 3 ? ms_arg(argv[3]) : 1200);
    ESP_LOGI(TAG, "ok press");
}

static void cmd_swipe(int argc, char **argv)
{
    // 150 px each way on the round screen; on a landscape one past a third of its width / height (a drag commits there)
    int dh = DISP_ROUND ? 150 : DISP_W * 22 / 100, dv = DISP_ROUND ? 150 : DISP_H * 22 / 100;
    int x0 = CX, y0 = CY, x1 = CX, y1 = CY;
    const char *dir = argc == 2 ? argv[1] : "";
    if (!strcmp(dir, "left"))       { x0 = CX + dh; x1 = CX - dh; }
    else if (!strcmp(dir, "right")) { x0 = CX - dh; x1 = CX + dh; }
    else if (!strcmp(dir, "up"))    { y0 = CY + dv; y1 = CY - dv; }
    else if (!strcmp(dir, "down"))  { y0 = CY - dv; y1 = CY + dv; }
    else { ESP_LOGW(TAG, "error swipe: left, right, up or down"); return; }
    finger_path(x0, y0, x1, y1, 200);
    finger_up(x1, y1);
    ESP_LOGI(TAG, "ok swipe %s", dir);
}

static void cmd_drag(int argc, char **argv)             // drag X1 Y1 X2 Y2 [ms]
{
    if (argc < 5) { ESP_LOGW(TAG, "error drag X1 Y1 X2 Y2 [ms]"); return; }
    int x1 = atoi(argv[3]), y1 = atoi(argv[4]);
    finger_path(atoi(argv[1]), atoi(argv[2]), x1, y1, argc > 5 ? ms_arg(argv[5]) : 400);
    finger_up(x1, y1);
    ESP_LOGI(TAG, "ok drag");
}

static void cmd_fps(int argc, char **argv)              // "fps reset", then "fps": frames since the reset
{
    display_stats_t d;
    display_get_test_stats(&d, true);
    if (argc == 2 && !strcmp(argv[1], "reset")) { ESP_LOGI(TAG, "ok fps reset"); return; }
    ESP_LOGI(TAG, "fps frames=%lu render_avg_ms=%.1f render_max_ms=%.1f anim_frames=%lu anim_fps=%.1f "
                  "gap_max_ms=%.1f gap_max_at_ms=%lu gap_max_kind=%s mpx=%.2f", (unsigned long)d.frames,
             d.frames ? d.render_us / 1000.0f / d.frames : 0, d.render_max_us / 1000.0f,
             (unsigned long)d.anim_frames, d.anim_us ? d.anim_frames * 1e6f / d.anim_us : 0,
             d.anim_gap_max_us / 1000.0f, (unsigned long)d.anim_gap_max_at_ms,
             d.anim_gap_max_kind[0] ? d.anim_gap_max_kind : "-", d.pixels / 1e6f);
}

#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
// "profile": one render-only frame of each screen with LVGL's profiler on; the trace lines go to the log and
// tools/harness/profile.py adds them up. Needs CONFIG_LV_USE_PROFILER(_BUILTIN) in the build's sdkconfig.
#include "misc/lv_profiler_builtin_private.h"
static uint32_t prof_tick(void) { return (uint32_t)esp_timer_get_time(); }
static void prof_flush(const char *buf) { printf("%s", buf); }
static void profile_task(void *arg)
{
    lv_profiler_builtin_uninit();
    lv_profiler_builtin_config_t cfg;
    lv_profiler_builtin_config_init(&cfg);
    cfg.buf_size = 512 * 1024;
    cfg.tick_per_sec = 1000000;
    cfg.tick_get_cb = prof_tick;
    cfg.flush_cb = prof_flush;
    lv_profiler_builtin_init(&cfg);
    lv_profiler_builtin_set_enable(false);
    lv_obj_t *scr[6];
    const char *name[6];
    display_lock(-1);
    int n = ui_bench_screens(scr, name, 6);
    display_bench_no_panel(true);
    for (int i = 0; i < n; i++) {
        lv_screen_load(scr[i]);
        lv_refr_now(NULL);                               // warm caches
        lv_profiler_builtin_set_enable(true);
        lv_obj_invalidate(scr[i]);
        lv_refr_now(NULL);
        lv_profiler_builtin_set_enable(false);
        printf("PROFILE-BEGIN %s\n", name[i]);
        lv_profiler_builtin_flush();
        printf("PROFILE-END %s\n", name[i]);
    }
    lv_screen_load(ui_main_screen());
    display_bench_no_panel(false);
    lv_obj_invalidate(lv_screen_active());
    display_unlock();
    ESP_LOGI(TAG, "ok profile done");
    vTaskDelete(NULL);
}

static void cmd_profile(int argc, char **argv)          // one frame of each screen (docs/TESTING.md §8)
{
    xTaskCreatePinnedToCore(profile_task, "profile", 10240, NULL, 4, NULL, 1);
}
#endif


/* "shot SCREEN [DIV]": a screen (ui_snapshot's names: weather, stop1, radar, settings, hourly0, ...) sent over USB as
 * base64 RGB565, every DIV-th pixel of every DIV-th row (DIV 1..4, default 2): "SHOT name w h", one "S:" line per row,
 * "SHOTEND". tools/shot.py turns a log into PNGs. For a session that can't reach the board's HTTPS /api/snapshot (the
 * cloud's), and for screens before Wi-Fi. Straight to the USB driver (testcon's): the UART console would take ~40 s. */
static void usb_line(const char *s, size_t n)
{
    for (size_t o = 0; o < n;) {
        size_t k = n - o > 128 ? 128 : n - o;             // the driver's ring buffer (testcon: 256 B) refuses bigger writes
        int w = usb_serial_jtag_write_bytes(s + o, k, pdMS_TO_TICKS(500));
        if (w <= 0) return;                               // no host reading: drop the rest
        o += w;
    }
}

// In its own task: rendering a screen (the hourly view's pages, say) overflowed the test console's stack
static char shot_name[24];
static int shot_div;
static void shot_task(void *arg);

static void cmd_shot(int argc, char **argv)
{
    strlcpy(shot_name, argc > 1 ? argv[1] : "shown", sizeof(shot_name));
    shot_div = argc > 2 ? atoi(argv[2]) : 2;
    if (shot_div < 1 || shot_div > 4) shot_div = 2;
    if (xTaskCreatePinnedToCore(shot_task, "shot", 16384, NULL, 2, NULL, 0) != pdPASS)
        ESP_LOGW(TAG, "error shot %s: no task", shot_name);
}

static void shot_task(void *arg)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char *name = shot_name;
    int div = shot_div;
    display_lock(-1);
    lv_draw_buf_t *db = ui_snapshot(name);
    display_unlock();
    if (!db) { ESP_LOGW(TAG, "error shot %s: no picture", name); vTaskDelete(NULL); return; }
    int w = db->header.w / div, h = db->header.h / div;
    uint16_t *row = heap_caps_malloc(w * 2, MALLOC_CAP_SPIRAM);
    char *line = heap_caps_malloc(w * 2 * 4 / 3 + 16, MALLOC_CAP_SPIRAM);
    if (row && line) {
        char hd[64];
        int n = snprintf(hd, sizeof(hd), "\nSHOT %s %d %d\n", name, w, h);
        usb_line(hd, n);
        for (int y = 0; y < h; y++) {
            const uint16_t *src = (const uint16_t *)(db->data + (size_t)y * div * db->header.stride);
            for (int x = 0; x < w; x++) row[x] = src[x * div];
            const uint8_t *b = (const uint8_t *)row;
            int len = w * 2, o = 0;
            line[o++] = 'S'; line[o++] = ':';
            for (int i = 0; i < len; i += 3) {
                uint32_t v = b[i] << 16 | (i + 1 < len ? b[i + 1] << 8 : 0) | (i + 2 < len ? b[i + 2] : 0);
                line[o++] = b64[v >> 18 & 63];
                line[o++] = b64[v >> 12 & 63];
                line[o++] = i + 1 < len ? b64[v >> 6 & 63] : '=';
                line[o++] = i + 2 < len ? b64[v & 63] : '=';
            }
            line[o++] = '\n';
            usb_line(line, o);
        }
        usb_line("SHOTEND\n", 8);
    }
    free(row);
    free(line);
    display_lock(-1);
    lv_draw_buf_destroy(db);
    display_unlock();
    ESP_LOGI(TAG, "ok shot %s %dx%d", name, w, h);
    vTaskDelete(NULL);
}

#if !DISP_SWAP
static void cmd_blitbench(int argc, char **argv) { display_blit_bench(); }
#endif

#if CONFIG_LWIP_STATS
// "netstat": lwIP's receive counters (whether connections from outside reach this chip: the P4 behind its C6)
static void cmd_netstat(int argc, char **argv)
{
    ESP_LOGI(TAG, "netstat link rx=%u drop=%u | ip rx=%u drop=%u | tcp rx=%u drop=%u err=%u proterr=%u rst=%u | udp rx=%u",
             (unsigned)lwip_stats.link.recv, (unsigned)lwip_stats.link.drop, (unsigned)lwip_stats.ip.recv,
             (unsigned)lwip_stats.ip.drop, (unsigned)lwip_stats.tcp.recv, (unsigned)lwip_stats.tcp.drop,
             (unsigned)lwip_stats.tcp.err, (unsigned)lwip_stats.tcp.proterr, (unsigned)lwip_stats.tcp.rterr,
             (unsigned)lwip_stats.udp.recv);
}
#endif


#if CONFIG_IDF_TARGET_ESP32P4
/* "c6update SIZE": the Wi-Fi co-processor's (ESP32-C6) firmware, from the P4's spare app slot (ota_1, where the flash
 * helper wrote it: p4/firmware/c6.bin), through esp-hosted's own update calls (as its host_performs_slave_ota
 * example). The board shipped with esp-hosted 2.3 on the C6, whose network split kept connections from the home
 * network (the settings page) for itself; 2.12 forwards them. The C6 writes its other slot and switches only once the
 * image checks out (2.3 needs no "activate"); the P4 restarts after, to start the link afresh. */
static void c6update_task(void *arg)
{
    uint32_t size = (uint32_t)(uintptr_t)arg;
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    uint8_t *buf = malloc(1500);
    if (!p || !buf) { ESP_LOGW(TAG, "error c6update: no image slot"); goto out; }
    // The image starts at 0, or after a 4 KB pad: the helper's esptool refuses to write another chip's image (c6.bin
    // goes with a pad of 0xFF in front, so it's plain data to esptool)
    uint8_t magic = 0;
    uint32_t base = 0;
    esp_partition_read(p, 0, &magic, 1);
    if (magic != 0xE9) { base = 4096; esp_partition_read(p, base, &magic, 1); }
    if (magic != 0xE9 || size + base > p->size) { ESP_LOGW(TAG, "error c6update: no image in ota_1"); goto out; }
    esp_hosted_coprocessor_fwver_t v = {0};
    esp_hosted_get_coprocessor_fwversion(&v);
    ESP_LOGI(TAG, "c6update: C6 runs %lu.%lu.%lu; sending %lu bytes", (unsigned long)v.major1, (unsigned long)v.minor1,
             (unsigned long)v.patch1, (unsigned long)size);
    esp_err_t e = esp_hosted_slave_ota_begin();
    if (e != ESP_OK) { ESP_LOGW(TAG, "error c6update: begin %s", esp_err_to_name(e)); goto out; }
    for (uint32_t off = 0; off < size && e == ESP_OK; off += 1500) {
        uint32_t n = size - off < 1500 ? size - off : 1500;
        e = esp_partition_read(p, base + off, buf, n);
        if (e == ESP_OK) e = esp_hosted_slave_ota_write(buf, n);
        if (off % (150 * 1500) == 0) ESP_LOGI(TAG, "c6update: %lu%%", (unsigned long)(off * 100ULL / size));
    }
    if (e != ESP_OK) { ESP_LOGW(TAG, "error c6update: write %s (the C6 keeps its firmware)", esp_err_to_name(e)); goto out; }
    e = esp_hosted_slave_ota_end();
    if (e != ESP_OK) { ESP_LOGW(TAG, "error c6update: end %s (the C6 keeps its firmware)", esp_err_to_name(e)); goto out; }
    if (v.major1 > 2 || (v.major1 == 2 && v.minor1 >= 6)) {
        e = esp_hosted_slave_ota_activate();
        ESP_LOGI(TAG, "c6update: activate %s", esp_err_to_name(e));
    }
    ESP_LOGI(TAG, "ok c6update: sent, restarting in 3 s");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
out:
    free(buf);
    vTaskDelete(NULL);
}

static void cmd_c6update(int argc, char **argv)
{
    uint32_t size = argc > 1 ? strtoul(argv[1], NULL, 10) : 0;
    if (!size) { ESP_LOGW(TAG, "error c6update SIZE"); return; }
    xTaskCreatePinnedToCore(c6update_task, "c6update", 6144, (void *)(uintptr_t)size, 2, NULL, 0);
}
#endif

static void cmd_pictest(int argc, char **argv)          // the picture of the screen shown = the screen? (slide.c)
{
    if (!display_lock(2000)) { ESP_LOGW(TAG, "error pictest: display busy for 2 s (send 'where')"); return; }
    slide_picture_check_async();
    display_unlock();
    ESP_LOGI(TAG, "ok pictest (result: \"slide: pictest\" line)");
}

static void (*alert_at_cb)(bool on, double lat, double lon);
void console_on_alert_at(void (*fn)(bool on, double lat, double lon)) { alert_at_cb = fn; }

static void cmd_alert(int argc, char **argv)            // the alert screen's layout; alerts from another point
{
    if (argc >= 3 && !strcmp(argv[1], "at") && alert_at_cb) {
        bool off = argc == 3 && !strcmp(argv[2], "off");
        if (!off && argc != 4) { ESP_LOGW(TAG, "error alert at LAT LON|off"); return; }
        double lat = off ? 0 : atof(argv[2]), lon = off ? 0 : atof(argv[3]);
        alert_at_cb(!off, lat, lon);
        if (off) ESP_LOGI(TAG, "ok alert at off");
        else ESP_LOGI(TAG, "ok alert at %.4f %.4f (the first place's alerts, until 'alert at off' or a restart)", lat, lon);
        return;
    }
    if (argc != 3 || strcmp(argv[1], "sample")) { ESP_LOGW(TAG, "error alert sample en|fr|max|off, alert at LAT LON|off"); return; }
    if (!display_lock(2000)) { ESP_LOGW(TAG, "error alert: display busy for 2 s (send 'where')"); return; }
    int th, lines, by;
    bool ok = ui_alert_sample(argv[2], &th, &lines, &by);
    display_unlock();
    if (!ok) { ESP_LOGW(TAG, "error alert sample: en, fr, max or off"); return; }
    ESP_LOGI(TAG, "alert sample %s title_y=40 title_h=%d lines=%d box_y=%d", argv[2], th, lines, by);
}

static void cmd_setup(int argc, char **argv)            // the Easy Connect page after a failed attempt
{
    if (argc != 2 || strcmp(argv[1], "fail")) { ESP_LOGW(TAG, "error setup fail"); return; }
    if (!display_lock(2000)) { ESP_LOGW(TAG, "error setup: display busy for 2 s (send 'where')"); return; }
    int lines, bottom;
    bool ok = ui_setup_fail_sample(&lines, &bottom);
    display_unlock();
    if (!ok) { ESP_LOGW(TAG, "error setup fail: only on the Easy Connect page"); return; }
    ESP_LOGI(TAG, "setup fail lines=%d bottom=%d", lines, bottom);
}

static void cmd_dirty(int argc, char **argv)            // as new data does: hidden pictures out of date, shown redrawn
{
    if (!display_lock(2000)) { ESP_LOGW(TAG, "error dirty: display busy for 2 s"); return; }
    slide_cache_dirty_hidden(NULL);
    lv_obj_invalidate(lv_screen_active());
    display_unlock();
    ESP_LOGI(TAG, "ok dirty");
}

// "memlow start", then "memlow stop": the lowest free PSRAM and internal RAM in between (ESP-IDF's local minimum; the
// since-boot minimums are then put back, as the lower of the two). Meanwhile "heap" and the diag lines' "min ever"
// are the window's. The harness's alert test reads PSRAM's low point over the test alone.
static void cmd_memlow(int argc, char **argv)
{
    static bool on;
    if (argc == 2 && !strcmp(argv[1], "start")) {
        if (on) heap_caps_monitor_local_minimum_free_size_stop();
        on = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
        if (on) ESP_LOGI(TAG, "ok memlow start");
        else ESP_LOGW(TAG, "error memlow: could not start");
        return;
    }
    if (argc != 2 || strcmp(argv[1], "stop")) { ESP_LOGW(TAG, "error memlow start|stop"); return; }
    if (!on) { ESP_LOGW(TAG, "error memlow: not started"); return; }
    unsigned psram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024;
    unsigned internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;
    heap_caps_monitor_local_minimum_free_size_stop();
    on = false;
    ESP_LOGI(TAG, "memlow psram_min=%u internal_min=%u", psram, internal);
}

static void cmd_hint(int argc, char **argv)             // first-run hint, places kept
{
    if (argc != 2 || strcmp(argv[1], "next-boot")) { ESP_LOGW(TAG, "error hint next-boot"); return; }
    config_hint_next_boot();
    ESP_LOGI(TAG, "ok hint next-boot");
}

// "memspeed" (replaces forge_core's): copy speeds that bound full-screen drawing (PSRAM <-> internal, CPU memcpy
// and LVGL's lv_memcpy)
static void cmd_memspeed(int argc, char **argv)
{
    const size_t big = 466 * 466 * 2, band = 466 * 16 * 2;
    uint8_t *ps = heap_caps_malloc(big, MALLOC_CAP_SPIRAM), *ps2 = heap_caps_malloc(big, MALLOC_CAP_SPIRAM);
    uint8_t *in = heap_caps_malloc(band, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!ps || !ps2 || !in) { ESP_LOGW(TAG, "error memspeed: no memory"); goto out; }
    memset(ps, 0x55, big); memset(ps2, 0x55, big);
    int64_t t0 = esp_timer_get_time();
    for (size_t o = 0; o + band <= big; o += band) memcpy(in, ps + o, band);           // PSRAM -> internal
    int64_t t1 = esp_timer_get_time();
    for (size_t o = 0; o + band <= big; o += band) memcpy(ps2 + o, in, band);          // internal -> PSRAM
    int64_t t2 = esp_timer_get_time();
    for (size_t o = 0; o + band <= big; o += band) lv_memcpy(in, ps + o, band);
    int64_t t3 = esp_timer_get_time();
    memcpy(ps2, ps, big);                                                               // PSRAM -> PSRAM
    int64_t t4 = esp_timer_get_time();
    ESP_LOGI(TAG, "memspeed screen=%u B: psram_to_internal_ms=%.1f internal_to_psram_ms=%.1f lv_memcpy_ms=%.1f "
                  "psram_to_psram_ms=%.1f", (unsigned)big, (t1 - t0) / 1000.0f, (t2 - t1) / 1000.0f,
             (t3 - t2) / 1000.0f, (t4 - t3) / 1000.0f);
out:
    free(ps); free(ps2); free(in);
}

/* ---------- render bench ("diag: bench" lines) ---------- */

// Full-screen render timing of each screen. Invisible: other screens are rendered without being sent to the panel;
// the panel transfer is timed by repainting the (unchanged) weather screen. Blocks the UI for about 1.5 s.
static bool bench(void)
{
    lv_obj_t *scr[6];
    const char *name[6];
    const int N = 5;
    display_lock(-1);
    lv_obj_t *was = lv_screen_active();
    lv_indev_t *in = lv_indev_get_next(NULL);
    // Only while the weather screen is idle: the last step repaints it on the panel, which would flash over any other
    // screen (seen once over the hourly view)
    if (was != ui_main_screen() || (in && lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED) || lv_anim_count_running()) {
        display_unlock();
        ESP_LOGI("diag", "bench postponed (screen in use)");
        return false;
    }
    display_bench_no_panel(true);
    int n = ui_bench_screens(scr, name, 6);
    char line[160];
    int len = 0;
    for (int i = 0; i < n; i++) {
        lv_screen_load(scr[i]);
        lv_refr_now(NULL);                                          // warm the glyph cache
        int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < N; k++) { lv_obj_invalidate(scr[i]); lv_refr_now(NULL); }
        len += snprintf(line + len, sizeof(line) - len, " %s %.1f ms |", name[i],
                        (esp_timer_get_time() - t0) / 1000.0f / N);
    }
    lv_screen_load(ui_main_screen());
    lv_refr_now(NULL);
    display_bench_no_panel(false);
    int64_t t0 = esp_timer_get_time();
    for (int k = 0; k < N; k++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
    float with_panel = (esp_timer_get_time() - t0) / 1000.0f / N;
    display_unlock();
    ESP_LOGI("diag", "bench render-only full screen:%s", line);
    ESP_LOGI("diag", "bench weather incl. panel transfer: %.1f ms/frame", with_panel);
    return true;
}

static void bench_task(void *arg)
{
    bench();
    vTaskDelete(NULL);
}

// In its own task: rendering needs the LVGL task's stack size, not the console's (4 KB overflowed and reset the board)
static void cmd_bench(int argc, char **argv)
{
    xTaskCreatePinnedToCore(bench_task, "bench", 10240, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "ok bench started (result: \"diag: bench\" lines)");
}

/* ---------- breadcrumbs and the periodic display line ---------- */

// "where" (no lock): what the moves and the display code are doing now
static void where_display(char *out, size_t n)
{
    extern volatile int slide_phase, raw_phase, raw_band;
    extern int display_lvgl_inflight(void);
    snprintf(out, n, " slide_phase=%d raw_phase=%d raw_band=%d lvgl_inflight=%d", slide_phase, raw_phase, raw_band,
             display_lvgl_inflight());
}

// After each "diag: heap" line: frame timing and lock contention since the last one
static void diag_display(void)
{
    display_stats_t d;
    display_get_stats(&d, true);
    ESP_LOGI("diag", "display: %lu frames, render avg %.1f ms max %.1f ms, %.1f Mpx sent | "
             "animation %.1f fps (%lu frames, worst gap %.0f ms) | LVGL lock wait max %.1f ms, "
             "longest hold %.1f ms by %s",
             (unsigned long)d.frames, d.frames ? d.render_us / 1000.0 / d.frames : 0, d.render_max_us / 1000.0,
             d.pixels / 1e6, d.anim_us ? d.anim_frames * 1e6 / d.anim_us : 0, (unsigned long)d.anim_frames,
             d.anim_gap_max_us / 1000.0, d.lvgl_wait_max_us / 1000.0, d.hold_max_us / 1000.0,
             d.hold_task[0] ? d.hold_task : "-");
}

void console_init(void)
{
    testcon_register("screen", "screen", cmd_screen);
    testcon_register("power", "power", cmd_power);
    testcon_register("page", "page", cmd_page);
    testcon_register("tap", "tap X Y", cmd_tap);
    testcon_register("press", "press X Y [ms]", cmd_press);
    testcon_register("swipe", "swipe left|right|up|down", cmd_swipe);
    testcon_register("drag", "drag X1 Y1 X2 Y2 [ms]", cmd_drag);
    // (presence [calibrate N] and wake: forge_presence's, registered by presence_start())
    testcon_register("fps", "fps [reset]", cmd_fps);
#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
    testcon_register("profile", "profile", cmd_profile);
#endif
    testcon_register("pictest", "pictest", cmd_pictest);
    testcon_register("shot", "shot SCREEN [DIV]", cmd_shot);
#if CONFIG_IDF_TARGET_ESP32P4
    testcon_register("c6update", "c6update SIZE", cmd_c6update);
#endif
#if CONFIG_LWIP_STATS
    testcon_register("netstat", "netstat", cmd_netstat);
#endif
#if !DISP_SWAP
    testcon_register("blitbench", "blitbench", cmd_blitbench);
#endif
    testcon_register("alert", "alert sample en|fr|max|off, alert at LAT LON|off", cmd_alert);
    testcon_register("setup", "setup fail", cmd_setup);
    testcon_register("memlow", "memlow start|stop", cmd_memlow);
    testcon_register("dirty", "dirty", cmd_dirty);
    testcon_register("hint", "hint next-boot", cmd_hint);
    testcon_register("bench", "bench", cmd_bench);
    testcon_register("memspeed", "memspeed", cmd_memspeed);    // after testcon_start(): replaces the built-in
    testcon_add_where(where_display);
    diag_add_hook(diag_display);
}
