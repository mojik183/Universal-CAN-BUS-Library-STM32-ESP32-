/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_port.c
 * @brief   Backend lookup table.
 */
#include "can_lib.h"

/* ---- weak platform defaults: the platform file replaces them ---- */

CAN_WEAK void CanPlatform_DelayMs(uint32_t ms)
{
    (void)ms;                                   /* No platform file linked: do not wait. */
}

CAN_WEAK CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{
    (void)port;
    (void)pin;
    (void)level;
    return CAN_ERR_NOT_SUPPORTED;               /* No platform file linked. */
}

CAN_WEAK CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    (void)hw;
    (void)tx;
    (void)rx;
    (void)len;
    return CAN_ERR_NOT_SUPPORTED;               /* No platform file linked. */
}

#if CAN_CFG_ENABLE_XCVR_CONTROL
CanResult_e CanPort_SetXcvrPin(const CanHwCfg_t *hw, CanXcvrMode_e mode)
{
    bool level;

    if (hw == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    if ((hw->xcvr_port == NULL) && (hw->xcvr_pin == 0U))
    {
        return CAN_ERR_NOT_SUPPORTED;           /* No mode pin wired / configured. */
    }
    /* Normal = opposite of the standby level; silent and standby use the standby level. */
    level = (mode == CAN_XCVR_MODE_NORMAL) ? !hw->xcvr_standby_level : hw->xcvr_standby_level;
    return CanPlatform_WritePin(hw->xcvr_port, hw->xcvr_pin, level);
}
#endif

const CanPortOps_t *CanPort_Get(CanBackend_e backend)
{
    switch (backend)
    {
#if CAN_CFG_ENABLE_BACKEND_STM32_BXCAN
        case CAN_BACKEND_STM32_BXCAN:   return &g_CanPortStm32Bxcan;
#endif
#if CAN_CFG_ENABLE_BACKEND_STM32_FDCAN
        case CAN_BACKEND_STM32_FDCAN:   return &g_CanPortStm32Fdcan;
#endif
#if CAN_CFG_ENABLE_BACKEND_ESP32_TWAI
        case CAN_BACKEND_ESP32_TWAI:    return &g_CanPortEsp32Twai;
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP25XX
        case CAN_BACKEND_MCP25XX:       return &g_CanPortMcp25xx;
#endif
#if CAN_CFG_ENABLE_BACKEND_MCP251XFD
        case CAN_BACKEND_MCP251XFD:     return &g_CanPortMcp251xfd;
#endif
#if CAN_CFG_ENABLE_BACKEND_VIRTUAL
        case CAN_BACKEND_VIRTUAL:       return &g_CanPortVirtual;
#endif
        default:                        return NULL;    /* Disabled or unknown backend. */
    }
}
