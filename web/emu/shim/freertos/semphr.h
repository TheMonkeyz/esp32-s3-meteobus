#pragma once
// Browser emulator: one thread, tasks are fibers (web/emu/emu_tasks.c). A mutex is always free (nothing holds one
// across a wait). A binary semaphore is real: departures.c's job_done, which the settings page's stop lookups
// (POST /api/favs, /api/route) wait on while the radar task asks RTC; its wait yields as vTaskDelay does.
#include "freertos/FreeRTOS.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return (SemaphoreHandle_t)1; }
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t emu_sem_take(SemaphoreHandle_t s, TickType_t ms);
BaseType_t emu_sem_give(SemaphoreHandle_t s);
#define xSemaphoreTake(s, t) emu_sem_take((s), (t))
#define xSemaphoreGive(s) emu_sem_give(s)
#define xSemaphoreTakeRecursive(s, t) pdTRUE
#define xSemaphoreGiveRecursive(s) pdTRUE
