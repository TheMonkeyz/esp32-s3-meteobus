// Who is downloading now (see netq.h)
#include "netq.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static unsigned busy;                    // NETQ_* bits

void netq_set(netq_who_t who, bool on)
{
    if (on) __atomic_fetch_or(&busy, (unsigned)who, __ATOMIC_ACQ_REL);
    else __atomic_fetch_and(&busy, ~(unsigned)who, __ATOMIC_ACQ_REL);
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
