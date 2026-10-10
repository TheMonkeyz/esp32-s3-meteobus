// What the screens ask of the rest of the firmware, answered for the browser: online, no setup network, no updates
// (Restart reloads the page). The microphones and the motion sensor are forge_presence itself (emu_main.c's hooks:
// main/audio.c on emu_audio.c, emu_imu.c); the speaker is sound.c with emu_audio.c. The service statuses are recorded as the firmware's are
// (svc_http), for the status page.
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "esp_timer.h"
#include "esp_err.h"
#include "net.h"
#include "ota.h"
#include "services.h"
#include "web.h"
#include "alerts.h"
#include "display.h"
#include "esp_system.h"
#include "testcon.h"

const char *esp_err_to_name(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_HTTP_CONNECT: return "ESP_ERR_HTTP_CONNECT";
    default: return "ESP_FAIL";
    }
}

int64_t esp_timer_get_time(void) { return (int64_t)(emscripten_get_now() * 1000.0); }

/* ---------- network: the browser's ---------- */
const char *net_setup_ap_pass(void) { return ""; }
bool net_is_connected(void) { return EM_ASM_INT({ return navigator.onLine ? 1 : 0; }); }
bool net_get_ip(char *out, size_t n) { snprintf(out, n, "browser"); return true; }
bool net_get_ssid(char *out, size_t n) { snprintf(out, n, "browser"); return true; }
bool net_in_portal(void) { return false; }
int net_ap_clients(void) { return 0; }
void net_setup_ap_start(void) {}
void net_setup_ap_stop(void) {}
void net_setup_ap_stop_any(void) {}
bool net_setup_ap_active(void) { return false; }
bool net_dpp_start(net_dpp_uri_cb_t on_uri, net_dpp_done_cb_t on_done) { (void)on_uri; (void)on_done; return false; }
void net_dpp_stop(void) {}
bool net_dpp_active(void) { return false; }

/* ---------- updates: none in the browser (the settings page's channel picker is remembered) ---------- */
static char channel[8] = "stable";
void ota_get_status(ota_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = OTA_UP_TO_DATE;
    snprintf(out->current, sizeof(out->current), "%s", EMU_VERSION);
    snprintf(out->channel, sizeof(out->channel), "%s", channel);
}
void ota_check_now(void) {}
bool ota_install(void) { return false; }
void ota_set_channel(const char *ch) { if (ch && (!strcmp(ch, "stable") || !strcmp(ch, "beta"))) snprintf(channel, sizeof(channel), "%s", ch); }
bool ota_pending_verify(void) { return false; }
void ota_get_notes(char *out, size_t size) { if (size) out[0] = 0; }
void ota_restart_when_safe(void) { esp_restart(); }   // Settings > Restart: the page reloads (settings are already saved)
void ota_set_err_text(const char *(*fn)(ota_err_t err)) { (void)fn; }

/* ---------- service statuses (forge_net's svc.h, recorded as the firmware does for the status page) ---------- */
static svc_info_t svc[SVC_MAX];
static int nsvc;
static const char *(*why_text)(svc_why_t, int);
int svc_add(const char *name, const char *api, svc_probe_url_t probe)
{
    (void)probe;
    if (nsvc >= SVC_MAX) return -1;
    svc[nsvc] = (svc_info_t){ .name = name, .api = api };
    return nsvc++;
}
int svc_count(void) { return nsvc; }
// The firmware's NTP row is the browser's clock here (emu_main adds it under this name)
int svc_find(const char *name)
{
    if (name && !strcmp(name, SVC_NAME_NTP)) name = "Browser clock";
    for (int i = 0; i < nsvc; i++) if (name && !strcmp(svc[i].name, name)) return i;
    return -1;
}
void svc_set_why_text(const char *(*fn)(svc_why_t, int)) { why_text = fn; }
void svc_http(int id, esp_err_t err, int status, int64_t t0)
{
    if (err == ESP_OK && status == 200) svc_ok(id, t0);
    else if (status > 0) { char why[40]; snprintf(why, sizeof(why), "HTTP %d", status); svc_fail(id, why, t0); }
    else svc_fail_why(id, SVC_WHY_CONNECT, t0);
}
void svc_fail_why(int id, svc_why_t code, int64_t t0)
{
    const char *t = why_text ? why_text(code, 0) : NULL;
    svc_fail(id, t ? t : "error", t0);
}
void svc_ok(int id, int64_t t0)
{
    if (id < 0 || id >= nsvc) return;
    svc[id].last_try = svc[id].last_ok = esp_timer_get_time();
    svc[id].ms = (int)((svc[id].last_try - t0) / 1000);
    svc[id].ok = true;
    svc[id].fails = 0;
}
void svc_fail(int id, const char *why, int64_t t0)
{
    if (id < 0 || id >= nsvc) return;
    svc[id].last_try = esp_timer_get_time();
    svc[id].ms = (int)((svc[id].last_try - t0) / 1000);
    svc[id].ok = false;
    svc[id].fails++;
    snprintf(svc[id].why, sizeof(svc[id].why), "%s", why);
}
void svc_get(int id, svc_info_t *out)
{
    static const svc_info_t none = { "", "" };
    *out = id >= 0 && id < nsvc ? svc[id] : none;
}
const char *svc_user_agent(void) { return "esp32-s3-meteobus emulator"; }   // (not sent: see emu_http.c)
void svc_probe_stale(void) {}

const char *web_key(void) { return "browser"; }

// forge_core's NVS check (config.c's saves): emu_nvs.c never fails
// The test console (forge_core's testcon): no USB console in the browser; forge_presence registers its commands
void testcon_register(const char *name, const char *usage, testcon_fn_t fn) { (void)name; (void)usage; (void)fn; }
void testcon_add_where(testcon_where_fn_t fn) { (void)fn; }

bool nvs_check(esp_err_t err, const char *what) { (void)what; return err == ESP_OK; }

/* ---------- the buses (MeteoBus): departures.c, rtc_api.c, favs.c and bus_routes.c are the firmware's own ---------- */
// netq.c only saves the board's internal RAM (one TLS download at a time) and pulls esp_wifi: here every download is a
// fetch() and the tasks take turns anyway
#include "netq.h"
void netq_set(netq_who_t who, bool busy) { (void)who; (void)busy; }
bool netq_others_busy(netq_who_t self) { (void)self; return false; }
bool netq_wait_others(int max_ms) { (void)max_ms; return true; }
void netq_awake(netq_who_t who, bool on) { (void)who; (void)on; }

/* ---------- Québec's clock, as the display's ---------- */
// main.c sets TZ=EST5EDT,M3.2.0,M11.1.0 and never changes it: localtime_r is Québec's time on the board (the stop
// pages' clock and departure times, departures.c's service date and the notices' query). Emscripten's localtime_r is
// the visitor's own zone (it ignores TZ), so a visitor in Vancouver saw departures 3 h early. The weather code uses
// each place's own offset (config_local_time), not this.
#include <time.h>
static long days_from_civil(int y, int m, int d)           // (rtc_api.c's: Howard Hinnant's algorithm)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
static time_t sunday_utc(int y, int m, int nth, int hour_utc)   // the nth Sunday of the month, at hour_utc
{
    long first = days_from_civil(y, m, 1);
    int wday = (int)((first % 7 + 11) % 7);                 // 1970-01-01 was a Thursday (4)
    return (time_t)((first + (7 - wday) % 7 + 7 * (nth - 1)) * 86400L + hour_utc * 3600L);
}
struct tm *localtime_r(const time_t *t, struct tm *out)
{
    struct tm g;
    gmtime_r(t, &g);
    int y = g.tm_year + 1900;
    // EDT from the 2nd Sunday of March, 2:00 EST (7:00 UTC), to the 1st Sunday of November, 2:00 EDT (6:00 UTC)
    bool dst = *t >= sunday_utc(y, 3, 2, 7) && *t < sunday_utc(y, 11, 1, 6);
    time_t local = *t + (dst ? -4 : -5) * 3600;
    gmtime_r(&local, out);
    out->tm_isdst = dst;
    out->tm_gmtoff = (dst ? -4 : -5) * 3600;
    out->tm_zone = dst ? "EDT" : "EST";
    return out;
}
struct tm *localtime(const time_t *t) { static struct tm tm; return localtime_r(t, &tm); }
