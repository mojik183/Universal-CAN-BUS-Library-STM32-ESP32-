/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_mcp251xfd.c
 * @brief   MCP2517FD / MCP2518FD backend: classic CAN and CAN FD over SPI.
 *
 * All SPI traffic goes through CanPlatform_SpiTransfer(), so this file has no
 * vendor code and also runs on the host against a simulated chip.
 */
#include "can_lib.h"

#if CAN_CFG_ENABLE_BACKEND_MCP251XFD

#include <string.h>

/** Largest SPI transfer: 2 command bytes + one RAM object (8 header bytes + 64 payload bytes). */
#define MCPFD_XFER_MAX          74U

/** Mode change polling: number of CiCON reads before giving up. */
#define MCPFD_MODE_TRIES        50U

/** Oscillator start-up polling: number of OSC reads (1 ms apart) before giving up. */
#define MCPFD_OSC_TRIES         20U

/** FIFO reset polling: number of FIFOCON reads before giving up. */
#define MCPFD_FRESET_TRIES      20U

/** Bytes written per SPI transfer while clearing the message RAM. */
#define MCPFD_CLEAR_CHUNK       64U

/** Private data of each instance (no malloc). Indexed by CanLib_t.instance. */
static CanMcpFdCtx_t s_mcpfd_ctx[CAN_CFG_MAX_INSTANCES];

/* ======================================================================== */
/* SPI HELPERS                                                              */
/* ======================================================================== */

/** Return the private data of an instance. */
static CanMcpFdCtx_t *fd_ctx(const CanLib_t *can)
{
    return (CanMcpFdCtx_t *)can->port_ctx;
}

/** One raw transfer. Counts failures in the context. */
static CanResult_e fd_xfer(const CanLib_t *can, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    CanResult_e    r   = CanPlatform_SpiTransfer(&can->hw, tx, rx, len);
    CanMcpFdCtx_t *ctx = fd_ctx(can);

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

/** Read n bytes (1..MCPFD_XFER_MAX-2) starting at a chip address. */
static CanResult_e fd_read(const CanLib_t *can, uint16_t addr, uint8_t *out, uint8_t n)
{
    uint8_t     tx[MCPFD_XFER_MAX] = {0};
    uint8_t     rx[MCPFD_XFER_MAX] = {0};
    CanResult_e r;

    tx[0] = (uint8_t)((MCPFD_CMD_READ << 4) | ((addr >> 8) & 0x0FU));
    tx[1] = (uint8_t)(addr & 0xFFU);
    r = fd_xfer(can, tx, rx, (uint16_t)(2U + n));
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    memcpy(out, &rx[2], n);
    return CAN_OK;
}

/** Write n bytes (1..MCPFD_XFER_MAX-2) starting at a chip address. */
static CanResult_e fd_write(const CanLib_t *can, uint16_t addr, const uint8_t *in, uint8_t n)
{
    uint8_t tx[MCPFD_XFER_MAX] = {0};
    uint8_t rx[MCPFD_XFER_MAX] = {0};

    tx[0] = (uint8_t)((MCPFD_CMD_WRITE << 4) | ((addr >> 8) & 0x0FU));
    tx[1] = (uint8_t)(addr & 0xFFU);
    memcpy(&tx[2], in, n);
    return fd_xfer(can, tx, rx, (uint16_t)(2U + n));
}

/** Read one 32 bit register (little endian on the wire). */
static CanResult_e fd_read32(const CanLib_t *can, uint16_t addr, uint32_t *value)
{
    uint8_t     b[4] = {0};
    CanResult_e r    = fd_read(can, addr, b, 4U);

    if (CAN_IS_ERR(r))
    {
        return r;
    }
    *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return CAN_OK;
}

/** Write one 32 bit register. */
static CanResult_e fd_write32(const CanLib_t *can, uint16_t addr, uint32_t value)
{
    uint8_t b[4];

    b[0] = (uint8_t)(value);
    b[1] = (uint8_t)(value >> 8);
    b[2] = (uint8_t)(value >> 16);
    b[3] = (uint8_t)(value >> 24);
    return fd_write(can, addr, b, 4U);
}

/** Write a single byte (used to change part of a register without read-modify-write). */
static CanResult_e fd_write8(const CanLib_t *can, uint16_t addr, uint8_t value)
{
    return fd_write(can, addr, &value, 1U);
}

/** Request an operation mode and wait until CiCON confirms it. */
static CanResult_e fd_set_mode(CanLib_t *can, uint8_t reqop)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);
    CanResult_e    r;
    uint8_t        b2 = 0U;
    uint8_t        i;

    r = fd_write8(can, (uint16_t)(MCPFD_REG_CON + 3U), reqop);      /* REQOP = bits 26..24 = byte 3 bits 2..0. */
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    for (i = 0U; i < MCPFD_MODE_TRIES; i++)
    {
        r = fd_read(can, (uint16_t)(MCPFD_REG_CON + 2U), &b2, 1U);  /* OPMOD = bits 23..21 = byte 2 bits 7..5. */
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        if (ctx != NULL)
        {
            ctx->last_con = ((uint32_t)b2 << 16);
        }
        if ((uint8_t)(b2 >> 5) == reqop)
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

CanResult_e CanMcpFd_CalcTiming(uint32_t sysclk_hz, uint32_t bitrate, uint16_t sample_point,
                                bool data_phase, CanTiming_t *out)
{
    uint32_t psc_max  = data_phase ? MCPFD_DAT_PSC_MAX  : MCPFD_NOM_PSC_MAX;
    uint32_t seg1_max = data_phase ? MCPFD_DAT_SEG1_MAX : MCPFD_NOM_SEG1_MAX;
    uint32_t seg2_max = data_phase ? MCPFD_DAT_SEG2_MAX : MCPFD_NOM_SEG2_MAX;
    uint32_t tq_cap   = data_phase ? (1U + seg1_max + seg2_max) : MCPFD_NOM_TQ_CAP;
    uint32_t seg_min  = data_phase ? 1U : 2U;       /* The data phase needs short segments at 8 Mbit/s. */
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
    if ((sysclk_hz == 0U) || (bitrate == 0U) || (sample_point == 0U) || (sample_point >= 1000U))
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

        if ((sysclk_hz % div) != 0U)
        {
            continue;                                   /* Not an exact bitrate with this prescaler. */
        }
        total = sysclk_hz / div;
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
        if (s2 < seg_min)
        {
            s2 = seg_min;
            s1 = total - 1U - s2;
        }
        if (s2 > seg2_max)
        {
            s2 = seg2_max;
            s1 = total - 1U - s2;
        }
        if ((s1 < seg_min) || (s1 > seg1_max))
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
    out->actual_bitrate = sysclk_hz / (best_psc * best_total);
    return CAN_OK;
}

/** Bit timing register value: BRP in bits 31..24, TSEG1 from bit 16, TSEG2 from bit 8, SJW in bit 0. */
static uint32_t fd_timing_reg(const CanTiming_t *t)
{
    return ((uint32_t)(t->prescaler - 1U) << 24) | ((uint32_t)(t->tseg1 - 1U) << 16) |
           ((uint32_t)(t->tseg2 - 1U) << 8) | (uint32_t)(t->sjw - 1U);
}

/* ======================================================================== */
/* FRAME CONVERSION                                                         */
/* ======================================================================== */

/** Build the two header words (ID word, control word) of a transmit object. */
static void fd_encode_header(const CanHeader_t *h, bool fd_frame, uint8_t dlc, uint32_t *t0, uint32_t *t1)
{
    if (h->id_type == CAN_ID_EXTENDED)
    {
        *t0 = ((h->id >> 18) & 0x7FFUL) | ((h->id & 0x3FFFFUL) << 11);     /* SID, then EID */
        *t1 = (1UL << 4);                                                  /* IDE */
    }
    else
    {
        *t0 = h->id & 0x7FFUL;
        *t1 = 0U;
    }
    *t1 |= (uint32_t)(dlc & 0x0FU);
    if (h->frame_type == CAN_FRAME_REMOTE)
    {
        *t1 |= (1UL << 5);                                                 /* RTR */
    }
    if (fd_frame)
    {
        *t1 |= (1UL << 7);                                                 /* FDF */
        if (h->bitrate_switch)
        {
            *t1 |= (1UL << 6);                                             /* BRS */
        }
    }
}

/** Fill a frame from the bytes of a receive object (8 header bytes, then payload). */
static void fd_decode_frame(const uint8_t *o, CanFrame_t *f, uint8_t payload)
{
    uint32_t r0 = (uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16) | ((uint32_t)o[3] << 24);
    uint32_t r1 = (uint32_t)o[4] | ((uint32_t)o[5] << 8) | ((uint32_t)o[6] << 16) | ((uint32_t)o[7] << 24);
    uint8_t  dlc   = (uint8_t)(r1 & 0x0FU);
    bool     ext   = ((r1 & (1UL << 4)) != 0U);
    bool     is_fd = ((r1 & (1UL << 7)) != 0U);
    uint8_t  len;

    memset(f, 0, sizeof(*f));
    if (ext)
    {
        f->header.id      = (((r0 & 0x7FFUL) << 18) | ((r0 >> 11) & 0x3FFFFUL));
        f->header.id_type = CAN_ID_EXTENDED;
    }
    else
    {
        f->header.id      = r0 & 0x7FFUL;
        f->header.id_type = CAN_ID_STANDARD;
    }
    f->header.frame_type     = (!is_fd && ((r1 & (1UL << 5)) != 0U)) ? CAN_FRAME_REMOTE : CAN_FRAME_DATA;
    f->header.format         = is_fd ? CAN_FORMAT_FD : CAN_FORMAT_CLASSIC;
    f->header.dlc            = dlc;
    f->header.bitrate_switch = is_fd && ((r1 & (1UL << 6)) != 0U);
    f->header.error_state    = is_fd && ((r1 & (1UL << 8)) != 0U);
    f->header.filter_index   = CAN_FILTER_NONE;

    if (f->header.frame_type == CAN_FRAME_DATA)
    {
        len = CanLib_DlcToLen(dlc, f->header.format);
        if (len > payload)
        {
            len = payload;                              /* Never read beyond the RAM object. */
        }
        if (len > CAN_MAX_DATA_LEN)
        {
            len = (uint8_t)CAN_MAX_DATA_LEN;            /* FD frame but FD is off in can_cfg.h: keep what fits. */
        }
        f->header.data_len = len;
        memcpy(f->body.data, &o[MCPFD_OBJ_HEADER], len);
    }
    f->header.timestamp_ms = CanLib_GetTickMs();
}

/* ======================================================================== */
/* PORT FUNCTIONS                                                           */
/* ======================================================================== */

/** Clear the whole message RAM (needed once when the ECC is switched on). */
static CanResult_e fd_clear_ram(const CanLib_t *can)
{
    uint8_t     zero[MCPFD_CLEAR_CHUNK] = {0};
    uint16_t    addr;
    CanResult_e r;

    for (addr = MCPFD_RAM_START; addr < (MCPFD_RAM_START + MCPFD_RAM_SIZE); addr = (uint16_t)(addr + MCPFD_CLEAR_CHUNK))
    {
        r = fd_write(can, addr, zero, MCPFD_CLEAR_CHUNK);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
    }
    return CAN_OK;
}

/** Reset a FIFO (write FIFOCON with FRESET) and wait until the chip clears the bit. */
static CanResult_e fd_config_fifo(const CanLib_t *can, uint16_t reg, uint32_t value)
{
    CanResult_e r;
    uint32_t    con = 0U;
    uint8_t     i;

    r = fd_write32(can, reg, value | MCPFD_FIFOCON_FRESET);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    for (i = 0U; i < MCPFD_FRESET_TRIES; i++)
    {
        r = fd_read32(can, reg, &con);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        if ((con & MCPFD_FIFOCON_FRESET) == 0U)
        {
            return CAN_OK;
        }
    }
    return CAN_ERR_TIMEOUT;
}

/** Reset the chip, check it answers, load clock, bit timing, FIFOs and filter. */
static CanResult_e fd_init(CanLib_t *can)
{
    CanMcpFdCtx_t *ctx;
    CanResult_e    r;
    uint8_t        reset_cmd[2] = { (uint8_t)(MCPFD_CMD_RESET << 4), 0U };
    uint8_t        dummy[2]     = {0};
    uint32_t       osc          = 0U;
    uint32_t       v            = 0U;
    uint32_t       want_osc;
    uint32_t       fifo_common;
    bool           use_pll;
    bool           fd_on = false;
    uint8_t        i;
    CanTiming_t    data_timing;

    if ((can->cfg.mcp_variant != CAN_MCP_2517FD) && (can->cfg.mcp_variant != CAN_MCP_2518FD))
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* Use CAN_BACKEND_MCP25XX for the MCP2515. */
    }
#if CAN_CFG_ENABLE_FD
    fd_on = can->cfg.fd_enabled;
#endif

    if ((can->hw.osc_hz != 4000000UL) && (can->hw.osc_hz != 20000000UL) && (can->hw.osc_hz != 40000000UL))
    {
        return CAN_ERR_BAD_PARAM;                       /* The chip supports 4, 20 and 40 MHz crystals only. */
    }
    use_pll = (can->hw.osc_hz == 4000000UL);

    ctx = &s_mcpfd_ctx[can->instance];
    memset(ctx, 0, sizeof(*ctx));
    can->port_ctx  = ctx;
    ctx->fd_mode   = fd_on;
    ctx->sysclk_hz = use_pll ? (can->hw.osc_hz * 10UL) : can->hw.osc_hz;

    r = CanMcpFd_CalcTiming(ctx->sysclk_hz, can->cfg.nominal.bitrate, can->cfg.nominal.sample_point,
                            false, &can->cfg.nominal);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
#if CAN_CFG_ENABLE_FD
    if (fd_on)
    {
        r = CanMcpFd_CalcTiming(ctx->sysclk_hz, can->cfg.data.bitrate, can->cfg.data.sample_point,
                                true, &data_timing);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        can->cfg.data = data_timing;
    }
#else
    (void)data_timing;
#endif

    /* Memory layout of the message RAM. */
    ctx->payload     = fd_on ? 64U : 8U;
    ctx->tx_obj_size = (uint8_t)(MCPFD_OBJ_HEADER + ctx->payload);
    ctx->rx_obj_size = ctx->tx_obj_size;
    ctx->tx_depth    = (uint8_t)MCPFD_TX_OBJECTS;
    ctx->rx_depth    = fd_on ? (uint8_t)MCPFD_RX_OBJECTS_FD : (uint8_t)MCPFD_RX_OBJECTS_CLASSIC;
    ctx->tx_base     = 0U;
    ctx->rx_base     = (uint16_t)(ctx->tx_depth * ctx->tx_obj_size);

    /* Reset: the chip enters configuration mode by itself. */
    r = fd_xfer(can, reset_cmd, dummy, 2U);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    CanPlatform_DelayMs(5U);

    for (i = 0U; i < MCPFD_OSC_TRIES; i++)
    {
        r = fd_read32(can, MCPFD_REG_OSC, &osc);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        if (((osc & MCPFD_OSC_OSCRDY) != 0U) && (osc != 0xFFFFFFFFUL))
        {
            break;
        }
        CanPlatform_DelayMs(1U);
    }
    if (i == MCPFD_OSC_TRIES)
    {
        return CAN_ERR_CHIP_NOT_FOUND;                  /* No oscillator: crystal, power, or wiring. */
    }

    r = fd_read32(can, MCPFD_REG_CON, &v);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_con = v;
    if (((v >> MCPFD_CON_OPMOD_SHIFT) & 0x07UL) != MCPFD_MODE_CONFIG)
    {
        return CAN_ERR_CHIP_NOT_FOUND;                  /* 0x00 / 0xFF here usually means wiring or no power. */
    }

    /* Clock: PLL only with a 4 MHz crystal, clock output / 10 like after reset. */
    want_osc = MCPFD_OSC_CLKODIV_10 | (use_pll ? MCPFD_OSC_PLLEN : 0U);
    r = fd_write32(can, MCPFD_REG_OSC, want_osc);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    for (i = 0U; i < MCPFD_OSC_TRIES; i++)
    {
        r = fd_read32(can, MCPFD_REG_OSC, &osc);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
        if (((osc & MCPFD_OSC_OSCRDY) != 0U) && (!use_pll || ((osc & MCPFD_OSC_PLLRDY) != 0U)))
        {
            break;
        }
        CanPlatform_DelayMs(1U);
    }
    if (i == MCPFD_OSC_TRIES)
    {
        return CAN_ERR_TIMEOUT;                         /* PLL did not lock. */
    }

    /* RAM: ECC on, then initialize the RAM. */
    r = fd_write32(can, MCPFD_REG_ECCCON, MCPFD_ECCCON_ECCEN);
    if (CAN_IS_ERR(r)) { return r; }
    r = fd_clear_ram(can);
    if (CAN_IS_ERR(r)) { return r; }

    /* Controller options (stay in configuration mode). */
    v = ((uint32_t)MCPFD_MODE_CONFIG << MCPFD_CON_REQOP_SHIFT) | MCPFD_CON_ISOCRCEN | MCPFD_CON_RTXAT;
    r = fd_write32(can, MCPFD_REG_CON, v);
    if (CAN_IS_ERR(r)) { return r; }

    /* Bit timing. Read back the nominal timing to prove the SPI link. */
    v = fd_timing_reg(&can->cfg.nominal);
    r = fd_write32(can, MCPFD_REG_NBTCFG, v);
    if (CAN_IS_ERR(r)) { return r; }
    {
        uint32_t back = 0U;

        r = fd_read32(can, MCPFD_REG_NBTCFG, &back);
        if (CAN_IS_ERR(r)) { return r; }
        if (back != v)
        {
            return CAN_ERR_SPI;
        }
    }
#if CAN_CFG_ENABLE_FD
    if (fd_on)
    {
        r = fd_write32(can, MCPFD_REG_DBTCFG, fd_timing_reg(&can->cfg.data));
        if (CAN_IS_ERR(r)) { return r; }

        /* Delay compensation from 2 Mbit/s: offset = prescaler * tseg1, at most 63. */
        if (can->cfg.data.actual_bitrate >= MCPFD_TDC_MIN_BITRATE)
        {
            uint32_t tdco = (uint32_t)can->cfg.data.prescaler * can->cfg.data.tseg1;

            if (tdco > 63U)
            {
                tdco = 63U;
            }
            r = fd_write32(can, MCPFD_REG_TDC, MCPFD_TDC_MODE_AUTO | MCPFD_TDC_EDGFLTEN | (tdco << 8));
        }
        else
        {
            r = fd_write32(can, MCPFD_REG_TDC, 0U);
        }
        if (CAN_IS_ERR(r)) { return r; }
    }
#endif

    /* FIFO 1 = transmit, FIFO 2 = receive. */
    fifo_common = (fd_on ? (7UL << MCPFD_FIFOCON_PLSIZE_POS) : 0UL);
    r = fd_config_fifo(can, MCPFD_REG_FIFOCON1,
                       MCPFD_FIFOCON_TXEN | (can->cfg.auto_retransmit ? MCPFD_FIFOCON_TXAT_UNL : 0UL) |
                       ((uint32_t)(ctx->tx_depth - 1U) << MCPFD_FIFOCON_FSIZE_POS) | fifo_common);
    if (CAN_IS_ERR(r)) { return r; }
    r = fd_config_fifo(can, MCPFD_REG_FIFOCON2,
                       ((uint32_t)(ctx->rx_depth - 1U) << MCPFD_FIFOCON_FSIZE_POS) | fifo_common);
    if (CAN_IS_ERR(r)) { return r; }

    /* Filter 0 accepts every frame into FIFO 2 (software filters decide). Disable it while editing. */
    r = fd_write8(can, MCPFD_REG_FLTCON0, 0x00U);
    if (CAN_IS_ERR(r)) { return r; }
    r = fd_write32(can, MCPFD_REG_FLTOBJ0, 0U);
    if (CAN_IS_ERR(r)) { return r; }
    r = fd_write32(can, MCPFD_REG_MASK0, 0U);
    if (CAN_IS_ERR(r)) { return r; }
    r = fd_write8(can, MCPFD_REG_FLTCON0, (uint8_t)(0x80U | 2U));      /* FLTEN | FIFO 2 */
    if (CAN_IS_ERR(r)) { return r; }

    switch (can->cfg.mode)
    {
        case CANLIB_MODE_LISTEN_ONLY: ctx->run_mode = (uint8_t)MCPFD_MODE_LISTEN;   break;
        case CANLIB_MODE_LOOPBACK:    ctx->run_mode = (uint8_t)MCPFD_MODE_LOOPBACK; break;
        default:                      ctx->run_mode = fd_on ? (uint8_t)MCPFD_MODE_NORMAL_FD
                                                            : (uint8_t)MCPFD_MODE_NORMAL_20; break;
    }

    ctx->inited = true;
    return CAN_OK;
}

/** Put the chip back in configuration mode and forget the context. */
static CanResult_e fd_deinit(CanLib_t *can)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);
    CanResult_e    r   = CAN_OK;

    if ((ctx != NULL) && ctx->inited)
    {
        r = fd_set_mode(can, (uint8_t)MCPFD_MODE_CONFIG);
        ctx->inited = false;
    }
    can->port_ctx = NULL;
    return r;
}

/** Join the bus. */
static CanResult_e fd_start(CanLib_t *can)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);

    if ((ctx == NULL) || !ctx->inited)
    {
        return CAN_ERR_NOT_INIT;
    }
    return fd_set_mode(can, ctx->run_mode);
}

/** Leave the bus. */
static CanResult_e fd_stop(CanLib_t *can)
{
    return fd_set_mode(can, (uint8_t)MCPFD_MODE_CONFIG);
}

/** Write one frame into the transmit FIFO and request the transmission. */
static CanResult_e fd_transmit(CanLib_t *can, const CanFrame_t *frame)
{
    CanMcpFdCtx_t     *ctx = fd_ctx(can);
    const CanHeader_t *h   = &frame->header;
    CanResult_e        r;
    uint32_t           sta = 0U;
    uint32_t           t0;
    uint32_t           t1;
    uint16_t           addr;
    uint8_t            idx;
    uint8_t            dlc = h->dlc;
    uint8_t            len;
    uint8_t            wlen;
    bool               is_fd = (h->format == CAN_FORMAT_FD);
    uint8_t            buf[MCPFD_OBJ_HEADER + 64U] = {0};

    if (is_fd && !ctx->fd_mode)
    {
        return CAN_ERR_NOT_SUPPORTED;                   /* CAN FD is not switched on for this instance. */
    }

    r = fd_read32(can, MCPFD_REG_FIFOSTA1, &sta);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_fifosta = sta;
    if ((sta & MCPFD_FIFOSTA_TFNRFNIF) == 0U)
    {
        return CAN_ERR_TX_FULL;
    }
    idx = (uint8_t)((sta >> MCPFD_FIFOSTA_CI_SHIFT) & MCPFD_FIFOSTA_CI_MASK);
    if (idx >= ctx->tx_depth)
    {
        return CAN_ERR_BACKEND_FAIL;                    /* The chip reports an index outside our FIFO. */
    }

    if (!is_fd && (dlc > CAN_CLASSIC_MAX_DLEN))
    {
        dlc = (uint8_t)CAN_CLASSIC_MAX_DLEN;            /* DLC 9..15 on a classic frame means 8 bytes. */
    }
    len = (h->frame_type == CAN_FRAME_REMOTE) ? 0U : CanLib_DlcToLen(dlc, h->format);
    if (len > ctx->payload)
    {
        len = ctx->payload;
    }
    wlen = (uint8_t)((len + 3U) & ~3U);                 /* The ECC wants whole 32 bit words. */

    fd_encode_header(h, is_fd, dlc, &t0, &t1);
    buf[0] = (uint8_t)(t0);       buf[1] = (uint8_t)(t0 >> 8);
    buf[2] = (uint8_t)(t0 >> 16); buf[3] = (uint8_t)(t0 >> 24);
    buf[4] = (uint8_t)(t1);       buf[5] = (uint8_t)(t1 >> 8);
    buf[6] = (uint8_t)(t1 >> 16); buf[7] = (uint8_t)(t1 >> 24);
    if (len > CAN_MAX_DATA_LEN)
    {
        len = (uint8_t)CAN_MAX_DATA_LEN;
    }
    memcpy(&buf[MCPFD_OBJ_HEADER], frame->body.data, len);

    addr = (uint16_t)(MCPFD_RAM_START + ctx->tx_base + ((uint16_t)idx * ctx->tx_obj_size));
    r = fd_write(can, addr, buf, (uint8_t)(MCPFD_OBJ_HEADER + wlen));
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    /* UINC and TXREQ are bits 8 and 9 = byte 1 of FIFOCON1. */
    return fd_write8(can, (uint16_t)(MCPFD_REG_FIFOCON1 + 1U),
                     (uint8_t)((MCPFD_FIFOCON_UINC | MCPFD_FIFOCON_TXREQ) >> 8));
}

/** Read one frame out of the receive FIFO, if any. */
static CanResult_e fd_receive(CanLib_t *can, CanFrame_t *frame)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);
    CanResult_e    r;
    uint32_t       sta = 0U;
    uint16_t       addr;
    uint8_t        idx;
    uint8_t        obj[MCPFD_OBJ_HEADER + 64U] = {0};

    r = fd_read32(can, MCPFD_REG_FIFOSTA2, &sta);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    ctx->last_fifosta = sta;
    if ((sta & MCPFD_FIFOSTA_TFNRFNIF) == 0U)
    {
        return CAN_ERR_RX_EMPTY;
    }
    idx = (uint8_t)((sta >> MCPFD_FIFOSTA_CI_SHIFT) & MCPFD_FIFOSTA_CI_MASK);
    if (idx >= ctx->rx_depth)
    {
        return CAN_ERR_BACKEND_FAIL;
    }

    addr = (uint16_t)(MCPFD_RAM_START + ctx->rx_base + ((uint16_t)idx * ctx->rx_obj_size));
    r = fd_read(can, addr, obj, ctx->rx_obj_size);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    fd_decode_frame(obj, frame, ctx->payload);

    /* Free the object: UINC is bit 8 = byte 1 of FIFOCON2. */
    return fd_write8(can, (uint16_t)(MCPFD_REG_FIFOCON2 + 1U), (uint8_t)(MCPFD_FIFOCON_UINC >> 8));
}

/** Read TEC, REC and the error flags into can->bus. */
static CanResult_e fd_get_errors(CanLib_t *can)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);
    CanResult_e    r;
    uint32_t       trec = 0U;
    uint32_t       sta  = 0U;
    uint8_t        flags;

    r = fd_read32(can, MCPFD_REG_TREC, &trec);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    r = fd_read32(can, MCPFD_REG_FIFOSTA2, &sta);
    if (CAN_IS_ERR(r))
    {
        return r;
    }

    flags                   = (uint8_t)(trec >> 16);
    can->bus.rec            = (uint8_t)(trec);
    can->bus.tec            = (uint8_t)(trec >> 8);
    can->bus.hw_error_flags = flags;
    ctx->last_trec          = trec;

    if ((sta & MCPFD_FIFOSTA_RXOVIF) != 0U)
    {
#if CAN_CFG_ENABLE_STATS
        can->stats.rx_dropped++;                        /* The chip lost a frame: count it, clear the flag. */
#endif
        r = fd_write8(can, MCPFD_REG_FIFOSTA2, 0x00U);
        if (CAN_IS_ERR(r))
        {
            return r;
        }
    }
    return ((flags & MCPFD_TREC_TXBO) != 0U) ? CAN_ERR_BUS_OFF : CAN_OK;
}

/** Leave bus-off: configuration mode resets the error state, then rejoin. */
static CanResult_e fd_recover(CanLib_t *can)
{
    CanMcpFdCtx_t *ctx = fd_ctx(can);
    CanResult_e    r;

    r = fd_set_mode(can, (uint8_t)MCPFD_MODE_CONFIG);
    if (CAN_IS_ERR(r))
    {
        return r;
    }
    return fd_set_mode(can, ctx->run_mode);
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
/** Set the transceiver mode pin. */
static CanResult_e fd_set_xcvr(CanLib_t *can, CanXcvrMode_e mode)
{
    return CanPort_SetXcvrPin(&can->hw, mode);
}
#endif

const CanMcpFdCtx_t *CanMcpFd_GetCtx(const CanLib_t *can)
{
    if ((can == NULL) || (can->port != &g_CanPortMcp251xfd) || (can->port_ctx == NULL))
    {
        return NULL;
    }
    return (const CanMcpFdCtx_t *)can->port_ctx;
}

/** Function table of the MCP2517FD / MCP2518FD backend. */
const CanPortOps_t g_CanPortMcp251xfd =
{
    "mcp251xfd",
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

#endif /* CAN_CFG_ENABLE_BACKEND_MCP251XFD */
