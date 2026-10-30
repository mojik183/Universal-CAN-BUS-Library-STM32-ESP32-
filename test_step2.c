/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_step2.c
 * @brief   Host-side tests of the core, using the virtual backend (not part of the library).
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"

#if !CAN_CFG_ENABLE_BACKEND_VIRTUAL
    #error "test_step2.c needs CAN_CFG_ENABLE_BACKEND_VIRTUAL 1 in can_cfg.h"
#endif

/* Controllable clock: overrides the weak CanLib_GetTickMs(). */
static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void)
{
    return g_tick;
}

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

static CanLib_t can1;

#if CAN_CFG_ENABLE_CALLBACKS
static int        cb_rx_count   = 0;
static int        cb_event_count = 0;
static CanEvent_e cb_last_event = CAN_EVENT_TX_DONE;

static void on_rx(CanLib_t *c, const CanFrame_t *f, void *ctx)
{
    (void)c; (void)f; (void)ctx;
    cb_rx_count++;
}

static void on_event(CanLib_t *c, CanEvent_e ev, int32_t arg, void *ctx)
{
    (void)c; (void)arg; (void)ctx;
    cb_event_count++;
    cb_last_event = ev;
}
#endif

/** Fresh instance, initialized and started. */
static void bring_up(CanMode_e mode)
{
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cfg.mode        = mode;
    can1.debug.log_level = CAN_LOG_DEBUG;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_READY);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_RUNNING);
}

static CanFrame_t make_frame(uint32_t id, uint8_t len)
{
    CanFrame_t f;
    uint8_t    i;

    memset(&f, 0, sizeof(f));
    f.header.id       = id;
    f.header.id_type  = CAN_ID_STANDARD;
    f.header.data_len = len;
    f.header.dlc      = len;
    for (i = 0U; (i < len) && (i < CAN_MAX_DATA_LEN); i++)
    {
        f.body.data[i] = (uint8_t)(0xA0U + i);
    }
    return f;
}

/* ------------------------------------------------------------------------ */

static void test_helpers(void)
{
    EXPECT(CanLib_LenToDlc(0) == 0);
    EXPECT(CanLib_LenToDlc(8) == 8);
    EXPECT(CanLib_LenToDlc(9) == 9);
    EXPECT(CanLib_LenToDlc(20) == 11);
    EXPECT(CanLib_LenToDlc(64) == 15);
    EXPECT(CanLib_LenToDlc(200) == 15);
    EXPECT(CanLib_DlcToLen(9, CAN_FORMAT_FD) == 12);
    EXPECT(CanLib_DlcToLen(9, CAN_FORMAT_CLASSIC) == 8);
    EXPECT(CanLib_DlcToLen(15, CAN_FORMAT_FD) == 64);
}

static void test_init_errors(void)
{
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_NONE) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BAD_PARAM);

#if !CAN_CFG_ENABLE_BACKEND_STM32_BXCAN
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN) == CAN_OK);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BACKEND_DISABLED);
    EXPECT(can1.status.last_error == CAN_ERR_BACKEND_DISABLED);
#endif

    bring_up(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_Init(&can1) == CAN_ERR_BAD_STATE);        /* Second init. */
    EXPECT(CanLib_Start(&can1) == CAN_ERR_BAD_STATE);       /* Already running. */
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_STOPPED);
    EXPECT(CanLib_Send(&can1, 0x1, NULL, 0) == CAN_ERR_BAD_STATE);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
    EXPECT(can1.status.state == CAN_STATE_UNINIT);
    EXPECT(CanLib_Send(&can1, 0x1, NULL, 0) == CAN_ERR_NOT_INIT);
    EXPECT(CanLib_Process(&can1) == CAN_ERR_NOT_INIT);
    EXPECT(CanLib_Send(NULL, 0x1, NULL, 0) == CAN_ERR_NULL_PTR);
}

static void test_send(void)
{
    uint8_t d[16] = { 1, 2, 3, 4, 5, 6, 7, 8 };      /* 16 bytes so the "too long" test reads valid memory. */
    const CanVirtualCtx_t *v;

    bring_up(CANLIB_MODE_NORMAL);
    v = CanVirtual_GetCtx(&can1);
    EXPECT(v != NULL);

    EXPECT(CanLib_Send(&can1, 0x123, d, 8) == CAN_OK);
    EXPECT(v->sent_count == 1U);
    EXPECT(v->last_sent.header.id == 0x123U);
    EXPECT(v->last_sent.header.dlc == 8U);
    EXPECT(v->last_sent.header.data_len == 8U);
    EXPECT(v->last_sent.header.id_type == CAN_ID_STANDARD);
    EXPECT(memcmp(v->last_sent.body.data, d, 8) == 0);
#if CAN_CFG_ENABLE_STATS
    EXPECT(can1.stats.tx_ok == 1U);
#endif

    /* Too long for classic: cut to 8 with a warning. */
    EXPECT(CanLib_Send(&can1, 0x124, d, 12) == CAN_WARN_DLC_CLAMPED);
    EXPECT(v->last_sent.header.data_len == 8U);
    EXPECT(can1.status.last_warning == CAN_WARN_DLC_CLAMPED);

    /* Extended identifier. */
#if CAN_CFG_ENABLE_EXT_ID
    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF, d, 4) == CAN_OK);
    EXPECT(v->last_sent.header.id_type == CAN_ID_EXTENDED);
    EXPECT(v->last_sent.header.id == 0x1ABCDEFU);
#else
    EXPECT(CanLib_SendExt(&can1, 0x1ABCDEF, d, 4) == CAN_ERR_NOT_SUPPORTED);
#endif

    /* Remote frame. */
#if CAN_CFG_ENABLE_REMOTE_FRAMES
    EXPECT(CanLib_SendRemote(&can1, 0x55, CAN_ID_STANDARD, 4) == CAN_OK);
    EXPECT(v->last_sent.header.frame_type == CAN_FRAME_REMOTE);
    EXPECT(v->last_sent.header.dlc == 4U);
#else
    EXPECT(CanLib_SendRemote(&can1, 0x55, CAN_ID_STANDARD, 4) == CAN_ERR_NOT_SUPPORTED);
#endif

    /* Bad arguments. */
    EXPECT(CanLib_Send(&can1, 0x1, NULL, 4) == CAN_ERR_NULL_PTR);
#if CAN_CFG_ENABLE_PARAM_CHECK
    EXPECT(CanLib_Send(&can1, 0x800, d, 1) == CAN_ERR_BAD_PARAM);
    EXPECT(can1.status.last_error == CAN_ERR_BAD_PARAM);
    EXPECT(CanLib_SendExt(&can1, 0x20000000UL, d, 1) == CAN_ERR_BAD_PARAM);
#endif
}

static void test_fd(void)
{
    uint8_t d[64];
    CanFrame_t f;
    const CanVirtualCtx_t *v;
    uint8_t i;

    for (i = 0U; i < 64U; i++)
    {
        d[i] = (uint8_t)(i + 1U);
    }

    /* FD requested while FD is not active: classic with a warning. */
    bring_up(CANLIB_MODE_NORMAL);
    v = CanVirtual_GetCtx(&can1);
    f = make_frame(0x200, 4);
    f.header.format = CAN_FORMAT_FD;
    f.header.bitrate_switch = true;
    EXPECT(CanLib_SendFrame(&can1, &f) == CAN_WARN_FD_FALLBACK);
    EXPECT(v->last_sent.header.format == CAN_FORMAT_CLASSIC);
    EXPECT(v->last_sent.header.bitrate_switch == false);

#if CAN_CFG_ENABLE_FD
    {
        CanFrame_t big;

        EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
        can1.cfg.fd_enabled = true;
        EXPECT(CanLib_Init(&can1) == CAN_OK);
        EXPECT(CanLib_Start(&can1) == CAN_OK);
        v = CanVirtual_GetCtx(&can1);

        EXPECT(CanLib_SendFd(&can1, 0x2FF, d, 20, false) == CAN_OK);
        EXPECT(v->last_sent.header.data_len == 20U);        /* 20 is a valid FD length: unchanged. */
        EXPECT(v->last_sent.header.dlc == 11U);

        EXPECT(CanLib_SendFd(&can1, 0x300, d, 21, true) == CAN_OK);
        EXPECT(v->last_sent.header.format == CAN_FORMAT_FD);
        EXPECT(v->last_sent.header.data_len == 24U);        /* 21 is not valid: rounded up to 24. */
        EXPECT(v->last_sent.header.dlc == 12U);
        EXPECT(v->last_sent.header.bitrate_switch == true);
        EXPECT(v->last_sent.body.data[20] == 21U);          /* Last real byte. */
        EXPECT(v->last_sent.body.data[21] == 0U);           /* Zero padding. */
        EXPECT(v->last_sent.body.data[23] == 0U);

        EXPECT(CanLib_SendFd(&can1, 0x301, d, 64, false) == CAN_OK);
        EXPECT(v->last_sent.header.data_len == 64U);
        EXPECT(v->last_sent.header.dlc == 15U);

        big = make_frame(0x302, 64);
        big.header.format = CAN_FORMAT_FD;
        big.header.data_len = 200;
        EXPECT(CanLib_SendFrame(&can1, &big) == CAN_WARN_DLC_CLAMPED);
        EXPECT(v->last_sent.header.data_len == 64U);
    }
#else
    EXPECT(CanLib_SendFd(&can1, 0x300, d, 20, true) == CAN_ERR_NOT_SUPPORTED);
#endif
}

static void test_loopback_receive(void)
{
    uint8_t    d[4] = { 9, 8, 7, 6 };
    CanFrame_t r;

    bring_up(CANLIB_MODE_LOOPBACK);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_ERR_RX_EMPTY);
    EXPECT(can1.status.error_count == 0U);                  /* Empty is not an error. */

    EXPECT(CanLib_Send(&can1, 0x7FF, d, 4) == CAN_OK);
    EXPECT(CanLib_Available(&can1) == 0U);                  /* Not read from the chip yet. */
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(CanLib_Available(&can1) == 1U);

    EXPECT(can1.rx.new_frame);
    EXPECT(can1.rx.header.id == 0x7FFU);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK);
    EXPECT(r.header.id == 0x7FFU);
    EXPECT(r.header.data_len == 4U);
    EXPECT(memcmp(r.body.data, d, 4) == 0);
    EXPECT(r.header.filter_index == CAN_FILTER_NONE);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_ERR_RX_EMPTY);
    EXPECT(CanLib_Available(&can1) == 0U);
#if CAN_CFG_ENABLE_STATS
    EXPECT(can1.stats.rx_ok == 1U);
#endif
    EXPECT(CanLib_Receive(&can1, NULL) == CAN_ERR_NULL_PTR);
}

#if CAN_CFG_ENABLE_FILTERS
static void test_filters(void)
{
    CanFrame_t f;
    CanFrame_t r;
    int        got = 0;

    bring_up(CANLIB_MODE_NORMAL);
    EXPECT(CanLib_AddFilter(&can1, CAN_ID_STANDARD, 0x100, 0x7F0) == CAN_OK);     /* 0x100..0x10F */
    EXPECT(CanLib_AddFilterExact(&can1, CAN_ID_STANDARD, 0x300) == CAN_OK);
    EXPECT(can1.filter_count == 2U);

    f = make_frame(0x105, 2); EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);   /* match slot 0 */
    f = make_frame(0x200, 2); EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);   /* rejected */
    f = make_frame(0x10F, 2); EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);   /* match slot 0 */
    f = make_frame(0x300, 2); EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);   /* match slot 1 */
    f = make_frame(0x301, 2); EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);   /* rejected */
    EXPECT(CanLib_Process(&can1) == CAN_OK);

#if CAN_CFG_ENABLE_RX_QUEUE
    EXPECT(CanLib_Available(&can1) == 3U);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK); EXPECT(r.header.id == 0x105U); EXPECT(r.header.filter_index == 0U);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK); EXPECT(r.header.id == 0x10FU);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK); EXPECT(r.header.id == 0x300U); EXPECT(r.header.filter_index == 1U);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_ERR_RX_EMPTY);
    got = 3;
#else
    while (CanLib_Receive(&can1, &r) == CAN_OK) { got++; }
    EXPECT(got == 1);                                       /* Only the last accepted frame is kept. */
    EXPECT(can1.rx.header.id == 0x300U);
#endif
    EXPECT(got >= 1);
#if CAN_CFG_ENABLE_STATS
    EXPECT(can1.stats.rx_dropped == 2U);
#endif

    EXPECT(CanLib_ClearFilters(&can1) == CAN_OK);
    EXPECT(can1.filter_count == 0U);

    /* Fill the table. */
    {
        uint8_t i;
        for (i = 0U; i < CAN_CFG_MAX_FILTERS; i++)
        {
            EXPECT(CanLib_AddFilterExact(&can1, CAN_ID_STANDARD, i) == CAN_OK);
        }
        EXPECT(CanLib_AddFilterExact(&can1, CAN_ID_STANDARD, 0x7FF) == CAN_ERR_FILTER_FULL);
    }
}
#endif

#if CAN_CFG_ENABLE_TX_QUEUE
static void test_tx_queue(void)
{
    uint8_t d[1] = { 0x5A };
    const CanVirtualCtx_t *v;
    uint32_t i;
    bool warned = false;

    bring_up(CANLIB_MODE_NORMAL);
    v = CanVirtual_GetCtx(&can1);

    CanVirtual_SetTxFull(&can1, true);                      /* Hardware has no free mailbox. */
    for (i = 0U; i < CAN_CFG_TX_QUEUE_DEPTH; i++)
    {
        CanResult_e r = CanLib_Send(&can1, 0x400U + i, d, 1);
        EXPECT(!CAN_IS_ERR(r));
        if (r == CAN_WARN_TX_QUEUE_NEAR_FULL)
        {
            warned = true;
        }
    }
    EXPECT(warned);
    EXPECT(can1.tx.queue.count == CAN_CFG_TX_QUEUE_DEPTH);
    EXPECT(can1.status.tx_pending == CAN_CFG_TX_QUEUE_DEPTH);
    EXPECT(v->sent_count == 0U);

    EXPECT(CanLib_Send(&can1, 0x4FF, d, 1) == CAN_ERR_TX_FULL);     /* Queue overflow. */
    EXPECT(can1.status.last_error == CAN_ERR_TX_FULL);
    EXPECT(can1.tx.queue.dropped == 1U);

    CanVirtual_SetTxFull(&can1, false);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.tx.queue.count == 0U);
    EXPECT(can1.status.tx_pending == 0U);
    EXPECT(v->sent_count == CAN_CFG_TX_QUEUE_DEPTH);
    EXPECT(v->last_sent.header.id == (0x400U + CAN_CFG_TX_QUEUE_DEPTH - 1U));      /* Order kept. */
    EXPECT(can1.tx.queue.high_water == CAN_CFG_TX_QUEUE_DEPTH);
}
#endif

#if CAN_CFG_ENABLE_RX_QUEUE
static void test_rx_overflow(void)
{
    CanFrame_t f;
    CanFrame_t r;
    uint32_t   i;

    /* Drop-new policy. */
    bring_up(CANLIB_MODE_NORMAL);
    for (i = 0U; i < (CAN_CFG_RX_QUEUE_DEPTH + 3U); i++)
    {
        f = make_frame(0x500U + i, 1);
        CanLib_OnRxFrame(&can1, &f);
    }
    EXPECT(can1.rx.queue.count == CAN_CFG_RX_QUEUE_DEPTH);
    EXPECT(can1.rx.queue.dropped == 3U);
    EXPECT(can1.status.last_error == CAN_ERR_RX_OVERFLOW);
    EXPECT(can1.status.last_warning == CAN_WARN_RX_QUEUE_NEAR_FULL);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK);
    EXPECT(r.header.id == 0x500U);                          /* Oldest kept. */

    /* Overwrite-oldest policy. */
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cfg.rx_overwrite_oldest = true;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    for (i = 0U; i < (CAN_CFG_RX_QUEUE_DEPTH + 2U); i++)
    {
        f = make_frame(0x500U + i, 1);
        CanLib_OnRxFrame(&can1, &f);
    }
    EXPECT(can1.rx.queue.count == CAN_CFG_RX_QUEUE_DEPTH);
    EXPECT(CanLib_Receive(&can1, &r) == CAN_OK);
    EXPECT(r.header.id == 0x502U);                          /* Two oldest were replaced. */
    EXPECT(can1.rx.queue.high_water == CAN_CFG_RX_QUEUE_DEPTH);
}
#endif

#if CAN_CFG_ENABLE_MANUAL_SEND
static void test_manual_send(void)
{
    const CanVirtualCtx_t *v;

    bring_up(CANLIB_MODE_NORMAL);
    v = CanVirtual_GetCtx(&can1);

    /* Fill header and body (as one would in the watch window), then request. */
    can1.tx.header.id        = 0x321U;
    can1.tx.header.id_type   = CAN_ID_STANDARD;
    can1.tx.header.data_len  = 3U;
    can1.tx.body.data[0]     = 0x11;
    can1.tx.body.data[1]     = 0x22;
    can1.tx.body.data[2]     = 0x33;
    can1.tx.manual_request   = true;

    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(!can1.tx.manual_request);                        /* One request, one frame. */
    EXPECT(v->sent_count == 1U);
    EXPECT(v->last_sent.header.id == 0x321U);
    EXPECT(v->last_sent.header.dlc == 3U);
    EXPECT(v->last_sent.body.data[2] == 0x33U);

    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(v->sent_count == 1U);                            /* Not sent again. */

    EXPECT(CanLib_SendManual(&can1) == CAN_OK);             /* Direct call. */
    EXPECT(v->sent_count == 2U);
}
#endif

static void test_bus_state(void)
{
    bring_up(CANLIB_MODE_NORMAL);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);

    CanVirtual_SetErrorCounters(&can1, 100, 0);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_WARNING);
    EXPECT(can1.status.last_warning == CAN_WARN_ERROR_WARNING);
    EXPECT(can1.bus.tec == 100U);

    CanVirtual_SetErrorCounters(&can1, 0, 200);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_PASSIVE);
    EXPECT(can1.status.last_warning == CAN_WARN_ERROR_PASSIVE);

    CanVirtual_SetErrorCounters(&can1, 0, 0);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);

    /* Bus-off. */
    g_tick = 1000U;
    CanVirtual_SetBusOff(&can1, true);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    EXPECT(can1.status.last_error == CAN_ERR_BUS_OFF);
    EXPECT(can1.bus.bus_off_since_ms == 1000U);
#if CAN_CFG_ENABLE_STATS
    EXPECT(can1.stats.bus_off_count == 1U);
#endif
    EXPECT(CanLib_Send(&can1, 0x10, NULL, 0) == CAN_ERR_BUS_OFF);

#if CAN_CFG_ENABLE_BUS_OFF_RECOVERY
    g_tick = 1000U + (CAN_CFG_BUS_OFF_RECOVER_MS / 2U);     /* Too early. */
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);

    g_tick = 1000U + CAN_CFG_BUS_OFF_RECOVER_MS + 10U;      /* Wait is over. */
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_ERROR_ACTIVE);
    EXPECT(can1.status.last_warning == CAN_WARN_BUS_RECOVERED);
    EXPECT(CanLib_Send(&can1, 0x10, NULL, 0) == CAN_OK);

    /* Auto recovery off: stays bus-off. */
    can1.cfg.bus_off_auto_recover = false;
    CanVirtual_SetBusOff(&can1, true);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
    g_tick += 10U * CAN_CFG_BUS_OFF_RECOVER_MS;
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(can1.status.bus_state == CAN_BUS_OFF);
#endif
}

#if CAN_CFG_ENABLE_CALLBACKS
static void test_callbacks(void)
{
    CanFrame_t f;
    int        before;

    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cb.on_rx    = on_rx;
    can1.cb.on_event = on_event;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
    EXPECT(cb_event_count == 2);                            /* READY and RUNNING. */
    EXPECT(cb_last_event == CAN_EVENT_STATE_CHANGE);

    cb_rx_count = 0;
    f = make_frame(0x77, 1);
    EXPECT(CanVirtual_InjectRx(&can1, &f) == CAN_OK);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(cb_rx_count == 1);

    before = cb_event_count;
    EXPECT(CanLib_Send(&can1, 0x78, NULL, 0) == CAN_OK);
    EXPECT(cb_event_count == (before + 1));
    EXPECT(cb_last_event == CAN_EVENT_TX_DONE);

    CanVirtual_SetBusOff(&can1, true);
    EXPECT(CanLib_Process(&can1) == CAN_OK);
    EXPECT(cb_last_event == CAN_EVENT_BUS_OFF);
}
#endif

static void test_xcvr_and_debug(void)
{
    bring_up(CANLIB_MODE_NORMAL);
#if CAN_CFG_ENABLE_XCVR_CONTROL
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_OK);
    EXPECT(CanVirtual_GetCtx(&can1)->xcvr_mode == CAN_XCVR_MODE_STANDBY);
#else
    EXPECT(CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY) == CAN_ERR_NOT_SUPPORTED);
#endif

#if CAN_CFG_ENABLE_DEBUG && (CAN_CFG_DEBUG_EVENT_LOG_SIZE > 0)
    EXPECT(can1.debug.log_count >= 3U);                     /* init, ready, start, running ... */
    EXPECT(can1.debug.log[0].event == CAN_LOG_EV_INIT);
#endif
}

int main(void)
{
    test_helpers();
    test_init_errors();
    test_send();
    test_fd();
    test_loopback_receive();
#if CAN_CFG_ENABLE_FILTERS
    test_filters();
#endif
#if CAN_CFG_ENABLE_TX_QUEUE
    test_tx_queue();
#endif
#if CAN_CFG_ENABLE_RX_QUEUE
    test_rx_overflow();
#endif
#if CAN_CFG_ENABLE_MANUAL_SEND
    test_manual_send();
#endif
    test_bus_state();
#if CAN_CFG_ENABLE_CALLBACKS
    test_callbacks();
#endif
    test_xcvr_and_debug();

    printf("sizeof(CanLib_t) = %lu bytes\n", (unsigned long)sizeof(CanLib_t));
    printf(fails == 0 ? "ALL STEP 2 CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", fails);
    return (fails == 0) ? 0 : 1;
}
