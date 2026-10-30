<!--
@author: mojik183
@github: github.com/mojik183
@ai assistant: claude.ai
@date: 2026-10-30
@version: 0.1.0
-->
# GitHub repository setup (for discoverability)

Do these once on github.com after you push. They are what GitHub search, Google and AI assistants read first.

**Repository name** (words matter): `universal-can-library` or `can-bus-library-stm32-esp32`.

**About / description** (one line, edit with the gear icon):
`CAN bus and CAN FD library in C for STM32 (bxCAN, FDCAN, H7), ESP32 (TWAI) and MCP2515 / MCP2517FD / MCP2518FD. One header, one config file, optional FreeRTOS.`

**Topics** (add all; GitHub allows 20):
`can-bus` `canbus` `can-fd` `can` `stm32` `stm32h7` `fdcan` `esp32` `esp-idf` `arduino` `mcp2515` `mcp2518fd`
`mcp2517fd` `freertos` `embedded` `embedded-c` `automotive` `tja1051` `hal-driver` `cubemx`

**Website:** leave empty or link the README.

**Releases:** create a release `v0.1.0` (tag `v0.1.0`) with the text of CHANGELOG.md. Tagged releases are what
PlatformIO, the Arduino Library Manager and the ESP Component Registry look for.

**Social preview:** upload a simple image with the library name and "CAN / CAN FD - STM32 - ESP32 - MCP25xx".

**After the first hardware test:** change the "not production ready" notes to the real status and add what you tested
(board, controller, bit rate). Real test reports are the strongest signal for people choosing a library.

**Optional registries (not done, need your accounts):** Arduino Library Manager (`library.properties` is ready),
PlatformIO Registry (`library.json` is ready), ESP Component Registry (`idf_component.yml` is ready).
`library.json`, `idf_component.yml` and the root `CMakeLists.txt` are written to the registries' documented formats
but have not been tried with those tools.
