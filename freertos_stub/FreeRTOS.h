/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Test-only stand-in for FreeRTOS on top of pthreads (1 tick = 1 ms). Never add it to a firmware project.
 */
#ifndef STUB_FREERTOS_H
#define STUB_FREERTOS_H
#include <stdint.h>
#include <stddef.h>

typedef long         BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t     TickType_t;
typedef uint32_t     configSTACK_DEPTH_TYPE;
typedef void        *TaskHandle_t;
typedef void        *SemaphoreHandle_t;

#define pdFALSE             ((BaseType_t)0)
#define pdTRUE              ((BaseType_t)1)
#define pdPASS              pdTRUE
#define pdMS_TO_TICKS(ms)   ((TickType_t)(ms))
#define portMAX_DELAY       ((TickType_t)0xFFFFFFFFUL)
#define portYIELD_FROM_ISR(x)   ((void)(x))

/** Set to 1 in a thread to make it behave like an interrupt handler (tests only). */
extern __thread int fake_in_isr;
#define portMUX_INITIALIZER_UNLOCKED        { 0 }
#define portENTER_CRITICAL(m)               ((void)(m))
#define portEXIT_CRITICAL(m)                ((void)(m))
#define portENTER_CRITICAL_ISR(m)           ((void)(m))
#define portEXIT_CRITICAL_ISR(m)            ((void)(m))
typedef struct { int dummy; } portMUX_TYPE;   /* Compile check of the ESP32 branch only. */
int xPortInIsrContext(void);                  /* Compile check of the ESP32 branch only. */
#endif
