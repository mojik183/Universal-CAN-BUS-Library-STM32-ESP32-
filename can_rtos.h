/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_rtos.h
 * @brief   Optional FreeRTOS layer: thread safety, blocking receive, library task.
 *
 * Switch it on with CAN_CFG_USE_FREERTOS 1 in can_cfg.h. Nothing else changes in
 * your code: the same CanLib_xxx functions become safe to call from several tasks.
 *
 * What it adds (CAN_CFG_USE_FREERTOS 1):
 *   - A recursive mutex per instance around every public function.
 *   - A "frame received" semaphore, used by CanLib_ReceiveWait().
 *   - A library task per instance (CAN_CFG_RTOS_AUTO_TASK 1) that calls CanLib_Process().
 *     It wakes on CanRtos_NotifyFromISR(), on a queued frame, and every CAN_CFG_RTOS_POLL_MS.
 *   - Interrupt-safe critical sections for the ring buffers.
 *
 * Typical use:
 *
 *     CanLib_Init(&can1);                 // creates mutex, semaphore, task
 *     CanLib_Start(&can1);
 *     CanFrame_t f;
 *     if (CanLib_ReceiveWait(&can1, &f, 100) == CAN_OK) { ... }   // sleeps up to 100 ms
 *
 * With an MCP25xx the chip's INT pin should wake the library task:
 *
 *     void EXTI4_IRQHandler(void) { ...; CanRtos_NotifyFromISR(&can1); }
 *
 * Without the library task (CAN_CFG_RTOS_AUTO_TASK 0) you call CanLib_Process() yourself;
 * CanLib_ReceiveWait() then runs it for you while it waits, and CanRtos_NotifyFromISR()
 * wakes that waiting caller at once.
 *
 * Rules:
 *   - Only CanLib_OnRxFrame() and CanRtos_NotifyFromISR() may be called from an interrupt.
 *     Every other library call returns CAN_ERR_MUTEX there.
 *   - Do not call CanLib_DeInit() while other tasks still use the instance.
 *   - Callbacks run inside the library task (or in an interrupt for CanLib_OnRxFrame()),
 *     so keep them short. They may call CanLib_Send() (the mutex is recursive).
 *   - FreeRTOS is the only supported RTOS here.
 */
#ifndef CAN_RTOS_H
#define CAN_RTOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

/** Pass as timeout to CanLib_ReceiveWait() to wait without limit. */
#define CAN_WAIT_FOREVER    0xFFFFFFFFUL

/**
 * @brief  Wait for a received frame.
 *
 * Works with and without FreeRTOS. With FreeRTOS the caller sleeps on a semaphore.
 * Without it the function polls CanLib_Process() until the time is over (busy wait).
 * @param  can        Instance (running).
 * @param  frame      Filled with the oldest received frame.
 * @param  timeout_ms 0 = do not wait, CAN_WAIT_FOREVER = wait without limit.
 * @return CAN_OK when a frame was returned, CAN_ERR_RX_EMPTY when the time ran out,
 *         or another error from CanLib_Receive() / CanLib_Process().
 */
CanResult_e CanLib_ReceiveWait(CanLib_t *can, CanFrame_t *frame, uint32_t timeout_ms);

#if CAN_CFG_USE_FREERTOS

/**
 * @brief  Wake the library task from an interrupt (e.g. MCP25xx INT pin). Also safe in a task.
 * @param  can Instance. NULL or not initialized is ignored.
 */
void CanRtos_NotifyFromISR(CanLib_t *can);

/* ---- used by the library core, not by the application ---- */

/** Create the mutex and the semaphore of an instance. */
CanResult_e CanRtos_Create(CanLib_t *can);

/** Create the library task of an instance (only when CAN_CFG_RTOS_AUTO_TASK is 1). */
CanResult_e CanRtos_StartTask(CanLib_t *can);

/** Delete the task, semaphore and mutex of an instance. */
void CanRtos_Destroy(CanLib_t *can);

/** Take the instance mutex (recursive). Returns CAN_OK, CAN_ERR_MUTEX on timeout. No mutex yet: CAN_OK. */
CanResult_e CanRtos_Lock(CanLib_t *can);

/** Release the instance mutex. */
void CanRtos_Unlock(CanLib_t *can);

/** A frame was stored: wake the tasks waiting in CanLib_ReceiveWait() (ISR safe). */
void CanRtos_NotifyRx(CanLib_t *can);

/** Something is waiting for the library task (queued frame): wake it (ISR safe). */
void CanRtos_WakeTask(CanLib_t *can);

/** One round of the library task: wait for a wake-up or the poll time, then CanLib_Process(). */
void CanRtos_TaskStep(CanLib_t *can);

/* Lock macros used inside can_core.c. */
#define CAN_LOCK(can)       CanRtos_Lock(can)
#define CAN_UNLOCK(can)     CanRtos_Unlock(can)

#else /* no FreeRTOS: the macros vanish */

#define CAN_LOCK(can)       (CAN_OK)
#define CAN_UNLOCK(can)     ((void)(can))

#endif /* CAN_CFG_USE_FREERTOS */

#ifdef __cplusplus
}
#endif

#endif /* CAN_RTOS_H */
