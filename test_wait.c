/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_wait.c
 * @brief   Host-side test of CanLib_ReceiveWait() without FreeRTOS (not part of the library).
 *
 * Build with the VIRTUAL backend enabled and CAN_CFG_USE_FREERTOS 0.
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"

static int fails = 0;
#define EXPECT(cond) \
    do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

/* Every read of the clock advances it by 1 ms, so waiting loops always end. */
static uint32_t g_tick = 0U;
uint32_t CanLib_GetTickMs(void)
{
    return g_tick++;
}

static CanLib_t can1;

int main(void)
{
    CanFrame_t f;
    uint8_t    d[2] = {5, 6};
    uint32_t   t0;

    EXPECT(CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_VIRTUAL) == CAN_OK);
    can1.cfg.mode = CANLIB_MODE_LOOPBACK;
    EXPECT(CanLib_Init(&can1) == CAN_OK);

    EXPECT(CanLib_ReceiveWait(&can1, &f, 10U) != CAN_OK);           /* Not started: error, no hang. */
    EXPECT(CanLib_Start(&can1) == CAN_OK);

    /* Nothing to receive: times out after about the requested time. */
    t0 = g_tick;
    EXPECT(CanLib_ReceiveWait(&can1, &f, 20U) == CAN_ERR_RX_EMPTY);
    EXPECT((g_tick - t0) >= 20U);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 0U) == CAN_ERR_RX_EMPTY);

    /* The frame is in the controller only: the wait loop runs the process loop and finds it. */
    EXPECT(CanLib_Send(&can1, 0x123U, d, 2U) == CAN_OK);
    EXPECT(CanLib_ReceiveWait(&can1, &f, 50U) == CAN_OK);
    EXPECT(f.header.id == 0x123U && f.body.data[1] == 6U);

    /* NULL checks. */
    EXPECT(CanLib_ReceiveWait(NULL, &f, 10U) == CAN_ERR_NULL_PTR);
    EXPECT(CanLib_ReceiveWait(&can1, NULL, 10U) == CAN_ERR_NULL_PTR);

    if (fails == 0) { printf("test_wait: ALL PASSED\n"); return 0; }
    printf("test_wait: %d FAILED\n", fails);
    return 1;
}
