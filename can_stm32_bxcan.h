/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_stm32_bxcan.h
 * @brief   STM32 bxCAN backend (classic CAN) on top of the ST HAL CAN driver.
 *
 * Setup in the application (CubeMX generates hcan1 and its pins and clock;
 * leave the CAN settings of CubeMX at default, the library sets bit timing):
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);
 *     can1.hw.handle = &hcan1;             // CAN_HandleTypeDef*
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 * The bit timing is computed from the APB1 clock (HAL_RCC_GetPCLK1Freq()).
 * The hardware filter accepts everything; software filters decide.
 */
#ifndef CAN_STM32_BXCAN_H
#define CAN_STM32_BXCAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_BACKEND_STM32_BXCAN

/** Limits of the bxCAN bit timing registers. */
#define BXCAN_PRESCALER_MAX     1024U   /**< Largest prescaler. */
#define BXCAN_TQ_MIN            4U      /**< Fewest time quanta in one bit used by the search. */
#define BXCAN_TQ_MAX            25U     /**< Most time quanta in one bit (1 + BS1 16 + BS2 8). */
#define BXCAN_BS1_MAX           16U     /**< Largest time segment 1. */
#define BXCAN_BS2_MAX           8U      /**< Largest time segment 2. */

/** @brief Backend state of one bxCAN instance (visible in the watch window as can->port_ctx). */
typedef struct
{
    bool     inited;        /**< true = init() finished. */
    uint32_t last_esr;      /**< ESR register at the last get_errors() (debug). */
    uint32_t last_mailbox;  /**< Mailbox used by the last transmit (debug). */
    uint32_t hal_error;     /**< HAL error code of the last failed HAL call (debug). */
    uint32_t hal_failures;  /**< Number of failed HAL calls (debug). */
} CanBxcanCtx_t;

/**
 * @brief  Calculate bxCAN bit timing for a bitrate.
 * @param  clk_hz  APB1 (CAN kernel) clock in Hz.
 * @param  bitrate Requested bitrate in bit/s.
 * @param  sample_point Requested sample point in 1/1000.
 * @param  out     Filled with prescaler (1..1024), tseg1 (BS1), tseg2 (BS2), sjw, actual_bitrate.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_PARAM, or CAN_ERR_BITRATE when no exact timing exists.
 */
CanResult_e CanBxcan_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out);

/**
 * @brief  Move every frame waiting in RX FIFO0 into the library (for interrupt mode).
 *
 * Call it from HAL_CAN_RxFifo0MsgPendingCallback() when CAN_CFG_BXCAN_USE_IRQ is 1:
 *     void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h) { CanBxcan_OnRxFifo0Pending(&can1); }
 * @param  can Instance that owns the HAL handle.
 */
void CanBxcan_OnRxFifo0Pending(CanLib_t *can);

/**
 * @brief  Get the private data of an initialized bxCAN instance (for watch / tests).
 * @param  can Instance.
 * @return Context, or NULL when the instance is not an initialized bxCAN backend.
 */
const CanBxcanCtx_t *CanBxcan_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_STM32_BXCAN */

#ifdef __cplusplus
}
#endif

#endif /* CAN_STM32_BXCAN_H */
