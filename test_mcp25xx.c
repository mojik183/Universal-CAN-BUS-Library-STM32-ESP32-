/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_mcp25xx.c
 * @brief   Host-side tests of the MCP2515 backend against a simulated chip (not part of the library).
 *
 * The simulator below implements the SPI instructions and registers the driver
 * uses, following the MCP2515 datasheet. Build with CAN_CFG_PLATFORM = GENERIC
 * and the MCP25XX backend enabled.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"

#if !CAN_CFG_ENABLE_BACKEND_MCP25XX
    #error "test_mcp25xx.c needs CAN_CFG_ENABLE_BACKEND_MCP25XX 1"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ======================================================================== */
/* SIMULATED MCP2515                                                        */
/* ======================================================================== */

/** State of the fake chip. */
typedef struct
{
    uint8_t  reg[128];          /* Register file. */
    uint8_t  txbuf[3][13];      /* Transmit buffers: SIDH SIDL EID8 EID0 DLC D0..D7. */
    bool     txreq[3];          /* TXREQ per buffer. */
    uint8_t  rxbuf[2][13];      /* Receive buffers. */
    bool     rxif[2];           /* RXnIF per buffer. */
    uint8_t  sent[8][13];       /* Frames put on the bus (normal mode). */
    int      sent_count;
    bool     dead;              /* true = no chip: every byte reads 0x00. */
    bool     fail_spi;          /* true = the SPI driver reports an error. */
    int      mode_delay;        /* CANSTAT reads that still show the old mode. */
    int      mode_delay_left;
    uint8_t  pending_mode;
    int      xfer_count;
} Sim_t;

static Sim_t sim;

static void sim_reset(void)
{
    memset(sim.reg, 0, sizeof(sim.reg));
    sim.reg[MCP_REG_CANSTAT] = 0x80U;
    sim.reg[MCP_REG_CANCTRL] = 0x87U;
    memset(sim.txreq, 0, sizeof(sim.txreq));
    memset(sim.rxif, 0, sizeof(sim.rxif));
}

static void sim_power_up(void)
{
    memset(&sim, 0, sizeof(sim));
    sim_reset();
}

static bool sim_in_config(void)
{
    return (sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK) == MCP_MODE_CONFIG;
}

/** Writing a register: some registers only accept writes in configuration mode. */
static void sim_write(uint8_t addr, uint8_t v)
{
    if (addr == MCP_REG_CANCTRL)
    {
        sim.reg[addr] = v;
        if (sim.mode_delay > 0)
        {
            sim.pending_mode    = (uint8_t)(v & MCP_MODE_MASK);
            sim.mode_delay_left = sim.mode_delay;
        }
        else
        {
            sim.reg[MCP_REG_CANSTAT] = (uint8_t)(v & MCP_MODE_MASK);
        }
        if ((v & MCP_MODE_MASK) == MCP_MODE_CONFIG)
        {
            sim.reg[MCP_REG_TEC]  = 0U;                 /* Configuration mode clears the counters. */
            sim.reg[MCP_REG_REC]  = 0U;
            sim.reg[MCP_REG_EFLG] &= (uint8_t)~MCP_EFLG_TXBO;
        }
        return;
    }
    if ((addr >= MCP_REG_CNF3) && (addr <= MCP_REG_CNF1) && !sim_in_config())
    {
        return;                                         /* CNFx are locked outside configuration mode. */
    }
    sim.reg[addr] = v;
}

/** Put a frame in a receive buffer as the chip would. Returns false on overflow. */
static bool sim_rx_frame(uint32_t id, bool ext, bool rtr, uint8_t dlc, const uint8_t *data)
{
    uint8_t b[13] = {0};
    int     n;

    if (ext)
    {
        b[0] = (uint8_t)(id >> 21);
        b[1] = (uint8_t)((((id >> 18) & 7U) << 5) | 0x08U | ((id >> 16) & 3U));
        b[2] = (uint8_t)(id >> 8);
        b[3] = (uint8_t)id;
        b[4] = (uint8_t)(dlc | (rtr ? 0x40U : 0U));
    }
    else
    {
        b[0] = (uint8_t)(id >> 3);
        b[1] = (uint8_t)(((id & 7U) << 5) | (rtr ? 0x10U : 0U));
        b[4] = dlc;
    }
    if ((data != NULL) && !rtr)
    {
        memcpy(&b[5], data, (dlc > 8U) ? 8U : dlc);
    }

    n = !sim.rxif[0] ? 0 : (!sim.rxif[1] ? 1 : -1);
    if (n < 0)
    {
        sim.reg[MCP_REG_EFLG] |= MCP_EFLG_RX0OVR;
        return false;
    }
    memcpy(sim.rxbuf[n], b, 13);
    sim.rxif[n] = true;
    return true;
}

/** Finish a pending transmission: the frame goes on the bus, or loops back to RX. */
static void sim_complete_tx(int n)
{
    uint8_t mode = (uint8_t)(sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK);

    if (!sim.txreq[n])
    {
        return;
    }
    sim.txreq[n] = false;
    if (mode == MCP_MODE_LOOPBACK)
    {
        int r = !sim.rxif[0] ? 0 : (!sim.rxif[1] ? 1 : -1);
        if (r >= 0)
        {
            memcpy(sim.rxbuf[r], sim.txbuf[n], 13);
            sim.rxif[r] = true;
        }
    }
    else if (sim.sent_count < 8)
    {
        memcpy(sim.sent[sim.sent_count++], sim.txbuf[n], 13);
    }
}

/** Platform hook: the chip answers SPI instructions here. */
CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    uint16_t i;
    uint8_t  cmd;

    (void)hw;
    sim.xfer_count++;
    memset(rx, 0, len);
    if (sim.fail_spi)
    {
        return CAN_ERR_SPI;
    }
    if (sim.dead)
    {
        return CAN_OK;                                  /* Nothing answers: all zero. */
    }
    cmd = tx[0];

    if (cmd == MCP_CMD_RESET)
    {
        sim_reset();
    }
    else if (cmd == MCP_CMD_READ)
    {
        uint8_t addr = tx[1];
        for (i = 2U; i < len; i++)
        {
            uint8_t a = (uint8_t)(addr + (i - 2U));
            if ((a == MCP_REG_CANSTAT) && (sim.mode_delay_left > 0))
            {
                sim.mode_delay_left--;
                if (sim.mode_delay_left == 0)
                {
                    sim.reg[MCP_REG_CANSTAT] = sim.pending_mode;
                }
            }
            rx[i] = sim.reg[a];
        }
    }
    else if (cmd == MCP_CMD_WRITE)
    {
        for (i = 2U; i < len; i++)
        {
            sim_write((uint8_t)(tx[1] + (i - 2U)), tx[i]);
        }
    }
    else if (cmd == MCP_CMD_BIT_MODIFY)
    {
        sim.reg[tx[1]] = (uint8_t)((sim.reg[tx[1]] & ~tx[2]) | (tx[3] & tx[2]));
    }
    else if (cmd == MCP_CMD_READ_STATUS)
    {
        uint8_t s = 0U;
        s |= sim.rxif[0] ? MCP_STATUS_RX0IF : 0U;
        s |= sim.rxif[1] ? MCP_STATUS_RX1IF : 0U;
        s |= sim.txreq[0] ? MCP_STATUS_TX0REQ : 0U;
        s |= sim.txreq[1] ? MCP_STATUS_TX1REQ : 0U;
        s |= sim.txreq[2] ? MCP_STATUS_TX2REQ : 0U;
        rx[1] = s;
    }
    else if ((cmd >= 0x40U) && (cmd <= 0x45U) && ((cmd & 1U) == 0U))
    {
        int n = (cmd - 0x40U) / 2;
        memset(sim.txbuf[n], 0, 13);
        memcpy(sim.txbuf[n], &tx[1], (size_t)len - 1U);
    }
    else if ((cmd & 0xF8U) == MCP_CMD_RTS_BASE)
    {
        int n;
        for (n = 0; n < 3; n++)
        {
            if ((cmd & (1U << n)) != 0U)
            {
                sim.txreq[n] = true;
            }
        }
    }
    else if ((cmd == 0x90U) || (cmd == 0x94U))
    {
        int n = (cmd == 0x90U) ? 0 : 1;
        memcpy(&rx[1], sim.rxbuf[n], (size_t)len - 1U);
        sim.rxif[n] = false;                            /* Reading from SIDH frees the buffer. */
    }
    return CAN_OK;
}

/* ---- other platform hooks ---- */

static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void)
{
    return g_tick;
}

static int  pin_calls = 0;
static bool pin_last_level = false;
static uint16_t pin_last_pin = 0U;
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{
    (void)port;
    pin_calls++;
    pin_last_level = level;
    pin_last_pin   = pin;
    return CAN_OK;
}

/* ======================================================================== */
/* TEST HELPERS                                                             */
/* ======================================================================== */

static CanLib_t can1;

/** Fresh chip and instance, initialized (not started). */
static CanResult_e bring_up(CanMode_e mode)
{
    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.cfg.mode        = mode;
    can1.hw.osc_hz       = 16000000UL;
    can1.debug.log_level = CAN_LOG_DEBUG;
    return CanLib_Init(&can1);
}

static void start_ok(CanMode_e mode)
{
    EXPECT(bring_up(mode) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_RUNNING);
}

/* ======================================================================== */
/* TESTS                                                                    */
/* ======================================================================== */

static void test_timing(void)
{
    CanTiming_t t;

    EXPECT(CanMcp_CalcTiming(16000000UL, 500000UL, 875U, &t) == CAN_OK);
    EXPECT(t.prescaler == 1U);
    EXPECT(t.tseg1 == 13U);
    EXPECT(t.tseg2 == 2U);
    EXPECT(t.actual_bitrate == 500000UL);

    EXPECT(CanMcp_CalcTiming(8000000UL, 125000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 125000UL);
    EXPECT(((1U + t.tseg1) * 1000U) / (1U + t.tseg1 + t.tseg2) >= 800U);

    EXPECT(CanMcp_CalcTiming(16000000UL, 1000000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 1000000UL);
    EXPECT(t.tseg2 >= 2U);

    EXPECT(CanMcp_CalcTiming(8000000UL, 20000UL, 875U, &t) == CAN_OK);        /* Long bit: tseg1 is capped. */
    EXPECT(t.actual_bitrate == 20000UL);
    EXPECT(t.tseg1 <= MCP_TSEG1_MAX && t.tseg2 >= 2U && t.tseg2 <= MCP_TSEG2_MAX);
    EXPECT(CanMcp_CalcTiming(8000000UL, 1000000UL, 875U, &t) == CAN_ERR_BITRATE);   /* Only 4 quanta. */
    EXPECT(CanMcp_CalcTiming(16000000UL, 3U, 875U, &t) == CAN_ERR_BITRATE);
    EXPECT(CanMcp_CalcTiming(0U, 500000UL, 875U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanMcp_CalcTiming(16000000UL, 500000UL, 1000U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanMcp_CalcTiming(16000000UL, 500000UL, 875U, NULL) == CAN_ERR_NULL_PTR);
}

static void test_init(void)
{
    const CanMcpCtx_t *ctx;

    /* Good chip. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_READY);
    ctx = CanMcp_GetCtx(&can1);
    EXPECT(ctx != NULL);
    EXPECT(sim.reg[MCP_REG_CNF1] == 0x00U);             /* SJW 1, prescaler 1. */
    EXPECT(sim.reg[MCP_REG_CNF3] == 0x01U);             /* PS2 = 2. */
    EXPECT((sim.reg[MCP_REG_CNF2] & 0x80U) != 0U);      /* BTLMODE. */
    EXPECT(sim.reg[MCP_REG_RXB0CTRL] == 0x64U);
    EXPECT(sim.reg[MCP_REG_RXB1CTRL] == 0x60U);
    EXPECT(sim.reg[MCP_REG_CANINTE] == 0x03U);
    EXPECT(can1.cfg.nominal.actual_bitrate == 500000UL);
    EXPECT(can1.cfg.nominal.prescaler == 1U);
    EXPECT(sim_in_config());

    /* Segments in CNF2 add up to the bit time: 1 + (prop) + (ps1) + ps2 = 16. */
    {
        uint32_t prop = (sim.reg[MCP_REG_CNF2] & 7U) + 1U;
        uint32_t ps1  = ((sim.reg[MCP_REG_CNF2] >> 3) & 7U) + 1U;
        uint32_t ps2  = (sim.reg[MCP_REG_CNF3] & 7U) + 1U;
        EXPECT((1U + prop + ps1 + ps2) == 16U);
    }

    /* No chip: SPI works but nothing answers. */
    sim_power_up();
    sim.dead = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_CHIP_NOT_FOUND);
    EXPECT(can1.status.state == CAN_STATE_FAULT);
    EXPECT(can1.status.last_error == CAN_ERR_CHIP_NOT_FOUND);

    /* SPI itself fails. */
    sim_power_up();
    sim.fail_spi = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_SPI);

    /* Wrong variant, missing oscillator, impossible bitrate. */
    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    can1.cfg.mcp_variant = CAN_MCP_2518FD;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_NOT_SUPPORTED);

    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 0U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BAD_PARAM);

    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    can1.cfg.nominal.bitrate = 7U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BITRATE);
}

static void test_modes(void)
{
    /* Normal mode. */
    start_ok(CANLIB_MODE_NORMAL);
    EXPECT((sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK) == MCP_MODE_NORMAL);
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(sim_in_config());

    /* Listen-only. */
    start_ok(CANLIB_MODE_LISTEN_ONLY);
    EXPECT((sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK) == MCP_MODE_LISTEN);

    /* Loopback. */
    start_ok(CANLIB_MODE_LOOPBACK);
    EXPECT((sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK) == MCP_MODE_LOOPBACK);

    /* One-shot mode when auto retransmit is off. */
    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    can1.cfg.auto_retransmit = false;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT((sim.reg[MCP_REG_CANCTRL] & MCP_CANCTRL_OSM) != 0U);

    /* Slow mode change: the driver waits for CANSTAT. */
    sim_power_up();
    sim.mode_delay = 5;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);

    /* Mode never changes: timeout error. */
    sim_power_up();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX) == CAN_OK);
    can1.hw.osc_hz = 16000000UL;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    sim.mode_delay = 1000;
    EXPECT(CanLib_Start(&can1) == CAN_ERR_TIMEOUT);
}

static void test_transmit(void)
{
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int     n;

    start_ok(CANLIB_MODE_NORMAL);

    /* Standard frame, 3 bytes. */
    EXPECT(CanLib_Send(&can1, 0x123U, data, 3U) == CAN_OK);
    EXPECT(sim.txreq[0]);
    EXPECT(sim.txbuf[0][0] == 0x24U);                   /* 0x123 >> 3 */
    EXPECT(sim.txbuf[0][1] == 0x60U);                   /* (0x123 & 7) << 5 */
    EXPECT((sim.txbuf[0][4] & 0x0FU) == 3U);
    EXPECT(sim.txbuf[0][5] == 1U);
    EXPECT(sim.txbuf[0][7] == 3U);
    sim_complete_tx(0);
    EXPECT(sim.sent_count == 1);

    /* Extended frame, 8 bytes. */
    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF0UL, data, 8U) == CAN_OK);
    EXPECT(sim.txreq[0]);
    EXPECT(sim.txbuf[0][0] == (uint8_t)(0x1ABCDEF0UL >> 21));
    EXPECT((sim.txbuf[0][1] & 0x08U) != 0U);            /* EXIDE */
    EXPECT((((uint32_t)(sim.txbuf[0][1] >> 5) << 18) | ((uint32_t)(sim.txbuf[0][1] & 3U) << 16) |
            ((uint32_t)sim.txbuf[0][2] << 8) | sim.txbuf[0][3] |
            ((uint32_t)sim.txbuf[0][0] << 21)) == 0x1ABCDEF0UL);
    EXPECT((sim.txbuf[0][4] & 0x0FU) == 8U);
    EXPECT(sim.txbuf[0][12] == 8U);
    sim_complete_tx(0);

    /* Remote frame. */
    EXPECT(CanLib_SendRemote(&can1, 0x321U, CAN_ID_STANDARD, 4U) == CAN_OK);
    EXPECT((sim.txbuf[0][4] & 0x40U) != 0U);            /* RTR */
    EXPECT((sim.txbuf[0][4] & 0x0FU) == 4U);
    sim_complete_tx(0);

    /* All three buffers busy: more frames wait in the software queue. */
    for (n = 0; n < 3; n++)
    {
        EXPECT(CanLib_Send(&can1, 0x100U + (uint32_t)n, data, 1U) == CAN_OK);
    }
    EXPECT(sim.txreq[0] && sim.txreq[1] && sim.txreq[2]);
    {
        CanFrame_t f;
        memset(&f, 0, sizeof(f));
        f.header.id = 0x555U;
        f.header.dlc = 1U;
        f.header.data_len = 1U;
        EXPECT(g_CanPortMcp25xx.transmit(&can1, &f) == CAN_ERR_TX_FULL);   /* Backend alone reports full. */
    }
    EXPECT(!CAN_IS_ERR(CanLib_Send(&can1, 0x200U, data, 1U)));
    EXPECT(can1.tx.queue.count == 1U);                  /* The 4th frame waits in the queue. */
    sim_complete_tx(1);
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(can1.tx.queue.count == 0U);                  /* Process sent it into the free buffer. */
    EXPECT(sim.txreq[1]);

    /* FD frame is refused by the backend. */
    {
        CanFrame_t f;
        memset(&f, 0, sizeof(f));
        f.header.format = CAN_FORMAT_FD;
        EXPECT(g_CanPortMcp25xx.transmit(&can1, &f) == CAN_ERR_NOT_SUPPORTED);
    }
}

static void test_receive(void)
{
    uint8_t    d1[8] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8};
    CanFrame_t f;
    uint32_t   dropped_before;

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_ERR_RX_EMPTY);

    /* Standard data frame. */
    g_tick = 1234U;
    EXPECT(sim_rx_frame(0x7FFU, false, false, 8U, d1));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Available(&can1) == 1U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x7FFU);
    EXPECT(f.header.id_type == CAN_ID_STANDARD);
    EXPECT(f.header.frame_type == CAN_FRAME_DATA);
    EXPECT(f.header.data_len == 8U);
    EXPECT(memcmp(f.body.data, d1, 8U) == 0);
    EXPECT(f.header.timestamp_ms == 1234U);
    EXPECT(can1.rx.header.id == 0x7FFU);                /* Last frame also in the watch struct. */
    EXPECT(can1.stats.rx_ok == 1U);

    /* Extended frame. */
    EXPECT(sim_rx_frame(0x1234567UL, true, false, 5U, d1));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1234567UL);
    EXPECT(f.header.id_type == CAN_ID_EXTENDED);
    EXPECT(f.header.data_len == 5U);
    EXPECT(memcmp(f.body.data, d1, 5U) == 0);

    /* Remote frames, standard and extended. */
    EXPECT(sim_rx_frame(0x010U, false, true, 3U, NULL));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE);
    EXPECT(f.header.dlc == 3U);
    EXPECT(f.header.data_len == 0U);

    EXPECT(sim_rx_frame(0x1FFFFFFFUL, true, true, 2U, NULL));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1FFFFFFFUL);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE);

    /* DLC 9..15 means 8 data bytes. */
    EXPECT(sim_rx_frame(0x22U, false, false, 12U, d1));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.data_len == 8U);
    EXPECT(f.header.dlc == 12U);

    /* Two frames waiting in both chip buffers are both read in one Process call. */
    EXPECT(sim_rx_frame(0x30U, false, false, 1U, d1));
    EXPECT(sim_rx_frame(0x31U, false, false, 1U, d1));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Available(&can1) == 2U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x30U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x31U);

    /* Chip overflow: third frame while both buffers are full. */
    EXPECT(sim_rx_frame(0x40U, false, false, 1U, d1));
    EXPECT(sim_rx_frame(0x41U, false, false, 1U, d1));
    EXPECT(!sim_rx_frame(0x42U, false, false, 1U, d1));
    dropped_before = can1.stats.rx_dropped;
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.stats.rx_dropped == dropped_before + 1U);
    EXPECT((sim.reg[MCP_REG_EFLG] & MCP_EFLG_RX0OVR) == 0U);   /* Flag cleared by the driver. */
    EXPECT(CanLib_Available(&can1) == 2U);
}

static void test_filters_and_loopback(void)
{
    uint8_t    data[2] = {0x55, 0xAA};
    CanFrame_t f;

    start_ok(CANLIB_MODE_LOOPBACK);
    EXPECT(CanLib_Send(&can1, 0x123U, data, 2U) == CAN_OK);
    sim_complete_tx(0);                                 /* Chip loops the frame into RX. */
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x123U);
    EXPECT(f.body.data[0] == 0x55U);
    EXPECT(f.body.data[1] == 0xAAU);

    /* Software filter still works on top of the chip (chip accepts everything). */
    EXPECT(CanLib_AddFilterExact(&can1, CAN_ID_STANDARD, 0x200U) == CAN_OK);
    EXPECT(sim_rx_frame(0x201U, false, false, 1U, data));
    EXPECT(sim_rx_frame(0x200U, false, false, 1U, data));
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Available(&can1) == 1U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x200U);
}

static void test_bus_state(void)
{
    uint8_t data[1] = {1};

    start_ok(CANLIB_MODE_NORMAL);

    /* Counters reach the warning and passive levels. */
    sim.reg[MCP_REG_TEC] = 100U;
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.bus.tec == 100U);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_WARNING);
    sim.reg[MCP_REG_REC] = 130U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);
    EXPECT(can1.bus.rec == 130U);

    /* Bus-off: sending is refused, then auto recovery after the wait time. */
    g_tick = 1000U;
    sim.reg[MCP_REG_TEC]  = 255U;
    sim.reg[MCP_REG_EFLG] |= MCP_EFLG_TXBO;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT((can1.bus.hw_error_flags & MCP_EFLG_TXBO) != 0U);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BUS_OFF);

    g_tick = 1000U + can1.cfg.bus_off_recover_ms + 5U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
    EXPECT((sim.reg[MCP_REG_CANSTAT] & MCP_MODE_MASK) == MCP_MODE_NORMAL);   /* Back on the bus. */
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_OK);
}

static void test_xcvr_and_errors(void)
{
    uint8_t data[1] = {1};
    int     dummy_port = 0;

    start_ok(CANLIB_MODE_NORMAL);

    /* No mode pin configured. */
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_ERR_NOT_SUPPORTED);

    /* Mode pin configured: standby level high. */
    can1.hw.xcvr_port          = &dummy_port;
    can1.hw.xcvr_pin           = 5U;
    can1.hw.xcvr_standby_level = true;
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_OK);
    EXPECT(pin_calls == 1);
    EXPECT(pin_last_pin == 5U);
    EXPECT(pin_last_level == true);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL) == CAN_OK);
    EXPECT(pin_last_level == false);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_SILENT) == CAN_OK);
    EXPECT(pin_last_level == true);

    /* Inverted pin (standby = low). */
    can1.hw.xcvr_standby_level = false;
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL) == CAN_OK);
    EXPECT(pin_last_level == true);

    /* SPI failure while sending is reported and counted. */
    sim.fail_spi = true;
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_SPI);
    EXPECT(can1.status.last_error == CAN_ERR_SPI);
    EXPECT(CanMcp_GetCtx(&can1)->spi_errors > 0U);
    sim.fail_spi = false;
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_OK);

    /* Helper checks. */
    EXPECT(CanMcp_GetCtx(NULL) == NULL);
}

static void test_deinit(void)
{
    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
    EXPECT(sim_in_config());
    EXPECT(can1.port_ctx == NULL);
    EXPECT(CanMcp_GetCtx(&can1) == NULL);
}

int main(void)
{
    test_timing();
    test_init();
    test_modes();
    test_transmit();
    test_receive();
    test_filters_and_loopback();
    test_bus_state();
    test_xcvr_and_errors();
    test_deinit();

    if (fails == 0)
    {
        printf("test_mcp25xx: ALL PASSED\n");
        return 0;
    }
    printf("test_mcp25xx: %d FAILED\n", fails);
    return 1;
}
