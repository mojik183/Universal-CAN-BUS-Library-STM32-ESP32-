/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_port_virtual.h
 * @brief   Virtual backend: a software "controller" for PC tests and bring-up.
 *
 * No hardware needed. Sent frames are stored (and looped back to the receive side
 * in CANLIB_MODE_LOOPBACK). The control functions below inject received frames and
 * simulate faults (no free mailbox, error counters, bus-off), so every path of the
 * core can be tested before real chips are connected.
 *
 * Enable with CAN_CFG_ENABLE_BACKEND_VIRTUAL in can_cfg.h.
 */
#ifndef CAN_PORT_VIRTUAL_H
#define CAN_PORT_VIRTUAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_port.h"

#if CAN_CFG_ENABLE_BACKEND_VIRTUAL

#define CAN_VIRTUAL_RX_DEPTH    16U     /**< Frames the simulated receive FIFO can hold. */

/** @brief State of one virtual controller (visible in the watch window). */
typedef struct
{
    bool          started;                              /**< true after start(). */
    bool          tx_full;                              /**< true = report "no free mailbox". */
    bool          bus_off;                              /**< true = simulate bus-off. */
    CanXcvrMode_e xcvr_mode;                            /**< Last transceiver mode requested. */
    uint8_t       tec;                                  /**< Simulated transmit error counter. */
    uint8_t       rec;                                  /**< Simulated receive error counter. */
    uint32_t      sent_count;                           /**< Frames "put on the bus". */
    CanFrame_t    last_sent;                            /**< Most recent frame sent. */
    CanFrame_t    rx_ring[CAN_VIRTUAL_RX_DEPTH];        /**< Simulated receive FIFO storage. */
    uint8_t       rx_head;                              /**< Next write index of the FIFO. */
    uint8_t       rx_tail;                              /**< Next read index of the FIFO. */
    uint8_t       rx_count;                             /**< Frames in the FIFO. */
} CanVirtualCtx_t;

/**
 * @brief  Put a frame into the simulated receive FIFO (as if it came from the bus).
 * @param  can   Instance using the virtual backend.
 * @param  frame Frame to inject.
 * @return CAN_OK, CAN_ERR_NULL_PTR, CAN_ERR_BAD_STATE, or CAN_ERR_RX_OVERFLOW when the FIFO is full.
 */
CanResult_e CanVirtual_InjectRx(CanLib_t *can, const CanFrame_t *frame);

/**
 * @brief  Simulate "no free transmit mailbox".
 * @param  can  Instance using the virtual backend.
 * @param  full true = every transmit fails with CAN_ERR_TX_FULL.
 */
void CanVirtual_SetTxFull(CanLib_t *can, bool full);

/**
 * @brief  Simulate bus-off.
 * @param  can     Instance using the virtual backend.
 * @param  bus_off true = node is bus-off until recover() is called.
 */
void CanVirtual_SetBusOff(CanLib_t *can, bool bus_off);

/**
 * @brief  Set the simulated hardware error counters.
 * @param  can Instance using the virtual backend.
 * @param  tec Transmit error counter value.
 * @param  rec Receive error counter value.
 */
void CanVirtual_SetErrorCounters(CanLib_t *can, uint8_t tec, uint8_t rec);

/**
 * @brief  Get the state of the virtual controller of an instance.
 * @param  can Instance using the virtual backend.
 * @return Read-only state, or NULL when the instance is not a virtual one.
 */
const CanVirtualCtx_t *CanVirtual_GetCtx(const CanLib_t *can);

#endif /* CAN_CFG_ENABLE_BACKEND_VIRTUAL */

#ifdef __cplusplus
}
#endif

#endif /* CAN_PORT_VIRTUAL_H */
