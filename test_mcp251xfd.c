/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_mcp251xfd.c
 * @brief   Host-side tests of the MCP2517FD / MCP2518FD backend against a simulated chip (not part of the library).
 *
 * The simulator models the SPI protocol, the register fields the library uses, the message RAM with its
 * FIFO geometry (FIFO1 then FIFO2 from RAM start), the self-clearing bits, the 32 bit ECC access rule and
 * the FIFO indexes. Build with CAN_CFG_PLATFORM = GENERIC, MCP251XFD on, MCP25XX / bxCAN off, CAN_CFG_ENABLE_FD 1.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"

#if !CAN_CFG_ENABLE_BACKEND_MCP251XFD || !CAN_CFG_ENABLE_FD
    #error "test_mcp251xfd.c needs the MCP251XFD backend and CAN_CFG_ENABLE_FD 1"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ======================================================================== */
/* SIMULATED CHIP                                                           */
/* ======================================================================== */

typedef struct
{
    uint32_t id;
    bool     ext, rtr, fd, brs, esi;
    uint8_t  dlc;
    uint8_t  data[64];
    uint8_t  index;         /* TX FIFO index the library used. */
} SimTx_t;

static struct
{
    uint8_t  mem[0x1000];
    bool     absent, spi_fail, mode_fail, tx_full, ovr;
    uint8_t  opmod;
    uint8_t  tx_depth, rx_depth, tx_obj, rx_obj;
    uint8_t  tx_head, rx_tail, rx_count;
    uint32_t tec, rec, trec_flags;
    int      ecc_violations;
    int      resets, mode_changes, spi_calls;
    SimTx_t  sent[32];
    int      sent_count;
    uint8_t  last_reqop;
} sim;

static uint32_t get32(uint16_t a) { return (uint32_t)sim.mem[a] | ((uint32_t)sim.mem[a + 1] << 8) | ((uint32_t)sim.mem[a + 2] << 16) | ((uint32_t)sim.mem[a + 3] << 24); }
static void     put32(uint16_t a, uint32_t v) { sim.mem[a] = (uint8_t)v; sim.mem[a + 1] = (uint8_t)(v >> 8); sim.mem[a + 2] = (uint8_t)(v >> 16); sim.mem[a + 3] = (uint8_t)(v >> 24); }

static void sim_geometry(void)
{
    uint32_t c1 = get32(MCPFD_REG_FIFOCON1);
    uint32_t c2 = get32(MCPFD_REG_FIFOCON2);
    sim.tx_depth = (uint8_t)(((c1 >> 24) & 0x1FU) + 1U);
    sim.rx_depth = (uint8_t)(((c2 >> 24) & 0x1FU) + 1U);
    sim.tx_obj   = (uint8_t)(8U + ((((c1 >> 29) & 7U) == 7U) ? 64U : 8U));
    sim.rx_obj   = (uint8_t)(8U + ((((c2 >> 29) & 7U) == 7U) ? 64U : 8U));
}

static void sim_reset(void)
{
    bool absent = sim.absent, spi_fail = sim.spi_fail, mode_fail = sim.mode_fail;
    int  resets = sim.resets;
    memset(&sim, 0, sizeof(sim));
    sim.absent = absent; sim.spi_fail = spi_fail; sim.mode_fail = mode_fail;
    sim.resets = resets;
    sim.opmod = MCPFD_MODE_CONFIG;
    put32(MCPFD_REG_OSC, MCPFD_OSC_OSCRDY | MCPFD_OSC_CLKODIV_10);
}

static void sim_full_reset(void) { memset(&sim, 0, sizeof(sim)); sim_reset(); }

static void sim_sync_read(void)
{
    uint32_t con = get32(MCPFD_REG_CON);
    con = (con & ~(7UL << 21)) | ((uint32_t)sim.opmod << 21);
    put32(MCPFD_REG_CON, con);
    put32(MCPFD_REG_FIFOSTA1, (sim.tx_full ? 0U : 1U) | ((uint32_t)sim.tx_head << 8));
    put32(MCPFD_REG_FIFOSTA2, (sim.rx_count > 0U ? 1U : 0U) | ((uint32_t)sim.rx_tail << 8) | (sim.ovr ? 8U : 0U));
    put32(MCPFD_REG_TREC, (sim.rec & 0xFFU) | ((sim.tec & 0xFFU) << 8) | (sim.trec_flags << 16));
}

static void sim_capture_tx(void)
{
    uint16_t   a   = (uint16_t)(0x400U + (uint16_t)sim.tx_head * sim.tx_obj);
    uint32_t   t0  = get32(a);
    uint32_t   t1  = get32((uint16_t)(a + 4U));
    SimTx_t   *s   = &sim.sent[sim.sent_count++];
    static const uint8_t len_of[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

    memset(s, 0, sizeof(*s));
    s->ext = (t1 >> 4) & 1U; s->rtr = (t1 >> 5) & 1U; s->brs = (t1 >> 6) & 1U; s->fd = (t1 >> 7) & 1U; s->esi = (t1 >> 8) & 1U;
    s->dlc = (uint8_t)(t1 & 15U);
    s->id  = s->ext ? (((t0 & 0x7FFU) << 18) | ((t0 >> 11) & 0x3FFFFU)) : (t0 & 0x7FFU);
    memcpy(s->data, &sim.mem[a + 8U], len_of[s->dlc]);
    s->index = sim.tx_head;
    sim.tx_head = (uint8_t)((sim.tx_head + 1U) % sim.tx_depth);
}

CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    uint8_t  cmd  = (uint8_t)(tx[0] >> 4);
    uint16_t addr = (uint16_t)(((tx[0] & 0x0FU) << 8) | tx[1]);
    uint16_t i;
    (void)hw;
    sim.spi_calls++;

    if (sim.spi_fail) { return CAN_ERR_SPI; }
    memset(rx, 0, len);
    if (sim.absent) { memset(rx, 0xFF, len); return CAN_OK; }

    if (cmd == MCPFD_CMD_RESET) { sim_reset(); sim.resets++; return CAN_OK; }

    if (cmd == MCPFD_CMD_READ)
    {
        sim_sync_read();
        for (i = 2U; i < len; i++) { rx[i] = sim.mem[addr + (i - 2U)]; }
        return CAN_OK;
    }
    if (cmd == MCPFD_CMD_WRITE)
    {
        uint16_t n = (uint16_t)(len - 2U);

        if ((addr >= 0x400U) && (addr < 0xC00U) && ((get32(MCPFD_REG_ECCCON) & 1U) != 0U))
        {
            if (((addr & 3U) != 0U) || ((n & 3U) != 0U)) { sim.ecc_violations++; }
        }
        if (addr + n > sizeof(sim.mem)) { return CAN_ERR_SPI; }

        if ((n == 1U) && (addr == (MCPFD_REG_FIFOCON1 + 1U)) && ((tx[2] & 3U) == 3U)) { sim_capture_tx(); return CAN_OK; }
        if ((n == 1U) && (addr == (MCPFD_REG_FIFOCON2 + 1U)) && ((tx[2] & 1U) != 0U))
        {
            if (sim.rx_count > 0U) { sim.rx_tail = (uint8_t)((sim.rx_tail + 1U) % sim.rx_depth); sim.rx_count--; }
            return CAN_OK;
        }
        if ((n == 1U) && (addr == MCPFD_REG_FIFOSTA2)) { sim.ovr = false; return CAN_OK; }
        if ((n == 1U) && (addr == (MCPFD_REG_CON + 3U)))
        {
            sim.mem[addr] = tx[2];
            sim.last_reqop = (uint8_t)(tx[2] & 7U);
            sim.mode_changes++;
            if (sim.last_reqop == MCPFD_MODE_CONFIG) { sim.trec_flags &= ~(uint32_t)MCPFD_TREC_TXBO; sim.tec = 0U; }   /* Config mode clears bus-off. */
            if (!(sim.mode_fail && (sim.last_reqop != MCPFD_MODE_CONFIG || sim.opmod != MCPFD_MODE_CONFIG))) { sim.opmod = sim.last_reqop; }
            if (sim.mode_fail && sim.last_reqop != MCPFD_MODE_CONFIG) { /* stays in the old mode */ }
            return CAN_OK;
        }

        for (i = 0U; i < n; i++) { sim.mem[addr + i] = tx[2U + i]; }
        if (addr == MCPFD_REG_OSC)
        {
            uint32_t v = get32(MCPFD_REG_OSC) | MCPFD_OSC_OSCRDY;
            if ((v & MCPFD_OSC_PLLEN) != 0U) { v |= MCPFD_OSC_PLLRDY; }
            put32(MCPFD_REG_OSC, v);
        }
        if ((addr == MCPFD_REG_FIFOCON1) || (addr == MCPFD_REG_FIFOCON2))
        {
            put32(addr, get32(addr) & ~MCPFD_FIFOCON_FRESET);
            if (addr == MCPFD_REG_FIFOCON1) { sim.tx_head = 0U; } else { sim.rx_tail = 0U; sim.rx_count = 0U; }
            sim_geometry();
        }
        return CAN_OK;
    }
    return CAN_ERR_SPI;
}

static void sim_push_rx(uint32_t id, bool ext, bool rtr, bool fd, bool brs, bool esi, uint8_t dlc, const uint8_t *data, uint8_t n)
{
    uint16_t a  = (uint16_t)(0x400U + (uint16_t)sim.tx_depth * sim.tx_obj + (uint16_t)((sim.rx_tail + sim.rx_count) % sim.rx_depth) * sim.rx_obj);
    uint32_t r0 = ext ? (((id >> 18) & 0x7FFU) | ((id & 0x3FFFFU) << 11)) : (id & 0x7FFU);
    uint32_t r1 = dlc | ((uint32_t)ext << 4) | ((uint32_t)rtr << 5) | ((uint32_t)brs << 6) | ((uint32_t)fd << 7) | ((uint32_t)esi << 8);
    put32(a, r0); put32((uint16_t)(a + 4U), r1);
    if (n > 0U) { memcpy(&sim.mem[a + 8U], data, n); }
    sim.rx_count++;
}

void CanPlatform_DelayMs(uint32_t ms) { (void)ms; }
static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void) { return g_tick; }

static int  pin_calls = 0;
static bool pin_level = false;
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{ (void)port; (void)pin; pin_calls++; pin_level = level; return CAN_OK; }

/* ======================================================================== */
/* TESTS                                                                    */
/* ======================================================================== */

static CanLib_t can1;
static int      g_dummy_spi;

static CanResult_e bring_up(CanMode_e mode, bool fd, uint32_t data_bitrate, uint32_t osc)
{
    sim_full_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    can1.cfg.mode         = mode;
    can1.cfg.fd_enabled   = fd;
    can1.cfg.data.bitrate = data_bitrate;
    can1.hw.handle        = &g_dummy_spi;
    can1.hw.cs_port       = &g_dummy_spi;
    can1.hw.cs_pin        = 4U;
    can1.hw.osc_hz        = osc;
    can1.debug.log_level  = CAN_LOG_DEBUG;
    return CanLib_Init(&can1);
}

static void start_ok(CanMode_e mode, bool fd)
{
    EXPECT(bring_up(mode, fd, 2000000UL, 40000000UL) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_RUNNING);
}

static void test_defaults(void)
{
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    EXPECT(can1.cfg.mcp_variant == CAN_MCP_2518FD);
    EXPECT(can1.hw.osc_hz == 40000000UL);
    EXPECT(CanPort_Get(CAN_BACKEND_MCP251XFD) == &g_CanPortMcp251xfd);
}

static void test_timing(void)
{
    CanTiming_t t;

    EXPECT(CanMcpFd_CalcTiming(40000000UL, 500000UL, 875U, false, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 500000UL);
    EXPECT(((uint32_t)t.prescaler * (1U + t.tseg1 + t.tseg2) * 500000UL) == 40000000UL);
    EXPECT(((1U + t.tseg1) * 1000U) / (1U + t.tseg1 + t.tseg2) >= 860U);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 1000000UL, 875U, false, &t) == CAN_OK && t.actual_bitrate == 1000000UL);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 125000UL, 875U, false, &t) == CAN_OK && t.actual_bitrate == 125000UL);
    EXPECT(CanMcpFd_CalcTiming(20000000UL, 500000UL, 875U, false, &t) == CAN_OK && t.actual_bitrate == 500000UL);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 20000UL, 875U, false, &t) == CAN_OK && t.actual_bitrate == 20000UL);

    EXPECT(CanMcpFd_CalcTiming(40000000UL, 2000000UL, 750U, true, &t) == CAN_OK && t.actual_bitrate == 2000000UL);
    EXPECT(t.tseg1 <= MCPFD_DAT_SEG1_MAX && t.tseg2 <= MCPFD_DAT_SEG2_MAX);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 5000000UL, 750U, true, &t) == CAN_OK && t.actual_bitrate == 5000000UL);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 8000000UL, 700U, true, &t) == CAN_OK && t.actual_bitrate == 8000000UL);

    EXPECT(CanMcpFd_CalcTiming(40000000UL, 7U, 875U, false, &t) == CAN_ERR_BITRATE);
    EXPECT(CanMcpFd_CalcTiming(0U, 500000UL, 875U, false, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 500000UL, 1000U, false, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanMcpFd_CalcTiming(40000000UL, 500000UL, 875U, false, NULL) == CAN_ERR_NULL_PTR);
}

static uint32_t timing_reg(const CanTiming_t *t)
{
    return ((uint32_t)(t->prescaler - 1U) << 24) | ((uint32_t)(t->tseg1 - 1U) << 16) | ((uint32_t)(t->tseg2 - 1U) << 8) | (uint32_t)(t->sjw - 1U);
}

static void test_init(void)
{
    uint16_t a;
    bool     ram_zero = true;

    /* Classic CAN. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL, 40000000UL) == CAN_OK);
    EXPECT(sim.resets == 1);
    EXPECT(get32(MCPFD_REG_NBTCFG) == timing_reg(&can1.cfg.nominal));
    EXPECT(can1.cfg.nominal.actual_bitrate == 500000UL);
    EXPECT((get32(MCPFD_REG_OSC) & MCPFD_OSC_PLLEN) == 0U);
    EXPECT((get32(MCPFD_REG_ECCCON) & 1U) == 1U);
    for (a = 0x400U; a < 0xC00U; a++) { if (sim.mem[a] != 0U) { ram_zero = false; } }
    EXPECT(ram_zero);
    EXPECT(sim.ecc_violations == 0);
    EXPECT((get32(MCPFD_REG_CON) & MCPFD_CON_ISOCRCEN) != 0U && (get32(MCPFD_REG_CON) & MCPFD_CON_RTXAT) != 0U);
    EXPECT(sim.opmod == MCPFD_MODE_CONFIG);
    EXPECT((get32(MCPFD_REG_FIFOCON1) & MCPFD_FIFOCON_TXEN) != 0U);
    EXPECT(((get32(MCPFD_REG_FIFOCON1) >> 24) & 0x1FU) == 7U && ((get32(MCPFD_REG_FIFOCON1) >> 29) & 7U) == 0U);
    EXPECT((get32(MCPFD_REG_FIFOCON1) & MCPFD_FIFOCON_TXAT_UNL) == MCPFD_FIFOCON_TXAT_UNL);
    EXPECT((get32(MCPFD_REG_FIFOCON2) & MCPFD_FIFOCON_TXEN) == 0U);
    EXPECT(((get32(MCPFD_REG_FIFOCON2) >> 24) & 0x1FU) == 31U);
    EXPECT(sim.mem[MCPFD_REG_FLTCON0] == 0x82U && get32(MCPFD_REG_FLTOBJ0) == 0U && get32(MCPFD_REG_MASK0) == 0U);
    EXPECT(CanMcpFd_GetCtx(&can1) != NULL && CanMcpFd_GetCtx(&can1)->payload == 8U && !CanMcpFd_GetCtx(&can1)->fd_mode);
    EXPECT(CanMcpFd_GetCtx(&can1)->sysclk_hz == 40000000UL);
    EXPECT(CanMcpFd_GetCtx(&can1)->rx_base == 8U * 16U);

    /* CAN FD at 2 Mbit/s: data timing, TDC, 64-byte payload. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, true, 2000000UL, 40000000UL) == CAN_OK);
    EXPECT(get32(MCPFD_REG_DBTCFG) == timing_reg(&can1.cfg.data));
    EXPECT(can1.cfg.data.actual_bitrate == 2000000UL);
    EXPECT((get32(MCPFD_REG_TDC) & MCPFD_TDC_MODE_AUTO) == MCPFD_TDC_MODE_AUTO);
    {
        uint32_t tdco = (uint32_t)can1.cfg.data.prescaler * can1.cfg.data.tseg1;
        if (tdco > 63U) { tdco = 63U; }
        EXPECT(((get32(MCPFD_REG_TDC) >> 8) & 0x7FU) == tdco);
    }
    EXPECT(((get32(MCPFD_REG_FIFOCON1) >> 29) & 7U) == 7U && ((get32(MCPFD_REG_FIFOCON2) >> 24) & 0x1FU) == 15U);
    EXPECT(CanMcpFd_GetCtx(&can1)->payload == 64U && CanMcpFd_GetCtx(&can1)->fd_mode);
    EXPECT(CanMcpFd_GetCtx(&can1)->rx_base == 8U * 72U);
    EXPECT(sim.ecc_violations == 0);

    /* 1 Mbit/s data: no delay compensation. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, true, 1000000UL, 40000000UL) == CAN_OK);
    EXPECT(get32(MCPFD_REG_TDC) == 0U);

    /* Crystals. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL, 4000000UL) == CAN_OK);
    EXPECT((get32(MCPFD_REG_OSC) & MCPFD_OSC_PLLEN) != 0U && CanMcpFd_GetCtx(&can1)->sysclk_hz == 40000000UL);
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL, 20000000UL) == CAN_OK);
    EXPECT(CanMcpFd_GetCtx(&can1)->sysclk_hz == 20000000UL && can1.cfg.nominal.actual_bitrate == 500000UL);
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL, 16000000UL) == CAN_ERR_BAD_PARAM);

    /* Variant, bitrate, chip errors. */
    sim_full_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    can1.cfg.mcp_variant = CAN_MCP_2515;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_NOT_SUPPORTED);

    sim_full_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    can1.cfg.nominal.bitrate = 7U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BITRATE);

    sim_full_reset(); sim.absent = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_CHIP_NOT_FOUND);

    sim_full_reset(); sim.spi_fail = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_SPI);
}

static void test_modes(void)
{
    start_ok(CANLIB_MODE_NORMAL, false);
    EXPECT(sim.opmod == MCPFD_MODE_NORMAL_20);
    EXPECT(CanLib_Stop(&can1) == CAN_OK && sim.opmod == MCPFD_MODE_CONFIG);

    start_ok(CANLIB_MODE_NORMAL, true);
    EXPECT(sim.opmod == MCPFD_MODE_NORMAL_FD);
    start_ok(CANLIB_MODE_LISTEN_ONLY, false);
    EXPECT(sim.opmod == MCPFD_MODE_LISTEN);
    start_ok(CANLIB_MODE_LOOPBACK, false);
    EXPECT(sim.opmod == MCPFD_MODE_LOOPBACK);

    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL, 40000000UL) == CAN_OK);
    sim.mode_fail = true;
    EXPECT(CanLib_Start(&can1) == CAN_ERR_TIMEOUT);

    start_ok(CANLIB_MODE_NORMAL, false);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK && can1.port_ctx == NULL);
}

static void test_transmit(void)
{
    uint8_t    d[64];
    CanFrame_t f;
    uint8_t    i;
    int        n;

    for (i = 0U; i < 64U; i++) { d[i] = (uint8_t)(i + 1U); }
    start_ok(CANLIB_MODE_NORMAL, true);

    EXPECT(CanLib_Send(&can1, 0x123U, d, 3U) == CAN_OK);
    EXPECT(sim.sent_count == 1);
    EXPECT(sim.sent[0].id == 0x123U && !sim.sent[0].ext && !sim.sent[0].fd && sim.sent[0].dlc == 3U);
    EXPECT(sim.sent[0].data[0] == 1U && sim.sent[0].data[2] == 3U);

    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF0UL, d, 8U) == CAN_OK);
    EXPECT(sim.sent[1].ext && sim.sent[1].id == 0x1ABCDEF0UL && sim.sent[1].dlc == 8U && sim.sent[1].data[7] == 8U);

    EXPECT(CanLib_SendRemote(&can1, 0x55U, CAN_ID_STANDARD, 2U) == CAN_OK);
    EXPECT(sim.sent[2].rtr && sim.sent[2].dlc == 2U && sim.sent[2].id == 0x55U);

    EXPECT(CanLib_SendFd(&can1, 0x200U, d, 20U, true) == CAN_OK);
    EXPECT(sim.sent[3].fd && sim.sent[3].brs && sim.sent[3].dlc == 11U && sim.sent[3].data[19] == 20U);
    EXPECT(CanLib_SendFd(&can1, 0x201U, d, 64U, false) == CAN_OK);
    EXPECT(sim.sent[4].fd && !sim.sent[4].brs && sim.sent[4].dlc == 15U && sim.sent[4].data[63] == 64U);
    EXPECT(CanLib_SendFd(&can1, 0x202U, d, 13U, true) == CAN_OK);
    EXPECT(sim.sent[5].dlc == 10U && sim.sent[5].data[12] == 13U && sim.sent[5].data[13] == 0U);
    EXPECT(sim.ecc_violations == 0);

    /* The FIFO index follows the chip and wraps (8 objects). */
    for (n = 0; n < 12; n++) { EXPECT(CanLib_Send(&can1, 0x300U + (uint32_t)n, d, 1U) == CAN_OK); }
    EXPECT(sim.sent_count == 18);
    EXPECT(sim.sent[17].index == (uint8_t)(17 % 8) && sim.sent[17].id == 0x30BU);
    EXPECT(sim.ecc_violations == 0);

    /* TX FIFO full: backend reports it, core queues, Process sends later. */
    sim.tx_full = true;
    memset(&f, 0, sizeof(f));
    EXPECT(g_CanPortMcp251xfd.transmit(&can1, &f) == CAN_ERR_TX_FULL);
    EXPECT(!CAN_IS_ERR(CanLib_Send(&can1, 0x400U, d, 1U)));
    EXPECT(can1.tx.queue.count == 1U);
    sim.tx_full = false;
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(can1.tx.queue.count == 0U && sim.sent[sim.sent_count - 1].id == 0x400U);

    /* FD frame on an instance without FD. */
    start_ok(CANLIB_MODE_NORMAL, false);
    memset(&f, 0, sizeof(f));
    f.header.format = CAN_FORMAT_FD;
    EXPECT(g_CanPortMcp251xfd.transmit(&can1, &f) == CAN_ERR_NOT_SUPPORTED);
    EXPECT(CanLib_SendFd(&can1, 0x500U, d, 8U, true) == CAN_WARN_FD_FALLBACK);
    EXPECT(!sim.sent[sim.sent_count - 1].fd && sim.sent[sim.sent_count - 1].id == 0x500U);
    EXPECT(sim.ecc_violations == 0);
}

static void test_receive(void)
{
    uint8_t    d[64];
    CanFrame_t f;
    uint8_t    i;
    int        n;

    for (i = 0U; i < 64U; i++) { d[i] = (uint8_t)(0x80U + i); }
    start_ok(CANLIB_MODE_NORMAL, true);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_ERR_RX_EMPTY);

    g_tick = 4242U;
    sim_push_rx(0x7FFU, false, false, false, false, false, 8U, d, 8U);
    sim_push_rx(0x1234567UL, true, false, false, false, false, 4U, d, 4U);
    sim_push_rx(0x10U, false, true, false, false, false, 3U, NULL, 0U);
    sim_push_rx(0x555U, false, false, true, true, true, 15U, d, 64U);
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(CanLib_Available(&can1) == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x7FFU && f.header.format == CAN_FORMAT_CLASSIC && f.header.data_len == 8U);
    EXPECT(memcmp(f.body.data, d, 8U) == 0 && f.header.timestamp_ms == 4242U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1234567UL && f.header.id_type == CAN_ID_EXTENDED && f.header.data_len == 4U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE && f.header.dlc == 3U && f.header.data_len == 0U && f.header.id == 0x10U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.format == CAN_FORMAT_FD && f.header.bitrate_switch && f.header.error_state);
    EXPECT(f.header.dlc == 15U && f.header.data_len == 64U && memcmp(f.body.data, d, 64U) == 0);
    EXPECT(sim.rx_count == 0U);

    /* More frames than RX objects over time: the FIFO index wraps (16 objects). */
    for (n = 0; n < 40; n++)
    {
        sim_push_rx(0x100U + (uint32_t)n, false, false, false, false, false, 2U, d, 2U);
        EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
        EXPECT(CanLib_Receive(&can1, &f) == CAN_OK && f.header.id == 0x100U + (uint32_t)n);
    }
}

static void test_bus_state(void)
{
    uint8_t data[1] = {1};
    int     modes;

    start_ok(CANLIB_MODE_NORMAL, false);

    sim.tec = 100U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.tec == 100U && can1.status.bus_state == CAN_BUS_ERROR_WARNING);

    sim.rec = 130U; sim.trec_flags = MCPFD_TREC_RXBP;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.rec == 130U && can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);
    EXPECT((can1.bus.hw_error_flags & MCPFD_TREC_RXBP) != 0U);

    sim.ovr = true;
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 1U && !sim.ovr);
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 1U);

    g_tick = 3000U;
    sim.trec_flags = MCPFD_TREC_TXBO; sim.tec = 255U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BUS_OFF);

    modes = sim.mode_changes;
    sim.rec = 0U;
    g_tick = 3000U + can1.cfg.bus_off_recover_ms + 5U;
    (void)CanLib_Process(&can1);
    EXPECT(sim.mode_changes == modes + 2);                    /* Config mode, then back to the run mode. */
    EXPECT(sim.opmod == MCPFD_MODE_NORMAL_20);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
}

static void test_xcvr(void)
{
    int dummy = 0;

    start_ok(CANLIB_MODE_NORMAL, false);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_ERR_NOT_SUPPORTED);
    can1.hw.xcvr_port = &dummy; can1.hw.xcvr_pin = 7U; can1.hw.xcvr_standby_level = true;
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_OK && pin_level == true);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL) == CAN_OK && pin_level == false);
    EXPECT(pin_calls == 2);
}

int main(void)
{
    test_defaults();
    test_timing();
    test_init();
    test_modes();
    test_transmit();
    test_receive();
    test_bus_state();
    test_xcvr();

    if (fails == 0) { printf("test_mcp251xfd: ALL PASSED\n"); return 0; }
    printf("test_mcp251xfd: %d FAILED\n", fails);
    return 1;
}
