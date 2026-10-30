/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_esp32_twai.c
 * @brief   ESP32 TWAI backend using the ESP-IDF twai driver (driver/twai.h).
 *
 * Compiled only for CAN_PLATFORM_ESP32 with the TWAI backend enabled.
 */
#include "can_lib.h"

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32) && CAN_CFG_ENABLE_BACKEND_ESP32_TWAI

#include <string.h>
#include "esp_err.h"
#include "driver/twai.h"

/** The one and only TWAI controller context (the driver supports a single controller). */
static CanTwaiCtx_t s_tw_ctx;

/* ======================================================================== */
/* BIT TIMING                                                               */
/* ======================================================================== */

CanResult_e CanTwai_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out)
{
    uint32_t brp;
    uint32_t best_err   = 0xFFFFFFFFUL;
    uint32_t best_total = 0U;
    uint32_t best_brp   = 0U;
    uint32_t best_t1    = 0U;
    uint32_t best_t2    = 0U;

    if (out == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    if ((clk_hz == 0U) || (bitrate == 0U) || (sample_point == 0U) || (sample_point >= 1000U))
    {
        return CAN_ERR_BAD_PARAM;
    }

    for (brp = 2U; brp <= CAN_CFG_TWAI_BRP_MAX; brp += 2U)      /* Prescaler must be even. */
    {
        uint32_t div = brp * bitrate;
        uint32_t total;
        uint32_t t1;
        uint32_t t2;
        uint32_t real_sp;
        uint32_t err;

        if ((clk_hz % div) != 0U)
        {
            continue;                                   /* Not an exact bitrate with this prescaler. */
        }
        total = clk_hz / div;
        if ((total < TWAI_LIB_TQ_MIN) || (total > (1U + TWAI_LIB_TSEG1_MAX + TWAI_LIB_TSEG2_MAX)))
        {
            continue;
        }

        t1 = (total * (uint32_t)sample_point + 500UL) / 1000UL;     /* sync + tseg1 */
        t1 = (t1 > 1U) ? (t1 - 1U) : 1U;                            /* tseg1 */
        if (t1 > TWAI_LIB_TSEG1_MAX)
        {
            t1 = TWAI_LIB_TSEG1_MAX;                                   /* Very long bit: cap, the sample point moves earlier. */
        }
        t2 = total - 1U - t1;
        if (t2 < 1U)
        {
            t2 = 1U;
            t1 = total - 2U;
        }
        if (t2 > TWAI_LIB_TSEG2_MAX)
        {
            t2 = TWAI_LIB_TSEG2_MAX;
            t1 = total - 1U - t2;
        }
        if ((t1 < 1U) || (t1 > TWAI_LIB_TSEG1_MAX))
        {
            continue;
        }

        real_sp = ((1U + t1) * 1000UL) / total;
        err = (real_sp > sample_point) ? (real_sp - sample_point) : ((uint32_t)sample_point - real_sp);
        if ((err < best_err) || ((err == best_err) && (total > best_total)))
        {
            best_err   = err;
            best_total = total;
            best_brp   = brp;
            best_t1    = t1;
            best_t2    = t2;
        }
    }

    if (best_total == 0U)
    {
        return CAN_ERR_BITRATE;
    }

    out->bitrate        = bitrate;
    out->sample_point   = sample_point;
    out->prescaler      = (uint16_t)best_brp;
    out->tseg1          = (uint8_t)best_t1;
    out->tseg2          = (uint8_t)best_t2;
    out->sjw            = (uint8_t)((best_t2 < TWAI_LIB_SJW_MAX) ? best_t2 : TWAI_LIB_SJW_MAX);
    out->actual_bitrate = clk_hz / (best_brp * best_total);
    return CAN_OK;
}

/* ======================================================================== */
/* HELPERS                                                                  */
/* ======================================================================== */

/** Keep the last driver error visible in the watch window and give a library result. */
static CanResult_e tw_fail(esp_err_t e)
{
    s_tw_ctx.last_esp_err = (int)e;
    s_tw_ctx.driver_failures++;
    return CAN_ERR_BACKEND_FAIL;
}

/* ======================================================================== */
/* PORT FUNCTIONS                                                           */
/* ======================================================================== */

/** Install the driver with the bit timing and mode; the controller is not on the bus yet. */
static CanResult_e tw_init(CanLib_t *can)
{
    twai_general_config_t g;
    twai_timing_config_t  t;
    twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();   /* Software filters decide. */
    twai_mode_t           mode;
    CanResult_e           r;
    esp_err_t             e;

    if (s_tw_ctx.inited)
    {
        return CAN_ERR_BUSY;                            /* Only one TWAI instance at a time. */
    }
    if ((can->hw.tx_gpio < 0) || (can->hw.rx_gpio < 0))
    {
        return CAN_ERR_BAD_PARAM;                       /* Set can.hw.tx_gpio and can.hw.rx_gpio. */
    }
#if CAN_CFG_ENABLE_FD
    if (can->cfg.fd_enabled)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* TWAI is classic CAN only. */
    }
#endif

    r = CanTwai_CalcTiming(CAN_CFG_TWAI_CLK_HZ, can->cfg.nominal.bitrate,
                           can->cfg.nominal.sample_point, &can->cfg.nominal);
    if (CAN_IS_ERR(r))
    {
        return r;
    }

    switch (can->cfg.mode)
    {
        case CANLIB_MODE_LISTEN_ONLY: mode = TWAI_MODE_LISTEN_ONLY; break;
        case CANLIB_MODE_LOOPBACK:    mode = TWAI_MODE_NO_ACK;      break;   /* Self reception: see tw_transmit. */
        default:                      mode = TWAI_MODE_NORMAL;      break;
    }

    g = (twai_general_config_t)TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)can->hw.tx_gpio,
                                                            (gpio_num_t)can->hw.rx_gpio, mode);
    g.tx_queue_len = CAN_CFG_TWAI_TX_QUEUE_LEN;
    g.rx_queue_len = CAN_CFG_TWAI_RX_QUEUE_LEN;

    memset(&t, 0, sizeof(t));
    t.brp             = can->cfg.nominal.prescaler;
    t.tseg_1          = can->cfg.nominal.tseg1;
    t.tseg_2          = can->cfg.nominal.tseg2;
    t.sjw             = can->cfg.nominal.sjw;
    t.triple_sampling = false;

    memset(&s_tw_ctx, 0, sizeof(s_tw_ctx));
    e = twai_driver_install(&g, &t, &f);
    if (e != ESP_OK)
    {
        return tw_fail(e);
    }
    s_tw_ctx.inited = true;
    can->port_ctx   = &s_tw_ctx;
    return CAN_OK;
}

/** Remove the driver. */
static CanResult_e tw_deinit(CanLib_t *can)
{
    esp_err_t e = twai_driver_uninstall();

    s_tw_ctx.inited = false;
    can->port_ctx   = NULL;
    return (e == ESP_OK) ? CAN_OK : tw_fail(e);
}

/** Join the bus. */
static CanResult_e tw_start(CanLib_t *can)
{
    esp_err_t e;

    (void)can;
    e = twai_start();
    return (e == ESP_OK) ? CAN_OK : tw_fail(e);
}

/** Leave the bus. */
static CanResult_e tw_stop(CanLib_t *can)
{
    esp_err_t e;

    (void)can;
    e = twai_stop();
    return (e == ESP_OK) ? CAN_OK : tw_fail(e);
}

/** Queue one frame in the driver. */
static CanResult_e tw_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    twai_message_t m;
    esp_err_t      e;
    uint8_t        len;

    if (frame->header.format == CAN_FORMAT_FD)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* TWAI cannot send CAN FD. */
    }

    memset(&m, 0, sizeof(m));
    m.identifier       = frame->header.id;
    m.data_length_code = (frame->header.dlc > CAN_CLASSIC_MAX_DLEN) ? CAN_CLASSIC_MAX_DLEN : frame->header.dlc;
    if (frame->header.id_type == CAN_ID_EXTENDED)
    {
        m.flags |= TWAI_MSG_FLAG_EXTD;
    }
    if (frame->header.frame_type == CAN_FRAME_REMOTE)
    {
        m.flags |= TWAI_MSG_FLAG_RTR;
    }
    else
    {
        len = (frame->header.data_len > CAN_CLASSIC_MAX_DLEN) ? (uint8_t)CAN_CLASSIC_MAX_DLEN : frame->header.data_len;
        memcpy(m.data, frame->body.data, len);
    }
    if (!can->cfg.auto_retransmit)
    {
        m.flags |= TWAI_MSG_FLAG_SS;                    /* Single shot: no resend after a lost frame. */
    }
    if (can->cfg.mode == CANLIB_MODE_LOOPBACK)
    {
        m.flags |= TWAI_MSG_FLAG_SELF;                  /* Receive our own frame (needs no other node). */
    }

    e = twai_transmit(&m, 0);                           /* Never block: the core queues when full. */
    if (e == ESP_OK)
    {
        return CAN_OK;
    }
    if (e == ESP_ERR_TIMEOUT)
    {
        return CAN_ERR_TX_FULL;                         /* Driver queue full. */
    }
    if (e == ESP_ERR_INVALID_STATE)
    {
        return CAN_ERR_BAD_STATE;                       /* Controller not started or bus-off. */
    }
    return tw_fail(e);
}

/** Take one frame out of the driver receive queue. */
static CanResult_e tw_receive(CanLib_t *can, CanFrame_t *frame)
{
    twai_message_t m;
    esp_err_t      e;
    uint8_t        len;

    (void)can;
    e = twai_receive(&m, 0);
    if (e == ESP_ERR_TIMEOUT)
    {
        return CAN_ERR_RX_EMPTY;
    }
    if (e == ESP_ERR_INVALID_STATE)
    {
        return CAN_ERR_BAD_STATE;
    }
    if (e != ESP_OK)
    {
        return tw_fail(e);
    }

    memset(frame, 0, sizeof(*frame));
    frame->header.id           = m.identifier;
    frame->header.id_type      = ((m.flags & TWAI_MSG_FLAG_EXTD) != 0U) ? CAN_ID_EXTENDED : CAN_ID_STANDARD;
    frame->header.frame_type   = ((m.flags & TWAI_MSG_FLAG_RTR) != 0U) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;
    frame->header.format       = CAN_FORMAT_CLASSIC;
    frame->header.dlc          = m.data_length_code;
    frame->header.filter_index = CAN_FILTER_NONE;
    if (frame->header.frame_type == CAN_FRAME_DATA)
    {
        len = (m.data_length_code > CAN_CLASSIC_MAX_DLEN) ? (uint8_t)CAN_CLASSIC_MAX_DLEN : m.data_length_code;
        frame->header.data_len = len;
        memcpy(frame->body.data, m.data, len);
    }
    frame->header.timestamp_ms = CanLib_GetTickMs();
    return CAN_OK;
}

/** Read the error counters and bus state from the driver. */
static CanResult_e tw_get_errors(CanLib_t *can)
{
    twai_status_info_t s;
    uint32_t           missed;
    esp_err_t          e = twai_get_status_info(&s);

    if (e != ESP_OK)
    {
        return tw_fail(e);
    }

    can->bus.tec = (s.tx_error_counter > 255U) ? 255U : (uint8_t)s.tx_error_counter;
    can->bus.rec = (s.rx_error_counter > 255U) ? 255U : (uint8_t)s.rx_error_counter;
    can->bus.hw_error_flags = (uint32_t)s.state | ((s.bus_error_count & 0xFFFFFFUL) << 8);

    s_tw_ctx.bus_errors = s.bus_error_count;
    s_tw_ctx.tx_failed  = s.tx_failed_count;
    s_tw_ctx.arb_lost   = s.arb_lost_count;

    missed = s.rx_missed_count + s.rx_overrun_count;    /* Frames the controller or driver lost. */
    if (missed > s_tw_ctx.missed_seen)
    {
#if CAN_CFG_ENABLE_STATS
        can->stats.rx_dropped += (missed - s_tw_ctx.missed_seen);
#endif
        s_tw_ctx.missed_seen = missed;
    }

    return ((s.state == TWAI_STATE_BUS_OFF) || (s.state == TWAI_STATE_RECOVERING)) ? CAN_ERR_BUS_OFF : CAN_OK;
}

/** Leave bus-off: start the hardware recovery, wait for it, rejoin the bus. Blocks up to the wait limit. */
static CanResult_e tw_recover(CanLib_t *can)
{
    twai_status_info_t s;
    esp_err_t          e;
    uint32_t           waited;

    (void)can;
    e = twai_get_status_info(&s);
    if (e != ESP_OK)
    {
        return tw_fail(e);
    }
    if (s.state == TWAI_STATE_BUS_OFF)
    {
        e = twai_initiate_recovery();
        if (e != ESP_OK)
        {
            return tw_fail(e);
        }
    }

    for (waited = 0U; waited < CAN_CFG_TWAI_RECOVER_WAIT_MS; waited++)
    {
        e = twai_get_status_info(&s);
        if (e != ESP_OK)
        {
            return tw_fail(e);
        }
        if (s.state == TWAI_STATE_STOPPED)
        {
            e = twai_start();
            return (e == ESP_OK) ? CAN_OK : tw_fail(e);
        }
        if (s.state == TWAI_STATE_RUNNING)
        {
            return CAN_OK;
        }
        CanPlatform_DelayMs(1U);
    }
    return CAN_ERR_BUSY;                                /* Still recovering: the core tries again later. */
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
/** Set the transceiver mode pin. */
static CanResult_e tw_set_xcvr(CanLib_t *can, CanXcvrMode_e mode)
{
    return CanPort_SetXcvrPin(&can->hw, mode);
}
#endif

const CanTwaiCtx_t *CanTwai_GetCtx(const CanLib_t *can)
{
    if ((can == NULL) || (can->port != &g_CanPortEsp32Twai) || (can->port_ctx == NULL))
    {
        return NULL;
    }
    return (const CanTwaiCtx_t *)can->port_ctx;
}

/** Function table of the TWAI backend. */
const CanPortOps_t g_CanPortEsp32Twai =
{
    "twai",
    tw_init,
    tw_deinit,
    tw_start,
    tw_stop,
    tw_transmit,
    tw_receive,
    tw_get_errors,
    tw_recover,
#if CAN_CFG_ENABLE_XCVR_CONTROL
    tw_set_xcvr
#endif
};

#endif /* CAN_PLATFORM_ESP32 && TWAI backend */
