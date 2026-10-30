/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 */
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, configSTACK_DEPTH_TYPE depth, void *arg, UBaseType_t prio, TaskHandle_t *out);
void vTaskDelete(TaskHandle_t t);
BaseType_t xTaskNotifyGive(TaskHandle_t t);
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
void fake_critical_enter(void);
void fake_critical_exit(void);
#define taskENTER_CRITICAL()            fake_critical_enter()
#define taskEXIT_CRITICAL()             fake_critical_exit()
#define taskENTER_CRITICAL_FROM_ISR()   (fake_critical_enter(), (UBaseType_t)0)
#define taskEXIT_CRITICAL_FROM_ISR(x)   do { (void)(x); fake_critical_exit(); } while (0)
/* Test helpers. */
int fake_task_count(void);
