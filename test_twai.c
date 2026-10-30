/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_twai.c
 * @brief   Host-side tests of the ESP32 TWAI backend against a fake ESP-IDF driver (not part of the library).
 *
 * Build with CAN_CFG_PLATFORM = ESP32, the TWAI backend enabled, and -Iesp_stub.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"
#include "esp_err.h"
#include "driver/twai.h"

#if !CAN_CFG_ENABLE_BACKEND_ESP32_TWAI
    #error "test_twai.c needs CAN_CFG_ENABLE_BACKEND_ESP32_TWAI 1"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ======================================================================== */
/* FAKE TWAI DRIVER                                                         */
/* ======================================================================== */

static struct
{
    bool                  installed;
    twai_state_t          state;
    twai_general_config_t g;
    twai_timing_config_t  t;
    twai_filter_config_t  f;
    twai_message_t        last_tx;
    int                   tx_count;
    bool                  tx_queue_full;
    twai_message_t        rxq[8];
    int                   rx_count;
    twai_status_info_t    status;
    int                   recovery_polls;     /* Status reads before RECOVERING becomes STOPPED. */
    int                   recovery_left;
    bool                  fail_install;
    bool                  fail_start;
    int                   start_count, stop_count, recovery_calls;
} fake;

static void fake_reset(void)
{
    memset(&fake, 0, sizeof(fake));
    fake.state = TWAI_STATE_STOPPED;
}

static void fake_push_rx(uint32_t id, uint32_t flags, uint8_t dlc, const uint8_t *d)
{
    twai_message_t *m = &fake.rxq[fake.rx_count++];
    memset(m, 0, sizeof(*m));
    m->identifier = id;
    m->flags = flags;
    m->data_length_code = dlc;
    if (d != NULL) { memcpy(m->data, d, 8U); }
}

esp_err_t twai_driver_install(const twai_general_config_t *g, const twai_timing_config_t *t, const twai_filter_config_t *f)
{
    if (fake.fail_install) { return ESP_FAIL; }
    if (fake.installed) { return ESP_ERR_INVALID_STATE; }
    fake.installed = true;
    fake.g = *g; fake.t = *t; fake.f = *f;
    return ESP_OK;
}
esp_err_t twai_driver_uninstall(void) { if (!fake.installed) { return ESP_ERR_INVALID_STATE; } fake.installed = false; return ESP_OK; }
esp_err_t twai_start(void)
{
    fake.start_count++;
    if (fake.fail_start || (fake.state != TWAI_STATE_STOPPED)) { return ESP_ERR_INVALID_STATE; }
    fake.state = TWAI_STATE_RUNNING;
    return ESP_OK;
}
esp_err_t twai_stop(void) { fake.stop_count++; fake.state = TWAI_STATE_STOPPED; return ESP_OK; }
esp_err_t twai_transmit(const twai_message_t *m, TickType_t w)
{
    (void)w;
    if (fake.state != TWAI_STATE_RUNNING) { return ESP_ERR_INVALID_STATE; }
    if (fake.tx_queue_full) { return ESP_ERR_TIMEOUT; }
    fake.last_tx = *m;
    fake.tx_count++;
    if ((m->flags & TWAI_MSG_FLAG_SELF) != 0U) { fake.rxq[fake.rx_count++] = *m; }   /* Self reception. */
    return ESP_OK;
}
esp_err_t twai_receive(twai_message_t *m, TickType_t w)
{
    int i;
    (void)w;
    if (fake.state == TWAI_STATE_STOPPED) { return ESP_ERR_INVALID_STATE; }
    if (fake.rx_count == 0) { return ESP_ERR_TIMEOUT; }
    *m = fake.rxq[0];
    for (i = 1; i < fake.rx_count; i++) { fake.rxq[i - 1] = fake.rxq[i]; }
    fake.rx_count--;
    return ESP_OK;
}
esp_err_t twai_get_status_info(twai_status_info_t *s)
{
    if (fake.state == TWAI_STATE_RECOVERING)
    {
        if (fake.recovery_left > 0) { fake.recovery_left--; }
        if (fake.recovery_left == 0) { fake.state = TWAI_STATE_STOPPED; }
    }
    *s = fake.status;
    s->state = fake.state;
    return ESP_OK;
}
esp_err_t twai_initiate_recovery(void)
{
    fake.recovery_calls++;
    if (fake.state != TWAI_STATE_BUS_OFF) { return ESP_ERR_INVALID_STATE; }
    fake.state = TWAI_STATE_RECOVERING;
    fake.recovery_left = fake.recovery_polls;
    return ESP_OK;
}

/* ---- library platform hooks ---- */

static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void) { return g_tick; }

static int delay_total = 0;
void CanPlatform_DelayMs(uint32_t ms) { delay_total += (int)ms; }

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
    if (CanTwai_GetCtx(&can1) != NULL) { (void)CanLib_DeInit(&can1); }
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI) == CAN_OK);
    can1.cfg.mode        = mode;
    can1.hw.tx_gpio      = 5;
    can1.hw.rx_gpio      = 4;
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

    EXPECT(CanTwai_CalcTiming(80000000UL, 500000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 500000UL);
    EXPECT((t.prescaler % 2U) == 0U);
    EXPECT(t.tseg1 <= TWAI_LIB_TSEG1_MAX && t.tseg2 <= TWAI_LIB_TSEG2_MAX);
    EXPECT(((1U + t.tseg1) * 1000U) / (1U + t.tseg1 + t.tseg2) >= 850U);

    EXPECT(CanTwai_CalcTiming(80000000UL, 1000000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 1000000UL);

    EXPECT(CanTwai_CalcTiming(80000000UL, 125000UL, 875U, &t) == CAN_OK);
    EXPECT(t.actual_bitrate == 125000UL);
    EXPECT(t.prescaler <= CAN_CFG_TWAI_BRP_MAX);

    EXPECT(CanTwai_CalcTiming(80000000UL, 25000UL, 875U, &t) == CAN_OK);       /* Slowest common rate fits brp 128. */
    EXPECT(t.actual_bitrate == 25000UL);

    EXPECT(CanTwai_CalcTiming(80000000UL, 7U, 875U, &t) == CAN_ERR_BITRATE);
    EXPECT(CanTwai_CalcTiming(0U, 500000UL, 875U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanTwai_CalcTiming(80000000UL, 500000UL, 0U, &t) == CAN_ERR_BAD_PARAM);
    EXPECT(CanTwai_CalcTiming(80000000UL, 500000UL, 875U, NULL) == CAN_ERR_NULL_PTR);
}

static void test_init(void)
{
    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    EXPECT(fake.installed);
    EXPECT(fake.g.mode == TWAI_MODE_NORMAL);
    EXPECT(fake.g.tx_io == 5 && fake.g.rx_io == 4);
    EXPECT(fake.g.tx_queue_len == CAN_CFG_TWAI_TX_QUEUE_LEN);
    EXPECT(fake.g.rx_queue_len == CAN_CFG_TWAI_RX_QUEUE_LEN);
    EXPECT(fake.f.acceptance_mask == 0xFFFFFFFFUL);     /* Accept all. */
    EXPECT(fake.t.brp == can1.cfg.nominal.prescaler);
    EXPECT(fake.t.tseg_1 == can1.cfg.nominal.tseg1);
    EXPECT(fake.t.tseg_2 == can1.cfg.nominal.tseg2);
    EXPECT(fake.t.sjw >= 1U);
    EXPECT(can1.cfg.nominal.actual_bitrate == 500000UL);
    EXPECT(CanTwai_GetCtx(&can1) != NULL);

    EXPECT(bring_up(CANLIB_MODE_LISTEN_ONLY) == CAN_OK);
    EXPECT(fake.g.mode == TWAI_MODE_LISTEN_ONLY);
    EXPECT(bring_up(CANLIB_MODE_LOOPBACK) == CAN_OK);
    EXPECT(fake.g.mode == TWAI_MODE_NO_ACK);

    /* Second instance is refused while the first one is installed. */
    {
        static CanLib_t can2;
        EXPECT(CanLib_SetDefaults(&can2, "can2", CAN_BACKEND_ESP32_TWAI) == CAN_OK);
        can2.instance = 1U;
        can2.hw.tx_gpio = 1;
        can2.hw.rx_gpio = 2;
        EXPECT(CanLib_Init(&can2) == CAN_ERR_BUSY);
    }

    /* Errors. */
    (void)CanLib_DeInit(&can1);
    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BAD_PARAM);    /* GPIOs not set. */

    fake_reset();
    fake.fail_install = true;
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI) == CAN_OK);
    can1.hw.tx_gpio = 5; can1.hw.rx_gpio = 4;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_FAIL);
    EXPECT(CanTwai_GetCtx(&can1) == NULL);

    fake_reset();
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI) == CAN_OK);
    can1.hw.tx_gpio = 5; can1.hw.rx_gpio = 4;
    can1.cfg.nominal.bitrate = 7U;
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BITRATE);
}

static void test_start_stop(void)
{
    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(fake.state == TWAI_STATE_RUNNING);
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(fake.state == TWAI_STATE_STOPPED);

    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    fake.fail_start = true;
    EXPECT(CanLib_Start(&can1) == CAN_ERR_BACKEND_FAIL);
    EXPECT(CanTwai_GetCtx(&can1)->driver_failures == 1U);

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
    EXPECT(!fake.installed);
    EXPECT(CanTwai_GetCtx(&can1) == NULL);
}

static void test_transmit(void)
{
    uint8_t    data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CanFrame_t f;

    start_ok(CANLIB_MODE_NORMAL);

    EXPECT(CanLib_Send(&can1, 0x123U, data, 3U) == CAN_OK);
    EXPECT(fake.last_tx.identifier == 0x123U);
    EXPECT((fake.last_tx.flags & (TWAI_MSG_FLAG_EXTD | TWAI_MSG_FLAG_RTR | TWAI_MSG_FLAG_SS | TWAI_MSG_FLAG_SELF)) == 0U);
    EXPECT(fake.last_tx.data_length_code == 3U);
    EXPECT(fake.last_tx.data[2] == 3U);

    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF0UL, data, 8U) == CAN_OK);
    EXPECT(fake.last_tx.identifier == 0x1ABCDEF0UL);
    EXPECT((fake.last_tx.flags & TWAI_MSG_FLAG_EXTD) != 0U);

    EXPECT(CanLib_SendRemote(&can1, 0x55U, CAN_ID_STANDARD, 2U) == CAN_OK);
    EXPECT((fake.last_tx.flags & TWAI_MSG_FLAG_RTR) != 0U);
    EXPECT(fake.last_tx.data_length_code == 2U);

    /* Driver queue full: backend reports it, core queues the frame, Process resends. */
    fake.tx_queue_full = true;
    memset(&f, 0, sizeof(f));
    EXPECT(g_CanPortEsp32Twai.transmit(&can1, &f) == CAN_ERR_TX_FULL);
    EXPECT(!CAN_IS_ERR(CanLib_Send(&can1, 0x200U, data, 1U)));
    EXPECT(can1.tx.queue.count == 1U);
    fake.tx_queue_full = false;
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(can1.tx.queue.count == 0U);
    EXPECT(fake.last_tx.identifier == 0x200U);

    /* Controller stopped: bad state. */
    fake.state = TWAI_STATE_STOPPED;
    EXPECT(g_CanPortEsp32Twai.transmit(&can1, &f) == CAN_ERR_BAD_STATE);
    fake.state = TWAI_STATE_RUNNING;

    /* FD refused. */
    f.header.format = CAN_FORMAT_FD;
    EXPECT(g_CanPortEsp32Twai.transmit(&can1, &f) == CAN_ERR_NOT_SUPPORTED);
}

static void test_one_shot_and_loopback(void)
{
    uint8_t    data[2] = {0x11, 0x22};
    CanFrame_t f;

    /* One-shot sets the SS flag. */
    EXPECT(bring_up(CANLIB_MODE_NORMAL) == CAN_OK);
    can1.cfg.auto_retransmit = false;
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 2U) == CAN_OK);
    EXPECT((fake.last_tx.flags & TWAI_MSG_FLAG_SS) != 0U);

    /* Loopback sets SELF and the frame comes back. */
    start_ok(CANLIB_MODE_LOOPBACK);
    EXPECT(CanLib_Send(&can1, 0x321U, data, 2U) == CAN_OK);
    EXPECT((fake.last_tx.flags & TWAI_MSG_FLAG_SELF) != 0U);
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x321U);
    EXPECT(f.body.data[1] == 0x22U);
}

static void test_receive(void)
{
    uint8_t    d[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    CanFrame_t f;

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_Receive(&can1, &f) == CAN_ERR_RX_EMPTY);

    g_tick = 999U;
    fake_push_rx(0x7FFU, 0U, 8U, d);
    fake_push_rx(0x1234567UL, TWAI_MSG_FLAG_EXTD, 4U, d);
    fake_push_rx(0x10U, TWAI_MSG_FLAG_RTR, 3U, NULL);
    fake_push_rx(0x20U, 0U, 12U, d);                    /* DLC above 8 means 8 bytes. */
    EXPECT(!CAN_IS_ERR(CanLib_Process(&can1)));
    EXPECT(CanLib_Available(&can1) == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x7FFU && f.header.id_type == CAN_ID_STANDARD);
    EXPECT(f.header.data_len == 8U && memcmp(f.body.data, d, 8U) == 0);
    EXPECT(f.header.timestamp_ms == 999U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.id == 0x1234567UL && f.header.id_type == CAN_ID_EXTENDED);
    EXPECT(f.header.data_len == 4U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.frame_type == CAN_FRAME_REMOTE);
    EXPECT(f.header.dlc == 3U && f.header.data_len == 0U);

    EXPECT(CanLib_Receive(&can1, &f) == CAN_OK);
    EXPECT(f.header.data_len == 8U && f.header.dlc == 12U);
}

static void test_bus_state(void)
{
    uint8_t data[1] = {1};

    start_ok(CANLIB_MODE_NORMAL);

    fake.status.tx_error_counter = 100U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.tec == 100U);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_WARNING);

    fake.status.rx_error_counter = 130U;
    fake.status.bus_error_count  = 7U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.rec == 130U);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);
    EXPECT(CanTwai_GetCtx(&can1)->bus_errors == 7U);

    /* Missed + overrun frames are added to rx_dropped once (counters only grow). */
    fake.status.rx_missed_count  = 2U;
    fake.status.rx_overrun_count = 1U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 3U);
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 3U);
    fake.status.rx_missed_count = 5U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.stats.rx_dropped == 6U);

    /* Counters saturate at 255 for the uint8_t fields. */
    fake.status.tx_error_counter = 300U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.bus.tec == 255U);

    /* Bus-off, then recovery which takes a few polls. */
    g_tick = 5000U;
    fake.state = TWAI_STATE_BUS_OFF;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_ERR_BUS_OFF);

    fake.recovery_polls = 4;
    fake.status.tx_error_counter = 0U;
    fake.status.rx_error_counter = 0U;
    g_tick = 5000U + can1.cfg.bus_off_recover_ms + 1U;
    delay_total = 0;
    (void)CanLib_Process(&can1);
    EXPECT(fake.recovery_calls == 1);
    EXPECT(fake.state == TWAI_STATE_RUNNING);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
    EXPECT(delay_total > 0 && delay_total < (int)CAN_CFG_TWAI_RECOVER_WAIT_MS);
    EXPECT(CanLib_Send(&can1, 0x10U, data, 1U) == CAN_OK);

    /* Recovery that never finishes: error, the core tries again later. */
    fake.state = TWAI_STATE_BUS_OFF;
    g_tick += 10U;
    (void)CanLib_Process(&can1);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    fake.recovery_polls = 100000;
    g_tick += can1.cfg.bus_off_recover_ms + 1U;
    EXPECT(CanLib_Process(&can1) == CAN_ERR_BUSY);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(can1.status.last_error == CAN_ERR_BUSY);
}

static void test_xcvr(void)
{
    int dummy = 0;

    start_ok(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_ERR_NOT_SUPPORTED);
    can1.hw.xcvr_port = &dummy;
    can1.hw.xcvr_pin  = 18U;
    can1.hw.xcvr_standby_level = true;
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_SILENT) == CAN_OK);
    EXPECT(pin_level == true);
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL) == CAN_OK);
    EXPECT(pin_level == false && pin_calls == 2);
}

int main(void)
{
    test_timing();
    test_init();
    test_start_stop();
    test_transmit();
    test_one_shot_and_loopback();
    test_receive();
    test_bus_state();
    test_xcvr();

    if (fails == 0)
    {
        printf("test_twai: ALL PASSED\n");
        return 0;
    }
    printf("test_twai: %d FAILED\n", fails);
    return 1;
}
