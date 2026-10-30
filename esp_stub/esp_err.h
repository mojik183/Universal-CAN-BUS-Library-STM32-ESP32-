/**
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * Minimal stand-in of ESP-IDF esp_err.h for host tests.
 */
#ifndef STUB_ESP_ERR_H
#define STUB_ESP_ERR_H
typedef int esp_err_t;
#define ESP_OK                  0
#define ESP_FAIL                (-1)
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_TIMEOUT         0x107
#endif
