/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_bxcan.c
 * @brief   Host-side tests of the bxCAN backend against a fake HAL (not part of the library).
 *
 * Build with CAN_CFG_PLATFORM = STM32, the bxCAN backend enabled, and
 * -Ihal_stub so that "stm32f4xx_hal.h" is the stub header.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"
#include CAN_CFG_STM32_HAL_HEADER

#if !CAN_CFG_ENABLE_BACKEND_STM32_BXCAN
    #error "test_bxcan.c needs CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 1"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ======================================================================== */
/* FAKE HAL                                                                 */
/* ======================================================================== */

typedef struct { CAN_RxHeaderTypeDef h; uint8_t d[8]; } FakeRx_t;

static struct
{
    bool                started;
    bool                init_called;
    bool                filter_called;
    bool                notify_on;
    bool                fail_init, fail_filter, fail_start, fail_add;
    uint32_t            free_mailboxes;
    CAN_TxHeaderTypeDef last_tx;
    uint8_t             last_tx_data[8];
    int                 tx_count;
    FakeRx_t            fifo[4];
    int                 fifo_count;
    CAN_FilterTypeDef   last_filter;
    uint32_t            pclk;
    uint32_t            hal_error;
    int                 start_count, stop_count;
} fake;

static CAN_TypeDef  fake_regs;
static CAN_HandleTypeDef hcan1;

static void fake_reset(void)
{
    memset(&fake, 0, sizeof(fake));
    memset(&fake_regs, 0, sizeof(fake_regs));
    memset(&hcan1, 0, sizeof(hcan1));
    hcan1.Instance = &fake_regs;
    fake.free_mailboxes = 3U;
    fake.pclk = 42000000UL;                             /* STM32F4 APB1. */
}

static void fake_push_rx(uint32_t id, bool ext, bool rtr, uint32_t dlc, const uint8_t *data)
{
    FakeRx_t *r = &fake.fifo[fake.fifo_count++];
    memset(r, 0, sizeof(*r));
    r->h.IDE = ext ? CAN_ID_EXT : CAN_ID_STD;
    r->h.RTR = rtr ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    r->h.DLC = dlc;
    if (ext) { r->h.ExtId = id; } else { r->h.StdId = id; }
    if (data != NULL) { memcpy(r->d, data, 8U); }
}

uint32_t HAL_GetTick(void) { return 777U; }
void HAL_Delay(uint32_t d) { (void)d; }
void HAL_GPIO_WritePin(GPIO_TypeDef *g, uint16_t p, GPIO_PinState s) { (void)g; (void)p; (void)s; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *t, uint8_t *r, uint16_t n, uint32_t to)
{ (void)h; (void)t; (void)r; (void)n; (void)to; return HAL_OK; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return fake.pclk; }
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h) { (void)h; fake.init_called = true; return fake.fail_init ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, const CAN_FilterTypeDef *f)
{ (void)h; fake.filter_called = true; fake.last_filter = *f; return fake.fail_filter ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void)h; fake.start_count++; if (fake.stop_count > 0) { fake_regs.ESR = 0U; } if (fake.fail_start) { return HAL_ERROR; } fake.started = true; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Stop(CAN_HandleTypeDef *h) { (void)h; fake.stop_count++; fake.started = false; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t it) { (void)h; fake.notify_on = (it != 0U); return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_DeactivateNotification(CAN_HandleTypeDef *h, uint32_t it) { (void)h; (void)it; fake.notify_on = false; return HAL_OK; }
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(const CAN_HandleTypeDef *h) { (void)h; return fake.free_mailboxes; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, const CAN_TxHeaderTypeDef *hd, const uint8_t d[], uint32_t *mb)
{
    (void)h;
    if (fake.fail_add) { fake.hal_error = 0x100U; return HAL_ERROR; }
    fake.last_tx = *hd;
    memcpy(fake.last_tx_data, d, 8U);
    fake.tx_count++;
    *mb = 1U;
    if (fake.free_mailboxes > 0U) { fake.free_mailboxes--; }
    return HAL_OK;
}
uint32_t HAL_CAN_GetRxFifoFillLevel(const CAN_HandleTypeDef *h, uint32_t fifo) { (void)h; (void)fifo; return (uint32_t)fake.fifo_count; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hd, uint8_t d[])
{
    int i;
    (void)h; (void)fifo;
    if (fake.fifo_count == 0) { return HAL_ERROR; }
    *hd = fake.fifo[0].h;
    memcpy(d, fake.fifo[0].d, 8U);
    for (i = 1; i < fake.fifo_count; i++) { fake.fifo[i - 1] = fake.fifo[i]; }
    fake.fifo_count--;
    return HAL_OK;
}
uint32_t HAL_CAN_GetError(const CAN_HandleTypeDef *h) { (void)h; return fake.hal_error; }

static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void) { return g_tick; }     /* Test clock overrides the platform tick. */

static int  pin_calls = 0;
static bool pin_level = false;
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{ (void)port; (void)pin; pin_calls++; pin_level = level; return CAN_OK; }

/* ======================================================================== */
/* TESTS                                                                    */
/* ======================================================================== */

static CanLib_t can1;

static CanResult_e bring_up(CanMode_e mode)
{
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    can1.cfg.mode        = mode;
    can1.hw.handle       = &hcan1;
    can1.debug.log_level = CAN_LOG_DEBUG;
    return CanLib_Init(&can1);
}

static void start_ok(CanMode_e mode)
{
    EXPECT(bring_up(mode) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_RUNNING);
}

static void test_timing(void)
{
    CanTiming_t t;

    EXPECT(CanBxcan_CalcTiming(42000000UL, 500000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 500000UL);
    EXPECT(t.prescaler == 6U);                          /* 42 MHz / 6 = 7 MHz = 14 quanta of 500 kbit/s. */
    EXPECT(t.tseg1 + t.tseg2 + 1U == 14U);

    EXPECT(CanBxcan_CalcTiming(36000000UL, 1000000UL, 875U, &t) == CAN_OK);   /* STM32F1: 36 MHz. */
    EXPECT(t.actual_bitrate == 1000000UL);

    EXPECT(CanBxcan_CalcTiming(36000000UL, 125000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 125000UL);
    EXPECT(t.tseg1 <= BXCAN_BS1_MAX);
    EXPECT(t.tseg2 <= BXCAN_BS2_MAX);

    EXPECT(CanBxcan_CalcTiming(36000000UL, 20000UL, 875U, &t) == CAN_OK);     /* Long bit: BS1 is capped. */
    EXPECT(t.actual_bitrate == 20000UL);
    EXPECT(t.tseg1 <= BXCAN_BS1_MAX && t.tseg2 >= 1U && t.tseg2 <= BXCAN_BS2_MAX);
    EXPECT(CanBxcan_CalcTiming(42000000UL, 7U, 875U, &t) == CAN_ERR_BITRATE);
    EXPECT(CanBxcan_CalcTiming(0U, 500000UL, 875U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanBxcan_CalcTiming(42000000UL, 500000UL, 0U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanBxcan_CalcTiming(42000000UL, 500000UL, 875U, NULL) == CAN_ERR_NULL_PTR);
}

static void test_init(void)
{
    uint32_t bs1, bs2;

    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    EXPECT(fake.init_called && fake.filter_called);
    EXPECT(hcan1.Init.Prescaler == 6U);
    bs1 = (hcan1.Init.TimeSeg1 >> 16) + 1U;
    bs2 = (hcan1.Init.TimeSeg2 >> 20) + 1U;
    EXPECT((1U + bs1 + bs2) == 14U);
    EXPECT(hcan1.Init.Mode == CAN_MODE_NORMAL);
    EXPECT(hcan1.Init.AutoBusOff == DISABLE);
    EXPECT(hcan1.Init.AutoRetransmission == ENABLE);
    EXPECT(fake.last_filter.FilterMode == CAN_FILTERMODE_IDMASK);
    EXPECT(fake.last_filter.FilterIdHigh == 0U && fake.last_filter.FilterMaskIdHigh == 0U);   /* Accept all. */
    EXPECT(fake.last_filter.FilterActivation == CAN_FILTER_ENABLE);
    EXPECT(can1.cfg.nominal.actual_bitrate == 500000UL);
    EXPECT(CanBxcan_GetCtx(&can1) != NULL);

    /* Modes. */
    EXPECT(bring_up(CANLIB_MODE_LISTEN_ONLY) == CAN_OK);
    EXPECT(hcan1.Init.Mode == CAN_MODE_SILENT);
    EXPECT(bring_up(CANLIB_MODE_LOOPBACK) == CAN_OK);
    EXPECT(hcan1.Init.Mode == CAN_MODE_LOOPBACK);

    /* One-shot. */
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    can1.hw.handle = &hcan1;
    can1.cfg.auto_retransmit = false;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(hcan1.Init.AutoRetransmission == DISABLE);

    /* Errors. */
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_NULL_PTR);     /* No handle set. */

    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    fake_reset();
    fake.fail_init = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    can1.hw.handle = &hcan1;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);

    fake_reset();
    fake.fail_filter = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    can1.hw.handle = &hcan1;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);

    fake_reset();
    fake.pclk = 42000000UL;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    can1.hw.handle = &hcan1;
    can1.cfg.nominal.bitrate = 7U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BITRATE);
}

static void test_start_stop(void)
{
    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(fake.started);
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(!fake.started);

    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    fake.fail_start = true;
    EXPECT(CanLib_Start(&can1) == CAN_ERR_BACKEND_FAIL);

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
    EXPECT(can1.port_ctx == NULL);
}

static void test_transmit(void)
{
    uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CanFrame_t f;

    start_ok(CANLIB_MODE_NORMAL);

    EXPECT(CanLib_Send(&can1, 0x123U, data, 3U) == CAN_OK);
    EXPECT(fake.last_tx.IDE == CAN_ID_STD);
    EXPECT(fake.last_tx.StdId == 0x123U);
    EXPECT(fake.last_tx.RTR == CAN_RTR_DATA);
    EXPECT(fake.last_tx.DLC == 3U);
    EXPECT(fake.last_tx_data[2] == 3U);

    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF0UL, data, 8U) == CAN_OK);
    EXPECT(fake.last_tx.IDE == CAN_ID_EXT);
    EXPECT(fake.last_tx.ExtId == 0x1ABCDEF0UL);
    EXPECT(fake.last_tx.DLC == 8U);

    EXPECT(CanLib_SendRemote(&can1, 0x55U, CAN_ID_STANDARD, 2U) == CAN_OK);
    EXPECT(fake.last_tx.RTR == CAN_RTR_REMOTE);
    EXPECT(fake.last_tx.DLC == 2U);

    /* Mailboxes full: backend reports it, core queues the frame. */
    fake.free_mailboxes = 0U;
    memset(&f, 0, sizeof(f));
    EXPECT(g_CanPortStm32Bxcan.transmit(&can1, &f) == CAN_ERR_TX_FULL);
    EXPECT(!CAN_IS_ERR(CanLib_Send(&can1, 0x200U, data, 1U)));
    EXPECT(can1.tx.queue.count == 1U);
    fake.free_mailboxes = 3U;
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(can1.tx.queue.count == 0U);
    EXPECT(fake.last_tx.StdId == 0x200U);

    /* HAL failure keeps its error code visible. */
    fake.fail_add = true;
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BACKEND_FAIL);
    EXPECT(CanBxcan_GetCtx(&can1)->hal_error == 0x100U);
    EXPECT(CanBxcan_GetCtx(&can1)->hal_failures == 1U);
    fake.fail_add = false;

    /* FD refused. */
    f.header.format = CAN_FORMAT_FD;
    EXPECT(g_CanPortStm32Bxcan.transmit(&can1, &f) == CAN_ERR_NOT_SUPPORTED);
}

static void test_receive(void)
{
    uint8_t    d[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    CanFrame_t f;

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_ERR_RX_EMPTY);

    g_tick = 4321U;
    fake_push_rx(0x7FFU, false, false, 8U, d);
    fake_push_rx(0x1234567UL, true, false, 4U, d);
    fake_push_rx(0x10U, false, true, 3U, NULL);
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(CanLib_Available(&can1) == 3U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x7FFU && f.header.id_type == CAN_ID_STANDARD);
    EXPECT(f.header.data_len == 8U && memcmp(f.body.data, d, 8U) == 0);
    EXPECT(f.header.timestamp_ms == 4321U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1234567UL && f.header.id_type == CAN_ID_EXTENDED);
    EXPECT(f.header.data_len == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE);
    EXPECT(f.header.dlc == 3U && f.header.data_len == 0U);

    EXPECT(can1.stats.rx_ok == 3U);
}

static void test_irq(void)
{
    uint8_t    d[8] = {1};
    CanFrame_t f;

    /* The IRQ hand-over works in either build; the notification only when CAN_CFG_BXCAN_USE_IRQ is 1. */
    start_ok(CANLIB_MODE_NORMAL);
#if CAN_CFG_BXCAN_USE_IRQ
    EXPECT(fake.notify_on);
#else
    EXPECT(!fake.notify_on);
#endif
    fake_push_rx(0x42U, false, false, 1U, d);
    fake_push_rx(0x43U, false, false, 1U, d);
    CanBxcan_OnRxFifo0Pending(&can1);
    EXPECT(fake.fifo_count == 0);
    EXPECT(CanLib_Available(&can1) == 2U);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK && f.header.id == 0x42U);
    CanBxcan_OnRxFifo0Pending(NULL);                    /* Must not crash. */
}

static void test_bus_state(void)
{
    uint8_t data[1] = {1};

    start_ok(CANLIB_MODE_NORMAL);

    fake_regs.ESR = (100UL << 16);                      /* TEC 100 */
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.tec == 100U);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_WARNING);

    fake_regs.ESR = (130UL << 24) | (100UL << 16);      /* REC 130 */
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.rec == 130U);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);

    /* FIFO overrun is counted once and cleared. */
    fake_regs.RF0R = CAN_RF0R_FOVR0;
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 1U);

    /* Bus-off, then automatic recovery. */
    g_tick = 2000U;
    fake_regs.ESR = (255UL << 16) | (1UL << 2);
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BUS_OFF);

    fake.stop_count = 0;
    fake.start_count = 0;
    g_tick = 2000U + can1.cfg.bus_off_recover_ms + 5U;
    (void)CanLib_Process(&can1);
    EXPECT(fake.stop_count == 1 && fake.start_count == 1);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
    EXPECT(fake.started);
}

static void test_xcvr(void)
{
    int dummy = 0;

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_ERR_NOT_SUPPORTED);
    can1.hw.xcvr_port = &dummy;
    can1.hw.xcvr_pin  = 3U;
    can1.hw.xcvr_standby_level = true;
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_OK);
    EXPECT(pin_level == true && pin_calls == 1);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL) == CAN_OK);
    EXPECT(pin_level == false);
}

int main(void)
{
    test_timing();
    test_init();
    test_start_stop();
    test_transmit();
    test_receive();
    test_irq();
    test_bus_state();
    test_xcvr();

    if (fails == 0)
    {
        printf("test_bxcan: ALL PASSED\n");
        return 0;
    }
    printf("test_bxcan: %d FAILED\n", fails);
    return 1;
}
