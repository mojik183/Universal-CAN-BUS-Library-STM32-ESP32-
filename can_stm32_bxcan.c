/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_stm32_bxcan.c
 * @brief   STM32 bxCAN backend using the ST HAL (HAL_CAN_xxx).
 *
 * Compiled only for CAN_PLATFORM_STM32 with the bxCAN backend enabled.
 */
#include "can_lib.h"

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_STM32) && CAN_CFG_ENABLE_BACKEND_STM32_BXCAN

#include <string.h>
#include CAN_CFG_STM32_HAL_HEADER

/** ESR register fields (same on every bxCAN family). */
#define BXCAN_ESR_BOFF      (1UL << 2)      /**< Bus-off flag. */
#define BXCAN_ESR_TEC_POS   16U             /**< Transmit error counter position. */
#define BXCAN_ESR_REC_POS   24U             /**< Receive error counter position. */

/** Private data of each instance (no malloc). Indexed by CanLib_t.instance. */
static CanBxcanCtx_t s_bx_ctx[CAN_CFG_MAX_INSTANCES];

/** First filter bank of the second CAN peripheral on dual-CAN devices. */
#define BXCAN_SLAVE_FILTER_START    14U

/* ======================================================================== */
/* BIT TIMING                                                               */
/* ======================================================================== */

CanResult_e CanBxcan_CalcTiming(uint32_t clk_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out)
{
    uint32_t prescaler;
    uint32_t best_err   = 0xFFFFFFFFUL;
    uint32_t best_total = 0U;
    uint32_t best_psc   = 0U;
    uint32_t best_b1    = 0U;
    uint32_t best_b2    = 0U;

    if (out == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    if ((clk_hz == 0U) || (bitrate == 0U) || (sample_point == 0U) || (sample_point >= 1000U))
    {
        return CAN_ERR_BAD_PARAM;
    }

    for (prescaler = 1U; prescaler <= BXCAN_PRESCALER_MAX; prescaler++)
    {
        uint32_t div = prescaler * bitrate;
        uint32_t total;
        uint32_t b1;
        uint32_t b2;
        uint32_t real_sp;
        uint32_t err;

        if ((clk_hz % div) != 0U)
        {
            continue;                                   /* Not an exact bitrate with this prescaler. */
        }
        total = clk_hz / div;
        if ((total < BXCAN_TQ_MIN) || (total > BXCAN_TQ_MAX))
        {
            continue;
        }

        b1 = (total * (uint32_t)sample_point + 500UL) / 1000UL;    /* sync + BS1 */
        b1 = (b1 > 1U) ? (b1 - 1U) : 1U;                           /* BS1 */
        if (b1 > BXCAN_BS1_MAX)
        {
            b1 = BXCAN_BS1_MAX;                                   /* Very long bit: cap, the sample point moves earlier. */
        }
        b2 = total - 1U - b1;
        if (b2 < 1U)
        {
            b2 = 1U;
            b1 = total - 2U;
        }
        if (b2 > BXCAN_BS2_MAX)
        {
            b2 = BXCAN_BS2_MAX;
            b1 = total - 1U - b2;
        }
        if ((b1 < 1U) || (b1 > BXCAN_BS1_MAX))
        {
            continue;
        }

        real_sp = ((1U + b1) * 1000UL) / total;
        err = (real_sp > sample_point) ? (real_sp - sample_point) : ((uint32_t)sample_point - real_sp);
        if ((err < best_err) || ((err == best_err) && (total > best_total)))
        {
            best_err   = err;
            best_total = total;
            best_psc   = prescaler;
            best_b1    = b1;
            best_b2    = b2;
        }
    }

    if (best_total == 0U)
    {
        return CAN_ERR_BITRATE;
    }

    out->bitrate        = bitrate;
    out->sample_point   = sample_point;
    out->prescaler      = (uint16_t)best_psc;
    out->tseg1          = (uint8_t)best_b1;
    out->tseg2          = (uint8_t)best_b2;
    out->sjw            = 1U;
    out->actual_bitrate = clk_hz / (best_psc * best_total);
    return CAN_OK;
}

/* ======================================================================== */
/* HELPERS                                                                  */
/* ======================================================================== */

/** Return the private data of an instance. */
static CanBxcanCtx_t *bx_ctx(const CanLib_t *can)
{
    return (CanBxcanCtx_t *)can->port_ctx;
}

/** Return the HAL handle of an instance. */
static CAN_HandleTypeDef *bx_hcan(const CanLib_t *can)
{
    return (CAN_HandleTypeDef *)can->hw.handle;
}

/** Remember a failed HAL call (visible in the watch window) and give a library result. */
static CanResult_e bx_hal_fail(const CanLib_t *can)
{
    CanBxcanCtx_t *ctx = bx_ctx(can);

    if (ctx != NULL)
    {
        ctx->hal_failures++;
        ctx->hal_error = HAL_CAN_GetError(bx_hcan(can));
    }
    return CAN_ERR_BACKEND_FAIL;
}

/** Convert a HAL receive header and data into a library frame. */
static void bx_decode_frame(const CAN_RxHeaderTypeDef *h, const uint8_t *data, CanFrame_t *f)
{
    uint8_t len = (h->DLC > CAN_CLASSIC_MAX_DLEN) ? (uint8_t)CAN_CLASSIC_MAX_DLEN : (uint8_t)h->DLC;

    memset(f, 0, sizeof(*f));
    if (h->IDE == CAN_ID_EXT)
    {
        f->header.id      = h->ExtId;
        f->header.id_type = CAN_ID_EXTENDED;
    }
    else
    {
        f->header.id      = h->StdId;
        f->header.id_type = CAN_ID_STANDARD;
    }
    f->header.frame_type   = (h->RTR == CAN_RTR_REMOTE) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;
    f->header.format       = CAN_FORMAT_CLASSIC;
    f->header.dlc          = (uint8_t)h->DLC;
    f->header.filter_index = CAN_FILTER_NONE;
    if (f->header.frame_type == CAN_FRAME_DATA)
    {
        f->header.data_len = len;
        memcpy(f->body.data, data, len);
    }
    f->header.timestamp_ms = CanLib_GetTickMs();
}

/* ======================================================================== */
/* PORT FUNCTIONS                                                           */
/* ======================================================================== */

/** Set bit timing and mode, accept all frames in the hardware filter. */
static CanResult_e bx_init(CanLib_t *can)
{
    CAN_HandleTypeDef *hcan = bx_hcan(can);
    CanBxcanCtx_t     *ctx;
    CAN_FilterTypeDef  flt;
    CanResult_e        r;

    if (hcan == NULL)
    {
        return CAN_ERR_NULL_PTR;                        /* Set can.hw.handle = &hcanX. */
    }
#if CAN_CFG_ENABLE_FD
    if (can->cfg.fd_enabled)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* bxCAN is classic CAN only. */
    }
#endif

    ctx = &s_bx_ctx[can->instance];
    memset(ctx, 0, sizeof(*ctx));
    can->port_ctx = ctx;

    r = CanBxcan_CalcTiming(HAL_RCC_GetPCLK1Freq(), can->cfg.nominal.bitrate,
                            can->cfg.nominal.sample_point, &can->cfg.nominal);
    if (CAN_IS_ERR(r))
    {
        return r;
    }

    /* Bit timing registers hold "value - 1" in their own bit positions. */
    hcan->Init.Prescaler         = can->cfg.nominal.prescaler;
    hcan->Init.SyncJumpWidth     = ((uint32_t)(can->cfg.nominal.sjw - 1U)) << 24;
    hcan->Init.TimeSeg1          = ((uint32_t)(can->cfg.nominal.tseg1 - 1U)) << 16;
    hcan->Init.TimeSeg2          = ((uint32_t)(can->cfg.nominal.tseg2 - 1U)) << 20;
    switch (can->cfg.mode)
    {
        case CANLIB_MODE_LISTEN_ONLY: hcan->Init.Mode = CAN_MODE_SILENT;   break;
        case CANLIB_MODE_LOOPBACK:    hcan->Init.Mode = CAN_MODE_LOOPBACK; break;
        default:                      hcan->Init.Mode = CAN_MODE_NORMAL;   break;
    }
    hcan->Init.TimeTriggeredMode    = DISABLE;
    hcan->Init.AutoBusOff           = DISABLE;          /* The library handles bus-off recovery. */
    hcan->Init.AutoWakeUp           = DISABLE;
    hcan->Init.AutoRetransmission   = can->cfg.auto_retransmit ? ENABLE : DISABLE;
    hcan->Init.ReceiveFifoLocked    = DISABLE;          /* FIFO full: newest frame overwrites the oldest. */
    hcan->Init.TransmitFifoPriority = DISABLE;          /* Lowest identifier goes first. */

    if (HAL_CAN_Init(hcan) != HAL_OK)
    {
        return bx_hal_fail(can);
    }

    /* Hardware filter: mask 0 accepts every frame into FIFO0. */
    memset(&flt, 0, sizeof(flt));
    flt.FilterBank           = 0U;
    flt.FilterMode           = CAN_FILTERMODE_IDMASK;
    flt.FilterScale          = CAN_FILTERSCALE_32BIT;
    flt.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    flt.FilterActivation     = CAN_FILTER_ENABLE;
#if defined(CAN2)
    flt.SlaveStartFilterBank = BXCAN_SLAVE_FILTER_START;
    if (hcan->Instance == CAN2)
    {
        flt.FilterBank = BXCAN_SLAVE_FILTER_START;      /* CAN2 uses the banks from 14 up. */
    }
#endif
    if (HAL_CAN_ConfigFilter(hcan, &flt) != HAL_OK)
    {
        return bx_hal_fail(can);
    }

    ctx->inited = true;
    return CAN_OK;
}

/** Stop the peripheral. The HAL de-init is left to the application (it owns pins and clocks). */
static CanResult_e bx_deinit(CanLib_t *can)
{
    CanBxcanCtx_t *ctx = bx_ctx(can);

    if (ctx != NULL)
    {
        ctx->inited = false;
    }
    can->port_ctx = NULL;
    return CAN_OK;
}

/** Join the bus. */
static CanResult_e bx_start(CanLib_t *can)
{
    CAN_HandleTypeDef *hcan = bx_hcan(can);

    if (HAL_CAN_Start(hcan) != HAL_OK)
    {
        return bx_hal_fail(can);
    }
#if CAN_CFG_BXCAN_USE_IRQ
    if (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        return bx_hal_fail(can);
    }
#endif
    return CAN_OK;
}

/** Leave the bus. */
static CanResult_e bx_stop(CanLib_t *can)
{
    CAN_HandleTypeDef *hcan = bx_hcan(can);

#if CAN_CFG_BXCAN_USE_IRQ
    (void)HAL_CAN_DeactivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING);
#endif
    if (HAL_CAN_Stop(hcan) != HAL_OK)
    {
        return bx_hal_fail(can);
    }
    return CAN_OK;
}

/** Put one frame in a free mailbox. */
static CanResult_e bx_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    CAN_HandleTypeDef *hcan = bx_hcan(can);
    CAN_TxHeaderTypeDef h;
    uint32_t            mailbox = 0U;

    if (frame->header.format == CAN_FORMAT_FD)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* bxCAN cannot send CAN FD. */
    }
    if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) == 0U)
    {
        return CAN_ERR_TX_FULL;
    }

    memset(&h, 0, sizeof(h));
    if (frame->header.id_type == CAN_ID_EXTENDED)
    {
        h.IDE   = CAN_ID_EXT;
        h.ExtId = frame->header.id;
    }
    else
    {
        h.IDE   = CAN_ID_STD;
        h.StdId = frame->header.id;
    }
    h.RTR = (frame->header.frame_type == CAN_FRAME_REMOTE) ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    h.DLC = (frame->header.dlc > CAN_CLASSIC_MAX_DLEN) ? CAN_CLASSIC_MAX_DLEN : frame->header.dlc;
    h.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(hcan, &h, (uint8_t *)frame->body.data, &mailbox) != HAL_OK)
    {
        return bx_hal_fail(can);
    }
    bx_ctx(can)->last_mailbox = mailbox;
    return CAN_OK;
}

/** Take one frame out of FIFO0. */
static CanResult_e bx_receive(CanLib_t *can, CanFrame_t *frame)
{
    CAN_HandleTypeDef  *hcan = bx_hcan(can);
    CAN_RxHeaderTypeDef h;
    uint8_t             data[8] = {0};

    if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) == 0U)
    {
        return CAN_ERR_RX_EMPTY;
    }
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &h, data) != HAL_OK)
    {
        return bx_hal_fail(can);
    }
    bx_decode_frame(&h, data, frame);
    return CAN_OK;
}

/** Read TEC, REC and the bus-off flag from ESR. Count FIFO overruns. */
static CanResult_e bx_get_errors(CanLib_t *can)
{
    CAN_HandleTypeDef *hcan = bx_hcan(can);
    uint32_t           esr  = hcan->Instance->ESR;

    can->bus.tec            = (uint8_t)((esr >> BXCAN_ESR_TEC_POS) & 0xFFU);
    can->bus.rec            = (uint8_t)((esr >> BXCAN_ESR_REC_POS) & 0xFFU);
    can->bus.hw_error_flags = esr;
    bx_ctx(can)->last_esr   = esr;

    if ((hcan->Instance->RF0R & CAN_RF0R_FOVR0) != 0U)
    {
#if CAN_CFG_ENABLE_STATS
        can->stats.rx_dropped++;                        /* FIFO overrun: count it, then clear the flag. */
#endif
        hcan->Instance->RF0R = CAN_RF0R_FOVR0;          /* Write 1 to clear. */
    }
    return ((esr & BXCAN_ESR_BOFF) != 0U) ? CAN_ERR_BUS_OFF : CAN_OK;
}

/** Leave bus-off: stop and start the peripheral (needs 128 x 11 recessive bits on the bus). */
static CanResult_e bx_recover(CanLib_t *can)
{
    CanResult_e r = bx_stop(can);

    if (CAN_IS_ERR(r))
    {
        return r;
    }
    return bx_start(can);
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
/** Set the transceiver mode pin. */
static CanResult_e bx_set_xcvr(CanLib_t *can, CanXcvrMode_e mode)
{
    return CanPort_SetXcvrPin(&can->hw, mode);
}
#endif

void CanBxcan_OnRxFifo0Pending(CanLib_t *can)
{
    CanFrame_t f;

    if ((can == NULL) || (can->port != &g_CanPortStm32Bxcan))
    {
        return;
    }
    while (bx_receive(can, &f) == CAN_OK)
    {
        CanLib_OnRxFrame(can, &f);                      /* Hand over to the library (ISR safe). */
    }
}

const CanBxcanCtx_t *CanBxcan_GetCtx(const CanLib_t *can)
{
    if ((can == NULL) || (can->port != &g_CanPortStm32Bxcan) || (can->port_ctx == NULL))
    {
        return NULL;
    }
    return (const CanBxcanCtx_t *)can->port_ctx;
}

/** Function table of the bxCAN backend. */
const CanPortOps_t g_CanPortStm32Bxcan =
{
    "bxcan",
    bx_init,
    bx_deinit,
    bx_start,
    bx_stop,
    bx_transmit,
    bx_receive,
    bx_get_errors,
    bx_recover,
#if CAN_CFG_ENABLE_XCVR_CONTROL
    bx_set_xcvr
#endif
};

#endif /* CAN_PLATFORM_STM32 && bxCAN backend */
