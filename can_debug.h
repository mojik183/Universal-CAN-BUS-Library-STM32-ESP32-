/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_debug.h
 * @brief   Error / warning reporting, logging macros, and parameter checks.
 *
 * Three ways to find a problem quickly:
 *   1. Watch window : can.status.last_error, can.debug.last_error_file/line,
 *                     can.debug.log[] (event history), and the globals
 *                     g_CanLastError / g_CanLastWarning.
 *   2. printf       : CAN_LOGE / CAN_LOGW / CAN_LOGI / CAN_LOGD (optional).
 *   3. Return codes : negative = error, positive = warning (see CanResult_e).
 */
#ifndef CAN_DEBUG_H
#define CAN_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "can_cfg.h"
#include "can_types.h"

#if CAN_CFG_ENABLE_DEBUG && CAN_CFG_DEBUG_USE_PRINTF
    #include CAN_CFG_PRINTF_HEADER
#endif

/* ======================================================================== */
/* GLOBALS FOR THE WATCH WINDOW (valid even without an instance pointer)    */
/* ======================================================================== */

extern volatile CanResult_e g_CanLastError;     /**< Most recent error of any instance. */
extern volatile CanResult_e g_CanLastWarning;   /**< Most recent warning of any instance. */
extern volatile uint32_t    g_CanErrorTotal;    /**< Errors reported by all instances. */
extern volatile uint32_t    g_CanWarningTotal;  /**< Warnings reported by all instances. */
extern volatile uint32_t    g_CanAssertLine;    /**< Line of the last failed CAN_ASSERT (0 = none). */
extern const char * volatile g_CanAssertFile;   /**< File of the last failed CAN_ASSERT. */

/* ======================================================================== */
/* FUNCTIONS                                                                */
/* ======================================================================== */

/**
 * @brief  Millisecond tick used for timestamps. Weak default returns 0.
 * @note   The platform port overrides it (HAL_GetTick, xTaskGetTickCount, ...).
 * @return Time in ms.
 */
uint32_t CanLib_GetTickMs(void);

/**
 * @brief  Convert a result code to a readable string, e.g. "CAN_ERR_TX_FULL".
 * @param  result Result code.
 * @return Constant string, never NULL.
 */
const char *CanLib_ResultToStr(CanResult_e result);

/**
 * @brief  Check whether a log level is currently enabled (compile time and runtime).
 * @param  can   Instance, or NULL to use only the compile-time level.
 * @param  level Level to test.
 * @return true when messages of this level should be produced.
 */
bool CanDbg_LevelEnabled(const CanLib_t *can, CanLogLevel_e level);

/**
 * @brief  Record a warning or error: updates status, counters, last error
 *         location, event log, and prints a line if printf is enabled.
 * @param  can  Instance, or NULL (only globals are updated).
 * @param  code Result code. CAN_OK is ignored.
 * @param  file Source file (from __FILE__).
 * @param  line Source line (from __LINE__).
 * @param  func Function name (from __func__).
 */
void CanDbg_Report(CanLib_t *can, CanResult_e code, const char *file, uint32_t line, const char *func);

/**
 * @brief  Append an entry to the instance event log ring.
 * @param  can   Instance (NULL is ignored).
 * @param  event Event type.
 * @param  code  CanResult_e value, or 0.
 * @param  arg   Extra value (identifier, state, line, ...).
 */
void CanDbg_LogEvent(CanLib_t *can, CanLogEvent_e event, int32_t code, uint32_t arg);

/**
 * @brief  Reset error counters, last-error info, and the event log of an instance.
 * @param  can Instance (NULL is ignored).
 */
void CanDbg_Clear(CanLib_t *can);

/**
 * @brief  Called when CAN_ASSERT fails. Stores file/line in globals, then halts.
 * @note   Weak: override it to reset, log, or break into the debugger.
 * @param  file Source file.
 * @param  line Source line.
 */
void CanDbg_AssertFailed(const char *file, uint32_t line);

/* ======================================================================== */
/* SOURCE LOCATION MACROS                                                   */
/* ======================================================================== */

#if CAN_CFG_ENABLE_DEBUG
    #define CAN_DBG_FILE    __FILE__                /**< Current source file. */
    #define CAN_DBG_LINE    ((uint32_t)__LINE__)    /**< Current source line. */
    #define CAN_DBG_FUNC    __func__                /**< Current function name. */
#else
    #define CAN_DBG_FILE    NULL                    /**< Debug off: no file text stored. */
    #define CAN_DBG_LINE    0U                      /**< Debug off: no line stored. */
    #define CAN_DBG_FUNC    NULL                    /**< Debug off: no function stored. */
#endif

/* ======================================================================== */
/* REPORTING MACROS                                                         */
/* ======================================================================== */

/** Report an error or warning and continue. */
#define CAN_REPORT(can, code) \
    CanDbg_Report((can), (code), CAN_DBG_FILE, CAN_DBG_LINE, CAN_DBG_FUNC)

/** Report an error and return it from the current function. */
#define CAN_RETURN_ERR(can, code) \
    do { CAN_REPORT((can), (code)); return (code); } while (0)

/** Report a warning and continue (use when the work still succeeded). */
#define CAN_WARN(can, code)     CAN_REPORT((can), (code))

/** Return CAN_ERR_NULL_PTR when the instance pointer is NULL. */
#define CAN_CHECK_HANDLE(can) \
    do { if ((can) == NULL) { CAN_REPORT(NULL, CAN_ERR_NULL_PTR); return CAN_ERR_NULL_PTR; } } while (0)

/** Return CAN_ERR_NULL_PTR / CAN_ERR_NOT_INIT unless the instance is initialized. */
#define CAN_CHECK_INIT(can) \
    do { \
        CAN_CHECK_HANDLE(can); \
        if ((can)->magic != CAN_LIB_MAGIC) { CAN_RETURN_ERR((can), CAN_ERR_NOT_INIT); } \
    } while (0)

/** Return CAN_ERR_BAD_PARAM when the condition is false (compiled out if param check is off). */
#if CAN_CFG_ENABLE_PARAM_CHECK
    #define CAN_CHECK_PARAM(can, cond) \
        do { if (!(cond)) { CAN_RETURN_ERR((can), CAN_ERR_BAD_PARAM); } } while (0)
#else
    /* The condition stays inside sizeof (never evaluated) so variables used only
       by the check do not raise "unused" warnings when checking is off. */
    #define CAN_CHECK_PARAM(can, cond)  do { (void)(can); (void)sizeof(cond); } while (0)
#endif

/** Internal consistency check. Calls CanDbg_AssertFailed() when false. */
#if CAN_CFG_ENABLE_ASSERT
    #define CAN_ASSERT(cond) \
        do { if (!(cond)) { CanDbg_AssertFailed(__FILE__, (uint32_t)__LINE__); } } while (0)
#else
    #define CAN_ASSERT(cond)    do { (void)sizeof(cond); } while (0)
#endif

/* ======================================================================== */
/* PRINTF LOGGING MACROS (empty when printf logging is off)                 */
/* ======================================================================== */

#if CAN_CFG_ENABLE_DEBUG && CAN_CFG_DEBUG_USE_PRINTF
    /** Internal: print one tagged line when the level is enabled. */
    #define CAN_LOG_(can, level, tag, ...) \
        do { \
            if (CanDbg_LevelEnabled((can), (level))) { \
                CAN_CFG_PRINTF_FN("[CAN][" tag "] "); \
                CAN_CFG_PRINTF_FN(__VA_ARGS__); \
                CAN_CFG_PRINTF_FN("\r\n"); \
            } \
        } while (0)
    #define CAN_LOGE(can, ...)  CAN_LOG_((can), CAN_LOG_ERROR, "E", __VA_ARGS__) /**< Log an error. */
    #define CAN_LOGW(can, ...)  CAN_LOG_((can), CAN_LOG_WARN,  "W", __VA_ARGS__) /**< Log a warning. */
    #define CAN_LOGI(can, ...)  CAN_LOG_((can), CAN_LOG_INFO,  "I", __VA_ARGS__) /**< Log information. */
    #define CAN_LOGD(can, ...)  CAN_LOG_((can), CAN_LOG_DEBUG, "D", __VA_ARGS__) /**< Log debug detail. */
#else
    #define CAN_LOGE(can, ...)  ((void)(can))   /**< Log an error (disabled). */
    #define CAN_LOGW(can, ...)  ((void)(can))   /**< Log a warning (disabled). */
    #define CAN_LOGI(can, ...)  ((void)(can))   /**< Log information (disabled). */
    #define CAN_LOGD(can, ...)  ((void)(can))   /**< Log debug detail (disabled). */
#endif

#ifdef __cplusplus
}
#endif

#endif /* CAN_DEBUG_H */
