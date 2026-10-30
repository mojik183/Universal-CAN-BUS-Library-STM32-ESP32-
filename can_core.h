/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_core.h
 * @brief   User API of the CAN library: init, start, send, receive, process, filters.
 *
 * Typical use (bare-metal):
 *
 *     static CanLib_t can1;                                  // add can1 to the Keil watch window
 *
 *     CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);
 *     can1.hw.handle = &hcan1;                               // your HAL handle
 *     CanLib_Init(&can1);
 *     CanLib_Start(&can1);
 *
 *     CanLib_Send(&can1, 0x123, data, 8);                    // send
 *
 *     for (;;) {
 *         CanLib_Process(&can1);                             // call often (main loop)
 *         CanFrame_t f;
 *         while (CanLib_Receive(&can1, &f) == CAN_OK) { ... } // receive
 *     }
 *
 * Return values: CAN_OK, a warning (positive, the call worked), or an error (negative).
 * Test with CAN_IS_ERR(r) to catch failures and still accept warnings.
 */
#ifndef CAN_CORE_H
#define CAN_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"
#include "can_debug.h"
#include "can_port.h"

/* ======================================================================== */
/* SETUP AND LIFE CYCLE                                                     */
/* ======================================================================== */

/**
 * @brief  Clear an instance and fill every setting with the defaults of can_cfg.h.
 * @note   Call first. Then change what you need (can.cfg, can.hw, callbacks) and call CanLib_Init().
 * @param  can     Instance to prepare.
 * @param  name    Short name for log lines (must stay valid, e.g. a string literal).
 * @param  backend Controller backend this instance will use.
 * @return CAN_OK or CAN_ERR_NULL_PTR.
 */
CanResult_e CanLib_SetDefaults(CanLib_t *can, const char *name, CanBackend_e backend);

/**
 * @brief  Initialize the instance and its controller. The bus is not joined yet.
 * @param  can Instance prepared with CanLib_SetDefaults().
 * @return CAN_OK, CAN_ERR_BACKEND_DISABLED, CAN_ERR_BAD_PARAM, CAN_ERR_BAD_STATE, or a backend error.
 */
CanResult_e CanLib_Init(CanLib_t *can);

/**
 * @brief  Stop (if running) and release the controller. Settings are kept.
 * @param  can Initialized instance.
 * @return CAN_OK or an error.
 */
CanResult_e CanLib_DeInit(CanLib_t *can);

/**
 * @brief  Join the bus. Allowed in state READY or STOPPED.
 * @param  can Initialized instance.
 * @return CAN_OK or an error.
 */
CanResult_e CanLib_Start(CanLib_t *can);

/**
 * @brief  Leave the bus. Allowed in state RUNNING.
 * @param  can Initialized instance.
 * @return CAN_OK or an error.
 */
CanResult_e CanLib_Stop(CanLib_t *can);

/**
 * @brief  Run the library: read the controller, send queued frames, handle the
 *         manual send request, check error counters, and recover from bus-off.
 * @note   Call often from the main loop (or let the RTOS task do it).
 * @param  can Initialized instance.
 * @return CAN_OK, or the first error met. CAN_ERR_BAD_STATE when not running (not reported).
 */
CanResult_e CanLib_Process(CanLib_t *can);

/* ======================================================================== */
/* SEND                                                                     */
/* ======================================================================== */

/**
 * @brief  Send a classic data frame with a standard (11-bit) identifier.
 * @param  can  Running instance.
 * @param  id   Identifier 0..0x7FF.
 * @param  data Payload bytes (may be NULL when len is 0).
 * @param  len  Number of bytes. More than 8 is cut to 8 with CAN_WARN_DLC_CLAMPED.
 * @return CAN_OK, a warning (frame queued near full, clamped), or an error (frame not sent).
 */
CanResult_e CanLib_Send(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief  Send a classic data frame with an extended (29-bit) identifier.
 * @param  can  Running instance.
 * @param  id   Identifier 0..0x1FFFFFFF.
 * @param  data Payload bytes.
 * @param  len  Number of bytes (max 8).
 * @return As CanLib_Send(); CAN_ERR_NOT_SUPPORTED when CAN_CFG_ENABLE_EXT_ID is 0.
 */
CanResult_e CanLib_SendExt(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief  Send a CAN FD frame with a standard identifier.
 * @note   Lengths between valid FD sizes (12, 16, 20, 24, 32, 48, 64) are rounded up and padded with zeros (e.g. 21 -> 24).
 *         When FD is not active the frame is sent as classic with CAN_WARN_FD_FALLBACK.
 * @param  can  Running instance.
 * @param  id   Identifier 0..0x7FF.
 * @param  data Payload bytes.
 * @param  len  Number of bytes (max 64).
 * @param  brs  true = send the data phase at the faster bitrate.
 * @return As CanLib_Send(); CAN_ERR_NOT_SUPPORTED when CAN_CFG_ENABLE_FD is 0.
 */
CanResult_e CanLib_SendFd(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len, bool brs);

/**
 * @brief  Send a remote transmission request.
 * @param  can     Running instance.
 * @param  id      Identifier.
 * @param  id_type Standard or extended.
 * @param  dlc     Requested data length 0..8.
 * @return As CanLib_Send(); CAN_ERR_NOT_SUPPORTED when CAN_CFG_ENABLE_REMOTE_FRAMES is 0.
 */
CanResult_e CanLib_SendRemote(CanLib_t *can, uint32_t id, CanIdType_e id_type, uint8_t dlc);

/**
 * @brief  Send a fully prepared frame (any type, format, and identifier length).
 * @param  can   Running instance.
 * @param  frame Frame to send (copied, may be a local variable).
 * @return As CanLib_Send().
 */
CanResult_e CanLib_SendFrame(CanLib_t *can, const CanFrame_t *frame);

/**
 * @brief  Manual send: send what is in can->tx.header and can->tx.body.
 * @note   Fill the two fields (in code or in the watch window), then call this,
 *         or set can->tx.manual_request = true and let CanLib_Process() send it.
 * @param  can Running instance.
 * @return As CanLib_Send(); CAN_ERR_NOT_SUPPORTED when CAN_CFG_ENABLE_MANUAL_SEND is 0.
 */
CanResult_e CanLib_SendManual(CanLib_t *can);

/* ======================================================================== */
/* RECEIVE                                                                  */
/* ======================================================================== */

/**
 * @brief  Take the oldest received frame.
 * @note   CAN_ERR_RX_EMPTY is normal when nothing arrived: it is returned but not reported as an error.
 * @param  can   Initialized instance.
 * @param  frame Where the frame is copied.
 * @return CAN_OK when a frame was returned, CAN_ERR_RX_EMPTY when none is waiting.
 */
CanResult_e CanLib_Receive(CanLib_t *can, CanFrame_t *frame);

/**
 * @brief  Number of received frames waiting to be read.
 * @param  can Instance (NULL gives 0).
 * @return Waiting frames.
 */
uint16_t CanLib_Available(const CanLib_t *can);

/* ======================================================================== */
/* FILTERS (software acceptance filters; none set = accept everything)      */
/* ======================================================================== */

/**
 * @brief  Add a mask filter: accept when (id & mask) == (filter id & mask).
 * @param  can     Instance (before or after init).
 * @param  id_type Identifier length the filter applies to.
 * @param  id      Identifier pattern.
 * @param  mask    Bits to compare (1 = compare).
 * @return CAN_OK, CAN_ERR_FILTER_FULL, or CAN_WARN_FILTER_IGNORED when filters are disabled.
 */
CanResult_e CanLib_AddFilter(CanLib_t *can, CanIdType_e id_type, uint32_t id, uint32_t mask);

/**
 * @brief  Add a filter that accepts exactly one identifier.
 * @param  can     Instance (before or after init).
 * @param  id_type Identifier length.
 * @param  id      Identifier to accept.
 * @return As CanLib_AddFilter().
 */
CanResult_e CanLib_AddFilterExact(CanLib_t *can, CanIdType_e id_type, uint32_t id);

/**
 * @brief  Remove all filters (accept everything again).
 * @param  can Instance.
 * @return CAN_OK or CAN_ERR_NULL_PTR.
 */
CanResult_e CanLib_ClearFilters(CanLib_t *can);

/* ======================================================================== */
/* TRANSCEIVER                                                              */
/* ======================================================================== */

/**
 * @brief  Set the transceiver mode through its mode pin (normal / silent / standby).
 * @param  can  Initialized instance.
 * @param  mode Requested mode.
 * @return CAN_OK, or CAN_ERR_NOT_SUPPORTED when the backend or cfg has no transceiver control.
 */
CanResult_e CanLib_SetXcvrMode(CanLib_t *can, CanXcvrMode_e mode);

/* ======================================================================== */
/* HELPERS                                                                  */
/* ======================================================================== */

/**
 * @brief  Convert a DLC code (0..15) to a payload length in bytes.
 * @param  dlc    DLC code.
 * @param  format Classic frames never exceed 8 bytes.
 * @return Length in bytes.
 */
uint8_t CanLib_DlcToLen(uint8_t dlc, CanFrameFormat_e format);

/**
 * @brief  Convert a payload length to the smallest DLC code that holds it.
 * @param  len Length in bytes (above 64 gives 15).
 * @return DLC code 0..15.
 */
uint8_t CanLib_LenToDlc(uint8_t len);

/* ======================================================================== */
/* HOOKS FOR BACKENDS AND INTERRUPTS                                        */
/* ======================================================================== */

/**
 * @brief  Hand a received frame to the library (filters, buffer, callbacks, statistics).
 * @note   Safe to call from an interrupt if CAN_CFG_ENTER_CRITICAL is mapped in can_cfg.h.
 *         Do not use printf logging inside interrupts.
 * @param  can   Instance.
 * @param  frame Received frame.
 */
void CanLib_OnRxFrame(CanLib_t *can, const CanFrame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* CAN_CORE_H */
