/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_core.c
 * @brief   Core of the CAN library: life cycle, send, receive, filters, process loop.
 */
#include <string.h>
#include "can_lib.h"

/* ======================================================================== */
/* INTERNAL MACROS                                                          */
/* ======================================================================== */

/** Statistics helpers: compile to nothing when CAN_CFG_ENABLE_STATS is 0. */
#if CAN_CFG_ENABLE_STATS
    #define CAN_STAT_INC(can, field)        ((can)->stats.field++)
    #define CAN_STAT_SET(can, field, v)     ((can)->stats.field = (v))
#else
    #define CAN_STAT_INC(can, field)        ((void)(can))
    #define CAN_STAT_SET(can, field, v)     ((void)(can), (void)(v))
#endif

/** Call the user event callback when callbacks are enabled and set. */
#if CAN_CFG_ENABLE_CALLBACKS
    #define CAN_NOTIFY(can, ev, arg) \
        do { \
            if ((can)->cb.on_event != NULL) \
            { \
                (can)->cb.on_event((can), (ev), (int32_t)(arg), (can)->cb.user_ctx); \
            } \
        } while (0)
#else
    #define CAN_NOTIFY(can, ev, arg)        do { (void)(can); (void)(ev); (void)(arg); } while (0)
#endif

/* ======================================================================== */
/* DLC <-> LENGTH                                                           */
/* ======================================================================== */

/** Payload length for each DLC code 0..15 (CAN FD table; classic stops at 8). */
static const uint8_t k_dlc_to_len[16] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U };

uint8_t CanLib_DlcToLen(uint8_t dlc, CanFrameFormat_e format)
{
    uint8_t len;

    if (dlc > 15U)
    {
        dlc = 15U;
    }
    len = k_dlc_to_len[dlc];
    if ((format == CAN_FORMAT_CLASSIC) && (len > CAN_CLASSIC_MAX_DLEN))
    {
        len = CAN_CLASSIC_MAX_DLEN;
    }
    return len;
}

uint8_t CanLib_LenToDlc(uint8_t len)
{
    uint8_t dlc;

    for (dlc = 0U; dlc < 16U; dlc++)
    {
        if (k_dlc_to_len[dlc] >= len)
        {
            return dlc;
        }
    }
    return 15U;
}

/* ======================================================================== */
/* RING BUFFER HELPERS (only compiled when a queue is enabled)              */
/* ======================================================================== */

#if CAN_CFG_ENABLE_RX_QUEUE || CAN_CFG_ENABLE_TX_QUEUE

/** Prepare an empty ring buffer with the given number of slots. */
static void q_init(CanQueueInfo_t *q, uint16_t capacity)
{
    memset(q, 0, sizeof(*q));
    q->capacity = capacity;
}

/** Append a frame. Returns false when the buffer is full. */
static bool q_push(CanQueueInfo_t *q, CanFrame_t *buf, const CanFrame_t *frame)
{
    if (q->count >= q->capacity)
    {
        return false;
    }
    buf[q->head] = *frame;
    q->head = (uint16_t)((q->head + 1U) % q->capacity);
    q->count++;
    if (q->count > q->high_water)
    {
        q->high_water = q->count;
    }
    return true;
}

/** Remove the oldest frame (copied to out when out is not NULL). Returns false when empty. */
static bool q_pop(CanQueueInfo_t *q, const CanFrame_t *buf, CanFrame_t *out)
{
    if (q->count == 0U)
    {
        return false;
    }
    if (out != NULL)
    {
        *out = buf[q->tail];
    }
    q->tail = (uint16_t)((q->tail + 1U) % q->capacity);
    q->count--;
    return true;
}

/** True when the fill level has just reached the "near full" warning level. */
static bool q_at_warn_level(const CanQueueInfo_t *q)
{
    return (q->count == (uint16_t)(((uint32_t)q->capacity * CAN_CFG_QUEUE_WARN_PERCENT) / 100U));
}

#endif /* queues */

/* ======================================================================== */
/* STATE HELPER                                                             */
/* ======================================================================== */

/** Change the life-cycle state, log it, and tell the user callback. */
static void can_set_state(CanLib_t *can, CanState_e state)
{
    can->status.state   = state;
    can->status.running = (state == CAN_STATE_RUNNING);
    CanDbg_LogEvent(can, CAN_LOG_EV_STATE, 0, (uint32_t)state);
    CAN_LOGI(can, "%s: state -> %d", (can->name != NULL) ? can->name : "can", (int)state);
    CAN_NOTIFY(can, CAN_EVENT_STATE_CHANGE, (int32_t)state);
}

/* ======================================================================== */
/* SETUP AND LIFE CYCLE                                                     */
/* ======================================================================== */

CanResult_e CanLib_SetDefaults(CanLib_t *can, const char *name, CanBackend_e backend)
{
    CAN_CHECK_HANDLE(can);

    memset(can, 0, sizeof(*can));

    can->name        = name;
    can->cfg.backend = backend;
    can->cfg.mode    = CANLIB_MODE_NORMAL;

    can->cfg.nominal.bitrate      = CAN_CFG_DEFAULT_BITRATE;
    can->cfg.nominal.sample_point = CAN_CFG_DEFAULT_SAMPLE_POINT;
    can->cfg.nominal.sjw          = 1U;

#if CAN_CFG_ENABLE_FD
    can->cfg.fd_enabled        = false;     /* Opt in: set cfg.fd_enabled = true before init. */
    can->cfg.data.bitrate      = CAN_CFG_DEFAULT_FD_BITRATE;
    can->cfg.data.sample_point = CAN_CFG_DEFAULT_FD_SAMPLE_POINT;
    can->cfg.data.sjw          = 1U;
#endif

    can->cfg.auto_retransmit     = true;
    can->cfg.rx_overwrite_oldest = false;
#if CAN_CFG_ENABLE_BUS_OFF_RECOVERY
    can->cfg.bus_off_auto_recover = true;
    can->cfg.bus_off_recover_ms   = CAN_CFG_BUS_OFF_RECOVER_MS;
#endif

    can->hw.tx_gpio = -1;
    can->hw.rx_gpio = -1;
#if CAN_CFG_ENABLE_BACKEND_MCP25XX || CAN_CFG_ENABLE_BACKEND_MCP251XFD
    can->hw.osc_hz       = CAN_CFG_MCP_DEFAULT_OSC_HZ;
    can->hw.spi_clock_hz = 10000000UL;                      /* MCP2515 maximum. */
    if (backend == CAN_BACKEND_MCP25XX)
    {
        can->cfg.mcp_variant = CAN_MCP_2515;                /* MCP2515 board. */
    }
    if (backend == CAN_BACKEND_MCP251XFD)
    {
        can->cfg.mcp_variant = CAN_MCP_2518FD;              /* Change to CAN_MCP_2517FD if needed. */
        can->hw.osc_hz       = 40000000UL;                  /* 40 MHz crystal (20 MHz and 4 MHz also work). */
    }
#endif

    can->debug.log_level = CAN_LOG_WARN;

    can->tx.header.filter_index = CAN_FILTER_NONE;
    can->status.state           = CAN_STATE_UNINIT;
    can->status.bus_state       = CAN_BUS_ERROR_ACTIVE;

    return CAN_OK;
}

CanResult_e CanLib_Init(CanLib_t *can)
{
    const CanPortOps_t *port;
    CanResult_e r;

    CAN_CHECK_HANDLE(can);

    if (can->magic == CAN_LIB_MAGIC)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BAD_STATE);             /* Already initialized. */
    }
    if (can->cfg.backend == CAN_BACKEND_NONE)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BAD_PARAM);             /* Backend was never chosen. */
    }
    CAN_CHECK_PARAM(can, can->cfg.nominal.bitrate > 0U);
    CAN_CHECK_PARAM(can, (can->cfg.nominal.sample_point > 0U) && (can->cfg.nominal.sample_point < 1000U));
    CAN_CHECK_PARAM(can, can->instance < CAN_CFG_MAX_INSTANCES);

    port = CanPort_Get(can->cfg.backend);
    if (port == NULL)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BACKEND_DISABLED);      /* Switched off in can_cfg.h. */
    }
    if ((port->init == NULL) || (port->start == NULL) || (port->stop == NULL) ||
        (port->transmit == NULL) || (port->receive == NULL))
    {
        CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);         /* Backend table is incomplete. */
    }
    can->port = port;

#if CAN_CFG_ENABLE_RX_QUEUE
    q_init(&can->rx.queue, CAN_CFG_RX_QUEUE_DEPTH);
#endif
#if CAN_CFG_ENABLE_TX_QUEUE
    q_init(&can->tx.queue, CAN_CFG_TX_QUEUE_DEPTH);
#endif

    r = port->init(can);
    if (CAN_IS_ERR(r))
    {
        can_set_state(can, CAN_STATE_FAULT);
        CAN_RETURN_ERR(can, r);
    }

#if CAN_CFG_USE_FREERTOS
    r = CanRtos_Create(can);                                /* Mutex and semaphore. */
    if (CAN_IS_ERR(r))
    {
        if (port->deinit != NULL)
        {
            (void)port->deinit(can);
        }
        can_set_state(can, CAN_STATE_FAULT);
        CAN_RETURN_ERR(can, r);
    }
#endif

    can->magic              = CAN_LIB_MAGIC;
    can->status.initialized = true;
    can->status.bus_state   = CAN_BUS_ERROR_ACTIVE;
    CanDbg_LogEvent(can, CAN_LOG_EV_INIT, 0, (uint32_t)can->cfg.backend);
    can_set_state(can, CAN_STATE_READY);

#if CAN_CFG_USE_FREERTOS && CAN_CFG_RTOS_AUTO_TASK
    r = CanRtos_StartTask(can);                             /* Last step: the task sees a finished instance. */
    if (CAN_IS_ERR(r))
    {
        (void)CanLib_DeInit(can);
        CAN_RETURN_ERR(can, r);
    }
#endif
    return r;
}

CanResult_e CanLib_DeInit(CanLib_t *can)
{
    CanResult_e r = CAN_OK;

    CAN_CHECK_INIT(can);
    if (CAN_IS_ERR(CAN_LOCK(can)))
    {
        CAN_RETURN_ERR(can, CAN_ERR_MUTEX);
    }

    if (can->status.state == CAN_STATE_RUNNING)
    {
        (void)can->port->stop(can);
    }
    if (can->port->deinit != NULL)
    {
        r = can->port->deinit(can);
    }

    can->magic              = 0U;
    can->status.initialized = false;
    can_set_state(can, CAN_STATE_UNINIT);
    CAN_UNLOCK(can);
#if CAN_CFG_USE_FREERTOS
    CanRtos_Destroy(can);                                   /* Task, semaphore, mutex. */
#endif

    if (CAN_IS_ERR(r))
    {
        CAN_RETURN_ERR(can, r);
    }
    return r;
}

/** Implementation of CanLib_Start() (called with the instance lock held). */
static CanResult_e api_start(CanLib_t *can)
{
    CanResult_e r;

    CAN_CHECK_INIT(can);

    if ((can->status.state != CAN_STATE_READY) && (can->status.state != CAN_STATE_STOPPED))
    {
        CAN_RETURN_ERR(can, CAN_ERR_BAD_STATE);
    }
    r = can->port->start(can);
    if (CAN_IS_ERR(r))
    {
        CAN_RETURN_ERR(can, r);
    }

    can->status.tx_busy = false;
    CanDbg_LogEvent(can, CAN_LOG_EV_START, 0, 0U);
    can_set_state(can, CAN_STATE_RUNNING);
    return r;
}

/** Implementation of CanLib_Stop() (called with the instance lock held). */
static CanResult_e api_stop(CanLib_t *can)
{
    CanResult_e r;

    CAN_CHECK_INIT(can);

    if (can->status.state != CAN_STATE_RUNNING)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BAD_STATE);
    }
    r = can->port->stop(can);
    if (CAN_IS_ERR(r))
    {
        CAN_RETURN_ERR(can, r);
    }

    CanDbg_LogEvent(can, CAN_LOG_EV_STOP, 0, 0U);
    can_set_state(can, CAN_STATE_STOPPED);
    return r;
}

/* ======================================================================== */
/* SEND                                                                     */
/* ======================================================================== */

/**
 * Check a frame and fix what can be fixed: identifier range, FD fallback,
 * length clamp, FD length rounding, and the DLC code.
 * Returns CAN_OK or a warning (frame is usable) or an error (already reported).
 */
static CanResult_e can_prepare_frame(CanLib_t *can, const CanFrame_t *in, CanFrame_t *out)
{
    CanResult_e  warn = CAN_OK;
    CanHeader_t *h;
    bool         fd_ok = false;
    uint8_t      max_len;

    *out = *in;
    h = &out->header;

    /* Identifier. */
    if (h->id_type == CAN_ID_EXTENDED)
    {
#if CAN_CFG_ENABLE_EXT_ID
        CAN_CHECK_PARAM(can, h->id <= CAN_EXT_ID_MAX);
#else
        CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
    }
    else
    {
        CAN_CHECK_PARAM(can, h->id <= CAN_STD_ID_MAX);
    }

    /* Remote frames are always classic. */
    if (h->frame_type == CAN_FRAME_REMOTE)
    {
#if CAN_CFG_ENABLE_REMOTE_FRAMES
        h->format         = CAN_FORMAT_CLASSIC;
        h->bitrate_switch = false;
#else
        CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
    }

    /* FD frame while FD is not active: send it as classic and warn. */
#if CAN_CFG_ENABLE_FD
    fd_ok = can->cfg.fd_enabled;
#endif
    if ((h->format == CAN_FORMAT_FD) && !fd_ok)
    {
        h->format         = CAN_FORMAT_CLASSIC;
        h->bitrate_switch = false;
        warn              = CAN_WARN_FD_FALLBACK;
    }
    if (h->format == CAN_FORMAT_CLASSIC)
    {
        h->bitrate_switch = false;
    }

    /* Length. */
    max_len = (h->format == CAN_FORMAT_FD) ? CAN_FD_MAX_DLEN : CAN_CLASSIC_MAX_DLEN;
    if (h->data_len > max_len)
    {
        h->data_len = max_len;
        if (warn == CAN_OK)
        {
            warn = CAN_WARN_DLC_CLAMPED;
        }
    }

    /* DLC code. FD lengths between sizes are rounded up and padded with zeros. */
    if ((h->format == CAN_FORMAT_FD) && (h->data_len > CAN_CLASSIC_MAX_DLEN))
    {
        uint8_t dlc     = CanLib_LenToDlc(h->data_len);
        uint8_t new_len = CanLib_DlcToLen(dlc, CAN_FORMAT_FD);

        if (new_len > h->data_len)
        {
            memset(&out->body.data[h->data_len], 0, (size_t)(new_len - h->data_len));
        }
        h->data_len = new_len;
        h->dlc      = dlc;
    }
    else
    {
        h->dlc = h->data_len;
    }

    h->error_state  = false;
    h->filter_index = CAN_FILTER_NONE;
    return warn;
}

/** Give one frame to the hardware. Updates statistics and tells the user on success. */
static CanResult_e can_tx_hw(CanLib_t *can, const CanFrame_t *frame)
{
    CanResult_e r = can->port->transmit(can, frame);

    if (r == CAN_OK)
    {
        CAN_STAT_INC(can, tx_ok);
        CAN_STAT_SET(can, last_tx_ms, CanLib_GetTickMs());
        if (CanDbg_LevelEnabled(can, CAN_LOG_DEBUG))
        {
            CanDbg_LogEvent(can, CAN_LOG_EV_TX, 0, frame->header.id);
        }
        CAN_LOGD(can, "tx id=0x%lX len=%u", (unsigned long)frame->header.id, (unsigned)frame->header.data_len);
        CAN_NOTIFY(can, CAN_EVENT_TX_DONE, frame->header.id);
    }
    return r;
}

/** Implementation of CanLib_SendFrame() (called with the instance lock held). */
static CanResult_e api_send_frame(CanLib_t *can, const CanFrame_t *frame)
{
    CanFrame_t  f;
    CanResult_e warn;
    CanResult_e r;
#if CAN_CFG_ENABLE_TX_QUEUE
    bool        queue_empty;
    bool        sent = false;
    bool        pushed;
    bool        near_full;
#endif

    CAN_CHECK_INIT(can);
    if (frame == NULL)
    {
        CAN_RETURN_ERR(can, CAN_ERR_NULL_PTR);
    }
    if (can->status.state != CAN_STATE_RUNNING)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BAD_STATE);
    }
    if (can->status.bus_state == CAN_BUS_OFF)
    {
        CAN_RETURN_ERR(can, CAN_ERR_BUS_OFF);
    }

    warn = can_prepare_frame(can, frame, &f);
    if (CAN_IS_ERR(warn))
    {
        return warn;                                        /* Already reported. */
    }
    f.header.timestamp_ms = CanLib_GetTickMs();

#if CAN_CFG_ENABLE_TX_QUEUE
    CAN_CFG_ENTER_CRITICAL();
    queue_empty = (can->tx.queue.count == 0U);
    CAN_CFG_EXIT_CRITICAL();

    if (queue_empty)
    {
        r = can_tx_hw(can, &f);                             /* Fast path: no waiting frames. */
        if (r == CAN_OK)
        {
            sent = true;
        }
        else if (r != CAN_ERR_TX_FULL)
        {
            CAN_STAT_INC(can, tx_fail);
            CAN_RETURN_ERR(can, r);
        }
    }

    if (!sent)
    {
        CAN_CFG_ENTER_CRITICAL();
        pushed    = q_push(&can->tx.queue, can->tx.buffer, &f);
        near_full = pushed && q_at_warn_level(&can->tx.queue);
        can->status.tx_pending = can->tx.queue.count;
        CAN_CFG_EXIT_CRITICAL();

        if (!pushed)
        {
            can->tx.queue.dropped++;
            CAN_STAT_INC(can, tx_fail);
            CAN_RETURN_ERR(can, CAN_ERR_TX_FULL);
        }
        if (near_full && (warn == CAN_OK))
        {
            warn = CAN_WARN_TX_QUEUE_NEAR_FULL;
        }
#if CAN_CFG_USE_FREERTOS
        CanRtos_WakeTask(can);                              /* Let the library task send it at once. */
#endif
    }
#else
    r = can_tx_hw(can, &f);
    if (CAN_IS_ERR(r))
    {
        CAN_STAT_INC(can, tx_fail);
        CAN_RETURN_ERR(can, r);
    }
#endif

    if (warn != CAN_OK)
    {
        CAN_WARN(can, warn);
    }
    return warn;
}

/** Build a simple frame from plain arguments and send it. */
static CanResult_e can_send_simple(CanLib_t *can, uint32_t id, CanIdType_e id_type,
                                   CanFrameFormat_e format, bool brs,
                                   const uint8_t *data, uint8_t len)
{
    CanFrame_t f;
    size_t     copy_len;

    CAN_CHECK_HANDLE(can);
    if ((len > 0U) && (data == NULL))
    {
        CAN_RETURN_ERR(can, CAN_ERR_NULL_PTR);
    }

    memset(&f, 0, sizeof(f));
    f.header.id             = id;
    f.header.id_type        = id_type;
    f.header.frame_type     = CAN_FRAME_DATA;
    f.header.format         = format;
    f.header.bitrate_switch = brs;
    f.header.data_len       = len;
    f.header.filter_index   = CAN_FILTER_NONE;

    /* Copy only what this frame format can carry; extra length is clamped later with a warning. */
    copy_len = (format == CAN_FORMAT_FD) ? (size_t)CAN_FD_MAX_DLEN : (size_t)CAN_CLASSIC_MAX_DLEN;
    if (copy_len > (size_t)CAN_MAX_DATA_LEN)
    {
        copy_len = (size_t)CAN_MAX_DATA_LEN;
    }
    if (copy_len > (size_t)len)
    {
        copy_len = (size_t)len;
    }
    if (copy_len > 0U)
    {
        memcpy(f.body.data, data, copy_len);
    }
    return api_send_frame(can, &f);
}

/** Implementation of CanLib_Send() (called with the instance lock held). */
static CanResult_e api_send(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len)
{
    return can_send_simple(can, id, CAN_ID_STANDARD, CAN_FORMAT_CLASSIC, false, data, len);
}

/** Implementation of CanLib_SendExt() (called with the instance lock held). */
static CanResult_e api_send_ext(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len)
{
#if CAN_CFG_ENABLE_EXT_ID
    return can_send_simple(can, id, CAN_ID_EXTENDED, CAN_FORMAT_CLASSIC, false, data, len);
#else
    (void)id; (void)data; (void)len;
    CAN_CHECK_HANDLE(can);
    CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
}

/** Implementation of CanLib_SendFd() (called with the instance lock held). */
static CanResult_e api_send_fd(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len, bool brs)
{
#if CAN_CFG_ENABLE_FD
    return can_send_simple(can, id, CAN_ID_STANDARD, CAN_FORMAT_FD, brs, data, len);
#else
    (void)id; (void)data; (void)len; (void)brs;
    CAN_CHECK_HANDLE(can);
    CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
}

/** Implementation of CanLib_SendRemote() (called with the instance lock held). */
static CanResult_e api_send_remote(CanLib_t *can, uint32_t id, CanIdType_e id_type, uint8_t dlc)
{
#if CAN_CFG_ENABLE_REMOTE_FRAMES
    CanFrame_t f;

    CAN_CHECK_HANDLE(can);

    memset(&f, 0, sizeof(f));
    f.header.id           = id;
    f.header.id_type      = id_type;
    f.header.frame_type   = CAN_FRAME_REMOTE;
    f.header.format       = CAN_FORMAT_CLASSIC;
    f.header.data_len     = dlc;
    f.header.filter_index = CAN_FILTER_NONE;
    return api_send_frame(can, &f);
#else
    (void)id; (void)id_type; (void)dlc;
    CAN_CHECK_HANDLE(can);
    CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
}

/** Implementation of CanLib_SendManual() (called with the instance lock held). */
static CanResult_e api_send_manual(CanLib_t *can)
{
#if CAN_CFG_ENABLE_MANUAL_SEND
    CanFrame_t f;

    CAN_CHECK_INIT(can);

    f.header = can->tx.header;
    f.body   = can->tx.body;
    return api_send_frame(can, &f);
#else
    CAN_CHECK_HANDLE(can);
    CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
}

/* ======================================================================== */
/* FILTERS                                                                  */
/* ======================================================================== */

/** True when the frame passes the filters. Sets *index to the matching slot. */
static bool can_filter_match(const CanLib_t *can, const CanHeader_t *h, uint8_t *index)
{
#if CAN_CFG_ENABLE_FILTERS
    uint8_t i;

    *index = CAN_FILTER_NONE;
    if (can->filter_count == 0U)
    {
        return true;                                        /* No filters: accept all. */
    }
    for (i = 0U; i < CAN_CFG_MAX_FILTERS; i++)
    {
        const CanFilter_t *flt = &can->filters[i];
        bool match;

        if (!flt->enabled || (flt->id_type != h->id_type))
        {
            continue;
        }
        if (flt->mode == CAN_FILTER_EXACT)
        {
            match = (h->id == flt->id);
        }
        else
        {
            match = ((h->id & flt->mask) == (flt->id & flt->mask));
        }
        if (match)
        {
            *index = i;
            return true;
        }
    }
    return false;
#else
    (void)can;
    (void)h;
    *index = CAN_FILTER_NONE;
    return true;
#endif
}

#if CAN_CFG_ENABLE_FILTERS
/** Store a filter in the first free slot. */
static CanResult_e can_add_filter(CanLib_t *can, CanFilterMode_e mode, CanIdType_e id_type,
                                  uint32_t id, uint32_t mask)
{
    uint8_t i;

    for (i = 0U; i < CAN_CFG_MAX_FILTERS; i++)
    {
        if (!can->filters[i].enabled)
        {
            can->filters[i].mode    = mode;
            can->filters[i].id_type = id_type;
            can->filters[i].id      = id;
            can->filters[i].mask    = mask;
            can->filters[i].enabled = true;
            can->filter_count++;
            return CAN_OK;
        }
    }
    CAN_RETURN_ERR(can, CAN_ERR_FILTER_FULL);
}
#endif

/** Implementation of CanLib_AddFilter() (called with the instance lock held). */
static CanResult_e api_add_filter(CanLib_t *can, CanIdType_e id_type, uint32_t id, uint32_t mask)
{
    CAN_CHECK_HANDLE(can);
#if CAN_CFG_ENABLE_FILTERS
    return can_add_filter(can, CAN_FILTER_MASK, id_type, id, mask);
#else
    (void)id_type; (void)id; (void)mask;
    CAN_WARN(can, CAN_WARN_FILTER_IGNORED);
    return CAN_WARN_FILTER_IGNORED;
#endif
}

/** Implementation of CanLib_AddFilterExact() (called with the instance lock held). */
static CanResult_e api_add_filter_exact(CanLib_t *can, CanIdType_e id_type, uint32_t id)
{
    CAN_CHECK_HANDLE(can);
#if CAN_CFG_ENABLE_FILTERS
    return can_add_filter(can, CAN_FILTER_EXACT, id_type, id, 0U);
#else
    (void)id_type; (void)id;
    CAN_WARN(can, CAN_WARN_FILTER_IGNORED);
    return CAN_WARN_FILTER_IGNORED;
#endif
}

/** Implementation of CanLib_ClearFilters() (called with the instance lock held). */
static CanResult_e api_clear_filters(CanLib_t *can)
{
    CAN_CHECK_HANDLE(can);
#if CAN_CFG_ENABLE_FILTERS
    memset(can->filters, 0, sizeof(can->filters));
    can->filter_count = 0U;
#endif
    return CAN_OK;
}

/* ======================================================================== */
/* RECEIVE                                                                  */
/* ======================================================================== */

void CanLib_OnRxFrame(CanLib_t *can, const CanFrame_t *frame)
{
    CanFrame_t f;
    uint8_t    filter_idx = CAN_FILTER_NONE;
    bool       stored     = true;
#if CAN_CFG_ENABLE_RX_QUEUE
    bool       overflow   = false;
    bool       near_full  = false;
#endif

    if ((can == NULL) || (frame == NULL))
    {
        CAN_REPORT(can, CAN_ERR_NULL_PTR);
        return;
    }

    f = *frame;
    if (f.header.timestamp_ms == 0U)
    {
        f.header.timestamp_ms = CanLib_GetTickMs();
    }

    if (!can_filter_match(can, &f.header, &filter_idx))
    {
        CAN_STAT_INC(can, rx_dropped);                      /* Rejected by the filters. */
        return;
    }
    f.header.filter_index = filter_idx;

    /* Latest frame: always updated, handy in the watch window. */
    can->rx.header    = f.header;
    can->rx.body      = f.body;
    can->rx.new_frame = true;

#if CAN_CFG_ENABLE_RX_QUEUE
    CAN_CFG_ENTER_CRITICAL();
    if (can->rx.queue.count >= can->rx.queue.capacity)
    {
        overflow = true;
        can->rx.queue.dropped++;
        if (can->cfg.rx_overwrite_oldest)
        {
            (void)q_pop(&can->rx.queue, can->rx.buffer, NULL);
            (void)q_push(&can->rx.queue, can->rx.buffer, &f);
        }
        else
        {
            stored = false;                                 /* Keep old frames, drop the new one. */
        }
    }
    else
    {
        (void)q_push(&can->rx.queue, can->rx.buffer, &f);
        near_full = q_at_warn_level(&can->rx.queue);
    }
    can->status.rx_pending = can->rx.queue.count;
    CAN_CFG_EXIT_CRITICAL();

    if (overflow)
    {
        CAN_STAT_INC(can, rx_dropped);
        CAN_REPORT(can, CAN_ERR_RX_OVERFLOW);
        CAN_NOTIFY(can, CAN_EVENT_RX_OVERFLOW, can->rx.queue.dropped);
    }
    else if (near_full)
    {
        CAN_WARN(can, CAN_WARN_RX_QUEUE_NEAR_FULL);
    }
#endif

    if (!stored)
    {
        return;
    }

    CAN_STAT_INC(can, rx_ok);
    CAN_STAT_SET(can, last_rx_ms, f.header.timestamp_ms);
#if CAN_CFG_USE_FREERTOS
    CanRtos_NotifyRx(can);                                  /* Wake CanLib_ReceiveWait() callers. */
#endif
    if (CanDbg_LevelEnabled(can, CAN_LOG_DEBUG))
    {
        CanDbg_LogEvent(can, CAN_LOG_EV_RX, 0, f.header.id);
    }
    CAN_LOGD(can, "rx id=0x%lX len=%u", (unsigned long)f.header.id, (unsigned)f.header.data_len);

#if CAN_CFG_ENABLE_CALLBACKS
    if (can->cb.on_rx != NULL)
    {
        can->cb.on_rx(can, &f, can->cb.user_ctx);
    }
#endif
}

/** Implementation of CanLib_Receive() (called with the instance lock held). */
static CanResult_e api_receive(CanLib_t *can, CanFrame_t *frame)
{
    CAN_CHECK_INIT(can);
    if (frame == NULL)
    {
        CAN_RETURN_ERR(can, CAN_ERR_NULL_PTR);
    }

#if CAN_CFG_ENABLE_RX_QUEUE
    {
        bool got;

        CAN_CFG_ENTER_CRITICAL();
        got = q_pop(&can->rx.queue, can->rx.buffer, frame);
        can->status.rx_pending = can->rx.queue.count;
        if (can->rx.queue.count == 0U)
        {
            can->rx.new_frame = false;
        }
        CAN_CFG_EXIT_CRITICAL();

        return got ? CAN_OK : CAN_ERR_RX_EMPTY;             /* Empty is normal: not reported. */
    }
#else
    if (!can->rx.new_frame)
    {
        return CAN_ERR_RX_EMPTY;
    }
    frame->header     = can->rx.header;
    frame->body       = can->rx.body;
    can->rx.new_frame = false;
    return CAN_OK;
#endif
}

uint16_t CanLib_Available(const CanLib_t *can)
{
    if (can == NULL)
    {
        return 0U;
    }
#if CAN_CFG_ENABLE_RX_QUEUE
    return can->rx.queue.count;
#else
    return can->rx.new_frame ? 1U : 0U;
#endif
}

/* ======================================================================== */
/* TRANSCEIVER                                                              */
/* ======================================================================== */

/** Implementation of CanLib_SetXcvrMode() (called with the instance lock held). */
static CanResult_e api_set_xcvr_mode(CanLib_t *can, CanXcvrMode_e mode)
{
#if CAN_CFG_ENABLE_XCVR_CONTROL
    CanResult_e r;

    CAN_CHECK_INIT(can);
    if (can->port->set_xcvr_mode == NULL)
    {
        CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
    }
    r = can->port->set_xcvr_mode(can, mode);
    if (CAN_IS_ERR(r))
    {
        CAN_RETURN_ERR(can, r);
    }
    return r;
#else
    (void)mode;
    CAN_CHECK_HANDLE(can);
    CAN_RETURN_ERR(can, CAN_ERR_NOT_SUPPORTED);
#endif
}

/* ======================================================================== */
/* PROCESS LOOP                                                             */
/* ======================================================================== */

/** Work out the bus error state from the counters and the bus-off flag. */
static CanBusState_e can_derive_bus_state(const CanLib_t *can, bool hw_bus_off)
{
    if (hw_bus_off)
    {
        return CAN_BUS_OFF;
    }
    if ((can->bus.tec > 127U) || (can->bus.rec > 127U))
    {
        return CAN_BUS_ERROR_PASSIVE;
    }
    if ((can->bus.tec >= 96U) || (can->bus.rec >= 96U))
    {
        return CAN_BUS_ERROR_WARNING;
    }
    return CAN_BUS_ERROR_ACTIVE;
}

/** Read the error counters and react to a change of the bus state. */
static void can_update_bus_state(CanLib_t *can)
{
    CanResult_e   r;
    CanBusState_e new_state;

    if (can->port->get_errors == NULL)
    {
        return;
    }
    r = can->port->get_errors(can);
    if (CAN_IS_ERR(r) && (r != CAN_ERR_BUS_OFF))
    {
        CAN_REPORT(can, r);                                 /* Could not read the counters. */
        return;
    }

    new_state = can_derive_bus_state(can, (r == CAN_ERR_BUS_OFF));
    if (new_state == can->status.bus_state)
    {
        return;
    }
    can->status.bus_state = new_state;

    switch (new_state)
    {
        case CAN_BUS_ERROR_WARNING:
            CAN_WARN(can, CAN_WARN_ERROR_WARNING);
            CAN_NOTIFY(can, CAN_EVENT_ERROR, (int32_t)new_state);
            break;
        case CAN_BUS_ERROR_PASSIVE:
            CAN_WARN(can, CAN_WARN_ERROR_PASSIVE);
            CAN_NOTIFY(can, CAN_EVENT_ERROR, (int32_t)new_state);
            break;
        case CAN_BUS_OFF:
            can->bus.bus_off_since_ms = CanLib_GetTickMs();
            CAN_STAT_INC(can, bus_off_count);
            CAN_REPORT(can, CAN_ERR_BUS_OFF);
            CanDbg_LogEvent(can, CAN_LOG_EV_BUS_OFF, 0, can->bus.bus_off_since_ms);
            CAN_NOTIFY(can, CAN_EVENT_BUS_OFF, 0);
            break;
        default:
            break;                                          /* Back to error active: nothing to report. */
    }
}

/** Implementation of CanLib_Process() (called with the instance lock held). */
static CanResult_e api_process(CanLib_t *can)
{
    CanResult_e result = CAN_OK;
    CanResult_e r;
    uint8_t     n;

    CAN_CHECK_INIT(can);
    if (can->status.state != CAN_STATE_RUNNING)
    {
        return CAN_ERR_BAD_STATE;                           /* Not on the bus: nothing to do (not reported). */
    }

    /* 1. Read frames waiting in the controller. */
    for (n = 0U; n < CAN_CFG_PROCESS_MAX_RX; n++)
    {
        CanFrame_t f;

        r = can->port->receive(can, &f);
        if (r == CAN_OK)
        {
            CanLib_OnRxFrame(can, &f);
        }
        else
        {
            if (r != CAN_ERR_RX_EMPTY)
            {
                CAN_REPORT(can, r);
                result = r;
            }
            break;
        }
    }

    /* 2. Error counters and bus state. */
    can_update_bus_state(can);

    /* 3. Automatic bus-off recovery. */
#if CAN_CFG_ENABLE_BUS_OFF_RECOVERY
    if ((can->status.bus_state == CAN_BUS_OFF) && can->cfg.bus_off_auto_recover &&
        (can->port->recover != NULL))
    {
        uint32_t now = CanLib_GetTickMs();

        if ((uint32_t)(now - can->bus.bus_off_since_ms) >= can->cfg.bus_off_recover_ms)
        {
            r = can->port->recover(can);
            if (CAN_IS_ERR(r))
            {
                CAN_REPORT(can, r);
                result = r;
                can->bus.bus_off_since_ms = now;            /* Try again after another wait. */
            }
            else
            {
                can->status.bus_state = CAN_BUS_ERROR_ACTIVE;
                can->bus.tec = 0U;
                can->bus.rec = 0U;
                CanDbg_LogEvent(can, CAN_LOG_EV_RECOVER, 0, now);
                CAN_WARN(can, CAN_WARN_BUS_RECOVERED);
                CAN_NOTIFY(can, CAN_EVENT_BUS_RECOVERED, 0);
            }
        }
    }
#endif

    /* 4. Send frames waiting in the transmit buffer. */
#if CAN_CFG_ENABLE_TX_QUEUE
    if (can->status.bus_state != CAN_BUS_OFF)
    {
        for (;;)
        {
            CanFrame_t f;
            bool       have;

            CAN_CFG_ENTER_CRITICAL();
            have = (can->tx.queue.count > 0U);
            if (have)
            {
                f = can->tx.buffer[can->tx.queue.tail];
            }
            CAN_CFG_EXIT_CRITICAL();
            if (!have)
            {
                break;
            }

            r = can_tx_hw(can, &f);
            if (r == CAN_ERR_TX_FULL)
            {
                break;                                      /* Hardware busy: try again next call. */
            }

            CAN_CFG_ENTER_CRITICAL();
            (void)q_pop(&can->tx.queue, can->tx.buffer, NULL);
            CAN_CFG_EXIT_CRITICAL();

            if (CAN_IS_ERR(r))
            {
                CAN_STAT_INC(can, tx_fail);                 /* Frame is dropped so the queue cannot jam. */
                CAN_REPORT(can, r);
                result = r;
            }
        }
        can->status.tx_pending = can->tx.queue.count;
    }
#endif

    /* 5. Manual send request (also settable from the watch window). */
#if CAN_CFG_ENABLE_MANUAL_SEND
    if (can->tx.manual_request)
    {
        can->tx.manual_request = false;                     /* Clear first: one request, one frame. */
        r = api_send_manual(can);
        if (CAN_IS_ERR(r))
        {
            result = r;
        }
    }
#endif

#if CAN_CFG_ENABLE_RX_QUEUE
    can->status.rx_pending = can->rx.queue.count;
#endif
    return result;
}

/* ======================================================================== */
/* PUBLIC API (each call takes the instance lock when FreeRTOS is enabled)   */
/* ======================================================================== */

/** Take the lock, run one implementation, release the lock, return its result. */
#define CAN_LOCKED_CALL(can, call) \
    do { \
        CanResult_e lk_ = CAN_LOCK(can); \
        CanResult_e res_; \
        if (CAN_IS_ERR(lk_)) \
        { \
            CAN_RETURN_ERR((can), CAN_ERR_MUTEX); \
        } \
        res_ = (call); \
        CAN_UNLOCK(can); \
        return res_; \
    } while (0)

CanResult_e CanLib_Start(CanLib_t *can)
{
    CAN_LOCKED_CALL(can, api_start(can));
}

CanResult_e CanLib_Stop(CanLib_t *can)
{
    CAN_LOCKED_CALL(can, api_stop(can));
}

CanResult_e CanLib_Process(CanLib_t *can)
{
    CAN_LOCKED_CALL(can, api_process(can));
}

CanResult_e CanLib_SendFrame(CanLib_t *can, const CanFrame_t *frame)
{
    CAN_LOCKED_CALL(can, api_send_frame(can, frame));
}

CanResult_e CanLib_Send(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_LOCKED_CALL(can, api_send(can, id, data, len));
}

CanResult_e CanLib_SendExt(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len)
{
    CAN_LOCKED_CALL(can, api_send_ext(can, id, data, len));
}

CanResult_e CanLib_SendFd(CanLib_t *can, uint32_t id, const uint8_t *data, uint8_t len, bool brs)
{
    CAN_LOCKED_CALL(can, api_send_fd(can, id, data, len, brs));
}

CanResult_e CanLib_SendRemote(CanLib_t *can, uint32_t id, CanIdType_e id_type, uint8_t dlc)
{
    CAN_LOCKED_CALL(can, api_send_remote(can, id, id_type, dlc));
}

CanResult_e CanLib_SendManual(CanLib_t *can)
{
    CAN_LOCKED_CALL(can, api_send_manual(can));
}

CanResult_e CanLib_Receive(CanLib_t *can, CanFrame_t *frame)
{
    CAN_LOCKED_CALL(can, api_receive(can, frame));
}

CanResult_e CanLib_AddFilter(CanLib_t *can, CanIdType_e id_type, uint32_t id, uint32_t mask)
{
    CAN_LOCKED_CALL(can, api_add_filter(can, id_type, id, mask));
}

CanResult_e CanLib_AddFilterExact(CanLib_t *can, CanIdType_e id_type, uint32_t id)
{
    CAN_LOCKED_CALL(can, api_add_filter_exact(can, id_type, id));
}

CanResult_e CanLib_ClearFilters(CanLib_t *can)
{
    CAN_LOCKED_CALL(can, api_clear_filters(can));
}

CanResult_e CanLib_SetXcvrMode(CanLib_t *can, CanXcvrMode_e mode)
{
    CAN_LOCKED_CALL(can, api_set_xcvr_mode(can, mode));
}
