/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_cfg.h
 * @brief   Central configuration of the CAN library (the ONLY file you edit).
 *
 * Every optional part of the library is switched here with a macro.
 * Set a macro to 1 to enable it and to 0 to disable it. Disabled parts are
 * not compiled, so they cost no flash and no RAM.
 *
 * Targets : STM32 (bxCAN / FDCAN), ESP32 (TWAI), MCP25xx (SPI)
 * Hardware: MCP25xx controllers, TJA10xx and ISO10xx transceivers
 */
#ifndef CAN_CFG_H
#define CAN_CFG_H

/* ======================================================================== */
/* 1. LIBRARY VERSION                                                       */
/* ======================================================================== */

#define CAN_LIB_VERSION_MAJOR   0   /**< Major version: incompatible API change. */
#define CAN_LIB_VERSION_MINOR   1   /**< Minor version: new backward compatible feature. */
#define CAN_LIB_VERSION_PATCH   0   /**< Patch version: bug fix only. */

/* ======================================================================== */
/* 2. PLATFORM SELECTION                                                    */
/* ======================================================================== */

#define CAN_PLATFORM_GENERIC    0   /**< No vendor SDK (host build, unit tests). */
#define CAN_PLATFORM_STM32      1   /**< STM32 with the ST HAL. */
#define CAN_PLATFORM_ESP32      2   /**< ESP32 with ESP-IDF. */

/** Platform the firmware is built for. Pick one of the CAN_PLATFORM_x values. */
#define CAN_CFG_PLATFORM        CAN_PLATFORM_STM32

/* ======================================================================== */
/* 3. BACKENDS (which CAN controllers the library may drive)                */
/*    Several can be enabled at once, each CAN instance picks one at init.  */
/* ======================================================================== */

#define CAN_CFG_ENABLE_BACKEND_STM32_BXCAN  1   /**< STM32 bxCAN peripheral (classic CAN). */
#define CAN_CFG_ENABLE_BACKEND_STM32_FDCAN  0   /**< STM32 FDCAN peripheral (CAN FD). */
#define CAN_CFG_ENABLE_BACKEND_ESP32_TWAI   0   /**< ESP32 built-in TWAI controller. */
#define CAN_CFG_ENABLE_BACKEND_MCP25XX      1   /**< External MCP25xx controller over SPI. */
#define CAN_CFG_ENABLE_BACKEND_MCP251XFD    0   /**< External MCP2517FD / MCP2518FD over SPI (CAN FD). */
#define CAN_CFG_ENABLE_BACKEND_VIRTUAL      0   /**< Software loopback backend for PC tests and bring-up (no hardware). */

/* ======================================================================== */
/* 4. TRANSCEIVER SUPPORT (TJA10xx, ISO10xx)                                */
/* ======================================================================== */

/** 1 = library can drive the transceiver mode pin (standby / silent). */
#define CAN_CFG_ENABLE_XCVR_CONTROL         1

/* ======================================================================== */
/* 5. FEATURE SWITCHES                                                      */
/* ======================================================================== */

#define CAN_CFG_ENABLE_FD               0   /**< 1 = CAN FD frames (needs FDCAN or MCP2517/18FD). */
#define CAN_CFG_ENABLE_EXT_ID           1   /**< 1 = 29-bit extended identifiers. */
#define CAN_CFG_ENABLE_REMOTE_FRAMES    1   /**< 1 = remote transmission request frames. */
#define CAN_CFG_ENABLE_FILTERS          1   /**< 1 = software acceptance filter table. */
#define CAN_CFG_ENABLE_RX_QUEUE         1   /**< 1 = software receive ring buffer. */
#define CAN_CFG_ENABLE_TX_QUEUE         1   /**< 1 = software transmit ring buffer. */
#define CAN_CFG_ENABLE_CALLBACKS        1   /**< 1 = user callbacks (rx, events). */
#define CAN_CFG_ENABLE_STATS            1   /**< 1 = traffic and error statistics. */
#define CAN_CFG_ENABLE_MANUAL_SEND      1   /**< 1 = manual send (tx header/body + request flag). */
#define CAN_CFG_ENABLE_BUS_OFF_RECOVERY 1   /**< 1 = automatic bus-off recovery. */

/* ======================================================================== */
/* 6. SIZES                                                                 */
/* ======================================================================== */

#define CAN_CFG_MAX_INSTANCES       2   /**< Number of CAN channels used at the same time. */
#define CAN_CFG_RX_QUEUE_DEPTH      16  /**< Frames in the receive ring buffer, per instance. */
#define CAN_CFG_TX_QUEUE_DEPTH      8   /**< Frames in the transmit ring buffer, per instance. */
#define CAN_CFG_MAX_FILTERS         8   /**< Software filter slots, per instance. */
#define CAN_CFG_QUEUE_WARN_PERCENT  75U /**< Buffer fill level (%) that raises a "near full" warning. */
#define CAN_CFG_PROCESS_MAX_RX      8U  /**< Max hardware frames read per CanLib_Process() call. */

/* ======================================================================== */
/* 7. DEFAULT BUS PARAMETERS                                                */
/* ======================================================================== */

#define CAN_CFG_DEFAULT_BITRATE             500000UL    /**< Nominal bitrate in bit/s. */
#define CAN_CFG_DEFAULT_SAMPLE_POINT        875U        /**< Nominal sample point in 1/1000 (875 = 87.5 %). */
#define CAN_CFG_DEFAULT_FD_BITRATE          2000000UL   /**< FD data phase bitrate in bit/s. */
#define CAN_CFG_DEFAULT_FD_SAMPLE_POINT     750U        /**< FD data phase sample point in 1/1000. */
#define CAN_CFG_BUS_OFF_RECOVER_MS          100UL       /**< Wait before a bus-off recovery attempt, in ms. */

/* ======================================================================== */
/* 7b. PLATFORM AND MCP25xx SETTINGS                                        */
/* ======================================================================== */

/** STM32 only: the HAL header of your family (stm32f1xx_hal.h, stm32g4xx_hal.h, ...). */
#define CAN_CFG_STM32_HAL_HEADER        "stm32f4xx_hal.h"

/** STM32 FDCAN: kernel clock of the FDCAN peripheral in Hz (check CubeMX clock tree, e.g. 160 MHz or 170 MHz on G4). */
#define CAN_CFG_FDCAN_CLK_HZ            160000000UL

/**
 * STM32 FDCAN: 1 = the HAL needs the message RAM layout in FDCAN_InitTypeDef (STM32H7, shared 10 KB RAM).
 * 0 = fixed layout (G0, G4, L5, U5, H5). The library then fills MessageRAMOffset, the RX FIFO0 and the TX FIFO.
 */
#define CAN_CFG_FDCAN_MESSAGE_RAM       0

/** H7 message RAM: total size of the shared RAM in 32 bit words (STM32H7: 2560 words = 10 KB). */
#define CAN_CFG_FDCAN_RAM_TOTAL_WORDS   2560U

/** H7 message RAM: number of transmit FIFO elements per instance. */
#define CAN_CFG_FDCAN_RAM_TX_ELEMENTS   8U

/** H7 message RAM: number of receive FIFO0 elements per instance. */
#define CAN_CFG_FDCAN_RAM_RX_ELEMENTS   16U

/**
 * H7 message RAM: first word of the instance in the shared RAM. Each library instance gets an equal block
 * (sized for 64-byte elements) one after the other. Override when your layout differs.
 */
#ifndef CAN_CFG_FDCAN_RAM_OFFSET
#define CAN_CFG_FDCAN_RAM_OFFSET(inst)  ((uint32_t)(inst) * ((CAN_CFG_FDCAN_RAM_TX_ELEMENTS + CAN_CFG_FDCAN_RAM_RX_ELEMENTS) * 18U))
#endif

/** STM32 FDCAN: 1 = enable the RX FIFO0 interrupt; call CanFdcan_OnRxFifo0Pending() from the HAL callback. 0 = polling. */
#define CAN_CFG_FDCAN_USE_IRQ           0

/** MCP25xx: default crystal / oscillator frequency in Hz (set can.hw.osc_hz to override). */
#define CAN_CFG_MCP_DEFAULT_OSC_HZ      16000000UL

/** STM32 bxCAN: 1 = enable the RX FIFO0 interrupt; call CanBxcan_OnRxFifo0Pending() from the HAL callback. 0 = polling. */
#define CAN_CFG_BXCAN_USE_IRQ           0

/** ESP32 TWAI: clock feeding the controller in Hz (80 MHz on ESP32, S2, S3, C3; check your chip). */
#define CAN_CFG_TWAI_CLK_HZ             80000000UL

/** ESP32 TWAI: largest baud rate prescaler (128 on ESP32, up to 32768 on newer chips). */
#define CAN_CFG_TWAI_BRP_MAX            128U

/** ESP32 TWAI: size of the driver transmit queue (frames). */
#define CAN_CFG_TWAI_TX_QUEUE_LEN       4U

/** ESP32 TWAI: size of the driver receive queue (frames). */
#define CAN_CFG_TWAI_RX_QUEUE_LEN       8U

/** ESP32 TWAI: longest wait for the controller to finish a bus-off recovery, in ms (blocks the caller). */
#define CAN_CFG_TWAI_RECOVER_WAIT_MS    100U

/** MCP25xx: timeout of one SPI transfer, in ms (STM32 HAL). */
#define CAN_CFG_MCP_SPI_TIMEOUT_MS      10U

/* ======================================================================== */
/* 8. FREERTOS (optional)                                                   */
/* ======================================================================== */

/** 1 = use FreeRTOS (recursive mutex, rx semaphore, library task). 0 = bare-metal polling / ISR. */
#define CAN_CFG_USE_FREERTOS            0

/** 1 = the library creates its own task that calls CanLib_Process(). 0 = you call it yourself. */
#define CAN_CFG_RTOS_AUTO_TASK          1

#define CAN_CFG_RTOS_TASK_STACK_WORDS   768U    /**< Stack of the library task in words (ESP32: counted x4 as bytes). */
#define CAN_CFG_RTOS_TASK_PRIORITY      3U      /**< Priority of the library task. */
#define CAN_CFG_RTOS_TASK_NAME          "can"   /**< Name of the library task (first 3 letters + instance number are used). */
#define CAN_CFG_RTOS_POLL_MS            10U     /**< Library task wakes at least this often (tx queue, bus state), in ms. */
#define CAN_CFG_RTOS_MUTEX_TIMEOUT_MS   20U     /**< Max wait for the instance mutex, in ms. */

/**
 * True when the code runs inside an interrupt handler. Used to pick the *FromISR
 * FreeRTOS calls. Defaults: STM32 (Cortex-M) reads IPSR, ESP32 uses xPortInIsrContext().
 * On a host build define it yourself.
 */
#ifndef CAN_CFG_RTOS_IN_ISR
    #if (CAN_CFG_PLATFORM == CAN_PLATFORM_STM32)
        #define CAN_CFG_RTOS_IN_ISR()   (__get_IPSR() != 0U)
    #elif (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32)
        #define CAN_CFG_RTOS_IN_ISR()   (xPortInIsrContext() != 0)
    #else
        #define CAN_CFG_RTOS_IN_ISR()   (0)
    #endif
#endif

/* ======================================================================== */
/* 9. DEBUG AND DIAGNOSTICS                                                 */
/* ======================================================================== */

/** 1 = file/line capture, event log, and printf logging (if enabled below). */
#define CAN_CFG_ENABLE_DEBUG            1

/** Compile-time log ceiling: 0 none, 1 error, 2 warning, 3 info, 4 debug. */
#define CAN_CFG_DEBUG_LEVEL             3

/** 1 = print log lines with printf. 0 = watch window only (no printf). */
#define CAN_CFG_DEBUG_USE_PRINTF        0

/** Header that declares the print function (used only if printf is on). */
#define CAN_CFG_PRINTF_HEADER           <stdio.h>

/** Print function used for log lines. Retarget to your own UART printf here. */
#define CAN_CFG_PRINTF_FN               printf

/** Entries in the per-instance event log ring (0 = no event log). */
#define CAN_CFG_DEBUG_EVENT_LOG_SIZE    16

/** 1 = enable CAN_ASSERT() checks inside the library. */
#define CAN_CFG_ENABLE_ASSERT           1

/** 1 = validate all user arguments and return CAN_ERR_BAD_PARAM when wrong. */
#define CAN_CFG_ENABLE_PARAM_CHECK      1

/* ======================================================================== */
/* 10. COMPILER HELPERS                                                     */
/* ======================================================================== */

/** Marks a function as overridable (weak). Works on Keil, IAR, and GCC. */
#if defined(__CC_ARM) || defined(__ICCARM__)
    #define CAN_WEAK    __weak
#else
    #define CAN_WEAK    __attribute__((weak))
#endif

/* ======================================================================== */
/* 10b. CRITICAL SECTION (protects ring buffers shared with interrupts)     */
/* ======================================================================== */

/**
 * Enter / leave a short critical section. Defaults do nothing (single context).
 * If frames are received in an interrupt and read in main or a task, map them:
 *   bare-metal STM32 : __disable_irq() / __enable_irq()
 *   FreeRTOS         : taskENTER_CRITICAL() / taskEXIT_CRITICAL()
 */
#ifndef CAN_CFG_ENTER_CRITICAL
    #if CAN_CFG_USE_FREERTOS
        void CanRtos_EnterCritical(void);   /* Safe in tasks and in interrupts (can_rtos.c). */
        void CanRtos_ExitCritical(void);
        #define CAN_CFG_ENTER_CRITICAL()    CanRtos_EnterCritical()
        #define CAN_CFG_EXIT_CRITICAL()     CanRtos_ExitCritical()
    #else
        #define CAN_CFG_ENTER_CRITICAL()    ((void)0)
        #define CAN_CFG_EXIT_CRITICAL()     ((void)0)
    #endif
#endif

/* ======================================================================== */
/* 11. SANITY CHECKS (catch wrong configuration at compile time)            */
/* ======================================================================== */

#if !(CAN_CFG_ENABLE_BACKEND_STM32_BXCAN || CAN_CFG_ENABLE_BACKEND_STM32_FDCAN || \
      CAN_CFG_ENABLE_BACKEND_ESP32_TWAI  || CAN_CFG_ENABLE_BACKEND_MCP25XX || \
      CAN_CFG_ENABLE_BACKEND_MCP251XFD || CAN_CFG_ENABLE_BACKEND_VIRTUAL)
    #error "can_cfg.h: enable at least one backend."
#endif

#if CAN_CFG_ENABLE_BACKEND_STM32_BXCAN && CAN_CFG_ENABLE_BACKEND_STM32_FDCAN
    #error "can_cfg.h: enable only one of the STM32 backends (a chip family has bxCAN or FDCAN, not both)."
#endif

#if (CAN_CFG_ENABLE_BACKEND_STM32_BXCAN || CAN_CFG_ENABLE_BACKEND_STM32_FDCAN) && \
    (CAN_CFG_PLATFORM != CAN_PLATFORM_STM32)
    #error "can_cfg.h: STM32 backends need CAN_CFG_PLATFORM = CAN_PLATFORM_STM32."
#endif

#if CAN_CFG_ENABLE_BACKEND_ESP32_TWAI && (CAN_CFG_PLATFORM != CAN_PLATFORM_ESP32)
    #error "can_cfg.h: the TWAI backend needs CAN_CFG_PLATFORM = CAN_PLATFORM_ESP32."
#endif

#if CAN_CFG_ENABLE_FD && !(CAN_CFG_ENABLE_BACKEND_STM32_FDCAN || CAN_CFG_ENABLE_BACKEND_MCP251XFD || \
                           CAN_CFG_ENABLE_BACKEND_VIRTUAL)
    #error "can_cfg.h: CAN FD needs the FDCAN, MCP251XFD, or VIRTUAL backend (MCP2515 is classic CAN only)."
#endif

#if CAN_CFG_FDCAN_MESSAGE_RAM && ((CAN_CFG_FDCAN_RAM_TX_ELEMENTS < 1) || (CAN_CFG_FDCAN_RAM_TX_ELEMENTS > 32) || \
                                  (CAN_CFG_FDCAN_RAM_RX_ELEMENTS < 1) || (CAN_CFG_FDCAN_RAM_RX_ELEMENTS > 64))
    #error "can_cfg.h: FDCAN RAM elements: TX 1..32, RX 1..64."
#endif

#if (CAN_CFG_DEBUG_LEVEL < 0) || (CAN_CFG_DEBUG_LEVEL > 4)
    #error "can_cfg.h: CAN_CFG_DEBUG_LEVEL must be 0..4."
#endif

#if (CAN_CFG_MAX_INSTANCES < 1)
    #error "can_cfg.h: CAN_CFG_MAX_INSTANCES must be at least 1."
#endif

#if CAN_CFG_ENABLE_RX_QUEUE && (CAN_CFG_RX_QUEUE_DEPTH < 2)
    #error "can_cfg.h: CAN_CFG_RX_QUEUE_DEPTH must be at least 2."
#endif

#if CAN_CFG_ENABLE_TX_QUEUE && (CAN_CFG_TX_QUEUE_DEPTH < 2)
    #error "can_cfg.h: CAN_CFG_TX_QUEUE_DEPTH must be at least 2."
#endif

#if CAN_CFG_ENABLE_FILTERS && (CAN_CFG_MAX_FILTERS < 1)
    #error "can_cfg.h: CAN_CFG_MAX_FILTERS must be at least 1."
#endif

#if CAN_CFG_USE_FREERTOS && (CAN_CFG_RTOS_POLL_MS < 1)
    #error "can_cfg.h: CAN_CFG_RTOS_POLL_MS must be at least 1."
#endif

#if CAN_CFG_USE_FREERTOS && (CAN_CFG_RTOS_TASK_STACK_WORDS < 128)
    #error "can_cfg.h: CAN_CFG_RTOS_TASK_STACK_WORDS is too small (at least 128)."
#endif

#endif /* CAN_CFG_H */
