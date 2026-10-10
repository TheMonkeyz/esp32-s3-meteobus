// The settings page's bus routes: the favourite stops (GET/POST /api/favs) and a route's directions (POST /api/route),
// from esp32-s3-rtcquebec's main.c. Every RTC request goes through departures.c's task (its polling rules).
#include "bus_routes.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "web.h"
#include "departures.h"
#include "favs.h"
#include "ui.h"
#include "radar.h"

static const char *TAG = "bus";

static cJSON *fav_json(const rtc_fav_t *f)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "stop", f->stop);
    cJSON_AddStringToObject(o, "route", f->route);
    cJSON_AddStringToObject(o, "dir", f->dir);
    return o;
}

// GET /api/favs: {"max":8,"favs":[{"stop","route","dir","stop_name","direction"}]} (names once fetched)
static esp_err_t favs_get(httpd_req_t *req)
{
    cJSON *j = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(j, "favs");
    cJSON_AddNumberToObject(j, "max", FAVS_MAX);
    dep_entry_t *e = heap_caps_malloc(sizeof(*e), MALLOC_CAP_SPIRAM);
    for (int i = 0; e && deps_get(i, e); i++) {
        cJSON *o = fav_json(&e->fav);
        if (e->state == DEP_OK) {
            cJSON_AddStringToObject(o, "stop_name", e->board.stop_name);
            cJSON_AddStringToObject(o, "direction", e->board.direction);
        }
        cJSON_AddItemToArray(a, o);
    }
    free(e);
    return web_send_json(req, j);
}

// POST /api/route {"route":"800"}: its two directions, asked from RTC.
// {"ok":true,"route","name","dirs":[{"code","name"},...]}, or {"ok":false,"why":"no_route"|"rtc"} (always 200: a
// refusal is an answer, like POST /api/favs's).
static esp_err_t route_post(httpd_req_t *req)
{
    cJSON *in = web_read_json(req);
    char route[8] = "";
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItem(in, "route"));
    if (s) strlcpy(route, s, sizeof(route));
    cJSON_Delete(in);
    rtc_route_t *r = heap_caps_calloc(1, sizeof(*r), MALLOC_CAP_SPIRAM);
    if (!r) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "memory");
    int got = route[0] ? deps_lookup_route(route, r) : 0;
    esp_err_t ret;
    if (got == 1) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "ok", true);
        cJSON *a = cJSON_AddArrayToObject(j, "dirs");
        cJSON_AddStringToObject(j, "route", r->route);
        cJSON_AddStringToObject(j, "name", r->name);
        for (int k = 0; k < 2; k++) {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddStringToObject(d, "code", r->dir_code[k]);
            cJSON_AddStringToObject(d, "name", r->dir_name[k]);
            cJSON_AddItemToArray(a, d);
        }
        ret = web_send_json(req, j);
    } else {
        ret = httpd_resp_sendstr(req, got == 0 ? "{\"ok\":false,\"why\":\"no_route\"}" : "{\"ok\":false,\"why\":\"rtc\"}");
    }
    free(r);
    return ret;
}

// POST /api/favs {"favs":[{"stop","route","dir"},...]}: the whole list, in page order. A favourite not in the current
// list is checked with RTC first. {"ok":true} or {"ok":false,"bad":<index>,"why":"invalid"|"not_served"|"rtc"|"save"}.
static esp_err_t favs_post(httpd_req_t *req)
{
    cJSON *in = web_read_json(req);
    const cJSON *arr = cJSON_GetObjectItem(in, "favs");
    rtc_fav_t f[FAVS_MAX], cur[FAVS_MAX];
    int n = 0, ncur = 0, bad = -1;
    const char *why = "invalid";
    dep_entry_t *e = heap_caps_malloc(sizeof(*e), MALLOC_CAP_SPIRAM);
    while (e && ncur < FAVS_MAX && deps_get(ncur, e)) cur[ncur++] = e->fav;
    free(e);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) > FAVS_MAX) bad = 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (bad >= 0 || n == FAVS_MAX) break;
        rtc_fav_t x = {0};
        const char *s;
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "stop")))) strlcpy(x.stop, s, sizeof(x.stop));
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "route")))) strlcpy(x.route, s, sizeof(x.route));
        if ((s = cJSON_GetStringValue(cJSON_GetObjectItem(o, "dir")))) strlcpy(x.dir, s, sizeof(x.dir));
        if (!rtc_fav_valid(&x)) { bad = n; break; }
        f[n++] = x;
    }
    cJSON_Delete(in);
    for (int i = 0; i < n && bad < 0; i++) {
        bool known = false;
        for (int k = 0; k < ncur && !known; k++) known = !memcmp(&cur[k], &f[i], sizeof(rtc_fav_t));
        if (known) continue;
        rtc_board_t *b = heap_caps_malloc(sizeof(*b), MALLOC_CAP_SPIRAM);
        int r = b ? deps_check_fav(&f[i], b) : -1;
        free(b);
        if (r != 1) { bad = i; why = r == 0 ? "not_served" : "rtc"; }
    }
    if (bad < 0 && !favs_save(f, n)) { bad = 0; why = "save"; }
    if (bad >= 0) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "ok", false);
        cJSON_AddNumberToObject(j, "bad", bad);
        cJSON_AddStringToObject(j, "why", why);
        ESP_LOGW(TAG, "favourites refused: #%d %s", bad, why);
        return web_send_json(req, j);
    }
    ESP_LOGI(TAG, "%d favourite stop(s) saved", n);
    deps_set_favs(f, n);
    ui_favs_changed();
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// The stops may be changed from the setup network too: its password on the display is the same proof as the key in
// the settings QR code (rtcquebec: a phone that joined it through Wi-Fi setup got 403 on "Find directions")
static const web_route_t routes[] = {
    { "/api/favs",  HTTP_GET,  favs_get, .keyed = false },
    { "/api/favs",  HTTP_POST, favs_post, .keyed = true },
    { "/api/route", HTTP_POST, route_post, .keyed = true },
};

void bus_start(void)
{
    deps_start(ui_deps_changed, radar_side_wake);   // its requests run in the radar task's idle time
    rtc_fav_t f[FAVS_MAX];
    int n = favs_load(f);
    deps_set_favs(f, n);
    ESP_LOGI(TAG, "%d favourite stop(s)", n);
}

void bus_routes_init(void)
{
    web_add_routes(routes, sizeof(routes) / sizeof(routes[0]));
}
