/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_mcp25xx.c
 * @brief   MCP2515 backend: classic CAN over SPI.
 *
 * All SPI traffic goes through CanPlatform_SpiTransfer(), so this file has no
 * vendor code and also runs on the host against a simulated chip.
 */
#include "can_lib.h"

#if CAN_CFG_ENABLE_BACKEND_MCP25XX

#include <string.h>

/** Largest SPI transfer used here: 1 command byte + 13 RX buffer bytes. */
#define MCP_XFER_MAX        14U

/** Mode change polling: number of CANSTAT reads before giving up. */
#define MCP_MODE_TRIES      50U

/** Private data of each instance (no malloc). Indexed by CanLib_t.instance. */
static CanMcpCtx_t s_mcp_ctx[CAN_CFG_MAX_INSTANCES];

/* ======================================================================== */
/* SPI HELPERS                                                              */
/* ======================================================================== */

/** Return the private data of an instance, or NULL when not initialized. */
static CanMcpCtx_t *mcp_ctx(const CanLib_t *can)
{
    return (CanMcpCtx_t *)can->port_ctx;
}

/** One raw transfer. Counts failures in the context. */
static CanResult_e mcp_xfer(const CanLib_t *can, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    CanResult_e  r = CanPlatform_SpiTransfer(&can->hw, tx, rx, len);
    CanMcpCtx_t *ctx = mcp_ctx(can);

    if (CAN_IS_ERR(r))
    {
        if (ctx != NULL)
        {
            ctx->spi_errors++;
        }
        if (r != CAN_ERR_NOT_SUPPORTED)
        {
            r = CAN_ERR_SPI;
        }
    }
    return r;
}

/** Read n (1..MCP_XFER_MAX-2) registers starting at addr. */
static CanResult_e mcp_read_regs(const CanLib_t *can, uint8_t addr, uint8_t *out, uint8_t n)
{
    uint8_t     tx[MCP_XFER_MAX] = {0};
    uint8_t     rx[MCP_XFER_MAX] = {0};
    CanResult_e r;

    tx[0] = (uint8_t)MCP_CMD_READ;
    tx[1] = addr;
    r = mcp_xfer(can, tx, rx, (uint16_t)(2U + n));
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    memcpy(out, &rx[2], n);
    return CAN_OK;
}

/** Write n (1..3) registers starting at addr. */
static CanResult_e mcp_write_regs(const CanLib_t *can, uint8_t addr, const uint8_t *in, uint8_t n)
{
    uint8_t tx[MCP_XFER_MAX] = {0};
    uint8_t rx[MCP_XFER_MAX] = {0};

    tx[0] = (uint8_t)MCP_CMD_WRITE;
    tx[1] = addr;
    memcpy(&tx[2], in, n);
    return mcp_xfer(can, tx, rx, (uint16_t)(2U + n));
}

/** Write one register. */
static CanResult_e mcp_write_reg(const CanLib_t *can, uint8_t addr, uint8_t value)
{
    return mcp_write_regs(can, addr, &value, 1U);
}

/** Change selected bits of one register: reg = (reg & ~mask) | (value & mask). */
static CanResult_e mcp_bit_modify(const CanLib_t *can, uint8_t addr, uint8_t mask, uint8_t value)
{
    uint8_t tx[4];
    uint8_t rx[4] = {0};

    tx[0] = (uint8_t)MCP_CMD_BIT_MODIFY;
    tx[1] = addr;
    tx[2] = mask;
    tx[3] = value;
    return mcp_xfer(can, tx, rx, 4U);
}

/** READ STATUS instruction: quick view of RX flags and TX requests. */
static CanResult_e mcp_read_status(const CanLib_t *can, uint8_t *status)
{
    uint8_t     tx[2] = { (uint8_t)MCP_CMD_READ_STATUS, 0U };
    uint8_t     rx[2] = {0};
    CanResult_e r = mcp_xfer(can, tx, rx, 2U);

    if (CAN_IS_ERR(r))
    {
        return r;
    }
    *status = rx[1];
    return CAN_OK;
}

/** Request an operation mode and wait until CANSTAT confirms it. */
static CanResult_e mcp_set_mode(CanLib_t *can, uint8_t canctrl)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);
    CanResult_e  r;
    uint8_t      stat = 0U;
    uint8_t      i;

    r = mcp_write_reg(can, MCP_REG_CANCTRL, canctrl);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    for (i = 0U; i < MCP_MODE_TRIES; i++)
    {
        r = mcp_read_regs(can, MCP_REG_CANSTAT, &stat, 1U);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        if (ctx != NULL)
        {
            ctx->last_canstat = stat;
        }
        if ((stat & MCP_MODE_MASK) == (canctrl & MCP_MODE_MASK))
        {
            return CAN_OK;
        }
        CanPlatform_DelayMs(1U);
    }
    return CAN_ERR_TIMEOUT;     /* Chip did not enter the mode (bus busy or shorted?). */
}

/* ======================================================================== */
/* BIT TIMING                                                               */
/* ======================================================================== */

CanResult_e CanMcp_CalcTiming(uint32_t osc_hz, uint32_t bitrate, uint16_t sample_point, CanTiming_t *out)
{
    uint32_t prescaler;
    uint32_t best_err   = 0xFFFFFFFFUL;
    uint32_t best_total = 0U;
    uint32_t best_psc   = 0U;
    uint32_t best_t1    = 0U;
    uint32_t best_t2    = 0U;

    if (out == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    if ((osc_hz == 0U) || (bitrate == 0U) || (sample_point == 0U) || (sample_point >= 1000U))
    {
        return CAN_ERR_BAD_PARAM;
    }

    for (prescaler = 1U; prescaler <= MCP_BRP_MAX; prescaler++)
    {
        uint32_t tq_div = 2UL * prescaler * bitrate;
        uint32_t total;
        uint32_t t1;
        uint32_t t2;
        uint32_t real_sp;
        uint32_t err;

        if ((osc_hz % tq_div) != 0U)
        {
            continue;                                   /* Not an exact bitrate with this prescaler. */
        }
        total = osc_hz / tq_div;
        if ((total < MCP_TQ_MIN) || (total > MCP_TQ_MAX))
        {
            continue;
        }

        t1 = (total * (uint32_t)sample_point + 500UL) / 1000UL;     /* sync + tseg1 */
        t1 = (t1 > 0U) ? (t1 - 1U) : 0U;                            /* tseg1 */
        if (t1 > MCP_TSEG1_MAX)
        {
            t1 = MCP_TSEG1_MAX;                                   /* Very long bit: cap, the sample point moves earlier. */
        }
        t2 = total - 1U - t1;
        if (t2 < 2U)
        {
            t2 = 2U;
            t1 = total - 3U;
        }
        if (t2 > MCP_TSEG2_MAX)
        {
            t2 = MCP_TSEG2_MAX;
            t1 = total - 1U - t2;
        }
        if ((t1 < 2U) || (t1 > MCP_TSEG1_MAX))
        {
            continue;
        }

        real_sp = ((1U + t1) * 1000UL) / total;
        err = (real_sp > sample_point) ? (real_sp - sample_point) : ((uint32_t)sample_point - real_sp);
        if ((err < best_err) || ((err == best_err) && (total > best_total)))
        {
            best_err   = err;
            best_total = total;
            best_psc   = prescaler;
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
    out->prescaler      = (uint16_t)best_psc;
    out->tseg1          = (uint8_t)best_t1;
    out->tseg2          = (uint8_t)best_t2;
    out->sjw            = 1U;
    out->actual_bitrate = osc_hz / (2UL * best_psc * best_total);
    return CAN_OK;
}

/** Convert a timing into CNF1..CNF3 register values. */
static void mcp_timing_to_regs(const CanTiming_t *t, uint8_t *cnf1, uint8_t *cnf2, uint8_t *cnf3)
{
    uint32_t ps1  = (uint32_t)t->tseg1 / 2U;     /* Split tseg1 into phase 1 and propagation. */
    uint32_t prop;

    if (ps1 < 1U)
    {
        ps1 = 1U;
    }
    prop = (uint32_t)t->tseg1 - ps1;
    if (prop > 8U)
    {
        prop = 8U;
        ps1  = (uint32_t)t->tseg1 - prop;
    }
    if (prop < 1U)
    {
        prop = 1U;
        ps1  = (uint32_t)t->tseg1 - prop;
    }

    *cnf1 = (uint8_t)((((uint32_t)t->sjw - 1U) << 6) | ((uint32_t)t->prescaler - 1U));
    *cnf2 = (uint8_t)(0x80U | ((ps1 - 1U) << 3) | (prop - 1U));     /* BTLMODE = 1: PS2 from CNF3. */
    *cnf3 = (uint8_t)((uint32_t)t->tseg2 - 1U);
}

/* ======================================================================== */
/* FRAME CONVERSION                                                         */
/* ======================================================================== */

/** Fill the 5 header bytes of a TX / RX buffer from a frame header. */
static void mcp_encode_header(const CanHeader_t *h, uint8_t *b, uint8_t dlc_byte)
{
    if (h->id_type == CAN_ID_EXTENDED)
    {
        b[0] = (uint8_t)(h->id >> 21);
        b[1] = (uint8_t)((((h->id >> 18) & 0x07UL) << 5) | 0x08UL | ((h->id >> 16) & 0x03UL));
        b[2] = (uint8_t)(h->id >> 8);
        b[3] = (uint8_t)(h->id);
    }
    else
    {
        b[0] = (uint8_t)(h->id >> 3);
        b[1] = (uint8_t)((h->id & 0x07UL) << 5);
        b[2] = 0U;
        b[3] = 0U;
    }
    b[4] = dlc_byte;
}

/** Fill a frame from the 13 bytes read out of an RX buffer. */
static void mcp_decode_frame(const uint8_t *b, CanFrame_t *f)
{
    uint8_t dlc = (uint8_t)(b[4] & 0x0FU);
    uint8_t len = (dlc > CAN_CLASSIC_MAX_DLEN) ? (uint8_t)CAN_CLASSIC_MAX_DLEN : dlc;
    bool    ext = ((b[1] & 0x08U) != 0U);

    memset(f, 0, sizeof(*f));
    if (ext)
    {
        f->header.id = ((uint32_t)b[0] << 21) | ((uint32_t)(b[1] >> 5) << 18) |
                       ((uint32_t)(b[1] & 0x03U) << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
        f->header.id_type    = CAN_ID_EXTENDED;
        f->header.frame_type = ((b[4] & 0x40U) != 0U) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;
    }
    else
    {
        f->header.id         = ((uint32_t)b[0] << 3) | (uint32_t)(b[1] >> 5);
        f->header.id_type    = CAN_ID_STANDARD;
        f->header.frame_type = ((b[1] & 0x10U) != 0U) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;   /* SRR bit */
    }
    f->header.format       = CAN_FORMAT_CLASSIC;
    f->header.dlc          = dlc;
    f->header.filter_index = CAN_FILTER_NONE;
    if (f->header.frame_type == CAN_FRAME_DATA)
    {
        f->header.data_len = len;
        memcpy(f->body.data, &b[5], len);
    }
    else
    {
        f->header.data_len = 0U;                        /* A remote frame carries no data. */
    }
    f->header.timestamp_ms = CanLib_GetTickMs();
}

/* ======================================================================== */
/* PORT FUNCTIONS                                                           */
/* ======================================================================== */

/** Reset the chip, check it answers, load bit timing and receive settings. */
static CanResult_e mcp_init(CanLib_t *can)
{
    CanMcpCtx_t *ctx;
    CanResult_e  r;
    uint8_t      reset_cmd = (uint8_t)MCP_CMD_RESET;
    uint8_t      dummy     = 0U;
    uint8_t      stat      = 0U;
    uint8_t      regs[3];
    uint8_t      back[3];

    if (can->cfg.mcp_variant != CAN_MCP_2515)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* 2517FD / 2518FD come in a later step. */
    }
#if CAN_CFG_ENABLE_FD
    if (can->cfg.fd_enabled)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* MCP2515 is classic CAN only. */
    }
#endif
    if (can->hw.osc_hz == 0U)
    {
        return CAN_ERR_BAD_PARAM;                       /* Set can.hw.osc_hz to the crystal frequency. */
    }

    ctx = &s_mcp_ctx[can->instance];
    memset(ctx, 0, sizeof(*ctx));
    can->port_ctx = ctx;

    r = CanMcp_CalcTiming(can->hw.osc_hz, can->cfg.nominal.bitrate, can->cfg.nominal.sample_point,
                          &can->cfg.nominal);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    mcp_timing_to_regs(&can->cfg.nominal, &ctx->cnf1, &ctx->cnf2, &ctx->cnf3);

    /* Reset: the chip enters configuration mode by itself. */
    r = mcp_xfer(can, &reset_cmd, &dummy, 1U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    CanPlatform_DelayMs(5U);

    r = mcp_read_regs(can, MCP_REG_CANSTAT, &stat, 1U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_canstat = stat;
    if ((stat & MCP_MODE_MASK) != MCP_MODE_CONFIG)
    {
        return CAN_ERR_CHIP_NOT_FOUND;                  /* 0x00 / 0xFF here usually means wiring or no power. */
    }

    /* Bit timing: CNF3, CNF2, CNF1 are consecutive registers. Read back to prove the SPI link. */
    regs[0] = ctx->cnf3;
    regs[1] = ctx->cnf2;
    regs[2] = ctx->cnf1;
    r = mcp_write_regs(can, MCP_REG_CNF3, regs, 3U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    r = mcp_read_regs(can, MCP_REG_CNF3, back, 3U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    if (memcmp(regs, back, 3U) != 0)
    {
        return CAN_ERR_SPI;
    }

    /* Receive: accept everything (software filters decide), roll over into buffer 1. */
    r = mcp_write_reg(can, MCP_REG_RXB0CTRL, 0x64U);    /* RXM = 11 (no filter), BUKT = 1. */
    if (CAN_IS_ERR(r)) { return r; }
    r = mcp_write_reg(can, MCP_REG_RXB1CTRL, 0x60U);    /* RXM = 11. */
    if (CAN_IS_ERR(r)) { return r; }
    r = mcp_write_reg(can, MCP_REG_CANINTF, 0x00U);     /* Clear old flags. */
    if (CAN_IS_ERR(r)) { return r; }
    r = mcp_write_reg(can, MCP_REG_EFLG, 0x00U);
    if (CAN_IS_ERR(r)) { return r; }
    r = mcp_write_reg(can, MCP_REG_CANINTE, (uint8_t)(MCP_INT_RX0IE | MCP_INT_RX1IE));
    if (CAN_IS_ERR(r)) { return r; }

    switch (can->cfg.mode)
    {
        case CANLIB_MODE_LISTEN_ONLY: ctx->run_mode = (uint8_t)MCP_MODE_LISTEN;   break;
        case CANLIB_MODE_LOOPBACK:    ctx->run_mode = (uint8_t)MCP_MODE_LOOPBACK; break;
        default:                   ctx->run_mode = (uint8_t)MCP_MODE_NORMAL;   break;
    }
    if (!can->cfg.auto_retransmit)
    {
        ctx->run_mode |= (uint8_t)MCP_CANCTRL_OSM;      /* One-shot: no resend after a lost frame. */
    }

    ctx->inited = true;
    return CAN_OK;
}

/** Put the chip back in configuration mode and forget the context. */
static CanResult_e mcp_deinit(CanLib_t *can)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);
    CanResult_e  r   = CAN_OK;

    if ((ctx != NULL) && ctx->inited)
    {
        r = mcp_set_mode(can, (uint8_t)MCP_MODE_CONFIG);
        ctx->inited = false;
    }
    can->port_ctx = NULL;
    return r;
}

/** Join the bus. */
static CanResult_e mcp_start(CanLib_t *can)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);

    if ((ctx == NULL) || !ctx->inited)
    {
        return CAN_ERR_NOT_INIT;
    }
    return mcp_set_mode(can, ctx->run_mode);
}

/** Leave the bus. */
static CanResult_e mcp_stop(CanLib_t *can)
{
    return mcp_set_mode(can, (uint8_t)MCP_MODE_CONFIG);
}

/** Load one free transmit buffer and request the transmission. */
static CanResult_e mcp_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    CanMcpCtx_t   *ctx = mcp_ctx(can);
    const CanHeader_t *h = &frame->header;
    CanResult_e    r;
    uint8_t        status = 0U;
    uint8_t        buf;
    uint8_t        len;
    uint8_t        dlc_byte;
    uint8_t        tx[MCP_XFER_MAX] = {0};
    uint8_t        rx[MCP_XFER_MAX] = {0};
    uint8_t        rts;

    if (h->format == CAN_FORMAT_FD)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* MCP2515 cannot send CAN FD. */
    }

    r = mcp_read_status(can, &status);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_status = status;

    if ((status & MCP_STATUS_TX0REQ) == 0U)      { buf = 0U; }
    else if ((status & MCP_STATUS_TX1REQ) == 0U) { buf = 1U; }
    else if ((status & MCP_STATUS_TX2REQ) == 0U) { buf = 2U; }
    else                                         { return CAN_ERR_TX_FULL; }

    len = (h->data_len > CAN_CLASSIC_MAX_DLEN) ? (uint8_t)CAN_CLASSIC_MAX_DLEN : h->data_len;
    dlc_byte = (h->dlc > 15U) ? 15U : h->dlc;
    if (h->frame_type == CAN_FRAME_REMOTE)
    {
        dlc_byte |= 0x40U;                              /* RTR */
        len = 0U;
    }

    tx[0] = (uint8_t)(MCP_CMD_LOAD_TX_BASE + (2U * buf));
    mcp_encode_header(h, &tx[1], dlc_byte);
    memcpy(&tx[6], frame->body.data, len);

    r = mcp_xfer(can, tx, rx, (uint16_t)(6U + len));
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    rts = (uint8_t)(MCP_CMD_RTS_BASE | (1U << buf));
    return mcp_xfer(can, &rts, rx, 1U);
}

/** Read one frame out of the chip, if any. */
static CanResult_e mcp_receive(CanLib_t *can, CanFrame_t *frame)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);
    CanResult_e  r;
    uint8_t      status = 0U;
    uint8_t      buf;
    uint8_t      tx[MCP_XFER_MAX] = {0};
    uint8_t      rx[MCP_XFER_MAX] = {0};

    r = mcp_read_status(can, &status);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_status = status;

    if ((status & MCP_STATUS_RX0IF) != 0U)      { buf = 0U; }
    else if ((status & MCP_STATUS_RX1IF) != 0U) { buf = 1U; }
    else                                        { return CAN_ERR_RX_EMPTY; }

    tx[0] = (uint8_t)(MCP_CMD_READ_RX_BASE + (4U * buf));   /* Reading from SIDH clears the flag. */
    r = mcp_xfer(can, tx, rx, MCP_XFER_MAX);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    mcp_decode_frame(&rx[1], frame);
    return CAN_OK;
}

/** Read TEC, REC and EFLG into can->bus. */
static CanResult_e mcp_get_errors(CanLib_t *can)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);
    CanResult_e  r;
    uint8_t      cnt[2] = {0};
    uint8_t      eflg   = 0U;

    r = mcp_read_regs(can, MCP_REG_TEC, cnt, 2U);       /* TEC then REC. */
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    r = mcp_read_regs(can, MCP_REG_EFLG, &eflg, 1U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }

    can->bus.tec            = cnt[0];
    can->bus.rec            = cnt[1];
    can->bus.hw_error_flags = eflg;
    ctx->last_eflg          = eflg;

    if ((eflg & (MCP_EFLG_RX0OVR | MCP_EFLG_RX1OVR)) != 0U)
    {
#if CAN_CFG_ENABLE_STATS
        can->stats.rx_dropped++;                        /* The chip lost a frame: count it, clear the flag. */
#endif
        r = mcp_bit_modify(can, MCP_REG_EFLG, (uint8_t)(MCP_EFLG_RX0OVR | MCP_EFLG_RX1OVR), 0U);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
    }
    return ((eflg & MCP_EFLG_TXBO) != 0U) ? CAN_ERR_BUS_OFF : CAN_OK;
}

/** Leave bus-off: configuration mode resets the counters, then rejoin. */
static CanResult_e mcp_recover(CanLib_t *can)
{
    CanMcpCtx_t *ctx = mcp_ctx(can);
    CanResult_e  r;

    r = mcp_set_mode(can, (uint8_t)MCP_MODE_CONFIG);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    r = mcp_write_reg(can, MCP_REG_EFLG, 0x00U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    return mcp_set_mode(can, ctx->run_mode);
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
/** Set the transceiver mode pin. */
static CanResult_e mcp_set_xcvr(CanLib_t *can, CanXcvrMode_e mode)
{
    return CanPort_SetXcvrPin(&can->hw, mode);
}
#endif

const CanMcpCtx_t *CanMcp_GetCtx(const CanLib_t *can)
{
    if ((can == NULL) || (can->port != &g_CanPortMcp25xx) || (can->port_ctx == NULL))
    {
        return NULL;
    }
    return (const CanMcpCtx_t *)can->port_ctx;
}

/** Function table of the MCP2515 backend. */
const CanPortOps_t g_CanPortMcp25xx =
{
    "mcp2515",
    mcp_init,
    mcp_deinit,
    mcp_start,
    mcp_stop,
    mcp_transmit,
    mcp_receive,
    mcp_get_errors,
    mcp_recover,
#if CAN_CFG_ENABLE_XCVR_CONTROL
    mcp_set_xcvr
#endif
};

#endif /* CAN_CFG_ENABLE_BACKEND_MCP25XX */
