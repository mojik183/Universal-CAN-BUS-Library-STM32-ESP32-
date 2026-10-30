/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Minimal stand-in of the ESP-IDF twai driver API for host tests (names follow ESP-IDF 5.x).
 */
#ifndef STUB_TWAI_H
#define STUB_TWAI_H
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef uint32_t TickType_t;
typedef enum { TWAI_MODE_NORMAL, TWAI_MODE_NO_ACK, TWAI_MODE_LISTEN_ONLY } twai_mode_t;
typedef enum { TWAI_STATE_STOPPED, TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF, TWAI_STATE_RECOVERING } twai_state_t;

#define TWAI_IO_UNUSED          (-1)
#define TWAI_ALERT_NONE         0x0
#define ESP_INTR_FLAG_LEVEL1    (1 << 1)
#define TWAI_MSG_FLAG_NONE      0x00
#define TWAI_MSG_FLAG_EXTD      0x01
#define TWAI_MSG_FLAG_RTR       0x02
#define TWAI_MSG_FLAG_SS        0x04
#define TWAI_MSG_FLAG_SELF      0x08
#define TWAI_MSG_FLAG_DLC_NON_COMP 0x10

typedef struct { twai_mode_t mode; gpio_num_t tx_io, rx_io, clkout_io, bus_off_io;
                 uint32_t tx_queue_len, rx_queue_len, alerts_enabled, clkout_divider; int intr_flags; } twai_general_config_t;
typedef struct { uint32_t brp; uint8_t tseg_1, tseg_2, sjw; bool triple_sampling; } twai_timing_config_t;
typedef struct { uint32_t acceptance_code, acceptance_mask; bool single_filter; } twai_filter_config_t;
typedef struct { uint32_t flags; uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; } twai_message_t;
typedef struct { twai_state_t state; uint32_t msgs_to_tx, msgs_to_rx, tx_error_counter, rx_error_counter,
                 tx_failed_count, rx_missed_count, rx_overrun_count, arb_lost_count, bus_error_count; } twai_status_info_t;

#define TWAI_GENERAL_CONFIG_DEFAULT(tx_io_num, rx_io_num, op_mode) \
    { .mode = op_mode, .tx_io = tx_io_num, .rx_io = rx_io_num, .clkout_io = TWAI_IO_UNUSED, .bus_off_io = TWAI_IO_UNUSED, \
      .tx_queue_len = 5, .rx_queue_len = 5, .alerts_enabled = TWAI_ALERT_NONE, .clkout_divider = 0, .intr_flags = ESP_INTR_FLAG_LEVEL1 }
#define TWAI_FILTER_CONFIG_ACCEPT_ALL() { .acceptance_code = 0, .acceptance_mask = 0xFFFFFFFF, .single_filter = true }

esp_err_t twai_driver_install(const twai_general_config_t *g_config, const twai_timing_config_t *t_config, const twai_filter_config_t *f_config);
esp_err_t twai_driver_uninstall(void);
esp_err_t twai_start(void);
esp_err_t twai_stop(void);
esp_err_t twai_transmit(const twai_message_t *message, TickType_t ticks_to_wait);
esp_err_t twai_receive(twai_message_t *message, TickType_t ticks_to_wait);
esp_err_t twai_get_status_info(twai_status_info_t *status_info);
esp_err_t twai_initiate_recovery(void);
#endif
