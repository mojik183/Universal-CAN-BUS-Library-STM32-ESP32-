/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_types.h
 * @brief   All enums and structs of the CAN library, and the main struct.
 *
 * The main struct is CanLib_t. Create one per CAN channel and add it to the
 * Keil watch window: every bus parameter, header, buffer, status value and
 * counter is a plain field inside it.
 *
 *   CanLib_t
 *    +-- cfg      bus configuration (mode, bit timing, options)
 *    +-- hw       pins and handles of the chip
 *    +-- rx       last received header/body + receive ring buffer
 *    +-- tx       header/body to send + transmit ring buffer + manual send
 *    +-- filters  software acceptance filters
 *    +-- status   state, last result, last error/warning
 *    +-- bus      hardware error counters (TEC, REC, bus state)
 *    +-- stats    traffic and error statistics
 *    +-- debug    log level, last error location, event log
 *    +-- cb       user callbacks
 *    +-- rtos     FreeRTOS handles (only when enabled)
 */
#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "can_cfg.h"

/* ======================================================================== */
/* CONSTANTS                                                                */
/* ======================================================================== */

#define CAN_CLASSIC_MAX_DLEN    8U          /**< Max payload bytes of a classic CAN frame. */
#define CAN_FD_MAX_DLEN         64U         /**< Max payload bytes of a CAN FD frame. */
#define CAN_STD_ID_MAX          0x7FFUL     /**< Largest 11-bit identifier. */
#define CAN_EXT_ID_MAX          0x1FFFFFFFUL /**< Largest 29-bit identifier. */
#define CAN_FILTER_NONE         0xFFU       /**< filter_index value: no filter matched. */
#define CAN_LIB_MAGIC           0xCA17ABCDUL /**< Value written to CanLib_t.magic when initialized. */

/** Payload buffer size: 64 bytes when FD is enabled, otherwise 8 bytes. */
#if CAN_CFG_ENABLE_FD
    #define CAN_MAX_DATA_LEN    CAN_FD_MAX_DLEN
#else
    #define CAN_MAX_DATA_LEN    CAN_CLASSIC_MAX_DLEN
#endif

/* ======================================================================== */
/* RESULT CODES: 0 = OK, positive = WARNING, negative = ERROR               */
/* ======================================================================== */

/**
 * @brief Result of every library function.
 *
 * Positive values are warnings (the call worked but something needs
 * attention). Negative values are errors (the call failed).
 * Use CAN_IS_ERR() / CAN_IS_WARN() to test them and CanLib_ResultToStr()
 * to print them.
 */
typedef enum
{
    CAN_OK                          = 0,    /**< Success. */

    /* ---- Warnings (> 0): operation done, check the cause ---- */
    CAN_WARN_RX_QUEUE_NEAR_FULL     = 1,    /**< Receive buffer above 75 % - read faster. */
    CAN_WARN_TX_QUEUE_NEAR_FULL     = 2,    /**< Transmit buffer above 75 % - send slower. */
    CAN_WARN_ERROR_WARNING          = 3,    /**< Error counters reached the warning level (96). */
    CAN_WARN_ERROR_PASSIVE          = 4,    /**< Node is error passive (counters above 127). */
    CAN_WARN_DLC_CLAMPED            = 5,    /**< Data length was too big and was reduced. */
    CAN_WARN_FD_FALLBACK            = 6,    /**< FD requested but not available: sent as classic. */
    CAN_WARN_FILTER_IGNORED         = 7,    /**< Filter not applied (filters disabled in cfg). */
    CAN_WARN_BUS_RECOVERED          = 8,    /**< Bus-off was left and the node is back on the bus. */

    /* ---- Errors (< 0): operation failed ---- */
    CAN_ERR_NULL_PTR                = -1,   /**< A required pointer argument was NULL. */
    CAN_ERR_NOT_INIT                = -2,   /**< Instance used before CanLib_Init(). */
    CAN_ERR_BAD_PARAM               = -3,   /**< An argument is out of range. */
    CAN_ERR_BAD_STATE               = -4,   /**< Call not allowed in the current state. */
    CAN_ERR_BACKEND_DISABLED        = -5,   /**< Backend is switched off in can_cfg.h. */
    CAN_ERR_BACKEND_FAIL            = -6,   /**< Backend (HAL / driver) returned a failure. */
    CAN_ERR_SPI                     = -7,   /**< SPI transfer to the MCP25xx failed. */
    CAN_ERR_CHIP_NOT_FOUND          = -8,   /**< MCP25xx did not answer (wiring, power, clock). */
    CAN_ERR_BITRATE                 = -9,   /**< No valid bit timing for the requested bitrate. */
    CAN_ERR_TX_FULL                 = -10,  /**< No free transmit mailbox or queue slot. */
    CAN_ERR_RX_EMPTY                = -11,  /**< No received frame available. */
    CAN_ERR_RX_OVERFLOW             = -12,  /**< A received frame was lost (buffer full). */
    CAN_ERR_TX_TIMEOUT              = -13,  /**< Transmission did not finish in time. */
    CAN_ERR_BUS_OFF                 = -14,  /**< Node is bus-off and cannot transmit. */
    CAN_ERR_ACK                     = -15,  /**< No node acknowledged the frame (bus alone?). */
    CAN_ERR_STUFF                   = -16,  /**< Bit stuffing error on the bus. */
    CAN_ERR_FORM                    = -17,  /**< Frame format error on the bus. */
    CAN_ERR_CRC                     = -18,  /**< CRC error on the bus. */
    CAN_ERR_BIT                     = -19,  /**< Bit error (bus level differs from sent level). */
    CAN_ERR_FILTER_FULL             = -20,  /**< No free filter slot. */
    CAN_ERR_MUTEX                   = -21,  /**< Could not take the instance mutex. */
    CAN_ERR_RTOS                    = -22,  /**< FreeRTOS object creation or call failed. */
    CAN_ERR_NOT_SUPPORTED           = -23,  /**< Feature not supported by this backend or chip. */
    CAN_ERR_BUSY                    = -24,  /**< Resource busy, try again. */
    CAN_ERR_TIMEOUT                 = -25   /**< Generic timeout. */
} CanResult_e;

/** True when the result is an error (negative). */
#define CAN_IS_ERR(r)       ((r) < 0)
/** True when the result is a warning (positive). */
#define CAN_IS_WARN(r)      ((r) > 0)
/** True when the result is exactly CAN_OK. */
#define CAN_IS_OK(r)        ((r) == CAN_OK)

/* ======================================================================== */
/* ENUMS                                                                    */
/* ======================================================================== */

/** @brief CAN controller backend used by one instance. */
typedef enum
{
    CAN_BACKEND_NONE = 0,       /**< Not selected yet. */
    CAN_BACKEND_STM32_BXCAN,    /**< STM32 bxCAN peripheral. */
    CAN_BACKEND_STM32_FDCAN,    /**< STM32 FDCAN peripheral. */
    CAN_BACKEND_ESP32_TWAI,     /**< ESP32 TWAI peripheral. */
    CAN_BACKEND_MCP25XX,        /**< External MCP25xx over SPI. */
    CAN_BACKEND_VIRTUAL,        /**< Software loopback for PC tests (no hardware). */
    CAN_BACKEND_MCP251XFD       /**< External MCP2517FD / MCP2518FD over SPI (CAN FD). */
} CanBackend_e;

/** @brief Chip variant when the backend is MCP25XX or MCP251XFD. */
typedef enum
{
    CAN_MCP_NONE = 0,           /**< Not an MCP25xx backend. */
    CAN_MCP_2515,               /**< MCP2515: classic CAN. */
    CAN_MCP_2517FD,             /**< MCP2517FD: CAN FD. */
    CAN_MCP_2518FD              /**< MCP2518FD: CAN FD. */
} CanMcpVariant_e;

/** @brief Transceiver family on the board. */
typedef enum
{
    CAN_XCVR_NONE = 0,          /**< No transceiver control. */
    CAN_XCVR_TJA10XX,           /**< NXP TJA10xx family. */
    CAN_XCVR_ISO10XX            /**< Isolated ISO10xx family. */
} CanXcvrType_e;

/** @brief Transceiver operating mode set through its mode pin. */
typedef enum
{
    CAN_XCVR_MODE_NORMAL = 0,   /**< Transmit and receive. */
    CAN_XCVR_MODE_SILENT,       /**< Receive only. */
    CAN_XCVR_MODE_STANDBY       /**< Low power, bus wake-up only. */
} CanXcvrMode_e;

/** @brief CAN controller operating mode. */
typedef enum
{
    CANLIB_MODE_NORMAL = 0,        /**< Normal bus operation. */
    CANLIB_MODE_LISTEN_ONLY,       /**< Receive only, never drives the bus (no ACK). */
    CANLIB_MODE_LOOPBACK           /**< Internal loopback for self test, no bus needed. */
} CanMode_e;

/** @brief Identifier length. */
typedef enum
{
    CAN_ID_STANDARD = 0,        /**< 11-bit identifier. */
    CAN_ID_EXTENDED             /**< 29-bit identifier. */
} CanIdType_e;

/** @brief Frame type. */
typedef enum
{
    CAN_FRAME_DATA = 0,         /**< Data frame. */
    CAN_FRAME_REMOTE            /**< Remote transmission request. */
} CanFrameType_e;

/** @brief Frame format. */
typedef enum
{
    CAN_FORMAT_CLASSIC = 0,     /**< Classic CAN, up to 8 data bytes. */
    CAN_FORMAT_FD               /**< CAN FD, up to 64 data bytes. */
} CanFrameFormat_e;

/** @brief Common nominal bitrates in bit/s. Use CAN_BITRATE_CUSTOM for others. */
typedef enum
{
    CAN_BITRATE_CUSTOM  = 0,        /**< Use the value in CanTiming_t.bitrate. */
    CAN_BITRATE_125K    = 125000,   /**< 125 kbit/s. */
    CAN_BITRATE_250K    = 250000,   /**< 250 kbit/s. */
    CAN_BITRATE_500K    = 500000,   /**< 500 kbit/s. */
    CAN_BITRATE_1M      = 1000000   /**< 1 Mbit/s. */
} CanBitrate_e;

/** @brief Common FD data-phase bitrates in bit/s. */
typedef enum
{
    CAN_FD_BITRATE_1M   = 1000000,  /**< 1 Mbit/s data phase. */
    CAN_FD_BITRATE_2M   = 2000000,  /**< 2 Mbit/s data phase. */
    CAN_FD_BITRATE_4M   = 4000000,  /**< 4 Mbit/s data phase. */
    CAN_FD_BITRATE_5M   = 5000000   /**< 5 Mbit/s data phase. */
} CanFdBitrate_e;

/** @brief Software filter mode. */
typedef enum
{
    CAN_FILTER_MASK = 0,        /**< Accept when (id & mask) == (filter id & mask). */
    CAN_FILTER_EXACT            /**< Accept only the exact identifier. */
} CanFilterMode_e;

/** @brief Life-cycle state of one instance. */
typedef enum
{
    CAN_STATE_UNINIT = 0,       /**< Not initialized. */
    CAN_STATE_READY,            /**< Initialized, bus not started. */
    CAN_STATE_RUNNING,          /**< On the bus. */
    CAN_STATE_STOPPED,          /**< Stopped by the user. */
    CAN_STATE_FAULT             /**< Failed, see status.last_error. */
} CanState_e;

/** @brief Bus error state of the node (from the error counters). */
typedef enum
{
    CAN_BUS_ERROR_ACTIVE = 0,   /**< Normal: counters below 96. */
    CAN_BUS_ERROR_WARNING,      /**< Counters 96..127. */
    CAN_BUS_ERROR_PASSIVE,      /**< Counters above 127. */
    CAN_BUS_OFF                 /**< Transmit counter above 255: node left the bus. */
} CanBusState_e;

/** @brief Log level used by printf logging and the runtime filter. */
typedef enum
{
    CAN_LOG_NONE = 0,           /**< No output. */
    CAN_LOG_ERROR,              /**< Errors only. */
    CAN_LOG_WARN,               /**< Errors and warnings. */
    CAN_LOG_INFO,               /**< Plus state changes. */
    CAN_LOG_DEBUG               /**< Plus every frame. */
} CanLogLevel_e;

/** @brief Type of an entry in the event log ring. */
typedef enum
{
    CAN_LOG_EV_NONE = 0,        /**< Empty slot. */
    CAN_LOG_EV_INIT,            /**< Instance initialized. */
    CAN_LOG_EV_START,           /**< Bus started. */
    CAN_LOG_EV_STOP,            /**< Bus stopped. */
    CAN_LOG_EV_TX,              /**< Frame sent (arg = identifier). */
    CAN_LOG_EV_RX,              /**< Frame received (arg = identifier). */
    CAN_LOG_EV_WARN,            /**< Warning reported (code = CanResult_e). */
    CAN_LOG_EV_ERROR,           /**< Error reported (code = CanResult_e). */
    CAN_LOG_EV_BUS_OFF,         /**< Node went bus-off. */
    CAN_LOG_EV_RECOVER,         /**< Node recovered from bus-off. */
    CAN_LOG_EV_STATE            /**< Life-cycle state changed (arg = new state). */
} CanLogEvent_e;

/** @brief Event passed to the user event callback. */
typedef enum
{
    CAN_EVENT_TX_DONE = 0,      /**< A frame was sent. */
    CAN_EVENT_RX_OVERFLOW,      /**< A received frame was lost. */
    CAN_EVENT_ERROR,            /**< A bus or driver error happened. */
    CAN_EVENT_BUS_OFF,          /**< Node went bus-off. */
    CAN_EVENT_BUS_RECOVERED,    /**< Node recovered from bus-off. */
    CAN_EVENT_STATE_CHANGE      /**< Life-cycle state changed. */
} CanEvent_e;

/* ======================================================================== */
/* FRAME STRUCTS                                                            */
/* ======================================================================== */

/** @brief Header (everything except the payload) of one CAN frame. */
typedef struct
{
    uint32_t         id;            /**< Identifier (11 or 29 bit, see id_type). */
    CanIdType_e      id_type;       /**< Standard or extended identifier. */
    CanFrameType_e   frame_type;    /**< Data or remote frame. */
    CanFrameFormat_e format;        /**< Classic or FD. */
    bool             bitrate_switch;/**< FD only: send the data phase at the faster rate (BRS). */
    bool             error_state;   /**< FD only: sender is error passive (ESI). */
    uint8_t          dlc;           /**< Raw DLC code 0..15 as on the bus. */
    uint8_t          data_len;      /**< Number of payload bytes (0..8, or 0..64 for FD). */
    uint8_t          filter_index;  /**< RX only: filter that matched, CAN_FILTER_NONE if none. */
    uint32_t         timestamp_ms;  /**< RX: time received. TX: time sent. */
} CanHeader_t;

/** @brief Payload (body) of one CAN frame. */
typedef struct
{
    uint8_t data[CAN_MAX_DATA_LEN]; /**< Payload bytes, valid up to header.data_len. */
} CanBody_t;

/** @brief One complete CAN frame: header and body. */
typedef struct
{
    CanHeader_t header;             /**< Frame header. */
    CanBody_t   body;               /**< Frame payload. */
} CanFrame_t;

/** @brief Bookkeeping of a frame ring buffer. */
typedef struct
{
    uint16_t head;                  /**< Index where the next frame is written. */
    uint16_t tail;                  /**< Index where the next frame is read. */
    uint16_t count;                 /**< Frames currently stored. */
    uint16_t capacity;              /**< Total slots in the buffer. */
    uint16_t high_water;            /**< Highest count ever reached (size your buffer with it). */
    uint32_t dropped;               /**< Frames lost because the buffer was full. */
} CanQueueInfo_t;

/* ======================================================================== */
/* CONFIGURATION STRUCTS                                                    */
/* ======================================================================== */

/** @brief Bit timing of one bus phase (nominal or FD data). */
typedef struct
{
    uint32_t bitrate;               /**< Requested bitrate in bit/s (use CanBitrate_e values). */
    uint16_t sample_point;          /**< Sample point in 1/1000 (875 = 87.5 %). */
    uint16_t prescaler;             /**< Baud rate prescaler (filled by the library). */
    uint8_t  tseg1;                 /**< Time segment 1 in quanta (filled by the library). */
    uint8_t  tseg2;                 /**< Time segment 2 in quanta (filled by the library). */
    uint8_t  sjw;                   /**< Synchronization jump width in quanta. */
    uint32_t actual_bitrate;        /**< Bitrate really achieved (compare with bitrate). */
} CanTiming_t;

/** @brief Runtime bus configuration of one instance. */
typedef struct
{
    CanBackend_e    backend;        /**< Controller backend to use. */
    CanMcpVariant_e mcp_variant;    /**< MCP25xx chip variant (MCP backend only). */
    CanMode_e       mode;           /**< Normal, listen-only, or loopback. */
    CanXcvrType_e   xcvr_type;      /**< Transceiver family (for mode pin control). */
    CanTiming_t     nominal;        /**< Nominal (arbitration) bit timing. */
#if CAN_CFG_ENABLE_FD
    bool            fd_enabled;     /**< true = CAN FD operation. */
    CanTiming_t     data;           /**< FD data-phase bit timing. */
#endif
    bool            auto_retransmit;        /**< true = resend automatically after a lost frame. */
    bool            rx_overwrite_oldest;    /**< true = full RX buffer drops the oldest frame. */
#if CAN_CFG_ENABLE_BUS_OFF_RECOVERY
    bool            bus_off_auto_recover;   /**< true = rejoin the bus automatically after bus-off. */
    uint32_t        bus_off_recover_ms;     /**< Wait time before rejoining, in ms. */
#endif
} CanConfig_t;

/** @brief Hardware description: handles and pins. Unused fields stay 0 / NULL. */
typedef struct
{
    void     *handle;               /**< STM32: CAN/FDCAN handle. MCP25xx: SPI handle. */
    void     *cs_port;              /**< MCP25xx: chip-select GPIO port. */
    uint16_t  cs_pin;               /**< MCP25xx: chip-select GPIO pin. */
    void     *int_port;             /**< MCP25xx: interrupt GPIO port. */
    uint16_t  int_pin;              /**< MCP25xx: interrupt GPIO pin. */
    uint32_t  spi_clock_hz;         /**< MCP25xx: SPI clock in Hz. */
    uint32_t  osc_hz;               /**< MCP25xx: crystal/oscillator frequency in Hz. */
    int16_t   tx_gpio;              /**< ESP32 TWAI: TX GPIO number (-1 = unused). */
    int16_t   rx_gpio;              /**< ESP32 TWAI: RX GPIO number (-1 = unused). */
    void     *xcvr_port;            /**< Transceiver mode pin GPIO port (NULL = unused). */
    uint16_t  xcvr_pin;             /**< Transceiver mode pin GPIO pin. */
    bool      xcvr_standby_level;   /**< Pin level (true = high) that puts the transceiver in standby. */
} CanHwCfg_t;

#if CAN_CFG_ENABLE_FILTERS
/** @brief One software acceptance filter. */
typedef struct
{
    bool            enabled;        /**< true = this slot is active. */
    CanFilterMode_e mode;           /**< Mask or exact match. */
    CanIdType_e     id_type;        /**< Identifier length this filter applies to. */
    uint32_t        id;             /**< Identifier to match. */
    uint32_t        mask;           /**< Mask for CAN_FILTER_MASK (1 bits are compared). */
} CanFilter_t;
#endif

/* ======================================================================== */
/* RX / TX BLOCKS                                                           */
/* ======================================================================== */

/** @brief Receive side of one instance. */
typedef struct
{
    CanHeader_t    header;          /**< Header of the most recently received frame. */
    CanBody_t      body;            /**< Payload of the most recently received frame. */
    bool           new_frame;       /**< true = a frame arrived since the last read. */
#if CAN_CFG_ENABLE_RX_QUEUE
    CanQueueInfo_t queue;           /**< Receive ring buffer bookkeeping. */
    CanFrame_t     buffer[CAN_CFG_RX_QUEUE_DEPTH]; /**< Receive ring buffer storage. */
#endif
} CanRxData_t;

/** @brief Transmit side of one instance. */
typedef struct
{
    CanHeader_t    header;          /**< Header of the frame to send (manual send uses this). */
    CanBody_t      body;            /**< Payload of the frame to send (manual send uses this). */
#if CAN_CFG_ENABLE_MANUAL_SEND
    bool           manual_request;  /**< Set to true (also from the watch window) to send header+body. */
#endif
#if CAN_CFG_ENABLE_TX_QUEUE
    CanQueueInfo_t queue;           /**< Transmit ring buffer bookkeeping. */
    CanFrame_t     buffer[CAN_CFG_TX_QUEUE_DEPTH]; /**< Transmit ring buffer storage. */
#endif
} CanTxData_t;

/* ======================================================================== */
/* STATUS, STATISTICS, DEBUG                                                */
/* ======================================================================== */

/** @brief Live status values of one instance. */
typedef struct
{
    CanState_e    state;            /**< Life-cycle state. */
    CanBusState_e bus_state;        /**< Error active / warning / passive / bus-off. */
    CanResult_e   last_result;      /**< Result of the most recent reported problem. */
    CanResult_e   last_error;       /**< Most recent error (negative code). */
    CanResult_e   last_warning;     /**< Most recent warning (positive code). */
    uint32_t      error_count;      /**< Total errors reported. */
    uint32_t      warning_count;    /**< Total warnings reported. */
    uint16_t      rx_pending;       /**< Frames waiting in the receive buffer. */
    uint16_t      tx_pending;       /**< Frames waiting in the transmit buffer. */
    bool          tx_busy;          /**< true = a frame is currently being sent. */
    bool          initialized;      /**< true = CanLib_Init() succeeded. */
    bool          running;          /**< true = the node is on the bus. */
} CanStatus_t;

/** @brief Hardware error counters and bus state. */
typedef struct
{
    uint8_t       tec;              /**< Transmit error counter. */
    uint8_t       rec;              /**< Receive error counter. */
    uint32_t      hw_error_flags;   /**< Raw error / status register of the chip (for debugging). */
    uint32_t      bus_off_since_ms; /**< Time the node went bus-off (used by auto recovery). */
} CanBusCounters_t;

#if CAN_CFG_ENABLE_STATS
/** @brief Traffic and error statistics. */
typedef struct
{
    uint32_t tx_ok;                 /**< Frames sent successfully. */
    uint32_t tx_fail;               /**< Frames that failed to send. */
    uint32_t rx_ok;                 /**< Frames received. */
    uint32_t rx_dropped;            /**< Frames rejected by filters or lost to overflow. */
    uint32_t err_ack;               /**< ACK errors. */
    uint32_t err_stuff;             /**< Stuff errors. */
    uint32_t err_form;              /**< Form errors. */
    uint32_t err_crc;               /**< CRC errors. */
    uint32_t err_bit;               /**< Bit errors. */
    uint32_t bus_off_count;         /**< Times the node went bus-off. */
    uint32_t last_tx_ms;            /**< Time of the last sent frame. */
    uint32_t last_rx_ms;            /**< Time of the last received frame. */
} CanStats_t;
#endif

/** @brief One entry of the event log ring. */
typedef struct
{
    uint32_t      tick_ms;          /**< Time of the event in ms. */
    CanLogEvent_e event;            /**< What happened. */
    int32_t       code;             /**< CanResult_e value for warning/error events. */
    uint32_t      arg;              /**< Extra value (identifier, state, line number). */
} CanLogEntry_t;

/** @brief Debug information of one instance. */
typedef struct
{
    CanLogLevel_e log_level;        /**< Runtime log level (capped by CAN_CFG_DEBUG_LEVEL). */
#if CAN_CFG_ENABLE_DEBUG
    const char   *last_error_file;  /**< Source file where the last error was reported. */
    const char   *last_error_func;  /**< Function where the last error was reported. */
    uint32_t      last_error_line;  /**< Line where the last error was reported. */
    uint32_t      last_error_tick_ms;/**< Time of the last error. */
#if CAN_CFG_DEBUG_EVENT_LOG_SIZE > 0
    CanLogEntry_t log[CAN_CFG_DEBUG_EVENT_LOG_SIZE]; /**< Event log ring (oldest overwritten). */
    uint16_t      log_head;         /**< Index of the next log slot to write. */
    uint16_t      log_count;        /**< Valid entries in the log. */
#endif
#endif
} CanDebug_t;

/* ======================================================================== */
/* CALLBACKS                                                                */
/* ======================================================================== */

/** Forward declaration of the main struct (needed by the callback types). */
typedef struct CanLib_s CanLib_t;

/** Called when a frame passed the filters and was stored (may run in an ISR). */
typedef void (*CanRxCallback_t)(CanLib_t *can, const CanFrame_t *frame, void *user_ctx);

/** Called for bus and driver events (may run in an ISR). */
typedef void (*CanEventCallback_t)(CanLib_t *can, CanEvent_e event, int32_t arg, void *user_ctx);

/** @brief User callbacks of one instance. */
typedef struct
{
#if CAN_CFG_ENABLE_CALLBACKS
    CanRxCallback_t    on_rx;       /**< Frame received (NULL = not used). */
    CanEventCallback_t on_event;    /**< Bus / driver event (NULL = not used). */
    void              *user_ctx;    /**< Passed back to both callbacks. */
#else
    uint8_t            unused;      /**< Keeps the struct non-empty when callbacks are off. */
#endif
} CanCallbacks_t;

/* ======================================================================== */
/* FREERTOS BLOCK                                                           */
/* ======================================================================== */

#if CAN_CFG_USE_FREERTOS
/** @brief FreeRTOS objects of one instance (stored as void* to keep this header OS-free). */
typedef struct
{
    void    *mutex;                 /**< Mutex protecting the instance (SemaphoreHandle_t). */
    void    *rx_sem;                /**< Semaphore given when a frame arrives (SemaphoreHandle_t). */
    void    *task;                  /**< Library task (TaskHandle_t). */
    uint32_t stack_free_words;      /**< Smallest free stack of the task so far (health check). */
    uint32_t wake_count;            /**< Times the library task woke up (debug). */
    uint32_t lock_timeouts;         /**< Times the instance mutex could not be taken in time (debug). */
} CanRtos_t;
#endif

/* ======================================================================== */
/* MAIN STRUCT                                                              */
/* ======================================================================== */

/** Forward declaration of the backend function table (defined in a later step). */
struct CanPortOps_s;

/**
 * @brief Main struct: one per CAN channel. Add it to the Keil watch window.
 */
struct CanLib_s
{
    uint32_t                    magic;      /**< CAN_LIB_MAGIC after a successful init. */
    uint8_t                     instance;   /**< Instance number (0..CAN_CFG_MAX_INSTANCES-1). */
    const char                 *name;       /**< Short name for log lines, e.g. "can1". */

    CanConfig_t                 cfg;        /**< Bus configuration. */
    CanHwCfg_t                  hw;         /**< Pins and handles. */

    CanRxData_t                 rx;         /**< Receive header, body, and buffer. */
    CanTxData_t                 tx;         /**< Transmit header, body, buffer, manual send. */

#if CAN_CFG_ENABLE_FILTERS
    CanFilter_t                 filters[CAN_CFG_MAX_FILTERS]; /**< Software filter table. */
    uint8_t                     filter_count;                 /**< Active filters. */
#endif

    CanStatus_t                 status;     /**< Live status. */
    CanBusCounters_t            bus;        /**< Hardware error counters. */
#if CAN_CFG_ENABLE_STATS
    CanStats_t                  stats;      /**< Traffic and error statistics. */
#endif
    CanDebug_t                  debug;      /**< Debug information. */
    CanCallbacks_t              cb;         /**< User callbacks. */
#if CAN_CFG_USE_FREERTOS
    CanRtos_t                   rtos;       /**< FreeRTOS objects. */
#endif

    const struct CanPortOps_s  *port;       /**< Backend function table (set by init). */
    void                       *port_ctx;   /**< Backend private data (set by init). */
};

#ifdef __cplusplus
}
#endif

#endif /* CAN_TYPES_H */
