/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_platform_stm32.c
 * @brief   STM32 HAL glue: millisecond tick, delay, GPIO pin, SPI transfer.
 *
 * Compiled only when CAN_CFG_PLATFORM is CAN_PLATFORM_STM32. The HAL header of
 * your family is set in can_cfg.h (CAN_CFG_STM32_HAL_HEADER).
 *
 * Needs the HAL modules for SPI and GPIO enabled in stm32xxxx_hal_conf.h.
 */
#include "can_lib.h"

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_STM32)

#include CAN_CFG_STM32_HAL_HEADER

/** Library time base: the HAL tick (1 ms). */
uint32_t CanLib_GetTickMs(void)
{
    return HAL_GetTick();
}

/** Blocking wait, used only during init and mode changes. */
void CanPlatform_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

/** Set a GPIO output pin. port is a GPIO_TypeDef*, pin a GPIO_PIN_x mask. */
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{
    if (port == NULL)
    {
        return CAN_ERR_NULL_PTR;
    }
    HAL_GPIO_WritePin((GPIO_TypeDef *)port, pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return CAN_OK;
}

#if CAN_CFG_ENABLE_BACKEND_MCP25XX || CAN_CFG_ENABLE_BACKEND_MCP251XFD
/** One SPI transfer framed by the chip-select pin. hw->handle is an SPI_HandleTypeDef*. */
CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    HAL_StatusTypeDef st;

    if ((hw == NULL) || (tx == NULL) || (rx == NULL) || (hw->handle == NULL) || (hw->cs_port == NULL))
    {
        return CAN_ERR_NULL_PTR;
    }

    HAL_GPIO_WritePin((GPIO_TypeDef *)hw->cs_port, hw->cs_pin, GPIO_PIN_RESET);     /* CS low */
    st = HAL_SPI_TransmitReceive((SPI_HandleTypeDef *)hw->handle, (uint8_t *)tx, rx, len,
                                 CAN_CFG_MCP_SPI_TIMEOUT_MS);
    HAL_GPIO_WritePin((GPIO_TypeDef *)hw->cs_port, hw->cs_pin, GPIO_PIN_SET);       /* CS high */

    return (st == HAL_OK) ? CAN_OK : CAN_ERR_SPI;
}
#endif

#endif /* CAN_PLATFORM_STM32 */
