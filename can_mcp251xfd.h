/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_mcp251xfd.h
 * @brief   MCP2517FD / MCP2518FD backend (classic CAN and CAN FD) over SPI, plus register constants.
 *
 * Wiring: MCU SPI (mode 0,0) -> MCP251xFD, chip select on a GPIO, transceiver
 * (TJA10xx / ISO10xx) on the chip's TXCAN / RXCAN pins. The crystal must be 40, 20 or 4 MHz.
 *
 * Setup in the application (STM32 example):
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD);
 *     can1.hw.handle   = &hspi1;           // SPI handle
 *     can1.hw.cs_port  = GPIOA;            // chip select
 *     can1.hw.cs_pin   = GPIO_PIN_4;
 *     can1.hw.osc_hz   = 40000000UL;       // crystal on the board (40, 20 or 4 MHz)
 *     can1.cfg.mcp_variant = CAN_MCP_2518FD;   // or CAN_MCP_2517FD
 *     #if CAN_CFG_ENABLE_FD
 *     can1.cfg.fd_enabled = true;          // CAN FD with bit rate switching
 *     #endif
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 * SPI clock: at most 0.85 * SYSCLK / 2 (17 MHz at 40 MHz SYSCLK on the MCP2517FD, 20 MHz on the MCP2518FD).
 *
 * Message RAM layout used by the library (2 KB on the chip):
 *   FIFO1 = transmit FIFO, FIFO2 = receive FIFO, no TXQ, no transmit event FIFO, no time stamps.
 *   Classic CAN: 8-byte payload, 8 TX objects, 32 RX objects.
 *   CAN FD:      64-byte payload, 8 TX objects, 16 RX objects.
 * The chip ECC is switched on and the RAM is cleared at init (the ECC needs initialized RAM).
 */
#ifndef CAN_MCP251XFD_H
#define CAN_MCP251XFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_BACKEND_MCP251XFD

/* ======================================================================== */
/* SPI INSTRUCTIONS (4-bit command + 12-bit address in two bytes)           */
/* ======================================================================== */

#define MCPFD_CMD_RESET         0x0U    /**< Reset the chip (enters configuration mode). */
#define MCPFD_CMD_WRITE         0x2U    /**< Write bytes: cmd/address, then data. */
#define MCPFD_CMD_READ          0x3U    /**< Read bytes: cmd/address, then data out. */

/* ======================================================================== */
/* REGISTERS (32 bit, little endian; byte access allowed)                   */
/* ======================================================================== */

#define MCPFD_REG_CON           0x000U  /**< CiCON: configuration, requested mode, current mode. */
#define MCPFD_REG_NBTCFG        0x004U  /**< Nominal bit time configuration. */
#define MCPFD_REG_DBTCFG        0x008U  /**< Data bit time configuration. */
#define MCPFD_REG_TDC           0x00CU  /**< Transmitter delay compensation. */
#define MCPFD_REG_TREC          0x034U  /**< Error counters and error state flags. */
#define MCPFD_REG_FIFOCON1      0x05CU  /**< FIFO 1 (transmit) control. */
#define MCPFD_REG_FIFOSTA1      0x060U  /**< FIFO 1 (transmit) status. */
#define MCPFD_REG_FIFOCON2      0x068U  /**< FIFO 2 (receive) control. */
#define MCPFD_REG_FIFOSTA2      0x06CU  /**< FIFO 2 (receive) status. */
#define MCPFD_REG_FLTCON0       0x1D0U  /**< Filter 0..3 control (one byte per filter). */
#define MCPFD_REG_FLTOBJ0       0x1F0U  /**< Filter 0 object (ID to match). */
#define MCPFD_REG_MASK0         0x1F4U  /**< Filter 0 mask. */
#define MCPFD_REG_OSC           0xE00U  /**< Oscillator control. */
#define MCPFD_REG_IOCON         0xE04U  /**< Pin configuration. */
#define MCPFD_REG_ECCCON        0xE0CU  /**< RAM ECC control. */

#define MCPFD_RAM_START         0x400U  /**< First address of the message RAM. */
#define MCPFD_RAM_SIZE          0x800U  /**< Size of the message RAM in bytes. */

/* CiCON bits. */
#define MCPFD_CON_ISOCRCEN      (1UL << 5)      /**< ISO CRC for CAN FD. */
#define MCPFD_CON_RTXAT         (1UL << 16)     /**< Restrict retransmission attempts (per FIFO TXAT). */
#define MCPFD_CON_REQOP_SHIFT   24U             /**< Requested operation mode (bits 26..24). */
#define MCPFD_CON_OPMOD_SHIFT   21U             /**< Current operation mode (bits 23..21). */

/* Operation modes (REQOP / OPMOD values). */
#define MCPFD_MODE_NORMAL_FD    0U      /**< Normal CAN FD mode (classic and FD frames). */
#define MCPFD_MODE_LOOPBACK     2U      /**< Internal loopback. */
#define MCPFD_MODE_LISTEN       3U      /**< Listen-only. */
#define MCPFD_MODE_CONFIG       4U      /**< Configuration mode. */
#define MCPFD_MODE_NORMAL_20    6U      /**< Normal CAN 2.0 mode (FD frames are errors). */

/* CiTREC byte 2 flags (bits 23..16 of the register). */
#define MCPFD_TREC_EWARN        0x01U   /**< TEC or REC reached 96. */
#define MCPFD_TREC_RXWARN       0x02U   /**< REC reached 96. */
#define MCPFD_TREC_TXWARN       0x04U   /**< TEC reached 96. */
#define MCPFD_TREC_RXBP         0x08U   /**< Receive error passive. */
#define MCPFD_TREC_TXBP         0x10U   /**< Transmit error passive. */
#define MCPFD_TREC_TXBO         0x20U   /**< Bus-off. */

/* CiFIFOCON bits. */
#define MCPFD_FIFOCON_RXOVIE    (1UL << 3)      /**< Overflow interrupt enable. */
#define MCPFD_FIFOCON_TXEN      (1UL << 7)      /**< 1 = transmit FIFO. */
#define MCPFD_FIFOCON_UINC      (1UL << 8)      /**< Increment the FIFO head / tail. */
#define MCPFD_FIFOCON_TXREQ     (1UL << 9)      /**< Request transmission. */
#define MCPFD_FIFOCON_FRESET    (1UL << 10)     /**< FIFO reset (clears by itself). */
#define MCPFD_FIFOCON_TXAT_UNL  (3UL << 21)     /**< Unlimited retransmission attempts. */
#define MCPFD_FIFOCON_FSIZE_POS 24U             /**< FIFO depth - 1 (bits 28..24). */
#define MCPFD_FIFOCON_PLSIZE_POS 29U            /**< Payload size code (bits 31..29): 0 = 8 bytes, 7 = 64 bytes. */

/* CiFIFOSTA bits. */
#define MCPFD_FIFOSTA_TFNRFNIF  (1UL << 0)      /**< TX: not full. RX: not empty. */
#define MCPFD_FIFOSTA_RXOVIF    (1UL << 3)      /**< RX FIFO overflowed (a frame was lost). */
#define MCPFD_FIFOSTA_CI_SHIFT  8U              /**< FIFO message index (bits 12..8). */
#define MCPFD_FIFOSTA_CI_MASK   0x1FUL          /**< Mask of the message index. */

/* OSC bits. */
#define MCPFD_OSC_PLLEN         (1UL << 0)      /**< System clock = 10 x crystal (4 MHz crystal only). */
#define MCPFD_OSC_CLKODIV_10    (3UL << 5)      /**< CLKO pin = SYSCLK / 10 (reset value). */
#define MCPFD_OSC_PLLRDY        (1UL << 8)      /**< PLL locked. */
#define MCPFD_OSC_OSCRDY        (1UL << 10)     /**< Oscillator running. */

/* ECCCON bits. */
#define MCPFD_ECCCON_ECCEN      (1UL << 0)      /**< RAM ECC enable. */

/* TDC bits. */
#define MCPFD_TDC_MODE_AUTO     (2UL << 16)     /**< Automatic delay measurement. */
#define MCPFD_TDC_EDGFLTEN      (1UL << 25)     /**< Edge filtering during bus integration. */

/* Message RAM objects: one transmit or receive object = 8 header bytes + payload. */
#define MCPFD_OBJ_HEADER        8U              /**< Bytes of ID word and control word. */
#define MCPFD_TX_OBJECTS        8U              /**< Depth of the transmit FIFO. */
#define MCPFD_RX_OBJECTS_CLASSIC 32U            /**< Depth of the receive FIFO with 8-byte payload. */
#define MCPFD_RX_OBJECTS_FD     16U             /**< Depth of the receive FIFO with 64-byte payload. */

/* Bit timing limits (nominal phase). */
#define MCPFD_NOM_PSC_MAX       256U    /**< Largest nominal prescaler. */
#define MCPFD_NOM_SEG1_MAX      256U    /**< Largest nominal time segment 1. */
#define MCPFD_NOM_SEG2_MAX      128U    /**< Largest nominal time segment 2. */
#define MCPFD_NOM_TQ_CAP        80U     /**< Most quanta per nominal bit used by the search. */
/* Bit timing limits (data phase). */
#define MCPFD_DAT_PSC_MAX       256U    /**< Largest data prescaler. */
#define MCPFD_DAT_SEG1_MAX      32U     /**< Largest data time segment 1. */
#define MCPFD_DAT_SEG2_MAX      16U     /**< Largest data time segment 2. */

/** Data bit rate from which the transmitter delay compensation is switched on (bit/s). */
#define MCPFD_TDC_MIN_BITRATE   2000000UL

/** @brief Backend state of one MCP251xFD (visible in the watch window as can->port_ctx). */
typedef struct
{
    bool     inited;        /**< true = init() finished. */
    bool     fd_mode;       /**< true = FD frames with bit rate switching are allowed. */
    uint8_t  run_mode;      /**< REQOP value used when the node is started. */
    uint8_t  payload;       /**< Payload bytes per RAM object (8 or 64). */
    uint8_t  tx_obj_size;   /**< Bytes of one transmit object in RAM. */
    uint8_t  rx_obj_size;   /**< Bytes of one receive object in RAM. */
    uint8_t  tx_depth;      /**< Objects in the transmit FIFO. */
    uint8_t  rx_depth;      /**< Objects in the receive FIFO. */
    uint16_t tx_base;       /**< Offset of the transmit FIFO inside the RAM. */
    uint16_t rx_base;       /**< Offset of the receive FIFO inside the RAM. */
    uint32_t sysclk_hz;     /**< System clock of the chip in Hz (used for bit timing). */
    uint32_t last_con;      /**< CiCON read at the last mode change (debug). */
    uint32_t last_trec;     /**< CiTREC read at the last get_errors() (debug). */
    uint32_t last_fifosta;  /**< Last FIFO status read by transmit / receive (debug). */
    uint32_t spi_errors;    /**< Failed SPI transfers (debug). */
} CanMcpFdCtx_t;

/**
 * @brief  Calculate MCP251xFD bit timing for one phase.
 * @param  sysclk_hz System clock of the chip in Hz (40 MHz or 20 MHz).
 * @param  bitrate   Requested bitrate in bit/s.
 * @param  sample_point Requested sample point in 1/1000.
 * @param  data_phase false = nominal phase limits, true = data phase limits.
 * @param  out       Filled with prescaler, tseg1, tseg2, sjw, actual_bitrate.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_PARAM, or CAN_ERR_BITRATE when no exact timing exists.
 */
CanResult_e CanMcpFd_CalcTiming(uint32_t sysclk_hz, uint32_t bitrate, uint16_t sample_point,
                                bool data_phase, CanTiming_t *out);

/**
 * @brief  Get the private data of an initialized MCP251xFD instance (for watch / tests).
 * @param  can Instance.
 * @return Context, or NULL when the instance is not an initialized MCP251xFD backend.
 */
const CanMcpFdCtx_t *CanMcpFd_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_MCP251XFD */

#ifdef __cplusplus
}
#endif

#endif /* CAN_MCP251XFD_H */
