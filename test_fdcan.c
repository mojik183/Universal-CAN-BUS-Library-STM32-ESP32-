/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_fdcan.c
 * @brief   Host-side tests of the STM32 FDCAN backend against a fake HAL (not part of the library).
 *
 * Build with CAN_CFG_PLATFORM = STM32, the FDCAN backend enabled (bxCAN off), CAN_CFG_ENABLE_FD 1,
 * CAN_CFG_STM32_HAL_HEADER "stm32g4xx_hal.h" and -Ihal_stub.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"
#include CAN_CFG_STM32_HAL_HEADER

#if !CAN_CFG_ENABLE_BACKEND_STM32_FDCAN || !CAN_CFG_ENABLE_FD
    #error "test_fdcan.c needs the FDCAN backend and CAN_CFG_ENABLE_FD 1"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ======================================================================== */
/* FAKE HAL                                                                 */
/* ======================================================================== */

typedef struct { FDCAN_RxHeaderTypeDef h; uint8_t d[64]; } FakeRx_t;

static struct
{
    bool                  started, notify_on;
    bool                  fail_init, fail_filter, fail_start, fail_add, fail_tdc;
    bool                  init_called, filter_called, tdc_cfg, tdc_on;
    uint32_t              tdc_offset;
    uint32_t              filter_args[4];
    uint32_t              tx_free;
    FDCAN_TxHeaderTypeDef last_tx;
    uint8_t               last_tx_data[64];
    int                   tx_count;
    FakeRx_t              fifo[4];
    int                   fifo_count;
    FDCAN_ErrorCountersTypeDef counters;
    FDCAN_ProtocolStatusTypeDef proto;
    uint32_t              hal_error;
    int                   start_count, stop_count;
} fake;

static FDCAN_GlobalTypeDef fake_regs;
static FDCAN_HandleTypeDef hfdcan1;

static void fake_reset(void)
{
    memset(&fake, 0, sizeof(fake));
    memset(&fake_regs, 0, sizeof(fake_regs));
    memset(&hfdcan1, 0, sizeof(hfdcan1));
    hfdcan1.Instance = &fake_regs;
    fake.tx_free = 3U;
}

static void fake_push_rx(uint32_t id, bool ext, bool rtr, bool fd, bool brs, bool esi, uint32_t dlc, const uint8_t *data, size_t n)
{
    FakeRx_t *r = &fake.fifo[fake.fifo_count++];
    memset(r, 0, sizeof(*r));
    r->h.Identifier          = id;
    r->h.IdType              = ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    r->h.RxFrameType         = rtr ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    r->h.DataLength          = dlc << 16;
    r->h.FDFormat            = fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    r->h.BitRateSwitch       = brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    r->h.ErrorStateIndicator = esi ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    if (data != NULL) { memcpy(r->d, data, n); }
}

uint32_t HAL_GetTick(void) { return 555U; }
void HAL_Delay(uint32_t d) { (void)d; }
void HAL_GPIO_WritePin(GPIO_TypeDef *g, uint16_t p, GPIO_PinState s) { (void)g; (void)p; (void)s; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *t, uint8_t *r, uint16_t n, uint32_t to)
{ (void)h; (void)t; (void)r; (void)n; (void)to; return HAL_OK; }

HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *h) { (void)h; fake.init_called = true; return fake.fail_init ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)h; fake.filter_called = true; fake.filter_args[0] = a; fake.filter_args[1] = b; fake.filter_args[2] = c; fake.filter_args[3] = d;
  return fake.fail_filter ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ConfigTxDelayCompensation(FDCAN_HandleTypeDef *h, uint32_t off, uint32_t flt)
{ (void)h; (void)flt; fake.tdc_cfg = true; fake.tdc_offset = off; return fake.fail_tdc ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_EnableTxDelayCompensation(FDCAN_HandleTypeDef *h) { (void)h; fake.tdc_on = true; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h)
{ (void)h; fake.start_count++; if (fake.fail_start) { return HAL_ERROR; } fake.started = true;
  if (fake.stop_count > 0) { fake.proto.BusOff = 0U; fake.counters.TxErrorCnt = 0U; } return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h) { (void)h; fake.stop_count++; fake.started = false; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *h, uint32_t it, uint32_t buf) { (void)h; (void)buf; fake.notify_on = (it != 0U); return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_DeactivateNotification(FDCAN_HandleTypeDef *h, uint32_t it) { (void)h; (void)it; fake.notify_on = false; return HAL_OK; }
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h) { (void)h; return fake.tx_free; }
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h, const FDCAN_TxHeaderTypeDef *hd, const uint8_t *d)
{
    uint32_t dlc = hd->DataLength >> 16;
    static const uint8_t len_of[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    (void)h;
    if (fake.fail_add) { fake.hal_error = 0x200U; return HAL_ERROR; }
    fake.last_tx = *hd;
    memset(fake.last_tx_data, 0, sizeof(fake.last_tx_data));
    memcpy(fake.last_tx_data, d, len_of[dlc & 15U]);     /* The HAL reads exactly the bytes of the DLC. */
    fake.tx_count++;
    return HAL_OK;
}
uint32_t HAL_FDCAN_GetRxFifoFillLevel(const FDCAN_HandleTypeDef *h, uint32_t f) { (void)h; (void)f; return (uint32_t)fake.fifo_count; }
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *h, uint32_t loc, FDCAN_RxHeaderTypeDef *hd, uint8_t *d)
{
    int i;
    (void)h; (void)loc;
    if (fake.fifo_count == 0) { return HAL_ERROR; }
    *hd = fake.fifo[0].h;
    memcpy(d, fake.fifo[0].d, 64U);
    for (i = 1; i < fake.fifo_count; i++) { fake.fifo[i - 1] = fake.fifo[i]; }
    fake.fifo_count--;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FDCAN_GetErrorCounters(const FDCAN_HandleTypeDef *h, FDCAN_ErrorCountersTypeDef *e) { (void)h; *e = fake.counters; return HAL_OK; }
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(const FDCAN_HandleTypeDef *h, FDCAN_ProtocolStatusTypeDef *p) { (void)h; *p = fake.proto; return HAL_OK; }
uint32_t HAL_FDCAN_GetError(const FDCAN_HandleTypeDef *h) { (void)h; return fake.hal_error; }

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

static CanResult_e bring_up(CanMode_e mode, bool fd, uint32_t data_bitrate)
{
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.cfg.mode          = mode;
    can1.cfg.fd_enabled    = fd;
    can1.hw.handle         = &hfdcan1;
    can1.cfg.data.bitrate  = data_bitrate;
    can1.debug.log_level   = CAN_LOG_DEBUG;
    return CanLib_Init(&can1);
}

static void start_ok(CanMode_e mode, bool fd)
{
    EXPECT(bring_up(mode, fd, 2000000UL) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_RUNNING);
}

static void test_timing(void)
{
    CanTiming_t t;

    EXPECT(CanFdcan_CalcTiming(160000000UL, 500000UL, 875U, false, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 500000UL);
    EXPECT(t.sjw >= 1U && t.sjw <= 4U);
    EXPECT(((1U + t.tseg1) * 1000U) / (1U + t.tseg1 + t.tseg2) >= 860U);
    EXPECT(((uint32_t)t.prescaler * (1U + t.tseg1 + t.tseg2) * 500000UL) == 160000000UL);

    EXPECT(CanFdcan_CalcTiming(160000000UL, 1000000UL, 875U, false, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 1000000UL);
    EXPECT(CanFdcan_CalcTiming(160000000UL, 125000UL, 875U, false, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 125000UL);

    /* Data phase: limits are smaller, so more quanta need a larger prescaler. */
    EXPECT(CanFdcan_CalcTiming(160000000UL, 2000000UL, 750U, true, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 2000000UL);
    EXPECT(t.tseg1 <= FDCAN_LIB_DAT_SEG1_MAX && t.tseg2 <= FDCAN_LIB_DAT_SEG2_MAX);
    EXPECT(t.prescaler <= FDCAN_LIB_DAT_PSC_MAX);
    EXPECT(CanFdcan_CalcTiming(160000000UL, 5000000UL, 750U, true, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 5000000UL);
    EXPECT(CanFdcan_CalcTiming(160000000UL, 8000000UL, 700U, true, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 8000000UL);

    EXPECT(CanFdcan_CalcTiming(170000000UL, 500000UL, 875U, false, &t) == CAN_OK);   /* G4 with 170 MHz. */
    EXPECT(t.actual_bitrate == 500000UL);

    EXPECT(CanFdcan_CalcTiming(160000000UL, 7U, 875U, false, &t) == CAN_ERR_BITRATE);
    EXPECT(CanFdcan_CalcTiming(0U, 500000UL, 875U, false, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanFdcan_CalcTiming(160000000UL, 500000UL, 1000U, true, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanFdcan_CalcTiming(160000000UL, 500000UL, 875U, false, NULL) == CAN_ERR_NULL_PTR);
}

static void test_init(void)
{
    /* Classic frame format. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL) == CAN_OK);
    EXPECT(fake.init_called && fake.filter_called);
    EXPECT(hfdcan1.Init.FrameFormat == FDCAN_FRAME_CLASSIC);
    EXPECT(hfdcan1.Init.Mode == FDCAN_MODE_NORMAL);
    EXPECT(hfdcan1.Init.AutoRetransmission == ENABLE);
    EXPECT(hfdcan1.Init.NominalPrescaler == can1.cfg.nominal.prescaler);
    EXPECT(hfdcan1.Init.NominalTimeSeg1 == can1.cfg.nominal.tseg1);
    EXPECT(hfdcan1.Init.StdFiltersNbr == 0U && hfdcan1.Init.ExtFiltersNbr == 0U);
    EXPECT(fake.filter_args[0] == FDCAN_ACCEPT_IN_RX_FIFO0 && fake.filter_args[1] == FDCAN_ACCEPT_IN_RX_FIFO0);
    EXPECT(!fake.tdc_cfg);
    EXPECT(CanFdcan_GetCtx(&can1) != NULL && !CanFdcan_GetCtx(&can1)->fd_mode);
    EXPECT(can1.cfg.nominal.actual_bitrate == 500000UL);

    /* CAN FD with 2 Mbit/s data: bit rate switching, TDC on. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, true, 2000000UL) == CAN_OK);
    EXPECT(hfdcan1.Init.FrameFormat == FDCAN_FRAME_FD_BRS);
    EXPECT(hfdcan1.Init.DataPrescaler == can1.cfg.data.prescaler);
    EXPECT(hfdcan1.Init.DataTimeSeg1 == can1.cfg.data.tseg1);
    EXPECT(hfdcan1.Init.DataTimeSeg2 == can1.cfg.data.tseg2);
    EXPECT(can1.cfg.data.actual_bitrate == 2000000UL);
    EXPECT(fake.tdc_cfg && fake.tdc_on);
    EXPECT(fake.tdc_offset == (uint32_t)can1.cfg.data.prescaler * can1.cfg.data.tseg1);
    EXPECT(CanFdcan_GetCtx(&can1)->fd_mode);

    /* Data rate below 2 Mbit/s: no TDC. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, true, 1000000UL) == CAN_OK);
    EXPECT(!fake.tdc_cfg);

    /* Modes and one-shot. */
    EXPECT(bring_up(CANLIB_MODE_LISTEN_ONLY, false, 2000000UL) == CAN_OK);
    EXPECT(hfdcan1.Init.Mode == FDCAN_MODE_BUS_MONITORING);
    EXPECT(bring_up(CANLIB_MODE_LOOPBACK, false, 2000000UL) == CAN_OK);
    EXPECT(hfdcan1.Init.Mode == FDCAN_MODE_INTERNAL_LOOPBACK);
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.hw.handle = &hfdcan1;
    can1.cfg.auto_retransmit = false;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(hfdcan1.Init.AutoRetransmission == DISABLE);

    /* Errors. */
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_NULL_PTR);     /* No handle. */

    fake_reset(); fake.fail_init = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.hw.handle = &hfdcan1;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);

    fake_reset(); fake.fail_filter = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.hw.handle = &hfdcan1;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);

    fake_reset(); fake.fail_tdc = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.hw.handle = &hfdcan1; can1.cfg.fd_enabled = true; can1.cfg.data.bitrate = 5000000UL;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);

    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.hw.handle = &hfdcan1; can1.cfg.fd_enabled = true; can1.cfg.data.bitrate = 7U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BITRATE);      /* Impossible data rate. */
}

static void test_start_stop(void)
{
    start_ok(CANLIB_MODE_NORMAL, false);
    EXPECT(fake.started);
#if CAN_CFG_FDCAN_USE_IRQ
    EXPECT(fake.notify_on);
#else
    EXPECT(!fake.notify_on);
#endif
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(!fake.started);

    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL) == CAN_OK);
    fake.fail_start = true;
    EXPECT(CanLib_Start(&can1) == CAN_ERR_BACKEND_FAIL);

    start_ok(CANLIB_MODE_NORMAL, false);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
    EXPECT(can1.port_ctx == NULL);
}

static void test_transmit(void)
{
    uint8_t    d[64];
    CanFrame_t f;
    uint8_t    i;

    for (i = 0U; i < 64U; i++) { d[i] = (uint8_t)(i + 1U); }

    start_ok(CANLIB_MODE_NORMAL, true);

    /* Classic frames. */
    EXPECT(CanLib_Send(&can1, 0x123U, d, 3U) == CAN_OK);
    EXPECT(fake.last_tx.Identifier == 0x123U && fake.last_tx.IdType == FDCAN_STANDARD_ID);
    EXPECT(fake.last_tx.DataLength == (3UL << 16));
    EXPECT(fake.last_tx.FDFormat == FDCAN_CLASSIC_CAN && fake.last_tx.BitRateSwitch == FDCAN_BRS_OFF);
    EXPECT(fake.last_tx_data[2] == 3U);

    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF0UL, d, 8U) == CAN_OK);
    EXPECT(fake.last_tx.IdType == FDCAN_EXTENDED_ID && fake.last_tx.Identifier == 0x1ABCDEF0UL);

    EXPECT(CanLib_SendRemote(&can1, 0x55U, CAN_ID_STANDARD, 2U) == CAN_OK);
    EXPECT(fake.last_tx.TxFrameType == FDCAN_REMOTE_FRAME);
    EXPECT(fake.last_tx.DataLength == (2UL << 16));

    /* CAN FD with bit rate switching: 20 bytes use DLC 11, 24 bytes DLC 12. */
    EXPECT(CanLib_SendFd(&can1, 0x200U, d, 20U, true) == CAN_OK);
    EXPECT(fake.last_tx.FDFormat == FDCAN_FD_CAN && fake.last_tx.BitRateSwitch == FDCAN_BRS_ON);
    EXPECT(fake.last_tx.DataLength == (11UL << 16));
    EXPECT(fake.last_tx_data[19] == 20U);
    EXPECT(CanLib_SendFd(&can1, 0x201U, d, 64U, false) == CAN_OK);
    EXPECT(fake.last_tx.DataLength == (15UL << 16));
    EXPECT(fake.last_tx.BitRateSwitch == FDCAN_BRS_OFF);
    EXPECT(fake.last_tx_data[63] == 64U);

    /* Odd FD length is rounded up and zero padded by the core: 13 -> 16. */
    EXPECT(CanLib_SendFd(&can1, 0x202U, d, 13U, true) == CAN_OK);
    EXPECT(fake.last_tx.DataLength == (10UL << 16));
    EXPECT(fake.last_tx_data[12] == 13U && fake.last_tx_data[13] == 0U);

    /* TX FIFO full: backend reports it, core queues and Process sends. */
    fake.tx_free = 0U;
    memset(&f, 0, sizeof(f));
    EXPECT(g_CanPortStm32Fdcan.transmit(&can1, &f) == CAN_ERR_TX_FULL);
    EXPECT(!CAN_IS_ERR(CanLib_Send(&can1, 0x300U, d, 1U)));
    EXPECT(can1.tx.queue.count == 1U);
    fake.tx_free = 3U;
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(can1.tx.queue.count == 0U);
    EXPECT(fake.last_tx.Identifier == 0x300U);

    /* HAL failure keeps its code visible. */
    fake.fail_add = true;
    EXPECT(CanLib_Send(&can1, 0x10U, d, 1U) == CAN_ERR_BACKEND_FAIL);
    EXPECT(CanFdcan_GetCtx(&can1)->hal_error == 0x200U && CanFdcan_GetCtx(&can1)->hal_failures == 1U);
    fake.fail_add = false;

    /* FD frame handed to an instance without FD mode: refused by the backend. */
    start_ok(CANLIB_MODE_NORMAL, false);
    memset(&f, 0, sizeof(f));
    f.header.format = CAN_FORMAT_FD;
    EXPECT(g_CanPortStm32Fdcan.transmit(&can1, &f) == CAN_ERR_NOT_SUPPORTED);
    /* The core turns an FD request into a classic frame with a warning in that case. */
    EXPECT(CanLib_SendFd(&can1, 0x400U, d, 8U, true) == CAN_WARN_FD_FALLBACK);
    EXPECT(fake.last_tx.FDFormat == FDCAN_CLASSIC_CAN);
}

static void test_receive(void)
{
    uint8_t    d[64];
    CanFrame_t f;
    uint8_t    i;

    for (i = 0U; i < 64U; i++) { d[i] = (uint8_t)(0x80U + i); }

    start_ok(CANLIB_MODE_NORMAL, true);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_ERR_RX_EMPTY);

    g_tick = 4242U;
    fake_push_rx(0x7FFU, false, false, false, false, false, 8U, d, 8U);
    fake_push_rx(0x1234567UL, true, false, false, false, false, 4U, d, 4U);
    fake_push_rx(0x10U, false, true, false, false, false, 3U, NULL, 0U);
    fake_push_rx(0x555U, false, false, true, true, true, 15U, d, 64U);     /* FD, BRS, ESI, 64 bytes. */
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(CanLib_Available(&can1) == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x7FFU && f.header.format == CAN_FORMAT_CLASSIC && f.header.data_len == 8U);
    EXPECT(memcmp(f.body.data, d, 8U) == 0);
    EXPECT(f.header.timestamp_ms == 4242U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1234567UL && f.header.id_type == CAN_ID_EXTENDED && f.header.data_len == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE && f.header.dlc == 3U && f.header.data_len == 0U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.format == CAN_FORMAT_FD && f.header.bitrate_switch && f.header.error_state);
    EXPECT(f.header.dlc == 15U && f.header.data_len == 64U);
    EXPECT(memcmp(f.body.data, d, 64U) == 0);
}

static void test_irq_handover(void)
{
    uint8_t    d[8] = {1};
    CanFrame_t f;

    start_ok(CANLIB_MODE_NORMAL, false);
    fake_push_rx(0x42U, false, false, false, false, false, 1U, d, 1U);
    fake_push_rx(0x43U, false, false, false, false, false, 1U, d, 1U);
    CanFdcan_OnRxFifo0Pending(&can1);
    EXPECT(fake.fifo_count == 0);
    EXPECT(CanLib_Available(&can1) == 2U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK && f.header.id == 0x42U);
    CanFdcan_OnRxFifo0Pending(NULL);
}

static void test_bus_state(void)
{
    uint8_t data[1] = {1};

    start_ok(CANLIB_MODE_NORMAL, false);

    fake.counters.TxErrorCnt = 100U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.tec == 100U && can1.status.bus_state == CAN_BUS_ERROR_WARNING);

    fake.counters.RxErrorCnt = 130U;
    fake.proto.ErrorPassive = 1U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.rec == 130U && can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);
    EXPECT((can1.bus.hw_error_flags & 0x02U) != 0U);

    /* Message lost flag is counted once and cleared. */
    fake_regs.IR = FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST;
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 1U);
    EXPECT((fake_regs.IR & FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST) == 0U);

    /* Bus-off and automatic recovery. */
    g_tick = 3000U;
    fake.proto.BusOff = 1U;
    fake.counters.TxErrorCnt = 255U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BUS_OFF);

    fake.stop_count = 0; fake.start_count = 0;
    g_tick = 3000U + can1.cfg.bus_off_recover_ms + 5U;
    (void)CanLib_Process(&can1);
    EXPECT(fake.stop_count == 1 && fake.start_count == 1);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
    EXPECT(fake.started);
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


#if CAN_CFG_FDCAN_MESSAGE_RAM
static void test_message_ram(void)
{
    /* Classic, instance 0: block starts at word 0, 4-word elements. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, false, 2000000UL) == CAN_OK);
    EXPECT(hfdcan1.Init.MessageRAMOffset == 0U);
    EXPECT(hfdcan1.Init.RxFifo0ElmtsNbr == CAN_CFG_FDCAN_RAM_RX_ELEMENTS && hfdcan1.Init.RxFifo0ElmtSize == FDCAN_DATA_BYTES_8);
    EXPECT(hfdcan1.Init.TxFifoQueueElmtsNbr == CAN_CFG_FDCAN_RAM_TX_ELEMENTS && hfdcan1.Init.TxElmtSize == FDCAN_DATA_BYTES_8);
    EXPECT(hfdcan1.Init.RxFifo1ElmtsNbr == 0U && hfdcan1.Init.RxBuffersNbr == 0U && hfdcan1.Init.TxBuffersNbr == 0U && hfdcan1.Init.TxEventsNbr == 0U);

    /* CAN FD: 64-byte elements. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL, true, 2000000UL) == CAN_OK);
    EXPECT(hfdcan1.Init.RxFifo0ElmtSize == FDCAN_DATA_BYTES_64 && hfdcan1.Init.TxElmtSize == FDCAN_DATA_BYTES_64);

    /* A second instance gets the next block of the shared RAM; blocks must not overlap. */
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN) == CAN_OK);
    can1.instance = 1U; can1.hw.handle = &hfdcan1; can1.cfg.fd_enabled = true;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(hfdcan1.Init.MessageRAMOffset == CAN_CFG_FDCAN_RAM_OFFSET(1U));
    EXPECT(hfdcan1.Init.MessageRAMOffset >= (CAN_CFG_FDCAN_RAM_TX_ELEMENTS + CAN_CFG_FDCAN_RAM_RX_ELEMENTS) * 18U);
    can1.instance = 0U;
}
#endif

int main(void)
{
    test_timing();
    test_init();
    test_start_stop();
    test_transmit();
    test_receive();
    test_irq_handover();
    test_bus_state();
    test_xcvr();
#if CAN_CFG_FDCAN_MESSAGE_RAM
    test_message_ram();
#endif

    if (fails == 0) { printf("test_fdcan: ALL PASSED\n"); return 0; }
    printf("test_fdcan: %d FAILED\n", fails);
    return 1;
}
