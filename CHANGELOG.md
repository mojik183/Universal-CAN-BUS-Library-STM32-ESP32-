<!--
@author: mojik183
@github: github.com/mojik183
@ai assistant: claude.ai
@date: 2026-10-30
@version: 0.1.0
-->
# Changelog

## 0.1.0 - 2026-10-29

First complete version, tested on the PC only (no real hardware yet).

* Core: main struct, send / receive / process, queues, software filters, callbacks, statistics, manual send.
* Debug: result codes (warnings and errors), last-error file/line, event log, optional printf, watch globals.
* Backends: STM32 bxCAN, STM32 FDCAN (G0/G4/L5/U5/H5/H7 message RAM), ESP32 TWAI, MCP2515, MCP2517FD/MCP2518FD, virtual.
* Optional FreeRTOS layer: mutex, blocking receive, library task.
* Arduino-ESP32 packaging script and example sketch.
* README with hardware, CubeMX setup, configuration reference, debugging and troubleshooting.
* Host test suites (`run_tests.sh`) with address, undefined-behavior and thread sanitizers.
