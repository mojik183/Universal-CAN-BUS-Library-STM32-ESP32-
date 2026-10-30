/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_rtos.c
 * @brief   Host-side tests of the FreeRTOS layer with a pthread based fake FreeRTOS (not part of the library).
 *
 * Build with CAN_CFG_USE_FREERTOS 1, the VIRTUAL backend enabled, -Ifreertos_stub,
 * freertos_stub/freertos_fake.c, -pthread, and
 *   -D'CAN_CFG_RTOS_IN_ISR()=(fake_in_isr)'
 * Run it under ThreadSanitizer to check the locking.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include "can_lib.h"
#include "FreeRTOS.h"
#include "task.h"

#if !CAN_CFG_USE_FREERTOS || !CAN_CFG_ENABLE_BACKEND_VIRTUAL
    #error "test_rtos.c needs CAN_CFG_USE_FREERTOS 1 and the VIRTUAL backend"
#endif

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* ---- real time base ---- */

uint32_t CanLib_GetTickMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U);
}

static void sleep_us(long us)
{
    struct timespec ts;
    ts.tv_sec  = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static CanLib_t can1;

static void bring_up(void)
{
    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cfg.mode = CANLIB_MODE_LOOPBACK;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);
}

static void tear_down(void)
{
    EXPECT(CanLib_DeInit(&can1) == CAN_OK);
}

/* ======================================================================== */
/* TESTS                                                                    */
/* ======================================================================== */

static void test_life_cycle(void)
{
    EXPECT(fake_task_count() == 0);
    bring_up();
    EXPECT(can1.rtos.mutex != NULL);
    EXPECT(can1.rtos.rx_sem != NULL);
#if CAN_CFG_RTOS_AUTO_TASK
    EXPECT(can1.rtos.task != NULL);
    EXPECT(fake_task_count() == 1);
    sleep_us(50000);
    CanRtos_Lock(&can1);                                /* Reading task-written values: take the lock. */
    EXPECT(can1.rtos.wake_count >= 2U);                 /* The task wakes on its poll time. */
    EXPECT(can1.rtos.stack_free_words == 100U);
    CanRtos_Unlock(&can1);
#else
    EXPECT(can1.rtos.task == NULL);
    EXPECT(fake_task_count() == 0);
#endif
    tear_down();
    EXPECT(can1.rtos.mutex == NULL);
    EXPECT(can1.rtos.rx_sem == NULL);
    EXPECT(fake_task_count() == 0);

    /* Init / DeInit can be repeated. */
    bring_up();
    tear_down();
    EXPECT(fake_task_count() == 0);
}

static void test_receive_wait(void)
{
    CanFrame_t f;
    uint8_t    data[2] = {0xA1, 0xB2};
    uint32_t   t0;
    uint32_t   dt;

    bring_up();

    /* Timeout with nothing to receive. */
    t0 = CanLib_GetTickMs();
    EXPECT(CanLib_ReceiveWait(&can1, &f, 60U) == CAN_ERR_RX_EMPTY);
    dt = CanLib_GetTickMs() - t0;
    EXPECT(dt >= 55U && dt < 400U);

    /* Zero timeout: one try. */
    t0 = CanLib_GetTickMs();
    EXPECT(CanLib_ReceiveWait(&can1, &f, 0U) == CAN_ERR_RX_EMPTY);
    EXPECT((CanLib_GetTickMs() - t0) < 20U);

    /* A frame arrives while the caller sleeps. */
    EXPECT(CanLib_Send(&can1, 0x123U, data, 2U) == CAN_OK);
    CanRtos_NotifyFromISR(&can1);                       /* Like the chip INT pin: wake the library task. */
    EXPECT(CanLib_ReceiveWait(&can1, &f, 500U) == CAN_OK);
    EXPECT(f.header.id == 0x123U && f.body.data[1] == 0xB2U);

    /* Not running: error is returned, not a hang. */
    EXPECT(CanLib_Stop(&can1) == CAN_OK);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 50U) != CAN_OK);

    tear_down();
}

/** Thread that sends after a delay (tests that a sleeping receiver is woken). */
static void *late_sender(void *arg)
{
    uint8_t d[1] = {7};
    (void)arg;
    sleep_us(40000);
    (void)CanLib_Send(&can1, 0x55U, d, 1U);
    CanRtos_NotifyFromISR(&can1);
    return NULL;
}

static void test_wake_up(void)
{
    pthread_t  th;
    CanFrame_t f;
    uint32_t   t0;

    bring_up();
    pthread_create(&th, NULL, late_sender, NULL);
    t0 = CanLib_GetTickMs();
    EXPECT(CanLib_ReceiveWait(&can1, &f, CAN_WAIT_FOREVER) == CAN_OK);
    EXPECT(f.header.id == 0x55U);
    EXPECT((CanLib_GetTickMs() - t0) < 500U);
    pthread_join(th, NULL);
    tear_down();
}

/* ---- stress: several senders, one receiver ---- */

#define SENDERS         4
#define PER_SENDER      150

static int g_sent_ok[SENDERS];   /* One writer per slot, read after join. */

static void *sender_main(void *arg)
{
    int     id = (int)(long)arg;
    uint8_t d[2];
    int     i;

    for (i = 0; i < PER_SENDER; i++)
    {
        d[0] = (uint8_t)id;
        d[1] = (uint8_t)i;
        for (;;)
        {
            CanResult_e r = CanLib_Send(&can1, 0x100U + (uint32_t)id, d, 2U);
            if (!CAN_IS_ERR(r)) { g_sent_ok[id]++; break; }
            sleep_us(200);                              /* Queue full: wait and retry. */
        }
        CanRtos_NotifyFromISR(&can1);
        sleep_us(300);                                  /* Pace the senders so the 16 slot buffers keep up. */
    }
    return NULL;
}

static int g_received;      /* Written by the receiver thread only, read after join. */
static int g_recv_done;     /* Accessed with __atomic builtins. */
static int g_last_seq[SENDERS];
static int g_order_errors;

static void *receiver_main(void *arg)
{
    CanFrame_t f;
    (void)arg;
    while (!__atomic_load_n(&g_recv_done, __ATOMIC_ACQUIRE))
    {
        if (CanLib_ReceiveWait(&can1, &f, 20U) == CAN_OK)
        {
            int id  = (int)(f.header.id - 0x100U);
            int seq = f.body.data[1];
            if ((id < 0) || (id >= SENDERS) || (f.body.data[0] != (uint8_t)id)) { g_order_errors++; continue; }
            if (seq <= g_last_seq[id]) { g_order_errors++; }        /* Frames of one sender stay in order. */
            g_last_seq[id] = seq;
            g_received++;
        }
    }
    return NULL;
}

static void test_stress(void)
{
    pthread_t snd[SENDERS];
    pthread_t rcv;
    int       i;
    int       total_sent = 0;
    uint32_t  lost;

    bring_up();
    for (i = 0; i < SENDERS; i++) { g_last_seq[i] = -1; g_sent_ok[i] = 0; }
    g_received = 0; __atomic_store_n(&g_recv_done, 0, __ATOMIC_RELEASE); g_order_errors = 0;

    pthread_create(&rcv, NULL, receiver_main, NULL);
    for (i = 0; i < SENDERS; i++) { pthread_create(&snd[i], NULL, sender_main, (void *)(long)i); }
    for (i = 0; i < SENDERS; i++) { pthread_join(snd[i], NULL); }
    sleep_us(200000);                                   /* Let the receiver drain. */
    __atomic_store_n(&g_recv_done, 1, __ATOMIC_RELEASE);
    pthread_join(rcv, NULL);

    for (i = 0; i < SENDERS; i++) { total_sent += g_sent_ok[i]; }
    CanRtos_Lock(&can1);
    lost = can1.rx.queue.dropped;
    printf("  stress: sent %d, received %d, dropped %u, high water rx %u\n",
           total_sent, g_received, (unsigned)lost, (unsigned)can1.rx.queue.high_water);
    CanRtos_Unlock(&can1);
    EXPECT(total_sent == SENDERS * PER_SENDER);
    EXPECT(g_received + (int)lost == total_sent);       /* Nothing disappears silently. */
    EXPECT(g_order_errors == 0);
    tear_down();
}

/* ---- interrupt context ---- */

static void *isr_thread(void *arg)
{
    CanFrame_t f;
    uint8_t    d[1] = {9};
    CanResult_e r;

    (void)arg;
    fake_in_isr = 1;

    /* The hand-over function works in an interrupt. */
    memset(&f, 0, sizeof(f));
    f.header.id = 0x77U;
    f.header.dlc = 1U;
    f.header.data_len = 1U;
    f.body.data[0] = 9U;
    CanLib_OnRxFrame(&can1, &f);
    CanRtos_NotifyFromISR(&can1);

    /* Other library calls are refused in an interrupt (not allowed, would block). */
    r = CanLib_Send(&can1, 0x10U, d, 1U);
    EXPECT(r == CAN_ERR_MUTEX);
    fake_in_isr = 0;
    return NULL;
}

static void test_isr(void)
{
    pthread_t  th;
    CanFrame_t f;

    bring_up();
    pthread_create(&th, NULL, isr_thread, NULL);
    pthread_join(th, NULL);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 200U) == CAN_OK);
    EXPECT(f.header.id == 0x77U && f.body.data[0] == 9U);
    tear_down();
}

/* ---- mutex timeout ---- */

static void *holder_thread(void *arg)
{
    (void)arg;
    EXPECT(CanRtos_Lock(&can1) == CAN_OK);
    sleep_us(150000);                                   /* Longer than CAN_CFG_RTOS_MUTEX_TIMEOUT_MS. */
    CanRtos_Unlock(&can1);
    return NULL;
}

static void test_lock_timeout(void)
{
    pthread_t th;
    uint8_t   d[1] = {1};

    bring_up();
    pthread_create(&th, NULL, holder_thread, NULL);
    sleep_us(30000);                                    /* Let the holder take the lock. */
    EXPECT(CanLib_Send(&can1, 0x10U, d, 1U) == CAN_ERR_MUTEX);
    pthread_join(th, NULL);
    EXPECT(can1.rtos.lock_timeouts >= 1U);              /* Thread is joined: no concurrent access now. */
    EXPECT(can1.status.last_error == CAN_ERR_MUTEX);
    EXPECT(CanLib_Send(&can1, 0x10U, d, 1U) == CAN_OK); /* Works again once the lock is free. */
    tear_down();
}

/* ---- callback that calls the library (recursive lock) ---- */

static int g_reply_sent;         /* Written in the library task, read after the frames arrived. */

static void on_rx_reply(CanLib_t *c, const CanFrame_t *f, void *ctx)
{
    uint8_t d[1] = {0};
    (void)ctx;
    if (f->header.id == 0x300U)
    {
        d[0] = 0x44U;
        if (!CAN_IS_ERR(CanLib_Send(c, 0x301U, d, 1U))) { g_reply_sent++; }   /* Same task: recursive mutex. */
    }
}

static void test_callback_recursion(void)
{
    CanFrame_t f;
    uint8_t    d[1] = {1};

    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cfg.mode = CANLIB_MODE_LOOPBACK;
    can1.cb.on_rx = on_rx_reply;
    EXPECT(CanLib_Init(&can1) == CAN_OK);
    EXPECT(CanLib_Start(&can1) == CAN_OK);

    g_reply_sent = 0;
    EXPECT(CanLib_Send(&can1, 0x300U, d, 1U) == CAN_OK);
    CanRtos_NotifyFromISR(&can1);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 300U) == CAN_OK);          /* The original frame. */
    CanRtos_NotifyFromISR(&can1);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 300U) == CAN_OK);          /* The reply sent from the callback. */
    EXPECT(f.header.id == 0x301U && f.body.data[0] == 0x44U);
    EXPECT(g_reply_sent == 1);
    tear_down();
}

int main(void)
{
    test_life_cycle();
    test_receive_wait();
    test_wake_up();
    test_stress();
    test_isr();
    test_lock_timeout();
    test_callback_recursion();

    if (fails == 0)
    {
        printf("test_rtos: ALL PASSED\n");
        return 0;
    }
    printf("test_rtos: %d FAILED\n", fails);
    return 1;
}
