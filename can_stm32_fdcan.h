/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_stm32_fdcan.h
 * @brief   STM32 FDCAN backend (classic CAN and CAN FD) on top of the ST HAL FDCAN driver.
 *
 * Works on every STM32 with the FDCAN peripheral. G0, G4, L5, U5 and H5 have a fixed message RAM
 * layout (CAN_CFG_FDCAN_MESSAGE_RAM 0). The STM32H7 shares 10 KB of message RAM between its FDCAN
 * units and the HAL wants the layout in FDCAN_InitTypeDef: set CAN_CFG_FDCAN_MESSAGE_RAM 1 and the
 * library fills it (offset per instance, RX FIFO0 and TX FIFO sizes from can_cfg.h).
 *
 * Setup in the application (CubeMX generates hfdcan1, its pins and the kernel clock;
 * enter the kernel clock in CAN_CFG_FDCAN_CLK_HZ; leave the CAN settings at default,
 * the library sets bit timing and mode):
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN);
 *     can1.hw.handle = &hfdcan1;           // FDCAN_HandleTypeDef*
 *     #if CAN_CFG_ENABLE_FD
 *     can1.cfg.fd_enabled = true;          // CAN FD with bit rate switching
 *     #endif
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 * The hardware accepts every frame into RX FIFO0; software filters decide.
 */
#ifndef CAN_STM32_FDCAN_H
#define CAN_STM32_FDCAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_BACKEND_STM32_FDCAN

/** Limits of the nominal (arbitration) bit timing registers. */
#define FDCAN_LIB_NOM_PSC_MAX       512U    /**< Largest nominal prescaler. */
#define FDCAN_LIB_NOM_SEG1_MAX      256U    /**< Largest nominal time segment 1. */
#define FDCAN_LIB_NOM_SEG2_MAX      128U    /**< Largest nominal time segment 2. */
#define FDCAN_LIB_NOM_TQ_CAP        80U     /**< Most quanta per nominal bit used by the search. */

/** Limits of the data phase bit timing registers. */
#define FDCAN_LIB_DAT_PSC_MAX       32U     /**< Largest data prescaler. */
#define FDCAN_LIB_DAT_SEG1_MAX      32U     /**< Largest data time segment 1. */
#define FDCAN_LIB_DAT_SEG2_MAX      16U     /**< Largest data time segment 2. */

/** Data bit rate from which the transmitter delay compensation is switched on (bit/s). */
#define FDCAN_LIB_TDC_MIN_BITRATE   2000000UL

/** @brief Backend state of one FDCAN instance (visible in the watch window as can->port_ctx). */
typedef struct
{
    bool     inited;            /**< true = init() finished. */
    bool     fd_mode;           /**< true = the peripheral runs with CAN FD and bit rate switching. */
    uint32_t last_psr;          /**< Raw protocol status flags of the last get_errors() (debug). */
    uint32_t hal_error;         /**< HAL error code of the last failed HAL call (debug). */
    uint32_t hal_failures;      /**< Number of failed HAL calls (debug). */
} CanFdcanCtx_t;

/**
 * @brief  Calculate FDCAN bit timing for one phase.
 * @param  clk_hz  FDCAN kernel clock in Hz (CAN_CFG_FDCAN_CLK_HZ).
 * @param  bitrate Requested bitrate in bit/s.
 * @param  sample_point Requested sample point in 1/1000.
 * @param  data_phase false = nominal phase limits, true = data phase limits.
 * @param  out     Filled with prescaler, tseg1, tseg2, sjw, actual_bitrate.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_PARAM, or CAN_ERR_BITRATE when no exact timing exists.
 */
CanResult_e CanFdcan_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point,
                                bool data_phase, CanTiming_t *out);

/**
 * @brief  Move every frame waiting in RX FIFO0 into the library (for interrupt mode).
 *
 * Call it from the HAL callback when CAN_CFG_FDCAN_USE_IRQ is 1:
 *     void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t it) { CanFdcan_OnRxFifo0Pending(&can1); }
 * @param  can Instance that owns the HAL handle.
 */
void CanFdcan_OnRxFifo0Pending(CanLib_t *can);

/**
 * @brief  Get the private data of an initialized FDCAN instance (for watch / tests).
 * @param  can Instance.
 * @return Context, or NULL when the instance is not an initialized FDCAN backend.
 */
const CanFdcanCtx_t *CanFdcan_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_STM32_FDCAN */

#ifdef __cplusplus
}
#endif

#endif /* CAN_STM32_FDCAN_H */
