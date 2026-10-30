/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_mcp25xx.h
 * @brief   MCP2515 (classic CAN) backend over SPI, plus register constants.
 *
 * Wiring: MCU SPI (mode 0,0) -> MCP2515, chip select on a GPIO, the transceiver
 * (TJA10xx / ISO10xx) on the chip's TXCAN / RXCAN pins.
 *
 * Setup in the application (STM32 example):
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX);
 *     can1.hw.handle   = &hspi1;           // SPI handle
 *     can1.hw.cs_port  = GPIOA;            // chip select
 *     can1.hw.cs_pin   = GPIO_PIN_4;
 *     can1.hw.osc_hz   = 8000000UL;        // crystal on the MCP2515 board (8 or 16 MHz)
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 * Only the MCP2515 is supported here. MCP2517FD / MCP2518FD need a different
 * register map and come in a later step (init returns CAN_ERR_NOT_SUPPORTED).
 */
#ifndef CAN_MCP25XX_H
#define CAN_MCP25XX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_BACKEND_MCP25XX

/* ======================================================================== */
/* SPI INSTRUCTIONS                                                         */
/* ======================================================================== */

#define MCP_CMD_RESET           0xC0U   /**< Reset the chip (enters configuration mode). */
#define MCP_CMD_READ            0x03U   /**< Read registers: cmd, address, then data out. */
#define MCP_CMD_WRITE           0x02U   /**< Write registers: cmd, address, data. */
#define MCP_CMD_READ_STATUS     0xA0U   /**< Quick status byte (see MCP_STATUS_x). */
#define MCP_CMD_BIT_MODIFY      0x05U   /**< Change selected bits: cmd, address, mask, value. */
#define MCP_CMD_LOAD_TX_BASE    0x40U   /**< Load TX buffer n from its SIDH: 0x40 + 2*n. */
#define MCP_CMD_RTS_BASE        0x80U   /**< Request to send: 0x80 | (1 << n). */
#define MCP_CMD_READ_RX_BASE    0x90U   /**< Read RX buffer n from its SIDH: 0x90 + 4*n (clears RXnIF). */

/* ======================================================================== */
/* REGISTERS                                                                */
/* ======================================================================== */

#define MCP_REG_TEC             0x1CU   /**< Transmit error counter. */
#define MCP_REG_REC             0x1DU   /**< Receive error counter. */
#define MCP_REG_CANSTAT         0x0EU   /**< Status: operation mode in bits 7..5. */
#define MCP_REG_CANCTRL         0x0FU   /**< Control: requested mode in bits 7..5. */
#define MCP_REG_CNF3            0x28U   /**< Bit timing 3 (PHSEG2). */
#define MCP_REG_CNF2            0x29U   /**< Bit timing 2 (BTLMODE, SAM, PHSEG1, PRSEG). */
#define MCP_REG_CNF1            0x2AU   /**< Bit timing 1 (SJW, BRP). */
#define MCP_REG_CANINTE         0x2BU   /**< Interrupt enable. */
#define MCP_REG_CANINTF         0x2CU   /**< Interrupt flags. */
#define MCP_REG_EFLG            0x2DU   /**< Error flags. */
#define MCP_REG_TXB_CTRL_BASE   0x30U   /**< TXBnCTRL = 0x30 + 0x10 * n. */
#define MCP_REG_RXB0CTRL        0x60U   /**< Receive buffer 0 control. */
#define MCP_REG_RXB1CTRL        0x70U   /**< Receive buffer 1 control. */

/* CANCTRL / CANSTAT operation modes (value already shifted into bits 7..5). */
#define MCP_MODE_NORMAL         0x00U   /**< Normal operation. */
#define MCP_MODE_LOOPBACK       0x40U   /**< Internal loopback. */
#define MCP_MODE_LISTEN         0x60U   /**< Listen-only. */
#define MCP_MODE_CONFIG         0x80U   /**< Configuration (bit timing can be written). */
#define MCP_MODE_MASK           0xE0U   /**< Mask of the mode bits. */
#define MCP_CANCTRL_OSM         0x08U   /**< One-shot mode: do not resend a lost frame. */

/* READ STATUS bits. */
#define MCP_STATUS_RX0IF        0x01U   /**< Receive buffer 0 holds a frame. */
#define MCP_STATUS_RX1IF        0x02U   /**< Receive buffer 1 holds a frame. */
#define MCP_STATUS_TX0REQ       0x04U   /**< Transmit buffer 0 is busy. */
#define MCP_STATUS_TX1REQ       0x10U   /**< Transmit buffer 1 is busy. */
#define MCP_STATUS_TX2REQ       0x40U   /**< Transmit buffer 2 is busy. */

/* EFLG bits. */
#define MCP_EFLG_EWARN          0x01U   /**< TEC or REC reached 96. */
#define MCP_EFLG_RXEP           0x08U   /**< Receive error passive. */
#define MCP_EFLG_TXEP           0x10U   /**< Transmit error passive. */
#define MCP_EFLG_TXBO           0x20U   /**< Bus-off. */
#define MCP_EFLG_RX0OVR         0x40U   /**< Receive buffer 0 overflowed. */
#define MCP_EFLG_RX1OVR         0x80U   /**< Receive buffer 1 overflowed. */

/* CANINTE bits. */
#define MCP_INT_RX0IE           0x01U   /**< Interrupt pin on receive buffer 0 full. */
#define MCP_INT_RX1IE           0x02U   /**< Interrupt pin on receive buffer 1 full. */

/* Bit timing limits of the MCP2515. */
#define MCP_BRP_MAX             64U     /**< Largest prescaler (BRP + 1). */
#define MCP_TQ_MIN              5U      /**< Fewest time quanta in one bit. */
#define MCP_TQ_MAX              25U     /**< Most time quanta in one bit. */
#define MCP_TSEG1_MAX           16U     /**< Prop segment (1..8) + phase segment 1 (1..8). */
#define MCP_TSEG2_MAX           8U      /**< Phase segment 2 (2..8). */

/** Number of transmit buffers of the MCP2515. */
#define MCP_TX_BUFFERS          3U

/* ======================================================================== */
/* PRIVATE DATA OF ONE CHIP                                                 */
/* ======================================================================== */

/** @brief Backend state of one MCP2515 (visible in the watch window as can->port_ctx). */
typedef struct
{
    bool     inited;        /**< true = init() finished. */
    uint8_t  run_mode;      /**< CANCTRL value used when the node is started (mode + one-shot flag). */
    uint8_t  cnf1;          /**< Value written to CNF1. */
    uint8_t  cnf2;          /**< Value written to CNF2. */
    uint8_t  cnf3;          /**< Value written to CNF3. */
    uint8_t  last_canstat;  /**< CANSTAT read at the last mode change (debug). */
    uint8_t  last_eflg;     /**< EFLG read at the last get_errors() (debug). */
    uint8_t  last_status;   /**< READ STATUS byte of the last transmit / receive (debug). */
    uint32_t spi_errors;    /**< Failed SPI transfers (debug). */
} CanMcpCtx_t;

/**
 * @brief  Calculate MCP2515 bit timing for a bitrate.
 *
 * Searches the prescaler and segment lengths that give exactly the requested
 * bitrate with the sample point closest to the request.
 * @param  osc_hz  Oscillator frequency in Hz.
 * @param  bitrate Requested bitrate in bit/s.
 * @param  sample_point Requested sample point in 1/1000 (875 = 87.5 %).
 * @param  out     Filled with prescaler (BRP + 1), tseg1 (prop + phase 1), tseg2,
 *                 sjw, and actual_bitrate. bitrate and sample_point are copied.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_PARAM, or CAN_ERR_BITRATE when no exact timing exists.
 */
CanResult_e CanMcp_CalcTiming(uint32_t osc_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out);

/**
 * @brief  Get the private data of an initialized MCP2515 instance (for watch / tests).
 * @param  can Instance.
 * @return Pointer to the context, or NULL when the instance is not an initialized MCP25xx.
 */
const CanMcpCtx_t *CanMcp_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_MCP25XX */

#ifdef __cplusplus
}
#endif

#endif /* CAN_MCP25XX_H */
