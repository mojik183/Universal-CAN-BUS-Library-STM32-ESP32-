# @author: mojik183
# @github: github.com/mojik183
# @ai assistant: claude.ai
# @date: 2026-10-30
# @version: 0.1.0
# ESP-IDF component description (the repository itself can be used as an IDF component).
# Set CAN_CFG_PLATFORM CAN_PLATFORM_ESP32 in can_cfg.h. Backends that are switched off compile to nothing.
idf_component_register(
    SRCS "can_core.c" "can_debug.c" "can_port.c" "can_rtos.c"
         "can_esp32_twai.c" "can_mcp25xx.c" "can_mcp251xfd.c" "can_platform_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos)
