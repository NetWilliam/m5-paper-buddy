// Stub freertos/FreeRTOS.h for host-side testing
#pragma once
#include <cstdint>

typedef void* SemaphoreHandle_t;
typedef void* EventGroupHandle_t;
typedef uint32_t EventBits_t;
typedef int BaseType_t;

#define pdTRUE       1
#define pdFALSE      0
#define pdMS_TO_TICKS(ms) (ms)
#define portTICK_PERIOD_MS 10

extern "C" uint32_t xTaskGetTickCount();

static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, uint32_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }

static inline EventGroupHandle_t xEventGroupCreate() { return (EventGroupHandle_t)1; }
static inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, uint32_t) { return 0; }
static inline EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t) { return 0; }
static inline EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t) { return 0; }
