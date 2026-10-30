/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_port.h
 * @brief   Backend interface: the function table every CAN controller driver provides.
 *
 * The core (can_core.c) never touches hardware. It calls the functions in
 * CanPortOps_t. To support a new chip, fill one CanPortOps_t and add it to
 * CanPort_Get() in can_port.c.
 *
 * Rules for backend authors:
 *   - Return a CanResult_e. Do NOT call CAN_REPORT: the core reports for you,
 *     so each problem is counted once.
 *   - init() reads can->cfg and can->hw and may store private data in can->port_ctx.
 *   - To hand a received frame to the library from an interrupt, call
 *     CanLib_OnRxFrame(). To only supply frames when asked, return them from receive().
 */
#ifndef CAN_PORT_H
#define CAN_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"
#include "can_debug.h"

/**
 * @brief Function table of one backend (one CAN controller family).
 */
struct CanPortOps_s
{
    const char *name;   /**< Backend name for log lines, e.g. "virtual". */

    /** Configure the controller (bit timing, mode, pins). Not on the bus yet. */
    CanResult_e (*init)(CanLib_t *can);

    /** Release the controller. May be NULL when nothing needs releasing. */
    CanResult_e (*deinit)(CanLib_t *can);

    /** Join the bus. */
    CanResult_e (*start)(CanLib_t *can);

    /** Leave the bus. */
    CanResult_e (*stop)(CanLib_t *can);

    /**
     * Put one frame into a hardware mailbox.
     * Return CAN_OK when accepted, CAN_ERR_TX_FULL when no mailbox is free.
     */
    CanResult_e (*transmit)(CanLib_t *can, const CanFrame_t *frame);

    /**
     * Take one frame out of the hardware receive FIFO.
     * Return CAN_OK with *frame filled, or CAN_ERR_RX_EMPTY when nothing is waiting.
     */
    CanResult_e (*receive)(CanLib_t *can, CanFrame_t *frame);

    /**
     * Read the error counters into can->bus (tec, rec, hw_error_flags).
     * Return CAN_ERR_BUS_OFF when the node is bus-off, CAN_OK otherwise.
     * May be NULL when the chip has no counters.
     */
    CanResult_e (*get_errors)(CanLib_t *can);

    /** Leave the bus-off state and rejoin the bus. May be NULL. */
    CanResult_e (*recover)(CanLib_t *can);

#if CAN_CFG_ENABLE_XCVR_CONTROL
    /** Set the transceiver mode pin (normal / silent / standby). May be NULL. */
    CanResult_e (*set_xcvr_mode)(CanLib_t *can, CanXcvrMode_e mode);
#endif
};

/** Short name for the backend function table type. */
typedef struct CanPortOps_s CanPortOps_t;

/* Function tables provided by the backend files (declared only when enabled). */
#if CAN_CFG_ENABLE_BACKEND_STM32_BXCAN
extern const CanPortOps_t g_CanPortStm32Bxcan;  /**< STM32 bxCAN backend (step 3). */
#endif
#if CAN_CFG_ENABLE_BACKEND_STM32_FDCAN
extern const CanPortOps_t g_CanPortStm32Fdcan;  /**< STM32 FDCAN backend (step 3). */
#endif
#if CAN_CFG_ENABLE_BACKEND_ESP32_TWAI
extern const CanPortOps_t g_CanPortEsp32Twai;   /**< ESP32 TWAI backend (step 3). */
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP25XX
extern const CanPortOps_t g_CanPortMcp25xx;     /**< MCP25xx SPI backend (step 3). */
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP251XFD
extern const CanPortOps_t g_CanPortMcp251xfd;   /**< MCP2517FD / MCP2518FD SPI backend. */
#endif
#if CAN_CFG_ENABLE_BACKEND_VIRTUAL
extern const CanPortOps_t g_CanPortVirtual;     /**< Virtual loopback backend (PC tests). */
#endif

/* ======================================================================== */
/* PLATFORM HOOKS (provided by can_platform_stm32.c / can_platform_esp32.c) */
/* ======================================================================== */

/**
 * @brief  Wait for a number of milliseconds. Weak default returns at once.
 * @param  ms Time to wait in ms.
 */
void CanPlatform_DelayMs(uint32_t ms);

/**
 * @brief  Set a GPIO output (used for the transceiver mode pin). Weak default: not supported.
 * @param  port GPIO port handle (STM32: GPIO_TypeDef*). ESP32 ignores it.
 * @param  pin  GPIO pin (STM32: GPIO_PIN_x mask, ESP32: GPIO number).
 * @param  level true = high, false = low.
 * @return CAN_OK, or CAN_ERR_NOT_SUPPORTED when no platform file is linked.
 */
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level);

/**
 * @brief  One full-duplex SPI transfer with chip select (used by the MCP25xx backend).
 *
 * The platform file pulls CS low (hw->cs_port / hw->cs_pin), clocks len bytes,
 * and releases CS. Weak default: not supported.
 * @param  hw  Hardware description (SPI handle and CS pin).
 * @param  tx  Bytes to send (len bytes).
 * @param  rx  Buffer for the bytes received (len bytes, never NULL).
 * @param  len Number of bytes to transfer.
 * @return CAN_OK, CAN_ERR_SPI on a bus failure, CAN_ERR_NOT_SUPPORTED when no platform file is linked.
 */
CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len);

#if CAN_CFG_ENABLE_XCVR_CONTROL
/**
 * @brief  Drive the transceiver mode pin (shared by all backends).
 *
 * hw->xcvr_standby_level is the pin level that switches the transceiver off the
 * normal mode: normal = opposite level, silent and standby = that level.
 * @param  hw   Hardware description (xcvr_port, xcvr_pin, xcvr_standby_level).
 * @param  mode Requested transceiver mode.
 * @return CAN_OK, CAN_ERR_NOT_SUPPORTED when no mode pin is configured (xcvr_port NULL and xcvr_pin 0)
 *         or no platform file is linked.
 */
CanResult_e CanPort_SetXcvrPin(const CanHwCfg_t *hw, CanXcvrMode_e mode);
#endif

/**
 * @brief  Find the function table of a backend.
 * @param  backend Backend id.
 * @return Function table, or NULL when the backend is disabled in can_cfg.h.
 */
const CanPortOps_t *CanPort_Get(CanBackend_e backend);

#ifdef __cplusplus
}
#endif

#endif /* CAN_PORT_H */
