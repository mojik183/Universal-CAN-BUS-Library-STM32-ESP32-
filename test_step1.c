/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    test_step1.c
 * @brief   Host-side check of the step 1 foundation (not part of the library).
 */
#include <stdio.h>
#include <string.h>
#include "can_lib.h"

static CanLib_t can1;   /* One instance, as a user would declare it. */

/* Always fails with TX_FULL: tests CAN_RETURN_ERR. */
static CanResult_e demo_fail(CanLib_t *c)
{
    CAN_RETURN_ERR(c, CAN_ERR_TX_FULL);
}

/* Tests CAN_CHECK_INIT and CAN_CHECK_PARAM. */
static CanResult_e demo_check(CanLib_t *c, int x)
{
    CAN_CHECK_INIT(c);
    CAN_CHECK_PARAM(c, x > 0);
    return CAN_OK;
}

#define EXPECT(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

int main(void)
{
    int fails = 0;

    memset(&can1, 0, sizeof(can1));
    can1.name = "can1";
    can1.debug.log_level = CAN_LOG_DEBUG;

    EXPECT(demo_check(&can1, 1) == CAN_ERR_NOT_INIT);   /* Not initialized yet. */
    can1.magic = CAN_LIB_MAGIC;
#if CAN_CFG_ENABLE_PARAM_CHECK
    EXPECT(demo_check(&can1, 0) == CAN_ERR_BAD_PARAM);
#else
    EXPECT(demo_check(&can1, 0) == CAN_OK);
#endif
    EXPECT(demo_check(&can1, 1) == CAN_OK);
    EXPECT(demo_check(NULL, 1) == CAN_ERR_NULL_PTR);
    EXPECT(demo_fail(&can1) == CAN_ERR_TX_FULL);

    EXPECT(can1.status.last_error == CAN_ERR_TX_FULL);
    EXPECT(g_CanLastError == CAN_ERR_TX_FULL);

    CAN_WARN(&can1, CAN_WARN_RX_QUEUE_NEAR_FULL);
    EXPECT(can1.status.last_warning == CAN_WARN_RX_QUEUE_NEAR_FULL);
    EXPECT(can1.status.warning_count == 1U);
    EXPECT(CAN_IS_WARN(can1.status.last_warning));
    EXPECT(CAN_IS_ERR(can1.status.last_error));

#if CAN_CFG_ENABLE_DEBUG
    EXPECT(can1.debug.last_error_line != 0U);
    EXPECT(can1.debug.last_error_file != NULL);
  #if CAN_CFG_DEBUG_EVENT_LOG_SIZE > 0
    EXPECT(can1.debug.log_count >= 2U);
  #endif
#endif

    CAN_LOGI(&can1, "hello from log macro %d", 5);
    CAN_LOGE(&can1, "error log macro");

    printf("result string : %s\n", CanLib_ResultToStr(CAN_ERR_BUS_OFF));
    printf("version       : %s\n", CAN_LIB_VERSION_STR);
    printf("sizeof(CanLib_t) = %lu bytes\n", (unsigned long)sizeof(CanLib_t));

    CanDbg_Clear(&can1);
    EXPECT(can1.status.error_count == 0U);
    EXPECT(can1.status.last_error == CAN_OK);

    printf(fails == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", fails);
    return fails;
}
