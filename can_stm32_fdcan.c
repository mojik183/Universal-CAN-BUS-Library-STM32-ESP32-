/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_stm32_fdcan.c
 * @brief   STM32 FDCAN backend using the ST HAL (HAL_FDCAN_xxx).
 *
 * Compiled only for CAN_PLATFORM_STM32 with the FDCAN backend enabled.
 */
#include "can_lib.h"

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_STM32) && CAN_CFG_ENABLE_BACKEND_STM32_FDCAN

#include <string.h>
#include CAN_CFG_STM32_HAL_HEADER

/** The HAL stores the DLC code (0..15) in bits 16..19 of DataLength. */
#define FDCAN_DLC_SHIFT         16U

/** Largest transmitter delay compensation offset of the peripheral. */
#define FDCAN_TDC_OFFSET_MAX    127U

/** Private data of each instance (no malloc). Indexed by CanLib_t.instance. */
static CanFdcanCtx_t s_fd_ctx[CAN_CFG_MAX_INSTANCES];

/* ======================================================================== */
/* BIT TIMING                                                               */
/* ======================================================================== */

CanResult_e CanFdcan_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point,
                                bool data_phase, CanTiming_t *out)
{
    uint32_t psc_max  = data_phase ? FDCAN_LIB_DAT_PSC_MAX  : FDCAN_LIB_NOM_PSC_MAX;
    uint32_t seg1_max = data_phase ? FDCAN_LIB_DAT_SEG1_MAX : FDCAN_LIB_NOM_SEG1_MAX;
    uint32_t seg2_max = data_phase ? FDCAN_LIB_DAT_SEG2_MAX : FDCAN_LIB_NOM_SEG2_MAX;
    uint32_t tq_cap   = data_phase ? (1U + seg1_max + seg2_max) : FDCAN_LIB_NOM_TQ_CAP;
    uint32_t prescaler;
    uint32_t best_err   = 0xFFFFFFFFUL;
    uint32_t best_total = 0U;
    uint32_t best_psc   = 0U;
    uint32_t best_s1    = 0U;
    uint32_t best_s2    = 0U;

    if (out == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    if ((clk_hz == 0U) || (bitrate == 0U) || (sample_point == 0U) || (sample_point >= 1000U))
    {
        return CAN_ERR_BAD_PARAM;
    }

    for (prescaler = 1U; prescaler <= psc_max; prescaler++)
    {
        uint32_t div = prescaler * bitrate;
        uint32_t total;
        uint32_t s1;
        uint32_t s2;
        uint32_t real_sp;
        uint32_t err;

        if ((clk_hz % div) != 0U)
        {
            continue;                                   /* Not an exact bitrate with this prescaler. */
        }
        total = clk_hz / div;
        if ((total < 5U) || (total > tq_cap))
        {
            continue;
        }

        s1 = (total * (uint32_t)sample_point + 500UL) / 1000UL;     /* sync + seg1 */
        s1 = (s1 > 1U) ? (s1 - 1U) : 1U;                            /* seg1 */
        if (s1 > seg1_max)
        {
            s1 = seg1_max;                                          /* Very long bit: cap segment 1. */
        }
        s2 = total - 1U - s1;
        if (s2 < 1U)
        {
            s2 = 1U;
            s1 = total - 2U;
        }
        if (s2 > seg2_max)
        {
            s2 = seg2_max;
            s1 = total - 1U - s2;
        }
        if ((s1 < 1U) || (s1 > seg1_max))
        {
            continue;
        }

        real_sp = ((1U + s1) * 1000UL) / total;
        err = (real_sp > sample_point) ? (real_sp - sample_point) : ((uint32_t)sample_point - real_sp);
        if ((err < best_err) || ((err == best_err) && (total > best_total)))
        {
            best_err   = err;
            best_total = total;
            best_psc   = prescaler;
            best_s1    = s1;
            best_s2    = s2;
        }
    }

    if (best_total == 0U)
    {
        return CAN_ERR_BITRATE;
    }

    out->bitrate        = bitrate;
    out->sample_point   = sample_point;
    out->prescaler      = (uint16_t)best_psc;
    out->tseg1          = (uint8_t)best_s1;             /* At most 78 (nominal cap 80) or 32 (data): fits. */
    out->tseg2          = (uint8_t)best_s2;
    out->sjw            = (uint8_t)((best_s2 < 4U) ? best_s2 : 4U);
    out->actual_bitrate = clk_hz / (best_psc * best_total);
    return CAN_OK;
}

/* ======================================================================== */
/* HELPERS                                                                  */
/* ======================================================================== */

/** Return the private data of an instance. */
static CanFdcanCtx_t *fd_ctx(const CanLib_t *can)
{
    return (CanFdcanCtx_t *)can->port_ctx;
}

/** Return the HAL handle of an instance. */
static FDCAN_HandleTypeDef *fd_hcan(const CanLib_t *can)
{
    return (FDCAN_HandleTypeDef *)can->hw.handle;
}

/** Remember a failed HAL call (visible in the watch window) and give a library result. */
static CanResult_e fd_hal_fail(const CanLib_t *can)
{
    CanFdcanCtx_t *ctx = fd_ctx(can);

    if (ctx != NULL)
    {
        ctx->hal_failures++;
        ctx->hal_error = HAL_FDCAN_GetError(fd_hcan(can));
    }
    return CAN_ERR_BACKEND_FAIL;
}

/** Convert a HAL receive header and data into a library frame. */
static void fd_decode_frame(const FDCAN_RxHeaderTypeDef *h, const uint8_t *data, CanFrame_t *f)
{
    uint8_t dlc    = (uint8_t)((h->DataLength >> FDCAN_DLC_SHIFT) & 0x0FU);
    bool    is_fd  = (h->FDFormat == FDCAN_FD_CAN);
    uint8_t len;

    memset(f, 0, sizeof(*f));
    f->header.id         = h->Identifier;
    f->header.id_type    = (h->IdType == FDCAN_EXTENDED_ID) ? CAN_ID_EXTENDED : CAN_ID_STANDARD;
    f->header.frame_type = (h->RxFrameType == FDCAN_REMOTE_FRAME) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;
    f->header.format     = is_fd ? CAN_FORMAT_FD : CAN_FORMAT_CLASSIC;
    f->header.dlc        = dlc;
    f->header.bitrate_switch = is_fd && (h->BitRateSwitch == FDCAN_BRS_ON);
    f->header.error_state    = is_fd && (h->ErrorStateIndicator == FDCAN_ESI_PASSIVE);
    f->header.filter_index   = CAN_FILTER_NONE;

    if (f->header.frame_type == CAN_FRAME_DATA)
    {
        len = CanLib_DlcToLen(dlc, f->header.format);
        if (len > CAN_MAX_DATA_LEN)
        {
            len = (uint8_t)CAN_MAX_DATA_LEN;            /* FD frame but FD is off in can_cfg.h: keep what fits. */
        }
        f->header.data_len = len;
        memcpy(f->body.data, data, len);
    }
    f->header.timestamp_ms = CanLib_GetTickMs();
}

/** Write bit timing, mode and options into the HAL handle. */
static CanResult_e fd_apply_config(CanLib_t *can, bool fd_on)
{
    FDCAN_HandleTypeDef *h = fd_hcan(can);

    h->Init.ClockDivider       = FDCAN_CLOCK_DIV1;
    h->Init.FrameFormat        = fd_on ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_CLASSIC;
    h->Init.AutoRetransmission = can->cfg.auto_retransmit ? ENABLE : DISABLE;
    h->Init.TransmitPause      = DISABLE;
    h->Init.ProtocolException  = ENABLE;
    h->Init.StdFiltersNbr      = 0U;                    /* No hardware filters: the global filter accepts all. */
    h->Init.ExtFiltersNbr      = 0U;
    h->Init.TxFifoQueueMode    = FDCAN_TX_FIFO_OPERATION;

#if CAN_CFG_FDCAN_MESSAGE_RAM
    {
        /* STM32H7: the shared message RAM is laid out here. One block per library instance. */
        uint32_t words_per_elem = fd_on ? 18U : 4U;     /* 2 header words + 16 / 2 data words. */
        uint32_t used           = (CAN_CFG_FDCAN_RAM_TX_ELEMENTS + CAN_CFG_FDCAN_RAM_RX_ELEMENTS) * words_per_elem;
        uint32_t offset         = CAN_CFG_FDCAN_RAM_OFFSET(can->instance);

        if ((offset + used) > CAN_CFG_FDCAN_RAM_TOTAL_WORDS)
        {
            return CAN_ERR_BAD_PARAM;                   /* Too many instances / elements for the shared RAM. */
        }
        h->Init.MessageRAMOffset     = offset;
        h->Init.RxFifo0ElmtsNbr      = CAN_CFG_FDCAN_RAM_RX_ELEMENTS;
        h->Init.RxFifo0ElmtSize      = fd_on ? FDCAN_DATA_BYTES_64 : FDCAN_DATA_BYTES_8;
        h->Init.RxFifo1ElmtsNbr      = 0U;
        h->Init.RxFifo1ElmtSize      = FDCAN_DATA_BYTES_8;
        h->Init.RxBuffersNbr         = 0U;
        h->Init.RxBufferSize         = FDCAN_DATA_BYTES_8;
        h->Init.TxEventsNbr          = 0U;
        h->Init.TxBuffersNbr         = 0U;
        h->Init.TxFifoQueueElmtsNbr  = CAN_CFG_FDCAN_RAM_TX_ELEMENTS;
        h->Init.TxElmtSize           = fd_on ? FDCAN_DATA_BYTES_64 : FDCAN_DATA_BYTES_8;
    }
#endif

    switch (can->cfg.mode)
    {
        case CANLIB_MODE_LISTEN_ONLY: h->Init.Mode = FDCAN_MODE_BUS_MONITORING;     break;
        case CANLIB_MODE_LOOPBACK:    h->Init.Mode = FDCAN_MODE_INTERNAL_LOOPBACK;  break;
        default:                      h->Init.Mode = FDCAN_MODE_NORMAL;             break;
    }

    h->Init.NominalPrescaler     = can->cfg.nominal.prescaler;
    h->Init.NominalSyncJumpWidth = can->cfg.nominal.sjw;
    h->Init.NominalTimeSeg1      = can->cfg.nominal.tseg1;
    h->Init.NominalTimeSeg2      = can->cfg.nominal.tseg2;

#if CAN_CFG_ENABLE_FD
    h->Init.DataPrescaler        = can->cfg.data.prescaler;
    h->Init.DataSyncJumpWidth    = can->cfg.data.sjw;
    h->Init.DataTimeSeg1         = can->cfg.data.tseg1;
    h->Init.DataTimeSeg2         = can->cfg.data.tseg2;
#else
    h->Init.DataPrescaler        = 1U;                  /* Not used in classic frame format; keep legal values. */
    h->Init.DataSyncJumpWidth    = 1U;
    h->Init.DataTimeSeg1         = 1U;
    h->Init.DataTimeSeg2         = 1U;
#endif
    return CAN_OK;
}

/* ======================================================================== */
/* PORT FUNCTIONS                                                           */
/* ======================================================================== */

/** Set bit timing and mode, accept all frames in the global filter. */
static CanResult_e fd_init(CanLib_t *can)
{
    FDCAN_HandleTypeDef *hcan = fd_hcan(can);
    CanFdcanCtx_t       *ctx;
    CanResult_e          r;
    bool                 fd_on = false;

    if (hcan == NULL)
    {
        return CAN_ERR_NULL_PTR;                        /* Set can.hw.handle = &hfdcanX. */
    }

    ctx = &s_fd_ctx[can->instance];
    memset(ctx, 0, sizeof(*ctx));
    can->port_ctx = ctx;

    r = CanFdcan_CalcTiming(CAN_CFG_FDCAN_CLK_HZ, can->cfg.nominal.bitrate,
                            can->cfg.nominal.sample_point, false, &can->cfg.nominal);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
#if CAN_CFG_ENABLE_FD
    fd_on = can->cfg.fd_enabled;
    if (fd_on)
    {
        r = CanFdcan_CalcTiming(CAN_CFG_FDCAN_CLK_HZ, can->cfg.data.bitrate,
                                can->cfg.data.sample_point, true, &can->cfg.data);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
    }
#endif
    ctx->fd_mode = fd_on;

    r = fd_apply_config(can, fd_on);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    if (HAL_FDCAN_Init(hcan) != HAL_OK)
    {
        return fd_hal_fail(can);
    }

    /* Global filter: frames that match no filter (all of them) go to RX FIFO0; remote frames are filtered too. */
    if (HAL_FDCAN_ConfigGlobalFilter(hcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
    {
        return fd_hal_fail(can);
    }

#if CAN_CFG_ENABLE_FD
    if (fd_on && (can->cfg.data.bitrate >= FDCAN_LIB_TDC_MIN_BITRATE))
    {
        uint32_t offset = (uint32_t)can->cfg.data.prescaler * (uint32_t)can->cfg.data.tseg1;

        if (offset > FDCAN_TDC_OFFSET_MAX)
        {
            offset = FDCAN_TDC_OFFSET_MAX;
        }
        /* Fast data phase: compensate the delay of the transceiver loop. */
        if ((HAL_FDCAN_ConfigTxDelayCompensation(hcan, offset, 0U) != HAL_OK) ||
            (HAL_FDCAN_EnableTxDelayCompensation(hcan) != HAL_OK))
        {
            return fd_hal_fail(can);
        }
    }
#endif

    ctx->inited = true;
    return CAN_OK;
}

/** Forget the context. The HAL de-init is left to the application (it owns pins and clocks). */
static CanResult_e fd_deinit(CanLib_t *can)
{
    CanFdcanCtx_t *ctx = fd_ctx(can);

    if (ctx != NULL)
    {
        ctx->inited = false;
    }
    can->port_ctx = NULL;
    return CAN_OK;
}

/** Join the bus. */
static CanResult_e fd_start(CanLib_t *can)
{
    FDCAN_HandleTypeDef *hcan = fd_hcan(can);

    if (HAL_FDCAN_Start(hcan) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
#if CAN_CFG_FDCAN_USE_IRQ
    if (HAL_FDCAN_ActivateNotification(hcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0U) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
#endif
    return CAN_OK;
}

/** Leave the bus. */
static CanResult_e fd_stop(CanLib_t *can)
{
    FDCAN_HandleTypeDef *hcan = fd_hcan(can);

#if CAN_CFG_FDCAN_USE_IRQ
    (void)HAL_FDCAN_DeactivateNotification(hcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE);
#endif
    if (HAL_FDCAN_Stop(hcan) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
    return CAN_OK;
}

/** Put one frame in the transmit FIFO. */
static CanResult_e fd_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    FDCAN_HandleTypeDef *hcan = fd_hcan(can);
    FDCAN_TxHeaderTypeDef h;
    bool                  is_fd = (frame->header.format == CAN_FORMAT_FD);
    uint8_t               dlc   = frame->header.dlc;

    if (is_fd && !fd_ctx(can)->fd_mode)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* CAN FD is not switched on for this instance. */
    }
    if (HAL_FDCAN_GetTxFifoFreeLevel(hcan) == 0U)
    {
        return CAN_ERR_TX_FULL;
    }

    if (!is_fd && (dlc > CAN_CLASSIC_MAX_DLEN))
    {
        dlc = (uint8_t)CAN_CLASSIC_MAX_DLEN;            /* DLC 9..15 on a classic frame means 8 bytes. */
    }

    memset(&h, 0, sizeof(h));
    h.Identifier          = frame->header.id;
    h.IdType              = (frame->header.id_type == CAN_ID_EXTENDED) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    h.TxFrameType         = (frame->header.frame_type == CAN_FRAME_REMOTE) ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    h.DataLength          = ((uint32_t)dlc) << FDCAN_DLC_SHIFT;
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch       = (is_fd && frame->header.bitrate_switch) ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    h.FDFormat            = is_fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    h.MessageMarker       = 0U;

    if (HAL_FDCAN_AddMessageToTxFifoQ(hcan, &h, (uint8_t *)frame->body.data) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
    return CAN_OK;
}

/** Take one frame out of FIFO0. */
static CanResult_e fd_receive(CanLib_t *can, CanFrame_t *frame)
{
    FDCAN_HandleTypeDef *hcan = fd_hcan(can);
    FDCAN_RxHeaderTypeDef h;
    uint8_t               data[CAN_FD_MAX_DLEN] = {0};

    if (HAL_FDCAN_GetRxFifoFillLevel(hcan, FDCAN_RX_FIFO0) == 0U)
    {
        return CAN_ERR_RX_EMPTY;
    }
    if (HAL_FDCAN_GetRxMessage(hcan, FDCAN_RX_FIFO0, &h, data) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
    fd_decode_frame(&h, data, frame);
    return CAN_OK;
}

/** Read the error counters and the protocol status (bus-off, passive, warning). */
static CanResult_e fd_get_errors(CanLib_t *can)
{
    FDCAN_HandleTypeDef       *hcan = fd_hcan(can);
    FDCAN_ErrorCountersTypeDef ec;
    FDCAN_ProtocolStatusTypeDef ps;

    if (HAL_FDCAN_GetErrorCounters(hcan, &ec) != HAL_OK)
    {
        return fd_hal_fail(can);
    }
    if (HAL_FDCAN_GetProtocolStatus(hcan, &ps) != HAL_OK)
    {
        return fd_hal_fail(can);
    }

    can->bus.tec = (ec.TxErrorCnt > 255U) ? 255U : (uint8_t)ec.TxErrorCnt;
    can->bus.rec = (ec.RxErrorCnt > 255U) ? 255U : (uint8_t)ec.RxErrorCnt;
    can->bus.hw_error_flags = ((ps.BusOff != 0U) ? 0x01U : 0U) |
                              ((ps.ErrorPassive != 0U) ? 0x02U : 0U) |
                              ((ps.Warning != 0U) ? 0x04U : 0U) |
                              ((ps.LastErrorCode & 0x07UL) << 4) |
                              ((ps.DataLastErrorCode & 0x07UL) << 8);
    fd_ctx(can)->last_psr = can->bus.hw_error_flags;

    if (__HAL_FDCAN_GET_FLAG(hcan, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST) != 0U)
    {
#if CAN_CFG_ENABLE_STATS
        can->stats.rx_dropped++;                        /* FIFO overrun: count it, then clear the flag. */
#endif
        __HAL_FDCAN_CLEAR_FLAG(hcan, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
    }
    return (ps.BusOff != 0U) ? CAN_ERR_BUS_OFF : CAN_OK;
}

/** Leave bus-off: stop and start the peripheral (starts the 129 x 11 recessive bit recovery). */
static CanResult_e fd_recover(CanLib_t *can)
{
    CanResult_e r = fd_stop(can);

    if (CAN_IS_ERR(r))
    {
        return r;
    }
    return fd_start(can);
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
/** Set the transceiver mode pin. */
static CanResult_e fd_set_xcvr(CanLib_t *can, CanXcvrMode_e mode)
{
    return CanPort_SetXcvrPin(&can->hw, mode);
}
#endif

void CanFdcan_OnRxFifo0Pending(CanLib_t *can)
{
    CanFrame_t f;

    if ((can == NULL) || (can->port != &g_CanPortStm32Fdcan))
    {
        return;
    }
    while (fd_receive(can, &f) == CAN_OK)
    {
        CanLib_OnRxFrame(can, &f);                      /* Hand over to the library (ISR safe). */
    }
}

const CanFdcanCtx_t *CanFdcan_GetCtx(const CanLib_t *can)
{
    if ((can == NULL) || (can->port != &g_CanPortStm32Fdcan) || (can->port_ctx == NULL))
    {
        return NULL;
    }
    return (const CanFdcanCtx_t *)can->port_ctx;
}

/** Function table of the FDCAN backend. */
const CanPortOps_t g_CanPortStm32Fdcan =
{
    "fdcan",
    fd_init,
    fd_deinit,
    fd_start,
    fd_stop,
    fd_transmit,
    fd_receive,
    fd_get_errors,
    fd_recover,
#if CAN_CFG_ENABLE_XCVR_CONTROL
    fd_set_xcvr
#endif
};

#endif /* CAN_PLATFORM_STM32 && FDCAN backend */
