/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct spi_device_t *spi_device_handle_t;
typedef struct { uint32_t flags; size_t length; size_t rxlength; const void *tx_buffer; void *rx_buffer; } spi_transaction_t;
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);
