/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_esp32_twai.h
 * @brief   ESP32 TWAI (built-in CAN controller) backend on top of the ESP-IDF twai driver.
 *
 * Wiring: GPIO TX and RX go to a CAN transceiver (TJA10xx, or an isolated ISO10xx).
 *
 * Setup in the application:
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI);
 *     can1.hw.tx_gpio = 5;
 *     can1.hw.rx_gpio = 4;
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 * Notes:
 *   - Only one TWAI instance at a time (the ESP-IDF driver used here is single controller).
 *   - CANLIB_MODE_LOOPBACK uses TWAI "no ack" mode with self reception: the frame
 *     is sent and received back without any other node on the bus.
 *   - Not supported: CAN FD.
 */
#ifndef CAN_ESP32_TWAI_H
#define CAN_ESP32_TWAI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_BACKEND_ESP32_TWAI

/** Limits of the TWAI bit timing registers. */
#define TWAI_LIB_TQ_MIN         4U      /**< Fewest time quanta in one bit used by the search. */
#define TWAI_LIB_TSEG1_MAX      16U     /**< Largest time segment 1. */
#define TWAI_LIB_TSEG2_MAX      8U      /**< Largest time segment 2. */
#define TWAI_LIB_SJW_MAX        3U      /**< Largest synchronization jump width used. */

/** @brief Backend state of the TWAI controller (visible in the watch window as can->port_ctx). */
typedef struct
{
    bool     inited;            /**< true = init() finished (driver installed). */
    int      last_esp_err;      /**< Last non-OK esp_err_t from the driver (debug). */
    uint32_t driver_failures;   /**< Failed driver calls (debug). */
    uint32_t bus_errors;        /**< Bus error count reported by the driver (debug). */
    uint32_t missed_seen;       /**< rx_missed + rx_overrun already added to the statistics. */
    uint32_t tx_failed;         /**< Frames the driver gave up sending (debug). */
    uint32_t arb_lost;          /**< Lost arbitrations (debug). */
} CanTwaiCtx_t;

/**
 * @brief  Calculate TWAI bit timing for a bitrate.
 * @param  clk_hz  Controller clock in Hz (CAN_CFG_TWAI_CLK_HZ).
 * @param  bitrate Requested bitrate in bit/s.
 * @param  sample_point Requested sample point in 1/1000.
 * @param  out     Filled with prescaler (even, 2..CAN_CFG_TWAI_BRP_MAX), tseg1, tseg2, sjw, actual_bitrate.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_PARAM, or CAN_ERR_BITRATE when no exact timing exists.
 */
CanResult_e CanTwai_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out);

/**
 * @brief  Get the private data of the initialized TWAI instance (for watch / tests).
 * @param  can Instance.
 * @return Context, or NULL when the instance is not an initialized TWAI backend.
 */
const CanTwaiCtx_t *CanTwai_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_ESP32_TWAI */

#ifdef __cplusplus
}
#endif

#endif /* CAN_ESP32_TWAI_H */
