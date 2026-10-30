/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_rtos.c
 * @brief   Optional FreeRTOS layer (mutex, rx semaphore, library task, critical sections)
 *          and the blocking CanLib_ReceiveWait() that also works without FreeRTOS.
 */
#include "can_lib.h"

#if CAN_CFG_USE_FREERTOS

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32)
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
#else
    #if (CAN_CFG_PLATFORM == CAN_PLATFORM_STM32)
        #include CAN_CFG_STM32_HAL_HEADER       /* For __get_IPSR() used by CAN_CFG_RTOS_IN_ISR(). */
    #endif
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
#endif

/** Longest name copied into the task name (the instance digit is appended). */
#define CAN_RTOS_NAME_LEN       6U

/** Stack depth in the unit xTaskCreate() expects: words on STM32, bytes on ESP32. */
#if (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32)
    #define CAN_RTOS_STACK_DEPTH    (CAN_CFG_RTOS_TASK_STACK_WORDS * 4U)
#else
    #define CAN_RTOS_STACK_DEPTH    CAN_CFG_RTOS_TASK_STACK_WORDS
#endif

/* ======================================================================== */
/* CRITICAL SECTION                                                         */
/* ======================================================================== */

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32)

/** ESP32 critical sections need a spinlock. */
static portMUX_TYPE s_can_mux = portMUX_INITIALIZER_UNLOCKED;

void CanRtos_EnterCritical(void)
{
    if (CAN_CFG_RTOS_IN_ISR())
    {
        portENTER_CRITICAL_ISR(&s_can_mux);
    }
    else
    {
        portENTER_CRITICAL(&s_can_mux);
    }
}

void CanRtos_ExitCritical(void)
{
    if (CAN_CFG_RTOS_IN_ISR())
    {
        portEXIT_CRITICAL_ISR(&s_can_mux);
    }
    else
    {
        portEXIT_CRITICAL(&s_can_mux);
    }
}

#else

/** Interrupt mask saved by the ISR variant (an interrupt does not nest with itself). */
static UBaseType_t s_can_isr_state;

void CanRtos_EnterCritical(void)
{
    if (CAN_CFG_RTOS_IN_ISR())
    {
        s_can_isr_state = taskENTER_CRITICAL_FROM_ISR();
    }
    else
    {
        taskENTER_CRITICAL();
    }
}

void CanRtos_ExitCritical(void)
{
    if (CAN_CFG_RTOS_IN_ISR())
    {
        taskEXIT_CRITICAL_FROM_ISR(s_can_isr_state);
    }
    else
    {
        taskEXIT_CRITICAL();
    }
}

#endif

/* ======================================================================== */
/* OBJECTS: MUTEX, SEMAPHORE                                                */
/* ======================================================================== */

CanResult_e CanRtos_Create(CanLib_t *can)
{
    SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    SemaphoreHandle_t s = xSemaphoreCreateBinary();

    if ((m == NULL) || (s == NULL))
    {
        if (m != NULL) { vSemaphoreDelete(m); }
        if (s != NULL) { vSemaphoreDelete(s); }
        return CAN_ERR_RTOS;
    }
    can->rtos.mutex            = m;
    can->rtos.rx_sem           = s;
    can->rtos.task             = NULL;
    can->rtos.stack_free_words = 0U;
    can->rtos.wake_count       = 0U;
    can->rtos.lock_timeouts    = 0U;
    return CAN_OK;
}

void CanRtos_Destroy(CanLib_t *can)
{
    SemaphoreHandle_t m = (SemaphoreHandle_t)can->rtos.mutex;
    SemaphoreHandle_t s = (SemaphoreHandle_t)can->rtos.rx_sem;

    if (m != NULL)
    {
        /* Take the lock so the task is not inside the library while it is deleted. */
        (void)xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(CAN_CFG_RTOS_MUTEX_TIMEOUT_MS));
    }
    if (can->rtos.task != NULL)
    {
        vTaskDelete((TaskHandle_t)can->rtos.task);
        can->rtos.task = NULL;
    }
    if (m != NULL)
    {
        (void)xSemaphoreGiveRecursive(m);
    }
    can->rtos.mutex  = NULL;
    can->rtos.rx_sem = NULL;
    if (s != NULL) { vSemaphoreDelete(s); }
    if (m != NULL) { vSemaphoreDelete(m); }
}

CanResult_e CanRtos_Lock(CanLib_t *can)
{
    SemaphoreHandle_t m;

    if (can == NULL)
    {
        return CAN_OK;                                  /* The function itself reports the NULL pointer. */
    }
    m = (SemaphoreHandle_t)can->rtos.mutex;
    if (m == NULL)
    {
        return CAN_OK;                                  /* Not initialized yet: the function reports that. */
    }
    if (CAN_CFG_RTOS_IN_ISR())
    {
        return CAN_ERR_MUTEX;                           /* Library calls are not allowed in an interrupt. */
    }
    if (xSemaphoreTakeRecursive(m, pdMS_TO_TICKS(CAN_CFG_RTOS_MUTEX_TIMEOUT_MS)) != pdTRUE)
    {
        CAN_CFG_ENTER_CRITICAL();                       /* The mutex is not ours: protect the counter another way. */
        can->rtos.lock_timeouts++;
        CAN_CFG_EXIT_CRITICAL();
        return CAN_ERR_MUTEX;
    }
    return CAN_OK;
}

void CanRtos_Unlock(CanLib_t *can)
{
    if ((can != NULL) && (can->rtos.mutex != NULL) && !CAN_CFG_RTOS_IN_ISR())
    {
        (void)xSemaphoreGiveRecursive((SemaphoreHandle_t)can->rtos.mutex);
    }
}

/* ======================================================================== */
/* WAKE-UPS                                                                 */
/* ======================================================================== */

void CanRtos_NotifyRx(CanLib_t *can)
{
    SemaphoreHandle_t s = (SemaphoreHandle_t)can->rtos.rx_sem;

    if (s == NULL)
    {
        return;
    }
    if (CAN_CFG_RTOS_IN_ISR())
    {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(s, &woken);
        portYIELD_FROM_ISR(woken);
    }
    else
    {
        (void)xSemaphoreGive(s);
    }
}

void CanRtos_WakeTask(CanLib_t *can)
{
    TaskHandle_t t = (TaskHandle_t)can->rtos.task;

    if (t == NULL)
    {
#if !CAN_CFG_RTOS_AUTO_TASK
        CanRtos_NotifyRx(can);          /* No library task: wake the CanLib_ReceiveWait() caller so it runs Process(). */
#endif
        return;
    }
    if (CAN_CFG_RTOS_IN_ISR())
    {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(t, &woken);
        portYIELD_FROM_ISR(woken);
    }
    else
    {
        (void)xTaskNotifyGive(t);
    }
}

void CanRtos_NotifyFromISR(CanLib_t *can)
{
    if ((can != NULL) && (can->magic == CAN_LIB_MAGIC))
    {
        CanRtos_WakeTask(can);
    }
}

/* ======================================================================== */
/* LIBRARY TASK                                                             */
/* ======================================================================== */

void CanRtos_TaskStep(CanLib_t *can)
{
    (void)ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(CAN_CFG_RTOS_POLL_MS));   /* One wake-up per notification. */

    if (CAN_IS_ERR(CanRtos_Lock(can)))
    {
        return;                                         /* Busy: try again next round. */
    }
    can->rtos.wake_count++;
    can->rtos.stack_free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if ((can->magic == CAN_LIB_MAGIC) && (can->status.state == CAN_STATE_RUNNING))
    {
        (void)CanLib_Process(can);                      /* Problems are reported inside Process(). */
    }
    CanRtos_Unlock(can);
}

#if CAN_CFG_RTOS_AUTO_TASK
/** Body of the library task: run forever until the instance is de-initialized (task is deleted). */
static void can_rtos_task(void *arg)
{
    CanLib_t *can = (CanLib_t *)arg;

    for (;;)
    {
        CanRtos_TaskStep(can);
    }
}
#endif

CanResult_e CanRtos_StartTask(CanLib_t *can)
{
#if CAN_CFG_RTOS_AUTO_TASK
    char       name[CAN_RTOS_NAME_LEN + 2U];
    const char *src = CAN_CFG_RTOS_TASK_NAME;
    uint8_t    i = 0U;
    TaskHandle_t handle = NULL;

    while ((src[i] != '\0') && (i < CAN_RTOS_NAME_LEN))
    {
        name[i] = src[i];
        i++;
    }
    name[i]      = (char)('0' + (can->instance % 10U));   /* "can0", "can1", ... */
    name[i + 1U] = '\0';

    if (xTaskCreate(can_rtos_task, name, (configSTACK_DEPTH_TYPE)CAN_RTOS_STACK_DEPTH, can,
                    (UBaseType_t)CAN_CFG_RTOS_TASK_PRIORITY, &handle) != pdPASS)
    {
        return CAN_ERR_RTOS;
    }
    can->rtos.task = handle;
    return CAN_OK;
#else
    (void)can;
    return CAN_OK;                                      /* The application calls CanLib_Process() itself. */
#endif
}

#endif /* CAN_CFG_USE_FREERTOS */

/* ======================================================================== */
/* BLOCKING RECEIVE (with or without FreeRTOS)                              */
/* ======================================================================== */

CanResult_e CanLib_ReceiveWait(CanLib_t *can, CanFrame_t *frame, uint32_t timeout_ms)
{
    uint32_t start = CanLib_GetTickMs();

    for (;;)
    {
        CanResult_e r = CanLib_Receive(can, frame);
        uint32_t    elapsed;
        uint32_t    remaining;

        if (r != CAN_ERR_RX_EMPTY)
        {
            return r;                                   /* A frame, or a real error. */
        }

        elapsed = CanLib_GetTickMs() - start;
        if ((timeout_ms != CAN_WAIT_FOREVER) && (elapsed >= timeout_ms))
        {
            return CAN_ERR_RX_EMPTY;                    /* Time is over. */
        }
        remaining = (timeout_ms == CAN_WAIT_FOREVER) ? CAN_WAIT_FOREVER : (timeout_ms - elapsed);

#if CAN_CFG_USE_FREERTOS
        {
            TickType_t ticks;

#if CAN_CFG_RTOS_AUTO_TASK
            ticks = (remaining == CAN_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(remaining);
#else
            /* No library task: wake up regularly and run the process loop here. */
            ticks = pdMS_TO_TICKS((remaining > CAN_CFG_RTOS_POLL_MS) ? CAN_CFG_RTOS_POLL_MS : remaining);
#endif
            if (can->rtos.rx_sem != NULL)
            {
                (void)xSemaphoreTake((SemaphoreHandle_t)can->rtos.rx_sem, ticks);
            }
#if !CAN_CFG_RTOS_AUTO_TASK
            r = CanLib_Process(can);
            if (CAN_IS_ERR(r))
            {
                return r;
            }
#endif
        }
#else
        (void)remaining;
        r = CanLib_Process(can);                        /* Bare metal: poll the controller. */
        if (CAN_IS_ERR(r))
        {
            return r;
        }
#endif
    }
}
