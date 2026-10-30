/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_debug.c
 * @brief   Error / warning reporting and event log implementation.
 */
#include <string.h>
#include "can_lib.h"

/* ======================================================================== */
/* GLOBALS FOR THE WATCH WINDOW                                             */
/* ======================================================================== */

volatile CanResult_e g_CanLastError    = CAN_OK;    /**< Most recent error of any instance. */
volatile CanResult_e g_CanLastWarning  = CAN_OK;    /**< Most recent warning of any instance. */
volatile uint32_t    g_CanErrorTotal   = 0U;        /**< Errors reported by all instances. */
volatile uint32_t    g_CanWarningTotal = 0U;        /**< Warnings reported by all instances. */
volatile uint32_t    g_CanAssertLine   = 0U;        /**< Line of the last failed CAN_ASSERT. */
const char * volatile g_CanAssertFile  = NULL;      /**< File of the last failed CAN_ASSERT. */

/* ======================================================================== */
/* WEAK HOOKS                                                               */
/* ======================================================================== */

CAN_WEAK uint32_t CanLib_GetTickMs(void)
{
    /* Weak default. The platform port provides the real millisecond tick. */
    return 0U;
}

CAN_WEAK void CanDbg_AssertFailed(const char *file, uint32_t line)
{
    /* Keep the location in watchable globals, then stop here for the debugger. */
    g_CanAssertFile = file;
    g_CanAssertLine = line;
    for (;;)
    {
        /* Halted on purpose: look at g_CanAssertFile / g_CanAssertLine. */
    }
}

/* ======================================================================== */
/* RESULT TO STRING                                                         */
/* ======================================================================== */

const char *CanLib_ResultToStr(CanResult_e result)
{
    switch (result)
    {
        case CAN_OK:                        return "CAN_OK";
        case CAN_WARN_RX_QUEUE_NEAR_FULL:   return "CAN_WARN_RX_QUEUE_NEAR_FULL";
        case CAN_WARN_TX_QUEUE_NEAR_FULL:   return "CAN_WARN_TX_QUEUE_NEAR_FULL";
        case CAN_WARN_ERROR_WARNING:        return "CAN_WARN_ERROR_WARNING";
        case CAN_WARN_ERROR_PASSIVE:        return "CAN_WARN_ERROR_PASSIVE";
        case CAN_WARN_DLC_CLAMPED:          return "CAN_WARN_DLC_CLAMPED";
        case CAN_WARN_FD_FALLBACK:          return "CAN_WARN_FD_FALLBACK";
        case CAN_WARN_FILTER_IGNORED:       return "CAN_WARN_FILTER_IGNORED";
        case CAN_WARN_BUS_RECOVERED:        return "CAN_WARN_BUS_RECOVERED";
        case CAN_ERR_NULL_PTR:              return "CAN_ERR_NULL_PTR";
        case CAN_ERR_NOT_INIT:              return "CAN_ERR_NOT_INIT";
        case CAN_ERR_BAD_PARAM:             return "CAN_ERR_BAD_PARAM";
        case CAN_ERR_BAD_STATE:             return "CAN_ERR_BAD_STATE";
        case CAN_ERR_BACKEND_DISABLED:      return "CAN_ERR_BACKEND_DISABLED";
        case CAN_ERR_BACKEND_FAIL:          return "CAN_ERR_BACKEND_FAIL";
        case CAN_ERR_SPI:                   return "CAN_ERR_SPI";
        case CAN_ERR_CHIP_NOT_FOUND:        return "CAN_ERR_CHIP_NOT_FOUND";
        case CAN_ERR_BITRATE:               return "CAN_ERR_BITRATE";
        case CAN_ERR_TX_FULL:               return "CAN_ERR_TX_FULL";
        case CAN_ERR_RX_EMPTY:              return "CAN_ERR_RX_EMPTY";
        case CAN_ERR_RX_OVERFLOW:           return "CAN_ERR_RX_OVERFLOW";
        case CAN_ERR_TX_TIMEOUT:            return "CAN_ERR_TX_TIMEOUT";
        case CAN_ERR_BUS_OFF:               return "CAN_ERR_BUS_OFF";
        case CAN_ERR_ACK:                   return "CAN_ERR_ACK";
        case CAN_ERR_STUFF:                 return "CAN_ERR_STUFF";
        case CAN_ERR_FORM:                  return "CAN_ERR_FORM";
        case CAN_ERR_CRC:                   return "CAN_ERR_CRC";
        case CAN_ERR_BIT:                   return "CAN_ERR_BIT";
        case CAN_ERR_FILTER_FULL:           return "CAN_ERR_FILTER_FULL";
        case CAN_ERR_MUTEX:                 return "CAN_ERR_MUTEX";
        case CAN_ERR_RTOS:                  return "CAN_ERR_RTOS";
        case CAN_ERR_NOT_SUPPORTED:         return "CAN_ERR_NOT_SUPPORTED";
        case CAN_ERR_BUSY:                  return "CAN_ERR_BUSY";
        case CAN_ERR_TIMEOUT:               return "CAN_ERR_TIMEOUT";
        default:                            return "CAN_UNKNOWN_RESULT";
    }
}

/* ======================================================================== */
/* LOG LEVEL                                                                */
/* ======================================================================== */

bool CanDbg_LevelEnabled(const CanLib_t *can, CanLogLevel_e level)
{
#if CAN_CFG_ENABLE_DEBUG
    if ((int)level > CAN_CFG_DEBUG_LEVEL)
    {
        return false;                       /* Above the compile-time ceiling. */
    }
    if (can != NULL)
    {
        return ((int)level <= (int)can->debug.log_level);   /* Runtime filter. */
    }
    return true;
#else
    (void)can;
    (void)level;
    return false;
#endif
}

/* ======================================================================== */
/* EVENT LOG                                                                */
/* ======================================================================== */

void CanDbg_LogEvent(CanLib_t *can, CanLogEvent_e event, int32_t code, uint32_t arg)
{
#if CAN_CFG_ENABLE_DEBUG && (CAN_CFG_DEBUG_EVENT_LOG_SIZE > 0)
    CanLogEntry_t *entry;

    if (can == NULL)
    {
        return;
    }

    entry = &can->debug.log[can->debug.log_head];
    entry->tick_ms = CanLib_GetTickMs();
    entry->event   = event;
    entry->code    = code;
    entry->arg     = arg;

    can->debug.log_head = (uint16_t)((can->debug.log_head + 1U) % CAN_CFG_DEBUG_EVENT_LOG_SIZE);
    if (can->debug.log_count < CAN_CFG_DEBUG_EVENT_LOG_SIZE)
    {
        can->debug.log_count++;
    }
#else
    (void)can;
    (void)event;
    (void)code;
    (void)arg;
#endif
}

/* ======================================================================== */
/* REPORT                                                                   */
/* ======================================================================== */

void CanDbg_Report(CanLib_t *can, CanResult_e code, const char *file, uint32_t line, const char *func)
{
    if (code == CAN_OK)
    {
        return;                             /* Nothing to report. */
    }

    /* Global mirrors: visible in the watch window without an instance pointer. */
    if (CAN_IS_ERR(code))
    {
        g_CanLastError = code;
        g_CanErrorTotal++;
    }
    else
    {
        g_CanLastWarning = code;
        g_CanWarningTotal++;
    }

    if (can != NULL)
    {
        can->status.last_result = code;

        if (CAN_IS_ERR(code))
        {
            can->status.last_error = code;
            can->status.error_count++;
#if CAN_CFG_ENABLE_DEBUG
            can->debug.last_error_file    = file;
            can->debug.last_error_func    = func;
            can->debug.last_error_line    = line;
            can->debug.last_error_tick_ms = CanLib_GetTickMs();
#endif
        }
        else
        {
            can->status.last_warning = code;
            can->status.warning_count++;
        }

        CanDbg_LogEvent(can, CAN_IS_ERR(code) ? CAN_LOG_EV_ERROR : CAN_LOG_EV_WARN, (int32_t)code, line);
    }

#if CAN_CFG_ENABLE_DEBUG && CAN_CFG_DEBUG_USE_PRINTF
    {
        CanLogLevel_e level = CAN_IS_ERR(code) ? CAN_LOG_ERROR : CAN_LOG_WARN;

        if (CanDbg_LevelEnabled(can, level))
        {
            CAN_CFG_PRINTF_FN("[CAN][%s] %s: %s (%d) at %s:%lu %s()\r\n",
                              CAN_IS_ERR(code) ? "E" : "W",
                              (can != NULL && can->name != NULL) ? can->name : "can",
                              CanLib_ResultToStr(code),
                              (int)code,
                              (file != NULL) ? file : "?",
                              (unsigned long)line,
                              (func != NULL) ? func : "?");
        }
    }
#else
    (void)file;
    (void)line;
    (void)func;
#endif
}

/* ======================================================================== */
/* CLEAR                                                                    */
/* ======================================================================== */

void CanDbg_Clear(CanLib_t *can)
{
    if (can == NULL)
    {
        return;
    }

    can->status.last_result   = CAN_OK;
    can->status.last_error    = CAN_OK;
    can->status.last_warning  = CAN_OK;
    can->status.error_count   = 0U;
    can->status.warning_count = 0U;

#if CAN_CFG_ENABLE_DEBUG
    can->debug.last_error_file    = NULL;
    can->debug.last_error_func    = NULL;
    can->debug.last_error_line    = 0U;
    can->debug.last_error_tick_ms = 0U;
#if CAN_CFG_DEBUG_EVENT_LOG_SIZE > 0
    memset(can->debug.log, 0, sizeof(can->debug.log));
    can->debug.log_head  = 0U;
    can->debug.log_count = 0U;
#endif
#endif
}
