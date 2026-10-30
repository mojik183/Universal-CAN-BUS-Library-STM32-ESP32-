/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_lib.h
 * @brief   Umbrella header: include this one file to get the whole library.
 *
 *     #include "can_lib.h"
 *
 * Library layout (files are added step by step):
 *   can_cfg.h          all configuration macros          (step 1, done)
 *   can_types.h        enums, structs, main struct       (step 1, done)
 *   can_debug.h/.c     errors, warnings, logging         (step 1, done)
 *   can_port.h/.c      backend interface and lookup      (step 2, done)
 *   can_core.h/.c      init, send, receive, process      (step 2, done)
 *   can_port_virtual   software backend for PC tests     (step 2, done)
 *   can_mcp25xx.h/.c   MCP2515 SPI backend               (step 3, done)
 *   can_platform_stm32.c  STM32 tick, delay, GPIO, SPI   (step 3, done)
 *   can_platform_esp32.c  ESP32 tick, delay, GPIO, SPI   (step 3, done)
 *   can_stm32_bxcan.h/.c  STM32 bxCAN backend            (step 3, done)
 *   can_stm32_fdcan.h/.c  STM32 FDCAN backend (CAN FD)    (step 5, done)
 *   can_esp32_twai.h/.c   ESP32 TWAI backend             (step 3, done)
 *   can_mcp251xfd.h/.c MCP2517FD / MCP2518FD SPI backend   (step 6, done)
 *   can_rtos.h/.c      optional FreeRTOS layer           (step 4, done)
 */
#ifndef CAN_LIB_H
#define CAN_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"
#include "can_debug.h"
#include "can_port.h"
#include "can_core.h"
#include "can_rtos.h"

#if CAN_CFG_ENABLE_BACKEND_VIRTUAL
    #include "can_port_virtual.h"
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP25XX
    #include "can_mcp25xx.h"
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP251XFD
    #include "can_mcp251xfd.h"
#endif
#if CAN_CFG_ENABLE_BACKEND_STM32_BXCAN
    #include "can_stm32_bxcan.h"
#endif
#if CAN_CFG_ENABLE_BACKEND_STM32_FDCAN
    #include "can_stm32_fdcan.h"
#endif
#if CAN_CFG_ENABLE_BACKEND_ESP32_TWAI
    #include "can_esp32_twai.h"
#endif

/** Helper that turns a number into a string for the version text. */
#define CAN_LIB_STR_(x)     #x
#define CAN_LIB_STR(x)      CAN_LIB_STR_(x)

/** Library version as text, e.g. "0.1.0". */
#define CAN_LIB_VERSION_STR \
    CAN_LIB_STR(CAN_LIB_VERSION_MAJOR) "." \
    CAN_LIB_STR(CAN_LIB_VERSION_MINOR) "." \
    CAN_LIB_STR(CAN_LIB_VERSION_PATCH)

#ifdef __cplusplus
}
#endif

#endif /* CAN_LIB_H */
