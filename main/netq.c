// Who is downloading now (see netq.h)
#include "netq.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "net.h"

static unsigned busy;                    // NETQ_* bits
static unsigned awake;                   // NETQ_* bits: map screens open (netq_awake)

void netq_set(netq_who_t who, bool on)
{
    if (on) __atomic_fetch_or(&busy, (unsigned)who, __ATOMIC_ACQ_REL);
    else __atomic_fetch_and(&busy, ~(unsigned)who, __ATOMIC_ACQ_REL);
}

void netq_awake(netq_who_t who, bool on)
{
    unsigned was = awake;
    awake = on ? was | (unsigned)who : was & ~(unsigned)who;
    if (!was == !awake || net_dpp_active()) return;
    esp_wifi_set_ps(awake ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    ESP_LOGI("netq", "Wi-Fi power save %s", awake ? "off (a map is open)" : "on");
}

bool netq_others_busy(netq_who_t self)
{
    return (__atomic_load_n(&busy, __ATOMIC_ACQUIRE) & ~(unsigned)self) != 0;
}

bool netq_wait_others(int max_ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)max_ms * 1000;
    while (__atomic_load_n(&busy, __ATOMIC_ACQUIRE)) {
        if (esp_timer_get_time() > end) return false;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return true;
}
