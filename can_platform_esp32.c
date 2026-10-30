/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * @file    can_platform_esp32.c
 * @brief   ESP-IDF glue: millisecond tick, delay, GPIO pin, SPI transfer.
 *
 * Compiled only when CAN_CFG_PLATFORM is CAN_PLATFORM_ESP32.
 *
 * For the MCP25xx backend, hw.handle is the spi_device_handle_t you created with
 * spi_bus_add_device(). ESP-IDF drives chip select itself (spics_io_num in the
 * device config), so hw.cs_port / hw.cs_pin are not used on this platform.
 */
#include "can_lib.h"

#if (CAN_CFG_PLATFORM == CAN_PLATFORM_ESP32)

#include <string.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

/** Library time base: ESP timer in microseconds, converted to ms. */
uint32_t CanLib_GetTickMs(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/** Blocking wait, used only during init and mode changes. */
void CanPlatform_DelayMs(uint32_t ms)
{
    esp_rom_delay_us(ms * 1000UL);
}

/** Set a GPIO output. port is ignored, pin is the GPIO number. */
CanResult_e CanPlatform_WritePin(void *port, uint16_t pin, bool level)
{
    (void)port;
    return (gpio_set_level((gpio_num_t)pin, level ? 1U : 0U) == ESP_OK) ? CAN_OK : CAN_ERR_BACKEND_FAIL;
}

#if CAN_CFG_ENABLE_BACKEND_MCP25XX || CAN_CFG_ENABLE_BACKEND_MCP251XFD
/** One SPI transfer. hw->handle is an spi_device_handle_t (it controls chip select). */
CanResult_e CanPlatform_SpiTransfer(const CanHwCfg_t *hw, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    spi_transaction_t t;

    if ((hw == NULL) || (tx == NULL) || (rx == NULL) || (hw->handle == NULL))
    {
        return CAN_ERR_NULL_PTR;
    }
    memset(&t, 0, sizeof(t));
    t.length    = (size_t)len * 8U;         /* Length is in bits. */
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    return (spi_device_polling_transmit((spi_device_handle_t)hw->handle, &t) == ESP_OK) ? CAN_OK : CAN_ERR_SPI;
}
#endif

#endif /* CAN_PLATFORM_ESP32 */
