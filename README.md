<img width="2800" height="1500" alt="can_mind_map" src="https://github.com/user-attachments/assets/c336628f-ff7f-4cd5-af95-c9837ef810bc" /># Universal CAN Library

 <img width="2800" height="1500" alt="can_mind_map" src="https://github.com/user-attachments/assets/9b0c6608-21b7-4369-9e73-aabc610cc3f2" />


One C library for **CAN bus and CAN FD** on **STM32** (bxCAN, FDCAN, STM32H7) and **ESP32** (TWAI, ESP-IDF, Arduino-ESP32).
It also drives the external **MCP2515**, **MCP2517FD** and **MCP2518FD** CAN controllers over **SPI** from any microcontroller, with **TJA1050 / TJA1051 / TJA1042 / TJA1057** and isolated **ISO1042 / ISO1050** transceivers (optional mode pin). Plain C99, optional **FreeRTOS**, no malloc, one umbrella header (`can_lib.h`), all settings in one file (`can_cfg.h`), and easy debugging from the Keil watch window or with printf.

```c
#include "can_lib.h"                       /* the only include you need */

static CanLib_t can1;                      /* the main struct: put it in the Keil watch window */

CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);
can1.hw.handle = &hcan1;
CanLib_Init(&can1);
CanLib_Start(&can1);

CanLib_Send(&can1, 0x123, data, 8);        /* send */

for (;;) {
    CanLib_Process(&can1);                 /* call often */
    CanFrame_t f;
    while (CanLib_Receive(&can1, &f) == CAN_OK) { /* use f.header / f.body */ }
}
```

---

## Quick start (5 minutes, no bus needed)

Goal: send one frame and get it back on your own board, using **loopback mode**. This proves clocks, pins, timing and the library.

1. **Copy** the library folder into your project and add it to the include path (details: [section 6](#6-adding-the-library-to-your-project)).
2. **Edit `can_cfg.h`** (the only file you change). Pick your platform and backend:

   | You use | Set in `can_cfg.h` |
   |---|---|
   | STM32 with `CAN` (F1/F4/...) | `CAN_CFG_PLATFORM CAN_PLATFORM_STM32`, `..._BACKEND_STM32_BXCAN 1`, `CAN_CFG_STM32_HAL_HEADER "stm32f4xx_hal.h"` (your family) |
   | STM32 with `FDCAN` (G4/H7/...) | platform STM32, `..._BACKEND_STM32_BXCAN 0`, `..._BACKEND_STM32_FDCAN 1`, your HAL header, and `CAN_CFG_FDCAN_CLK_HZ` = your FDCAN kernel clock (H7: also `CAN_CFG_FDCAN_MESSAGE_RAM 1`) |
   | ESP32 | `CAN_CFG_PLATFORM CAN_PLATFORM_ESP32`, `..._BACKEND_ESP32_TWAI 1`, `..._BACKEND_STM32_BXCAN 0` |
   | MCP2515 / MCP2517FD / MCP2518FD board | the backend `..._MCP25XX 1` or `..._MCP251XFD 1` (plus `CAN_CFG_ENABLE_FD 1` for CAN FD), and the platform of your MCU |

3. **Set up the peripheral** in CubeMX (STM32, [section 6a](#6a-stm32cubemx-setup)) or ESP-IDF / Arduino ([section 6b](#6b-arduino-and-esp-idf)). For an MCP board also wire SPI and chip select ([section 5.4](#54-wiring-tables)).
4. **Paste this** (change the marked lines for your controller):

   ```c
   #include "can_lib.h"

   static CanLib_t can1;                                    /* add it to the watch window */

   void can_test(void)
   {
       CanFrame_t f;
       uint8_t data[2] = { 0xCA, 0xFE };

       CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);  /* <- your backend */
       can1.hw.handle = &hcan1;                                     /* <- your handle / GPIOs / SPI */
       can1.cfg.mode  = CANLIB_MODE_LOOPBACK;                       /* self test, no bus needed */

       if (CAN_IS_ERR(CanLib_Init(&can1)))  { return; }             /* look at can1.status.last_error */
       if (CAN_IS_ERR(CanLib_Start(&can1))) { return; }

       CanLib_Send(&can1, 0x123, data, 2);
       CanLib_Process(&can1);                                       /* call often in a real program */
       if (CanLib_Receive(&can1, &f) == CAN_OK) { /* f.header.id == 0x123, f.body.data = CA FE: it works */ }
   }
   ```

   Per-controller handle lines: bxCAN `can1.hw.handle = &hcan1;` | FDCAN `&hfdcan1` | ESP32 `can1.hw.tx_gpio = 5; can1.hw.rx_gpio = 4;` |
   MCP `can1.hw.handle = &hspi1; can1.hw.cs_port = GPIOA; can1.hw.cs_pin = GPIO_PIN_4; can1.hw.osc_hz = 8000000UL;`
   (full examples in [section 7](#7-quick-start-per-controller)).
5. **Check it:** in the debugger watch `can1` (`status.state` = `CAN_STATE_RUNNING`, `stats.tx_ok`, `stats.rx_ok`). If a call fails, `can1.status.last_error` and `can1.debug.last_error_line` tell what and where ([section 11](#11-debugging)).
6. **Go on the real bus:** change the mode to `CANLIB_MODE_NORMAL`, connect a transceiver and a second node with 120 Ω at both ends, same bit rate ([section 5](#5-hardware)).

Then read [section 9](#9-using-the-api) for filters, callbacks, FD and manual send, and [section 13](#13-troubleshooting) if something does not work.

---

## Contents

0. [Quick start (5 minutes)](#quick-start-5-minutes-no-bus-needed)
1. [Status and honest limits](#1-status-and-honest-limits)
2. [What is supported](#2-what-is-supported)
3. [How it is built](#3-how-it-is-built)
4. [Files](#4-files)
5. [Hardware](#5-hardware)
6. [Adding the library to your project](#6-adding-the-library-to-your-project)
   * [6a. STM32CubeMX setup](#6a-stm32cubemx-setup)
   * [6b. Arduino and ESP-IDF](#6b-arduino-and-esp-idf)
7. [Quick start per controller](#7-quick-start-per-controller)
8. [Configuration (can_cfg.h)](#8-configuration-can_cfgh)
9. [Using the API](#9-using-the-api)
10. [FreeRTOS](#10-freertos)
11. [Debugging](#11-debugging)
12. [Result codes](#12-result-codes)
13. [Troubleshooting](#13-troubleshooting)
14. [Backend notes and limits](#14-backend-notes-and-limits)
15. [Compile and test on the PC](#15-compile-and-test-on-the-pc)
16. [Porting to another controller or MCU](#16-porting-to-another-controller-or-mcu)

---

## 1. Status and honest limits

* All code is covered by **host tests** (sections 15): fake HAL / driver layers and simulated MCP chips, with
  address, undefined-behavior and thread sanitizers. This proves the logic, not the hardware.
* **Nothing has been run on real silicon yet.** The register-level code for the MCP2515 and especially the
  MCP2517FD / MCP2518FD was written from the datasheets and is only checked against simulators written by the same
  author. Expect to meet small differences on the first board. Use the loopback mode first (section 13), then a
  second node.
* The ST HAL and ESP-IDF calls are checked against stub headers that mirror the real signatures, not against the
  real SDKs. A compile error against your SDK version is possible; the glue is short and isolated
  (`can_stm32_*.c`, `can_esp32_twai.c`, `can_platform_*.c`).
* FreeRTOS is the only supported RTOS.

---

## 2. What is supported

| Controller | Backend id | Classic CAN | CAN FD | Notes |
|---|---|---|---|---|
| STM32 bxCAN (F0/F1/F2/F3/F4/F7/L4/...) | `CAN_BACKEND_STM32_BXCAN` | yes | no | HAL `HAL_CAN_*` |
| STM32 FDCAN (G0/G4/L5/U5/H5/**H7**) | `CAN_BACKEND_STM32_FDCAN` | yes | yes | HAL `HAL_FDCAN_*`, H7 message RAM supported |
| ESP32 TWAI (ESP32, S2, S3, C3, ...) | `CAN_BACKEND_ESP32_TWAI` | yes | no | one controller, ESP-IDF `twai` driver |
| MCP2515 over SPI | `CAN_BACKEND_MCP25XX` | yes | no | any MCU with an SPI |
| MCP2517FD / MCP2518FD over SPI | `CAN_BACKEND_MCP251XFD` | yes | yes | any MCU with an SPI |
| Virtual (software loopback) | `CAN_BACKEND_VIRTUAL` | yes | yes | PC tests, bring-up without hardware |

Features: standard and extended IDs, remote frames, CAN FD with bit-rate switching (BRS) up to 64 bytes,
software acceptance filters, receive and transmit ring buffers, callbacks, statistics, bus-off detection and
automatic recovery, manual send from the watch window, optional FreeRTOS layer (mutex, blocking receive,
library task), printf and watch-window debugging. Every feature can be switched off with a macro.

---

## 3. How it is built

![Upload<svg xmlns="http://www.w3.org/2000/svg" width="2800" height="1500" viewBox="0 0 2800 1500" font-family="Segoe UI, Helvetica, Arial, sans-serif">
<rect width="2800" height="1500" fill="#f8fafc"/>
<text x="1400" y="52" text-anchor="middle" font-size="34" font-weight="700" fill="#0f172a">Universal CAN Library - mind map</text>
<text x="1400" y="84" text-anchor="middle" font-size="20" fill="#475569">CAN / CAN FD for STM32, ESP32 and MCP25xx  |  author: mojik183  |  version 0.1.0</text>
<path d="M1550,750 C1680,750 1606,364 1746,364" fill="none" stroke="#2563eb" stroke-width="6" stroke-linecap="round"/>
<rect x="1746" y="338" width="169" height="52" rx="26" fill="#2563eb"/>
<text x="1830" y="372" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Hardware</text>
<path d="M1914,364 C1954,364 1954,245 1994,245" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="230" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="251" text-anchor="start" font-size="19" fill="#0f172a">STM32 bxCAN (F1/F4...)</text>
<path d="M1914,364 C1954,364 1954,279 1994,279" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="264" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="285" text-anchor="start" font-size="19" fill="#0f172a">STM32 FDCAN (G0/G4/L5/U5/H5/H7)</text>
<path d="M1914,364 C1954,364 1954,313 1994,313" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="298" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="319" text-anchor="start" font-size="19" fill="#0f172a">STM32H7 message RAM</text>
<path d="M1914,364 C1954,364 1954,347 1994,347" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="332" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="353" text-anchor="start" font-size="19" fill="#0f172a">ESP32 TWAI</text>
<path d="M1914,364 C1954,364 1954,381 1994,381" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="366" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="387" text-anchor="start" font-size="19" fill="#0f172a">MCP2515 over SPI</text>
<path d="M1914,364 C1954,364 1954,415 1994,415" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="400" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="421" text-anchor="start" font-size="19" fill="#0f172a">MCP2517FD / MCP2518FD over SPI</text>
<path d="M1914,364 C1954,364 1954,449 1994,449" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="434" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="455" text-anchor="start" font-size="19" fill="#0f172a">Transceivers: TJA10xx, ISO10xx</text>
<path d="M1914,364 C1954,364 1954,483 1994,483" fill="none" stroke="#2563eb" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="468" width="387" height="30" rx="8" fill="#fff" stroke="#2563eb" stroke-width="1.6"/>
<text x="2008" y="489" text-anchor="start" font-size="19" fill="#0f172a">Virtual backend (PC tests)</text>
<path d="M1550,750 C1680,750 1606,662 1746,662" fill="none" stroke="#059669" stroke-width="6" stroke-linecap="round"/>
<rect x="1746" y="636" width="169" height="52" rx="26" fill="#059669"/>
<text x="1830" y="670" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Core API</text>
<path d="M1914,662 C1954,662 1954,543 1994,543" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="528" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="549" text-anchor="start" font-size="19" fill="#0f172a">SetDefaults &gt; Init &gt; Start &gt; Process</text>
<path d="M1914,662 C1954,662 1954,577 1994,577" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="562" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="583" text-anchor="start" font-size="19" fill="#0f172a">Send / SendExt / SendFd / SendRemote</text>
<path d="M1914,662 C1954,662 1954,611 1994,611" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="596" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="617" text-anchor="start" font-size="19" fill="#0f172a">Receive / Available / ReceiveWait</text>
<path d="M1914,662 C1954,662 1954,645 1994,645" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="630" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="651" text-anchor="start" font-size="19" fill="#0f172a">Software filters (mask, exact)</text>
<path d="M1914,662 C1954,662 1954,679 1994,679" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="664" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="685" text-anchor="start" font-size="19" fill="#0f172a">Callbacks: on_rx, on_event</text>
<path d="M1914,662 C1954,662 1954,713 1994,713" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="698" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="719" text-anchor="start" font-size="19" fill="#0f172a">Manual send (watch window)</text>
<path d="M1914,662 C1954,662 1954,747 1994,747" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="732" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="753" text-anchor="start" font-size="19" fill="#0f172a">Bus-off auto recovery</text>
<path d="M1914,662 C1954,662 1954,781 1994,781" fill="none" stroke="#059669" stroke-width="2.2" opacity="0.7"/>
<rect x="1994" y="766" width="444" height="30" rx="8" fill="#fff" stroke="#059669" stroke-width="1.6"/>
<text x="2008" y="787" text-anchor="start" font-size="19" fill="#0f172a">Transceiver mode pin</text>
<path d="M1550,750 C1680,750 1519,943 1659,943" fill="none" stroke="#d97706" stroke-width="6" stroke-linecap="round"/>
<rect x="1659" y="917" width="342" height="52" rx="26" fill="#d97706"/>
<text x="1830" y="951" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Main struct CanLib_t</text>
<path d="M2001,943 C2041,943 2041,841 2081,841" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="826" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="847" text-anchor="start" font-size="19" fill="#0f172a">cfg: bit timing, mode, FD</text>
<path d="M2001,943 C2041,943 2041,875 2081,875" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="860" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="881" text-anchor="start" font-size="19" fill="#0f172a">hw: handles, pins, crystal</text>
<path d="M2001,943 C2041,943 2041,909 2081,909" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="894" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="915" text-anchor="start" font-size="19" fill="#0f172a">rx / tx: header, body, buffer</text>
<path d="M2001,943 C2041,943 2041,943 2081,943" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="928" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="949" text-anchor="start" font-size="19" fill="#0f172a">filters, status, bus counters</text>
<path d="M2001,943 C2041,943 2041,977 2081,977" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="962" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="983" text-anchor="start" font-size="19" fill="#0f172a">stats, debug, callbacks</text>
<path d="M2001,943 C2041,943 2041,1011 2081,1011" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="996" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="1017" text-anchor="start" font-size="19" fill="#0f172a">rtos objects</text>
<path d="M2001,943 C2041,943 2041,1045 2081,1045" fill="none" stroke="#d97706" stroke-width="2.2" opacity="0.7"/>
<rect x="2081" y="1030" width="364" height="30" rx="8" fill="#fff" stroke="#d97706" stroke-width="1.6"/>
<text x="2095" y="1051" text-anchor="start" font-size="19" fill="#0f172a">port + port_ctx (backend)</text>
<path d="M1550,750 C1680,750 1627,1207 1767,1207" fill="none" stroke="#dc2626" stroke-width="6" stroke-linecap="round"/>
<rect x="1767" y="1181" width="126" height="52" rx="26" fill="#dc2626"/>
<text x="1830" y="1215" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Debug</text>
<path d="M1893,1207 C1933,1207 1933,1105 1973,1105" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1090" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1111" text-anchor="start" font-size="19" fill="#0f172a">Keil watch window</text>
<path d="M1893,1207 C1933,1207 1933,1139 1973,1139" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1124" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1145" text-anchor="start" font-size="19" fill="#0f172a">Errors (negative) / warnings (positive)</text>
<path d="M1893,1207 C1933,1207 1933,1173 1973,1173" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1158" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1179" text-anchor="start" font-size="19" fill="#0f172a">Last error file / line / function</text>
<path d="M1893,1207 C1933,1207 1933,1207 1973,1207" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1192" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1213" text-anchor="start" font-size="19" fill="#0f172a">Event log ring</text>
<path d="M1893,1207 C1933,1207 1933,1241 1973,1241" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1226" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1247" text-anchor="start" font-size="19" fill="#0f172a">Optional printf, log levels</text>
<path d="M1893,1207 C1933,1207 1933,1275 1973,1275" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1260" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1281" text-anchor="start" font-size="19" fill="#0f172a">Watch globals, assert hook</text>
<path d="M1893,1207 C1933,1207 1933,1309 1973,1309" fill="none" stroke="#dc2626" stroke-width="2.2" opacity="0.7"/>
<rect x="1973" y="1294" width="478" height="30" rx="8" fill="#fff" stroke="#dc2626" stroke-width="1.6"/>
<text x="1987" y="1315" text-anchor="start" font-size="19" fill="#0f172a">Statistics + error counters</text>
<path d="M1250,750 C1120,750 1231,432 1091,432" fill="none" stroke="#7c3aed" stroke-width="6" stroke-linecap="round"/>
<rect x="850" y="406" width="241" height="52" rx="26" fill="#7c3aed"/>
<text x="970" y="440" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Configuration</text>
<path d="M849,432 C809,432 810,347 770,347" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="332" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="353" text-anchor="end" font-size="19" fill="#0f172a">can_cfg.h = the only file to edit</text>
<path d="M849,432 C809,432 810,381 770,381" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="366" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="387" text-anchor="end" font-size="19" fill="#0f172a">Platform + backend switches</text>
<path d="M849,432 C809,432 810,415 770,415" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="400" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="421" text-anchor="end" font-size="19" fill="#0f172a">Feature macros (FD, filters, queues)</text>
<path d="M849,432 C809,432 810,449 770,449" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="434" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="455" text-anchor="end" font-size="19" fill="#0f172a">Sizes (queues, filters, instances)</text>
<path d="M849,432 C809,432 810,483 770,483" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="468" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="489" text-anchor="end" font-size="19" fill="#0f172a">FDCAN clock, H7 RAM, IRQ options</text>
<path d="M849,432 C809,432 810,517 770,517" fill="none" stroke="#7c3aed" stroke-width="2.2" opacity="0.7"/>
<rect x="326" y="502" width="444" height="30" rx="8" fill="#fff" stroke="#7c3aed" stroke-width="1.6"/>
<text x="756" y="523" text-anchor="end" font-size="19" fill="#0f172a">Sanity checks with #error</text>
<path d="M1250,750 C1120,750 1274,662 1134,662" fill="none" stroke="#0891b2" stroke-width="6" stroke-linecap="round"/>
<rect x="807" y="636" width="327" height="52" rx="26" fill="#0891b2"/>
<text x="970" y="670" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">FreeRTOS (optional)</text>
<path d="M806,662 C766,662 767,577 727,577" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="562" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="583" text-anchor="end" font-size="19" fill="#0f172a">CAN_CFG_USE_FREERTOS</text>
<path d="M806,662 C766,662 767,611 727,611" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="596" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="617" text-anchor="end" font-size="19" fill="#0f172a">Recursive mutex per instance</text>
<path d="M806,662 C766,662 767,645 727,645" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="630" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="651" text-anchor="end" font-size="19" fill="#0f172a">Library task (auto)</text>
<path d="M806,662 C766,662 767,679 727,679" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="664" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="685" text-anchor="end" font-size="19" fill="#0f172a">Blocking CanLib_ReceiveWait</text>
<path d="M806,662 C766,662 767,713 727,713" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="698" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="719" text-anchor="end" font-size="19" fill="#0f172a">ISR rules, notify from ISR</text>
<path d="M806,662 C766,662 767,747 727,747" fill="none" stroke="#0891b2" stroke-width="2.2" opacity="0.7"/>
<rect x="374" y="732" width="353" height="30" rx="8" fill="#fff" stroke="#0891b2" stroke-width="1.6"/>
<text x="713" y="753" text-anchor="end" font-size="19" fill="#0f172a">Critical sections</text>
<path d="M1250,750 C1120,750 1173,892 1033,892" fill="none" stroke="#be185d" stroke-width="6" stroke-linecap="round"/>
<rect x="907" y="866" width="126" height="52" rx="26" fill="#be185d"/>
<text x="970" y="900" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Setup</text>
<path d="M907,892 C867,892 867,807 827,807" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="792" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="813" text-anchor="end" font-size="19" fill="#0f172a">STM32CubeMX (CAN, FDCAN, SPI, FreeRTOS)</text>
<path d="M907,892 C867,892 867,841 827,841" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="826" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="847" text-anchor="end" font-size="19" fill="#0f172a">STM32CubeIDE / Keil</text>
<path d="M907,892 C867,892 867,875 827,875" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="860" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="881" text-anchor="end" font-size="19" fill="#0f172a">ESP-IDF component</text>
<path d="M907,892 C867,892 867,909 827,909" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="894" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="915" text-anchor="end" font-size="19" fill="#0f172a">Arduino-ESP32 package</text>
<path d="M907,892 C867,892 867,943 827,943" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="928" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="949" text-anchor="end" font-size="19" fill="#0f172a">Wiring + 120 ohm termination</text>
<path d="M907,892 C867,892 867,977 827,977" fill="none" stroke="#be185d" stroke-width="2.2" opacity="0.7"/>
<rect x="349" y="962" width="478" height="30" rx="8" fill="#fff" stroke="#be185d" stroke-width="1.6"/>
<text x="813" y="983" text-anchor="end" font-size="19" fill="#0f172a">Quick start: loopback test</text>
<path d="M1250,750 C1120,750 1259,1122 1119,1122" fill="none" stroke="#4b5563" stroke-width="6" stroke-linecap="round"/>
<rect x="821" y="1096" width="298" height="52" rx="26" fill="#4b5563"/>
<text x="970" y="1130" text-anchor="middle" font-size="24" font-weight="700" fill="#fff">Quality + Release</text>
<path d="M821,1122 C781,1122 781,1037 741,1037" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1022" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1043" text-anchor="end" font-size="19" fill="#0f172a">Host tests (14 suites)</text>
<path d="M821,1122 C781,1122 781,1071 741,1071" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1056" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1077" text-anchor="end" font-size="19" fill="#0f172a">Fake HAL, IDF, FreeRTOS, MCP sim</text>
<path d="M821,1122 C781,1122 781,1105 741,1105" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1090" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1111" text-anchor="end" font-size="19" fill="#0f172a">ASan / UBSan / TSan</text>
<path d="M821,1122 C781,1122 781,1139 741,1139" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1124" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1145" text-anchor="end" font-size="19" fill="#0f172a">README, FAQ, troubleshooting</text>
<path d="M821,1122 C781,1122 781,1173 741,1173" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1158" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1179" text-anchor="end" font-size="19" fill="#0f172a">MIT, CI, CHANGELOG, llms.txt</text>
<path d="M821,1122 C781,1122 781,1207 741,1207" fill="none" stroke="#4b5563" stroke-width="2.2" opacity="0.7"/>
<rect x="343" y="1192" width="398" height="30" rx="8" fill="#fff" stroke="#4b5563" stroke-width="1.6"/>
<text x="727" y="1213" text-anchor="end" font-size="19" fill="#0f172a">Not yet run on real hardware</text>
<ellipse cx="1400" cy="750" rx="175" ry="95" fill="#0f172a"/>
<text x="1400" y="740" text-anchor="middle" font-size="34" font-weight="800" fill="#fff">Universal</text>
<text x="1400" y="778" text-anchor="middle" font-size="34" font-weight="800" fill="#fff">CAN Library</text>
<text x="1400" y="812" text-anchor="middle" font-size="16" fill="#94a3b8">include "can_lib.h"</text>
</svg>ing can_mind_map.svg…]()


*Mind map of the whole library.*

```
        your application
              |
        can_lib.h  (umbrella)
              |
   +----------+-----------+
   |  can_core   (user API, queues, filters, stats, debug)   <-- can_rtos (optional FreeRTOS)
   +----------+-----------+
              |  CanPortOps_t (function table of one controller)
   +------+------+------+--------+-----------+----------+
   bxCAN  FDCAN  TWAI   MCP2515  MCP2517/18FD  virtual
              |
   platform hooks: tick, delay, GPIO, SPI  (can_platform_stm32.c / can_platform_esp32.c)
```

* The **core** never touches hardware. It calls the function table of the backend you selected.
* A **backend** only knows one controller. It never reports errors itself; it returns a result code and the
  core reports it once (so every problem is counted exactly once).
* The **platform file** provides what is vendor specific: millisecond tick, delay, GPIO write, SPI transfer.
* The **main struct** `CanLib_t` holds one CAN channel: configuration, hardware description, receive and
  transmit headers/bodies/buffers, filters, status, error counters, statistics, debug, callbacks and the
  FreeRTOS objects. Everything is plain data, so it is all visible in a watch window.
* No `malloc` anywhere. All memory is static and sized in `can_cfg.h`.

---

## 4. Files

Copy the whole folder into your project (section 6). Only `can_cfg.h` is edited by you.

| File | Purpose |
|---|---|
| `can_lib.h` | Umbrella header. Include only this. |
| `can_cfg.h` | **All configuration macros.** |
| `can_types.h` | Enums, structs, the main struct `CanLib_t`. |
| `can_core.h/.c` | User API: init, send, receive, process, filters, transceiver. |
| `can_debug.h/.c` | Error and warning reporting, log macros, watch globals. |
| `can_port.h/.c` | Backend function table, weak platform hooks. |
| `can_rtos.h/.c` | Optional FreeRTOS layer (`CAN_CFG_USE_FREERTOS`). |
| `can_stm32_bxcan.h/.c` | STM32 bxCAN backend. |
| `can_stm32_fdcan.h/.c` | STM32 FDCAN backend (classic + FD, H7 RAM). |
| `can_esp32_twai.h/.c` | ESP32 TWAI backend. |
| `can_mcp25xx.h/.c` | MCP2515 backend (SPI). |
| `can_mcp251xfd.h/.c` | MCP2517FD / MCP2518FD backend (SPI). |
| `can_port_virtual.h/.c` | Virtual backend (PC tests). |
| `can_platform_stm32.c` | STM32 tick, delay, GPIO, SPI glue. |
| `can_platform_esp32.c` | ESP32 tick, delay, GPIO, SPI glue. |
| `run_tests.sh`, `test_*.c`, `hal_stub/`, `esp_stub/`, `freertos_stub/` | Host tests and stubs. **Never add to firmware.** |
| `docs/can_mind_map.png`, `docs/can_mind_map.svg` | Mind map of the library. |
| `make_arduino_lib.sh`, `arduino_example/` | Builds an Arduino library folder (ESP32) with an example sketch (section 6b). |

Files of backends you did not enable compile to nothing, so you may add all `.c` files to the project.

---

## 5. Hardware

### 5.1 Pick a controller

| You have | Use |
|---|---|
| STM32 with `CAN` peripheral (F1/F4/...) | bxCAN backend + a transceiver |
| STM32 with `FDCAN` (G4/H7/...) | FDCAN backend + a transceiver (needed for CAN FD) |
| ESP32 | TWAI backend + a transceiver |
| Any MCU, no CAN peripheral, or you need more channels | MCP2515 (classic) or MCP2517FD / 2518FD (CAN FD) board; the chip contains the transceiver-side logic but you still need a **transceiver** unless the board has one (most do) |

Rule of thumb: **MCP2515 modules** (8 or 16 MHz crystal, usually with TJA1050 on board) are cheap but classic only.
**MCP2518FD** (40 MHz crystal) is the one for CAN FD. The MCP2517FD works the same; mainly its SPI clock limit is a bit lower.

### 5.2 Transceiver (TJA10xx, ISO10xx)

The controller's `TX` / `RX` logic pins (3.3 V) go to a CAN transceiver, which drives `CANH` / `CANL`.

| Family | Typical parts | Notes |
|---|---|---|
| NXP TJA10xx | TJA1042, TJA1051, TJA1057 (FD capable), TJA1050 (classic) | Mode pin `STB` / `S`: **high = standby / silent**, low = normal. |
| TI/ADI ISO10xx | ISO1042, ISO1050 (isolated) | Galvanic isolation; needs isolated supply on the bus side. Some parts have no mode pin. |

Use an **FD-rated transceiver** (TJA1042/1051/1057, ISO1042) for CAN FD above 1 Mbit/s data rate;
TJA1050 and ISO1050 are classic-only parts.

Mode pin control is optional. If your board ties the pin permanently, skip it (section 9.8).

### 5.3 Bus wiring rules (every setup)

* Two wires, `CANH` and `CANL`, twisted pair. A common **GND** reference helps with non-isolated nodes.
* **120 Ω termination at both physical ends** of the bus, and only there. A two-node bench has two resistors,
  measuring about **60 Ω** between CANH and CANL with power off.
* Supply: 3.3 V logic on the MCU side. Many TJA10xx boards need 5 V for the bus side with a 3.3 V I/O pin
  (TJA1051T/3, TJA1057GT/3, TJA1042T/3 handle that); check your part.
* Keep the stubs short, especially for CAN FD (5 Mbit/s needs very short stubs and clean termination).
* A single node alone on a bus never gets an ACK and goes error passive after sending. Use **loopback mode**
  (section 13) for a one-board test.

### 5.4 Wiring tables

**STM32 bxCAN / FDCAN or ESP32 TWAI** (built-in controller):

```
MCU CAN_TX  ----- TXD  \
MCU CAN_RX  ----- RXD   >  transceiver  ----- CANH / CANL ---- bus (120 R at each end)
MCU GPIO    ----- STB  /   (optional mode pin)
3V3 / 5V, GND to the transceiver as its datasheet requires
```

**MCP2515 / MCP2517FD / MCP2518FD board over SPI:**

| Board pin | MCU |
|---|---|
| SCK | SPI SCK |
| SI (MOSI) | SPI MOSI |
| SO (MISO) | SPI MISO |
| CS | any GPIO (STM32), or the ESP-IDF device CS pin (ESP32) |
| INT | optional GPIO (polling works without it; wire it to wake the library task, section 10) |
| VCC / GND | per board (3.3 V or 5 V, check the board; MCU logic stays 3.3 V) |
| CANH / CANL | bus |

SPI mode **0** (CPOL = 0, CPHA = first edge), 8-bit, MSB first.
Maximum clock: MCP2515 10 MHz; MCP2517FD 0.85 x SYSCLK / 2 (about 17 MHz at 40 MHz); MCP2518FD 20 MHz.
Use 1 to 4 MHz while you bring up a new board.

---

## 6. Adding the library to your project

Copy the library folder (everything except the test files if you like) into the project, for example
`Middlewares/can_lib/`, then:

### STM32CubeIDE / Makefile / CMake (GCC)

1. Copy the folder into the project.
2. Add the folder to the **include paths** (`-I`) and make sure the `.c` files are compiled
   (CubeIDE: right-click the folder, *Add/remove include path*; folders inside the project are built automatically).
3. In `can_cfg.h` set `CAN_CFG_PLATFORM CAN_PLATFORM_STM32`, the backend, and `CAN_CFG_STM32_HAL_HEADER`
   to your family header (`"stm32f1xx_hal.h"`, `"stm32g4xx_hal.h"`, `"stm32h7xx_hal.h"`, ...).
4. In CubeMX enable the peripheral you use (CAN / FDCAN / SPI) so clocks and pins are generated.
5. Compile with C99 or later (`-std=gnu99`, the CubeIDE default is gnu11: fine).

### Keil MDK (uVision)

1. Add the library `.c` files to a group (*Manage Project Items*).
2. *Options for Target > C/C++ > Include Paths*: add the library folder.
3. Tick **C99 Mode** (ARM Compiler 5) or use `-std=c99` (ARM Compiler 6). The library uses `__func__`.
4. The weak platform functions use `__weak` automatically (`CAN_WEAK`).
5. To see the printf output on a debug viewer, retarget `printf` (ITM or UART) or set `CAN_CFG_PRINTF_FN`.

### ESP-IDF

Make the library an IDF component, for example `components/can_lib/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "can_core.c" "can_debug.c" "can_port.c" "can_rtos.c"
         "can_esp32_twai.c" "can_mcp25xx.c" "can_mcp251xfd.c" "can_platform_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos)
```

Put the library files in that component folder (leave out the `test_*.c`, stubs and `run_tests.sh`),
and in `can_cfg.h` set `CAN_CFG_PLATFORM CAN_PLATFORM_ESP32`.
Add `REQUIRES can_lib` to the component that uses it.

### In your code

```c
#include "can_lib.h"
```

That is all. The umbrella header pulls in everything the enabled backends need.

---

## 6a. STM32CubeMX setup

CubeMX generates what the library does **not** do itself: clocks, pins, the peripheral handle (`hcan1`, `hfdcan1`,
`hspi1`) and the interrupt vectors. The library then re-initializes the CAN peripheral with its own bit timing, mode
and options, so most CAN parameters in CubeMX do not matter. What matters is listed per step below.

### Common steps

1. *New project* and pick your MCU or board. In **System Core > SYS** set the debug interface (Serial Wire) so you can still program the chip.
2. **System Core > RCC**: choose your clock source (HSE crystal for a reliable CAN bit rate; the internal RC oscillator is not accurate enough for CAN above low bit rates).
3. **Clock Configuration**: build a clock tree you know. **Write down the clock that feeds the CAN peripheral** (see the peripheral sections below): the library computes the bit timing from it.
4. **Project Manager > Code Generator**: tick *Generate peripheral initialization as a pair of .c/.h files per peripheral* (optional but tidy). Click *Generate Code*.
5. Add the library folder to the project and the include path (section 6). In `can_cfg.h` select the platform `CAN_PLATFORM_STM32`, the backend, and `CAN_CFG_STM32_HAL_HEADER` for your family.
6. In `main.c`, **call the generated `MX_xxx_Init()` functions** before `CanLib_Init()`. They set the peripheral instance in the handle (for example `hcan1.Instance = CAN1`), which the library needs.

### bxCAN (STM32F0 / F1 / F2 / F3 / F4 / F7 / L4 ...)

1. **Connectivity > CAN1**: tick **Activated**. Choose the pins (for example PA11 = CAN_RX, PA12 = CAN_TX; alternates exist) in the Pinout view, or set the pin functions to `CAN1_RX` / `CAN1_TX`.
2. *Parameter Settings*: leave everything at the defaults. The library overwrites *prescaler, time quanta, SJW, mode, auto retransmission, auto bus-off, auto wake-up, FIFO locking and TX priority*. (It selects automatic bus-off *off* because the library handles bus-off recovery itself.)
3. **Clock Configuration**: the library reads the real APB1 clock at run time (`HAL_RCC_GetPCLK1Freq()`), so you do not enter a number in `can_cfg.h`. APB1 must be a value that can make your bit rate: for example 36 MHz or 42 MHz give 500 kbit/s and 1 Mbit/s exactly. If `CanLib_Init()` returns `CAN_ERR_BITRATE`, change the APB1 clock or the bit rate.
4. *NVIC Settings* (only with `CAN_CFG_BXCAN_USE_IRQ 1`): tick **CAN1 RX0 interrupts**. In user code add the HAL callback from section 7.1.
5. Generate code and call `MX_CAN1_Init()`, then use `&hcan1` as `can1.hw.handle`.

### FDCAN (STM32G0 / G4 / L5 / U5 / H5 / H7)

1. **Connectivity > FDCAN1** (or FDCAN2 / FDCAN3): tick **Activated**. Set the pins to `FDCAN1_RX` / `FDCAN1_TX`.
2. *Parameter Settings*: leave the defaults (the library sets frame format, mode, prescalers, segments, retransmission, message RAM).
3. **Clock Configuration**: select the **FDCAN kernel clock source** (G4: *FDCAN clock mux* = PCLK1 / PLLQ / HSE; H7: *FDCAN clock mux* = PLL1Q / PLL2Q / HSE). Choose a clock that divides well, for example **80 MHz, 160 MHz or 40 MHz** (170 MHz works for classic rates but gives fewer exact CAN FD rates).
4. Enter that exact frequency in `can_cfg.h`: `#define CAN_CFG_FDCAN_CLK_HZ 160000000UL`. A mismatch gives a wrong bit rate with no error message.
5. *Clock divider* (H7, *Parameter Settings > Clock Calibration / Clock Divider*): set **Divider 1**. The library uses divider 1.
6. **STM32H7 only**: set `#define CAN_CFG_FDCAN_MESSAGE_RAM 1` in `can_cfg.h`. You do not configure the message RAM in CubeMX; the library fills it (section 7.2). Check that no other part of your project uses the same shared RAM area.
7. *NVIC Settings* (only with `CAN_CFG_FDCAN_USE_IRQ 1`): tick **FDCAN1 interrupt 0**. Add the HAL callback from section 7.2. Interrupt mode is unverified on real silicon; start with polling.
8. Generate code and call `MX_FDCAN1_Init()`, then use `&hfdcan1` as `can1.hw.handle`.

### SPI for MCP2515 / MCP2517FD / MCP2518FD

1. **Connectivity > SPI1** (any SPI): *Mode* = **Full-Duplex Master**, *Hardware NSS Signal* = **Disable** (the library drives chip select as a normal GPIO).
2. *Parameter Settings*: *Frame Format* Motorola, **Data Size 8 Bits**, **First Bit MSB First**, **Clock Polarity (CPOL) Low**, **Clock Phase (CPHA) 1 Edge** (SPI mode 0), *CRC Calculation* Disabled, *NSS Signal Type* Software.
3. *Prescaler*: choose a **Baud Rate <= 10 MHz** for the MCP2515 and, for the MCP2517FD / MCP2518FD, at most 0.85 x SYSCLK / 2 (about 17 MHz at 40 MHz SYSCLK; 8.5 MHz if the chip runs from a 20 MHz crystal). Start with 1 to 2 MHz while you bring up a board.
4. **Chip select**: in the Pinout view set a free pin to **GPIO_Output**. *GPIO output level* **High**, mode Push-Pull, no pull, speed High, user label for example `MCP_CS`. CubeMX generates `MCP_CS_GPIO_Port` and `MCP_CS_Pin`; use them as `can1.hw.cs_port` / `can1.hw.cs_pin`.
5. **INT pin** (optional): set it to **GPIO_EXTI**, trigger *Falling edge*, pull-up, tick the EXTI line in *NVIC Settings*. The polling mode of the library does not need it. With FreeRTOS, call `CanRtos_NotifyFromISR(&can1)` in the EXTI callback to wake the library task at once.
6. No DMA is needed. The library uses the blocking `HAL_SPI_TransmitReceive()` with `CAN_CFG_MCP_SPI_TIMEOUT_MS`.
7. Generate code, call `MX_GPIO_Init()` and `MX_SPI1_Init()`, then use `&hspi1` as `can1.hw.handle`.

### Transceiver mode pin (optional)

Set a free pin to **GPIO_Output**, user label for example `CAN_STB`, initial level **Low** (normal mode for TJA10xx). Pass `CAN_STB_GPIO_Port` and `CAN_STB_Pin` as `can1.hw.xcvr_port` / `can1.hw.xcvr_pin` and set `can1.hw.xcvr_standby_level = true` (section 9.8).

### FreeRTOS in CubeMX (only with `CAN_CFG_USE_FREERTOS 1`)

1. **Middleware > FREERTOS**: choose an interface (CMSIS_V1, CMSIS_V2 or the native one). The library calls the native FreeRTOS API (`xTaskCreate`, semaphores) directly, so any of them works.
2. *Config parameters* (they end up in `FreeRTOSConfig.h`):
   * `USE_MUTEXES` = Enabled and **`USE_RECURSIVE_MUTEXES` = Enabled** (the library uses a recursive mutex per instance),
   * `USE_TASK_NOTIFICATIONS` = Enabled (default),
   * `INCLUDE_vTaskDelete` = Enabled (needed to remove the library task in `CanLib_DeInit()`),
   * `INCLUDE_uxTaskGetStackHighWaterMark` = Enabled (gives `can.rtos.stack_free_words`),
   * `configSUPPORT_DYNAMIC_ALLOCATION` = Enabled.
3. **Heap**: `TOTAL_HEAP_SIZE` must hold, per CAN instance, the library task (`CAN_CFG_RTOS_TASK_STACK_WORDS` x 4 bytes = 3 KB by default) plus about 0.5 KB for the task control block, mutex and semaphore, on top of your own tasks.
4. **System Core > SYS > Timebase Source**: select a **hardware timer (for example TIM6 or TIM7), not SysTick**, as CubeMX itself recommends when FreeRTOS is on. FreeRTOS owns SysTick.
5. **Interrupt priorities**: CAN / FDCAN / EXTI interrupts that call `FromISR` functions need a priority number **at or above** `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` (in CubeMX: *NVIC > Priority Group* 4 bits and a preemption priority of 5 or higher for those interrupts). A numerically lower value (higher urgency) than that limit causes hard-to-find crashes inside FreeRTOS.
6. Create `can1` and call `CanLib_Init()` / `CanLib_Start()` before the scheduler starts or inside a task, as in section 10.

### Check list when it does not work

* `CAN_ERR_NULL_PTR` from `CanLib_Init()`: `MX_xxx_Init()` was not called (handle has no instance) or `hw.handle` not set.
* `CAN_ERR_BITRATE`: the clock number is wrong or the bit rate cannot be made from it.
* Pins: in the Pinout view check the alternate function and that the right transceiver pins are connected.
* The library never changes your pin or clock setup. If CAN does not work in loopback mode (section 13), fix clocks and pins first.

---

## 6b. Arduino and ESP-IDF

**ESP-IDF:** native. Use the component from section 6.

**Arduino (ESP32):** yes, with the *Arduino-ESP32* core, because that core is built on ESP-IDF and ships the same
`driver/twai.h`, `driver/spi_master.h`, `esp_timer.h` and FreeRTOS headers the library uses. The library is plain C
and its headers are C++ safe (`extern "C"` guards, checked by compiling them with `g++`), so a sketch can include
`can_lib.h` directly.

Honest status: the library compiles as C and the example sketch compiles as C++ against it on the PC (with the virtual
backend and a fake `Arduino.h`). It has **not** been built with the Arduino IDE or run on a real ESP32 board.

### Install as an Arduino library

```sh
sh make_arduino_lib.sh              # creates ./UniversalCAN (src/, examples/, library.properties)
```

Copy the `UniversalCAN` folder into your Arduino `libraries` folder (the sketchbook location is shown in
*File > Preferences*), restart the IDE, then open **File > Examples > UniversalCAN > CanLoopback**.
The generated `src/can_cfg.h` is preset for an ESP32 with the TWAI backend (platform ESP32, TWAI on, bxCAN and MCP2515 off).
To change settings, edit `libraries/UniversalCAN/src/can_cfg.h`. Settings cannot be passed with `#define` in the sketch,
because the library is compiled separately; edit the file instead.

### Use it in a sketch

```cpp
#include <can_lib.h>

static CanLib_t can1;

void setup() {
    Serial.begin(115200);
    CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI);
    can1.hw.tx_gpio = 5;  can1.hw.rx_gpio = 4;       /* transceiver TXD / RXD */
    CanLib_Init(&can1);
    CanLib_Start(&can1);
}

void loop() {
    CanFrame_t f;
    CanLib_Process(&can1);                            /* call often */
    while (CanLib_Receive(&can1, &f) == CAN_OK) { /* use f */ }
}
```

Notes:

* `loop()` already runs in a FreeRTOS task on the ESP32, so `CAN_CFG_USE_FREERTOS 1` works there too (the library task
  and mutex then protect calls from several tasks).
* **MCP25xx on Arduino-ESP32:** the library needs an ESP-IDF SPI device handle, not the Arduino `SPI` object. Create it
  with `spi_bus_initialize()` and `spi_bus_add_device()` from `driver/spi_master.h` inside your sketch (mode 0,
  `spics_io_num` = your CS pin) and pass the handle as `can1.hw.handle`. Enable the MCP backend in `can_cfg.h`.
* The Arduino package covers the ESP32 family only (`architectures=esp32`). The AVR and other non-ESP Arduino cores
  are not supported.
* **STM32duino** (Arduino core for STM32) uses the same HAL, so the STM32 backends may compile there, but you
  would have to create the HAL handles (`hcan1`, `hfdcan1`, `hspi1`) yourself. This combination is not tested or
  documented here; the supported STM32 route is CubeMX / CubeIDE / Keil (section 6a).

---

## 7. Quick start per controller

All examples: `static CanLib_t can1;` at file scope (keep it in RAM, visible to the debugger).
Check every `CanLib_xxx` return value in real code: `if (CAN_IS_ERR(r)) { ... }`
(negative = error, positive = warning but the call worked, section 12).

### 7.1 STM32 bxCAN

`can_cfg.h`: `CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 1`, FDCAN 0, platform STM32.

CubeMX: enable CAN1 (pins, clock). Leave bit timing at default: the library computes it from the
APB1 clock (`HAL_RCC_GetPCLK1Freq()`).

```c
CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);
can1.cfg.nominal.bitrate = CAN_BITRATE_500K;
can1.hw.handle = &hcan1;                  /* CAN_HandleTypeDef* from CubeMX */
CanLib_Init(&can1);
CanLib_Start(&can1);
```

Interrupt reception (optional): set `CAN_CFG_BXCAN_USE_IRQ 1`, enable the CAN RX0 interrupt in CubeMX and:

```c
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *h) { CanBxcan_OnRxFifo0Pending(&can1); }
```

Also map `CAN_CFG_ENTER_CRITICAL` / `CAN_CFG_EXIT_CRITICAL` (section 8.9) because the queue is now shared with an interrupt.

### 7.2 STM32 FDCAN (G0, G4, L5, U5, H5, H7)

`can_cfg.h`:

```c
#define CAN_CFG_ENABLE_BACKEND_STM32_BXCAN  0
#define CAN_CFG_ENABLE_BACKEND_STM32_FDCAN  1
#define CAN_CFG_ENABLE_FD                   1      /* only if you want CAN FD */
#define CAN_CFG_STM32_HAL_HEADER            "stm32g4xx_hal.h"
#define CAN_CFG_FDCAN_CLK_HZ                160000000UL   /* FDCAN kernel clock from the CubeMX clock tree */
/* STM32H7 only: */
#define CAN_CFG_FDCAN_MESSAGE_RAM           1
```

`CAN_CFG_FDCAN_CLK_HZ` **must match the real kernel clock** (CubeMX *Clock Configuration*). If it is wrong,
the bit rate is wrong and the bus will show errors.

```c
CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_FDCAN);
can1.hw.handle        = &hfdcan1;
can1.cfg.nominal.bitrate = CAN_BITRATE_500K;
#if CAN_CFG_ENABLE_FD
can1.cfg.fd_enabled   = true;                  /* CAN FD with bit-rate switching */
can1.cfg.data.bitrate = CAN_FD_BITRATE_2M;     /* data phase */
#endif
CanLib_Init(&can1);
CanLib_Start(&can1);
```

* From a 2 Mbit/s data rate the library switches on transmitter delay compensation automatically.
* **STM32H7:** with `CAN_CFG_FDCAN_MESSAGE_RAM 1` the library fills the message RAM layout in the HAL init
  structure: RX FIFO0 with `CAN_CFG_FDCAN_RAM_RX_ELEMENTS` and the TX FIFO with `CAN_CFG_FDCAN_RAM_TX_ELEMENTS`
  elements, in 8-byte (classic) or 64-byte (FD) element size. Each library instance gets its own block of the
  shared 10 KB RAM (`CAN_CFG_FDCAN_RAM_OFFSET(instance)`); if the blocks do not fit, `CanLib_Init()` returns
  `CAN_ERR_BAD_PARAM`. Override `CAN_CFG_FDCAN_RAM_OFFSET` if you share the RAM with other code.
* Polling by default. For interrupts set `CAN_CFG_FDCAN_USE_IRQ 1` and:

```c
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *h, uint32_t it) { CanFdcan_OnRxFifo0Pending(&can1); }
```

### 7.3 ESP32 TWAI

`can_cfg.h`: platform `CAN_PLATFORM_ESP32`, `CAN_CFG_ENABLE_BACKEND_ESP32_TWAI 1`, other backends as you need,
`CAN_CFG_TWAI_CLK_HZ` (80 MHz on ESP32, S2, S3, C3).

```c
CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI);
can1.hw.tx_gpio = 5;           /* GPIO numbers wired to the transceiver TXD / RXD */
can1.hw.rx_gpio = 4;
CanLib_Init(&can1);
CanLib_Start(&can1);
```

The ESP-IDF TWAI driver is installed by the library. Only one TWAI instance at a time
(a second one returns `CAN_ERR_BUSY`). CAN FD is not supported by the TWAI hardware.

### 7.4 MCP2515 (any MCU)

`can_cfg.h`: `CAN_CFG_ENABLE_BACKEND_MCP25XX 1`.

STM32: CubeMX SPI in *Full-Duplex Master*, 8 bit, MSB first, CPOL low, CPHA 1 edge, software NSS,
baud rate <= 10 MHz, CS pin as GPIO output (initial level high).

```c
CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP25XX);   /* variant defaults to MCP2515 */
can1.hw.handle  = &hspi1;      /* SPI_HandleTypeDef* */
can1.hw.cs_port = GPIOA;       /* chip select */
can1.hw.cs_pin  = GPIO_PIN_4;
can1.hw.osc_hz  = 8000000UL;   /* the crystal on YOUR board: usually 8 MHz or 16 MHz */
CanLib_Init(&can1);
CanLib_Start(&can1);
```

ESP32: create the device with `spi_bus_add_device()` (mode 0, `spics_io_num` set to your CS pin) and pass
the `spi_device_handle_t` as `hw.handle`. `hw.cs_port/cs_pin` are unused on ESP32 (ESP-IDF drives CS).

Wrong `osc_hz` is the most common mistake: the bit rate is silently wrong. Read the number on the crystal.

### 7.5 MCP2517FD / MCP2518FD (any MCU)

`can_cfg.h`: `CAN_CFG_ENABLE_BACKEND_MCP251XFD 1`, and `CAN_CFG_ENABLE_FD 1` for CAN FD.

```c
CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_MCP251XFD);
can1.cfg.mcp_variant = CAN_MCP_2518FD;         /* or CAN_MCP_2517FD */
can1.hw.handle  = &hspi1;
can1.hw.cs_port = GPIOA;
can1.hw.cs_pin  = GPIO_PIN_4;
can1.hw.osc_hz  = 40000000UL;                  /* 40, 20 or 4 MHz crystals are supported */
#if CAN_CFG_ENABLE_FD
can1.cfg.fd_enabled   = true;
can1.cfg.data.bitrate = CAN_FD_BITRATE_2M;
#endif
CanLib_Init(&can1);
CanLib_Start(&can1);
```

With a 4 MHz crystal the chip PLL (x10) is used. With a 20 MHz crystal the system clock is 20 MHz, so lower the
SPI clock accordingly (0.85 x SYSCLK / 2 = 8.5 MHz). The library uses an 8 / 16 / 32-object FIFO layout in the chip
RAM and keeps the chip ECC on (see `can_mcp251xfd.h`).

### 7.6 Virtual backend (PC)

`CAN_CFG_ENABLE_BACKEND_VIRTUAL 1`, `CAN_CFG_PLATFORM CAN_PLATFORM_GENERIC`. Frames sent in
`CANLIB_MODE_LOOPBACK` come back on the receive side. `can_port_virtual.h` has functions that inject received
frames and simulate bus-off, full mailboxes and error counters. This is how the tests exercise the core.

---

## 8. Configuration (can_cfg.h)

Set a macro to `1` to enable, `0` to disable. Disabled code is not compiled (no flash, no RAM).
Mistakes are caught at compile time with `#error` messages (for example two STM32 backends, CAN FD without an FD backend).

### 8.1 Platform and backends

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_PLATFORM` | `CAN_PLATFORM_STM32` | `..._STM32`, `..._ESP32` or `..._GENERIC` (host). |
| `CAN_CFG_ENABLE_BACKEND_STM32_BXCAN` | 1 | bxCAN backend. |
| `CAN_CFG_ENABLE_BACKEND_STM32_FDCAN` | 0 | FDCAN backend (not together with bxCAN). |
| `CAN_CFG_ENABLE_BACKEND_ESP32_TWAI` | 0 | TWAI backend. |
| `CAN_CFG_ENABLE_BACKEND_MCP25XX` | 1 | MCP2515 backend. |
| `CAN_CFG_ENABLE_BACKEND_MCP251XFD` | 0 | MCP2517FD / 2518FD backend. |
| `CAN_CFG_ENABLE_BACKEND_VIRTUAL` | 0 | Virtual backend (tests). |

Several backends may be enabled together; each instance picks one with `CanLib_SetDefaults(.., backend)`.

### 8.2 Features

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_ENABLE_XCVR_CONTROL` | 1 | Library may drive the transceiver mode pin. |
| `CAN_CFG_ENABLE_FD` | 0 | CAN FD frames (64-byte payload, larger structs). |
| `CAN_CFG_ENABLE_EXT_ID` | 1 | 29-bit identifiers. |
| `CAN_CFG_ENABLE_REMOTE_FRAMES` | 1 | Remote frames. |
| `CAN_CFG_ENABLE_FILTERS` | 1 | Software acceptance filters. |
| `CAN_CFG_ENABLE_RX_QUEUE` | 1 | Software receive ring buffer. |
| `CAN_CFG_ENABLE_TX_QUEUE` | 1 | Software transmit ring buffer (used when the hardware is busy). |
| `CAN_CFG_ENABLE_CALLBACKS` | 1 | `on_rx` / `on_event` callbacks. |
| `CAN_CFG_ENABLE_STATS` | 1 | Traffic and error statistics. |
| `CAN_CFG_ENABLE_MANUAL_SEND` | 1 | Manual send from `can.tx.header` / `can.tx.body`. |
| `CAN_CFG_ENABLE_BUS_OFF_RECOVERY` | 1 | Automatic bus-off recovery. |

### 8.3 Sizes (per instance, static)

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_MAX_INSTANCES` | 2 | Number of CAN channels in the firmware. |
| `CAN_CFG_RX_QUEUE_DEPTH` | 16 | Frames in the receive buffer. |
| `CAN_CFG_TX_QUEUE_DEPTH` | 8 | Frames in the transmit buffer. |
| `CAN_CFG_MAX_FILTERS` | 8 | Software filter slots. |
| `CAN_CFG_QUEUE_WARN_PERCENT` | 75 | Fill level that raises a "near full" warning. |
| `CAN_CFG_PROCESS_MAX_RX` | 8 | Hardware frames read per `CanLib_Process()` call. |

RAM use: one frame is 36 bytes without FD and 92 bytes with FD, times the two buffer depths, times the
number of instances (with the default sizes `CanLib_t` is about 1.7 KB without FD and 3.2 KB with FD). `can.rx.queue.high_water` tells you how deep the receive buffer really got, so you can size it.

### 8.4 Default bus parameters

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_DEFAULT_BITRATE` | 500000 | Nominal bit rate (bit/s). |
| `CAN_CFG_DEFAULT_SAMPLE_POINT` | 875 | Nominal sample point in 1/1000. |
| `CAN_CFG_DEFAULT_FD_BITRATE` | 2000000 | FD data-phase bit rate. |
| `CAN_CFG_DEFAULT_FD_SAMPLE_POINT` | 750 | FD data-phase sample point. |
| `CAN_CFG_BUS_OFF_RECOVER_MS` | 100 | Wait before a bus-off recovery attempt. |

Each can be changed per instance at run time after `CanLib_SetDefaults()`:
`can1.cfg.nominal.bitrate = 250000;`. The library searches the prescaler and segments that give the
**exact** bit rate with the sample point closest to the request. If no exact timing exists with your clock,
`CanLib_Init()` returns `CAN_ERR_BITRATE` instead of silently running at a wrong rate.
After init, `can.cfg.nominal.actual_bitrate` (and `.prescaler`, `.tseg1`, `.tseg2`) show what was programmed.

### 8.5 Platform specific

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_STM32_HAL_HEADER` | `"stm32f4xx_hal.h"` | HAL header of your family (STM32 only). |
| `CAN_CFG_FDCAN_CLK_HZ` | 160000000 | FDCAN kernel clock in Hz. **Must match CubeMX.** |
| `CAN_CFG_FDCAN_USE_IRQ` | 0 | 1 = receive interrupt (call `CanFdcan_OnRxFifo0Pending`). |
| `CAN_CFG_FDCAN_MESSAGE_RAM` | 0 | 1 = STM32H7 style message RAM layout is filled by the library. |
| `CAN_CFG_FDCAN_RAM_TOTAL_WORDS` | 2560 | H7: shared message RAM size in words. |
| `CAN_CFG_FDCAN_RAM_TX_ELEMENTS` | 8 | H7: TX FIFO elements per instance. |
| `CAN_CFG_FDCAN_RAM_RX_ELEMENTS` | 16 | H7: RX FIFO0 elements per instance. |
| `CAN_CFG_FDCAN_RAM_OFFSET(inst)` | block per instance | H7: first RAM word of an instance (overridable). |
| `CAN_CFG_BXCAN_USE_IRQ` | 0 | 1 = bxCAN receive interrupt (call `CanBxcan_OnRxFifo0Pending`). |
| `CAN_CFG_TWAI_CLK_HZ` | 80000000 | TWAI clock. |
| `CAN_CFG_TWAI_BRP_MAX` | 128 | Largest TWAI prescaler of your chip. |
| `CAN_CFG_TWAI_TX_QUEUE_LEN` / `_RX_QUEUE_LEN` | 4 / 8 | ESP-IDF driver queues. |
| `CAN_CFG_TWAI_RECOVER_WAIT_MS` | 100 | Longest wait in a bus-off recovery (blocks the caller). |
| `CAN_CFG_MCP_DEFAULT_OSC_HZ` | 16000000 | Default crystal for MCP2515 (set `hw.osc_hz` instead). |
| `CAN_CFG_MCP_SPI_TIMEOUT_MS` | 10 | Timeout of one SPI transfer (STM32 HAL). |

### 8.6 FreeRTOS

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_USE_FREERTOS` | 0 | 1 = mutex, blocking receive, library task. See section 10. |
| `CAN_CFG_RTOS_AUTO_TASK` | 1 | The library creates its own processing task per instance. |
| `CAN_CFG_RTOS_TASK_STACK_WORDS` | 768 | Library task stack (words; ESP32 counts it in bytes x4). |
| `CAN_CFG_RTOS_TASK_PRIORITY` | 3 | Library task priority. |
| `CAN_CFG_RTOS_TASK_NAME` | `"can"` | Task name prefix. |
| `CAN_CFG_RTOS_POLL_MS` | 10 | Library task wakes at least this often. |
| `CAN_CFG_RTOS_MUTEX_TIMEOUT_MS` | 20 | Longest wait for the instance mutex. |
| `CAN_CFG_RTOS_IN_ISR()` | per platform | "Am I in an interrupt?" test; define it on unusual ports. |

### 8.7 Debug

| Macro | Default | Meaning |
|---|---|---|
| `CAN_CFG_ENABLE_DEBUG` | 1 | File/line capture, event log, log macros. |
| `CAN_CFG_DEBUG_LEVEL` | 3 | Compile-time ceiling: 0 none, 1 error, 2 warning, 3 info, 4 debug. |
| `CAN_CFG_DEBUG_USE_PRINTF` | 0 | 1 = print log lines. |
| `CAN_CFG_PRINTF_HEADER` / `CAN_CFG_PRINTF_FN` | `<stdio.h>` / `printf` | Retarget to your UART print function. |
| `CAN_CFG_DEBUG_EVENT_LOG_SIZE` | 16 | Entries of the event ring (0 = none). |
| `CAN_CFG_ENABLE_ASSERT` | 1 | Internal `CAN_ASSERT` checks. |
| `CAN_CFG_ENABLE_PARAM_CHECK` | 1 | Validate every argument, return `CAN_ERR_BAD_PARAM`. |

### 8.8 Release build

For the smallest build set `CAN_CFG_ENABLE_DEBUG 0`, `CAN_CFG_ENABLE_ASSERT 0`, `CAN_CFG_ENABLE_PARAM_CHECK 0`
and turn off features you do not use. The debug options cost flash (file names and function names are stored for the
last-error report), so switch them off for production.

### 8.9 Critical sections

If frames reach the library from an interrupt (`CanLib_OnRxFrame`, or the `*_USE_IRQ` options) and are read in
`main` or a task, the ring buffers are shared and must be protected. Define before including or edit in `can_cfg.h`:

```c
#define CAN_CFG_ENTER_CRITICAL()   __disable_irq()      /* bare-metal STM32 */
#define CAN_CFG_EXIT_CRITICAL()    __enable_irq()
```

With `CAN_CFG_USE_FREERTOS 1` this is mapped automatically to FreeRTOS critical sections (ESP32 uses a spinlock).

---

## 9. Using the API

Include `can_lib.h`. Every function returns a `CanResult_e`: `CAN_OK` (0), a **warning** (positive: it worked but
look at it) or an **error** (negative). Use `CAN_IS_ERR(r)` to test for failure only.

### 9.1 Life cycle

```c
CanResult_e CanLib_SetDefaults(CanLib_t *can, const char *name, CanBackend_e backend); /* 1. clear + defaults */
/* 2. change what you need: can.cfg, can.hw, can.cb, filters */
CanResult_e CanLib_Init(CanLib_t *can);     /* 3. configure the controller, not on the bus yet */
CanResult_e CanLib_Start(CanLib_t *can);    /* 4. join the bus */
CanResult_e CanLib_Stop(CanLib_t *can);     /* leave the bus */
CanResult_e CanLib_DeInit(CanLib_t *can);   /* release everything */
```

State is in `can.status.state`: `CAN_STATE_UNINIT -> READY -> RUNNING -> STOPPED` (or `FAULT`).

### 9.2 Running the library: `CanLib_Process()`

```c
CanResult_e CanLib_Process(CanLib_t *can);
```

Call it **often** from the main loop (every few ms), or let the FreeRTOS library task do it. It:

1. reads frames from the controller into the receive buffer (up to `CAN_CFG_PROCESS_MAX_RX` per call),
2. reads the error counters, updates `can.status.bus_state`, raises warnings and events,
3. performs the automatic bus-off recovery,
4. sends queued transmit frames when the hardware has room,
5. sends the manual frame if `can.tx.manual_request` is set.

If you receive in an interrupt (`*_USE_IRQ 1`), step 1 is done there and `CanLib_Process()` still handles
2 to 5. It returns `CAN_ERR_BAD_STATE` (not reported as an error) while the node is not started.

### 9.3 Sending

```c
uint8_t d[8] = {1,2,3,4,5,6,7,8};

CanLib_Send(&can1, 0x123, d, 8);                         /* standard ID, classic */
CanLib_SendExt(&can1, 0x18FF1234UL, d, 8);               /* extended 29-bit ID */
CanLib_SendRemote(&can1, 0x123, CAN_ID_STANDARD, 4);     /* remote frame, requested length 4 */
CanLib_SendFd(&can1, 0x200, buf, 24, true);              /* CAN FD, 24 bytes, bit-rate switch on */
```

For full control build a `CanFrame_t` and call `CanLib_SendFrame(&can1, &frame)`:

```c
CanFrame_t f = {0};
f.header.id = 0x321;  f.header.id_type = CAN_ID_STANDARD;
f.header.frame_type = CAN_FRAME_DATA;  f.header.format = CAN_FORMAT_CLASSIC;
f.header.data_len = 2;  f.body.data[0] = 0xAA;  f.body.data[1] = 0x55;
CanLib_SendFrame(&can1, &f);
```

Behavior:

* If the hardware has a free mailbox, the frame goes out at once.
* If not, it is stored in the transmit queue and `CanLib_Process()` sends it later (`status.tx_pending`).
* A full queue returns `CAN_ERR_TX_FULL` and counts in `can.tx.queue.dropped`.
* `CAN_WARN_TX_QUEUE_NEAR_FULL` appears at `CAN_CFG_QUEUE_WARN_PERCENT`.
* Bus-off gives `CAN_ERR_BUS_OFF`. Not started gives `CAN_ERR_BAD_STATE`.
* Lengths above the limit are clamped with `CAN_WARN_DLC_CLAMPED`. CAN FD lengths that are not a valid FD size
  (13, 17, ...) are rounded up to the next one with zero padding.
* An FD request on an instance without FD is sent as a classic frame with `CAN_WARN_FD_FALLBACK`;
  data beyond 8 bytes is cut to 8.

### 9.4 Manual send (from code or the watch window)

Fill `can.tx.header` and `can.tx.body`, then either call `CanLib_SendManual(&can1)` or set
`can1.tx.manual_request = true`; the next `CanLib_Process()` sends it and clears the flag.
In the Keil watch window you can type the ID, length and data bytes and set the request flag by hand,
with no code changes. Handy for quick bus experiments.

### 9.5 Receiving

```c
CanFrame_t f;
while (CanLib_Receive(&can1, &f) == CAN_OK)
{
    /* f.header.id, id_type, frame_type, format, dlc, data_len, timestamp_ms, filter_index */
    /* f.body.data[0 .. f.header.data_len-1] */
}
```

`CAN_ERR_RX_EMPTY` means nothing is waiting; it is normal and is not counted as an error.
`CanLib_Available(&can1)` gives the number of waiting frames.
The most recent frame is also kept in `can.rx.header` / `can.rx.body` with `can.rx.new_frame`, so you can
see traffic in the watch window.

If the receive buffer is full, new frames are dropped (`can.rx.queue.dropped`, `stats.rx_dropped`);
set `can.cfg.rx_overwrite_oldest = true` to drop the oldest frame instead.

### 9.6 Software filters

No filter set = accept everything (the controllers' hardware filters are opened wide, software decides).

```c
CanLib_AddFilterExact(&can1, CAN_ID_STANDARD, 0x123);               /* only 0x123 */
CanLib_AddFilter(&can1, CAN_ID_STANDARD, 0x100, 0x700);             /* 0x100..0x1FF: (id & 0x700) == 0x100 */
CanLib_AddFilter(&can1, CAN_ID_EXTENDED, 0x18FF0000UL, 0x1FFF0000UL);
CanLib_ClearFilters(&can1);                                         /* accept all again */
```

Up to `CAN_CFG_MAX_FILTERS` filters; more returns `CAN_ERR_FILTER_FULL`. A frame passes if any filter matches.
The matching filter number is in `header.filter_index`.

### 9.7 Callbacks

```c
static void on_rx(CanLib_t *can, const CanFrame_t *f, void *ctx) { /* keep it short */ }
static void on_event(CanLib_t *can, CanEvent_e ev, int32_t arg, void *ctx)
{
    if (ev == CAN_EVENT_BUS_OFF) { /* ... */ }
}

can1.cb.on_rx = on_rx;  can1.cb.on_event = on_event;  can1.cb.user_ctx = NULL;
```

Events: `CAN_EVENT_TX_DONE`, `CAN_EVENT_RX_OVERFLOW`, `CAN_EVENT_ERROR`, `CAN_EVENT_BUS_OFF`,
`CAN_EVENT_BUS_RECOVERED`, `CAN_EVENT_STATE_CHANGE`. Callbacks may run in an interrupt (for the interrupt receive
path) or in the library task: do not block and do not printf in them.

### 9.8 Transceiver mode pin

```c
can1.hw.xcvr_port = GPIOB;               /* STM32: GPIO port. ESP32: ignored */
can1.hw.xcvr_pin  = GPIO_PIN_0;          /* STM32: GPIO_PIN_x. ESP32: GPIO number */
can1.hw.xcvr_standby_level = true;       /* level that switches the transceiver off normal mode (TJA: high) */
can1.cfg.xcvr_type = CAN_XCVR_TJA10XX;

CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_NORMAL);    /* transmit and receive */
CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_SILENT);    /* receive only */
CanLib_SetXcvrMode(&can1, CAN_XCVR_MODE_STANDBY);   /* low power */
```

Normal mode drives the pin to the opposite of `xcvr_standby_level`; silent and standby drive it to
`xcvr_standby_level`. Without a configured pin the call returns `CAN_ERR_NOT_SUPPORTED`. The GPIO must already be
configured as an output by you (CubeMX / `gpio_config`).

### 9.9 Controller modes

`can1.cfg.mode`: `CANLIB_MODE_NORMAL`, `CANLIB_MODE_LISTEN_ONLY` (never drives the bus, no ACK) or
`CANLIB_MODE_LOOPBACK` (internal self test). Set before `CanLib_Init()`.

Other options: `can1.cfg.auto_retransmit` (resend after lost arbitration/error, default true),
`can1.cfg.bus_off_auto_recover` and `can1.cfg.bus_off_recover_ms`.

### 9.10 Complete bare-metal example (STM32 bxCAN)

```c
#include "can_lib.h"

static CanLib_t can1;

static void on_rx(CanLib_t *can, const CanFrame_t *f, void *ctx) { (void)can; (void)f; (void)ctx; }

int main(void)
{
    HAL_Init(); SystemClock_Config(); MX_GPIO_Init(); MX_CAN1_Init();

    CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_STM32_BXCAN);
    can1.hw.handle           = &hcan1;
    can1.cfg.nominal.bitrate = CAN_BITRATE_500K;
    can1.cb.on_rx            = on_rx;
    (void)CanLib_AddFilter(&can1, CAN_ID_STANDARD, 0x100, 0x7F0);

    if (CAN_IS_ERR(CanLib_Init(&can1)))  { Error_Handler(); }   /* see can1.status.last_error */
    if (CAN_IS_ERR(CanLib_Start(&can1))) { Error_Handler(); }

    uint32_t t = HAL_GetTick();
    for (;;)
    {
        CanFrame_t f;
        (void)CanLib_Process(&can1);
        while (CanLib_Receive(&can1, &f) == CAN_OK)
        {
            (void)CanLib_Send(&can1, f.header.id + 1U, f.body.data, f.header.data_len);   /* echo +1 */
        }
        if ((HAL_GetTick() - t) >= 1000U)
        {
            uint8_t hb[1] = { 0x55 };
            t = HAL_GetTick();
            (void)CanLib_Send(&can1, 0x700, hb, 1);                  /* heartbeat */
        }
    }
}
```

---

## 10. FreeRTOS

Set `CAN_CFG_USE_FREERTOS 1`. Your code does not change; the same functions become thread safe.

| What | How |
|---|---|
| Thread safety | A recursive mutex per instance around every public call. Callbacks may call `CanLib_Send()`. |
| Library task | `CAN_CFG_RTOS_AUTO_TASK 1`: one task per instance calls `CanLib_Process()` for you. It wakes on a received frame, on a queued frame and every `CAN_CFG_RTOS_POLL_MS`. |
| Blocking receive | `CanLib_ReceiveWait(&can1, &f, timeout_ms)` sleeps on a semaphore (`CAN_WAIT_FOREVER` for no limit). Works without FreeRTOS too (it then polls). |
| Interrupt wake | `CanRtos_NotifyFromISR(&can1)` from the INT pin handler of an MCP chip wakes the task at once. |
| Critical sections | Mapped to FreeRTOS automatically. |

```c
CanLib_Init(&can1);          /* creates mutex, semaphore and the library task */
CanLib_Start(&can1);

void app_task(void *arg)
{
    CanFrame_t f;
    for (;;)
    {
        if (CanLib_ReceiveWait(&can1, &f, 100) == CAN_OK) { /* handle f */ }
        CanLib_Send(&can1, 0x123, data, 8);      /* safe from any task */
    }
}
```

Rules:

* Only `CanLib_OnRxFrame()` and `CanRtos_NotifyFromISR()` may be called from an interrupt. Any other library call
  from an interrupt returns `CAN_ERR_MUTEX`.
* Do not call `CanLib_DeInit()` while other tasks still use the instance.
* Callbacks run in the library task (or an interrupt): keep them short.
* Without the library task (`CAN_CFG_RTOS_AUTO_TASK 0`) call `CanLib_Process()` yourself;
  `CanLib_ReceiveWait()` then runs it for you while it waits.
* Check `can.rtos.stack_free_words` (smallest free stack of the library task so far) and `can.rtos.lock_timeouts`
  in the watch window. If the stack gets close to 0, raise `CAN_CFG_RTOS_TASK_STACK_WORDS`. This matters most with
  `CAN_CFG_DEBUG_USE_PRINTF 1`, because `printf` uses a lot of stack.
* On STM32 give the SysTick/HAL timebase and FreeRTOS the usual priorities; keep CAN interrupt priorities numerically
  at or above `configMAX_SYSCALL_INTERRUPT_PRIORITY` if the interrupt calls the FreeRTOS `FromISR` functions.

---

## 11. Debugging

The library is built so that a problem is visible within seconds, with **no printf needed**.

### 11.1 Watch window (Keil, CubeIDE live expressions, VS Code)

Add the main struct `can1` (or `&can1` with the structure expanded). The useful members:

| Expression | What it tells you |
|---|---|
| `can1.status.state` | `UNINIT / READY / RUNNING / STOPPED / FAULT`. |
| `can1.status.bus_state` | `ERROR_ACTIVE / WARNING / PASSIVE / BUS_OFF`. |
| `can1.status.last_error`, `last_warning` | The most recent error (negative) and warning (positive) code. |
| `can1.status.error_count`, `warning_count` | How many have happened. |
| `can1.debug.last_error_file / _func / _line` | **Exactly where in the library** the last error was raised. |
| `can1.debug.log[]`, `log_head`, `log_count` | Ring of the last events: init, start, tx, rx, warnings, errors, bus-off, recover. |
| `can1.cfg.nominal` | Requested `bitrate`, and what was really programmed: `actual_bitrate`, `prescaler`, `tseg1`, `tseg2`, `sjw`. |
| `can1.cfg.data` | The same for the CAN FD data phase. |
| `can1.bus.tec`, `can1.bus.rec`, `can1.bus.hw_error_flags` | Hardware error counters and the raw error flags of the chip. |
| `can1.stats` | `tx_ok`, `tx_fail`, `rx_ok`, `rx_dropped`, `err_*`, `bus_off_count`, `last_tx_ms`, `last_rx_ms`. |
| `can1.rx.header`, `can1.rx.body`, `can1.rx.new_frame` | The last received frame. |
| `can1.rx.queue`, `can1.tx.queue` | `count`, `high_water` (use it to size the buffers), `dropped`. |
| `can1.tx.header`, `can1.tx.body`, `can1.tx.manual_request` | Type a frame, set the flag: it is sent. |
| `can1.port_ctx` | Backend private state (see below). |
| `g_CanLastError`, `g_CanLastWarning`, `g_CanErrorTotal`, `g_CanWarningTotal` | Globals valid even without an instance pointer. |
| `g_CanAssertLine`, `g_CanAssertFile` | Location of a failed internal assertion. |

The backend context (`can1.port_ctx`, cast to the backend type, for example `(CanMcpFdCtx_t*)can1.port_ctx`)
holds backend debug values: last raw status registers, SPI error counts, HAL error codes and failure counters.

To see what a numeric code means, read section 12, or call `CanLib_ResultToStr(code)`.

### 11.2 printf logging (optional)

```c
#define CAN_CFG_ENABLE_DEBUG       1
#define CAN_CFG_DEBUG_USE_PRINTF   1
#define CAN_CFG_PRINTF_HEADER      <stdio.h>        /* or your own header */
#define CAN_CFG_PRINTF_FN          printf           /* or your UART print function */
#define CAN_CFG_DEBUG_LEVEL        4                /* 1 error, 2 warning, 3 info, 4 debug */
```

At run time `can1.debug.log_level` (`CAN_LOG_NONE .. CAN_LOG_DEBUG`) lowers the level further (it cannot exceed the
compile-time level). The macros `CAN_LOGE / CAN_LOGW / CAN_LOGI / CAN_LOGD(&can1, "text %d", x)` are available for
your own messages. Lines look like `[CAN][E] ...`. Do not log inside interrupts.

### 11.3 Return codes

Every function returns a code you can test and print. Errors are negative, warnings positive.

```c
CanResult_e r = CanLib_Init(&can1);
if (CAN_IS_ERR(r)) { printf("init failed: %s\r\n", CanLib_ResultToStr(r)); }
```

### 11.4 Assertions

Internal consistency checks call `CanDbg_AssertFailed(file, line)`, which stores the location in the globals and
halts. It is a weak function: override it to reset the chip or break into the debugger.
`CAN_CFG_ENABLE_PARAM_CHECK 1` turns bad user arguments into `CAN_ERR_BAD_PARAM` (with file/line) instead of
undefined behavior.

### 11.5 A 60-second diagnosis

1. `can1.status.last_error` and `can1.debug.last_error_line` tell **what** and **where**.
2. `can1.cfg.nominal.actual_bitrate` equal to the bit rate you asked for? If not, the clock macro is wrong.
3. `can1.bus.tec / rec` climbing: the bus has problems (section 13).
4. `can1.stats.tx_ok` not increasing while `tx_fail` does: nobody ACKs, or the bit rates differ.
5. `can1.rx.queue.high_water` close to the depth: read faster or raise `CAN_CFG_RX_QUEUE_DEPTH`.

---

## 12. Result codes

Test with `CAN_IS_ERR(r)`. `CanLib_ResultToStr(r)` returns the name.

**Warnings (positive, the call worked):**

| Code | Meaning |
|---|---|
| `CAN_WARN_RX_QUEUE_NEAR_FULL` | Receive buffer above the warn level: read faster. |
| `CAN_WARN_TX_QUEUE_NEAR_FULL` | Transmit buffer above the warn level: send slower. |
| `CAN_WARN_ERROR_WARNING` | Error counters reached 96. |
| `CAN_WARN_ERROR_PASSIVE` | Node is error passive (above 127): bus trouble. |
| `CAN_WARN_DLC_CLAMPED` | Data length was reduced to the frame format limit. |
| `CAN_WARN_FD_FALLBACK` | FD requested but not available: sent as classic. |
| `CAN_WARN_FILTER_IGNORED` | Filter call ignored because filters are disabled in `can_cfg.h`. |
| `CAN_WARN_BUS_RECOVERED` | Bus-off was left, node is back on the bus. |

**Errors (negative):**

| Code | Typical cause |
|---|---|
| `CAN_ERR_NULL_PTR` | NULL argument, or `hw.handle` not set. |
| `CAN_ERR_NOT_INIT` | Function used before `CanLib_Init()`. |
| `CAN_ERR_BAD_PARAM` | Argument out of range; unsupported crystal (MCP251xFD); H7 RAM blocks do not fit. |
| `CAN_ERR_BAD_STATE` | Call not allowed now (for example send before `CanLib_Start()`). |
| `CAN_ERR_BACKEND_DISABLED` | The backend is switched off in `can_cfg.h`. |
| `CAN_ERR_BACKEND_FAIL` | HAL / driver call failed. See the backend context (`hal_error`, `driver_failures`). |
| `CAN_ERR_SPI` | SPI transfer failed or a register read-back mismatched: wiring, SPI mode, clock, CS. |
| `CAN_ERR_CHIP_NOT_FOUND` | MCP chip did not answer: power, crystal, wiring, CS, wrong SPI mode. |
| `CAN_ERR_BITRATE` | No exact timing for this clock and bit rate: check the clock macro / crystal / bit rate. |
| `CAN_ERR_TX_FULL` | No free mailbox and the transmit queue is full. |
| `CAN_ERR_RX_EMPTY` | Nothing received (normal, not reported). |
| `CAN_ERR_RX_OVERFLOW` | A frame was lost: buffer full. |
| `CAN_ERR_TX_TIMEOUT` | Transmission did not finish in time. |
| `CAN_ERR_BUS_OFF` | Node is bus-off and cannot send. |
| `CAN_ERR_ACK` / `STUFF` / `FORM` / `CRC` / `BIT` | Bus level errors (counted in `stats.err_*`). |
| `CAN_ERR_FILTER_FULL` | No free software filter slot. |
| `CAN_ERR_MUTEX` | FreeRTOS mutex not obtained in time, or a library call was made from an interrupt. |
| `CAN_ERR_RTOS` | FreeRTOS object creation failed (heap too small?). |
| `CAN_ERR_NOT_SUPPORTED` | Feature not available in this backend (FD on MCP2515, TWAI FD, no transceiver pin...). |
| `CAN_ERR_BUSY` | Resource busy (for example a second TWAI instance). |
| `CAN_ERR_TIMEOUT` | Generic timeout (for example the MCP chip did not enter a mode: bus shorted?). |

---

## 13. Troubleshooting

### First test on a new board: loopback

```c
can1.cfg.mode = CANLIB_MODE_LOOPBACK;      /* before CanLib_Init() */
...
CanLib_Send(&can1, 0x123, data, 8);
CanLib_Process(&can1);
CanLib_Receive(&can1, &f);                 /* the frame must come back */
```

Loopback needs no bus, no transceiver and no second node. It proves: clock, bit timing, driver, SPI link, FIFOs,
the library. If loopback fails, the problem is not the bus.

### Then: two nodes

Connect two boards (CANH-CANH, CANL-CANL, GND, 120 Ω at both ends), same bit rate on both, normal mode.
`stats.tx_ok` increases only when the other node ACKs.

### Symptom table

| Symptom | Likely cause | What to check |
|---|---|---|
| `CanLib_Init()` returns `CAN_ERR_NULL_PTR` | `hw.handle` not set | Set `can1.hw.handle`. |
| `CAN_ERR_BITRATE` | Clock macro or crystal wrong, or impossible rate | `CAN_CFG_FDCAN_CLK_HZ`, `hw.osc_hz`, `CAN_CFG_TWAI_CLK_HZ`, APB1 clock. |
| `CAN_ERR_CHIP_NOT_FOUND` (MCP) | No SPI communication | Power, crystal, CS pin, MISO/MOSI swapped, SPI mode 0, SPI clock too high, SPI peripheral not enabled. |
| `CAN_ERR_SPI` (MCP) | Read-back mismatch / HAL SPI failure | Slow the SPI to 1 MHz, check wiring and ground, check `port_ctx->spi_errors`. |
| `CAN_ERR_TIMEOUT` on `CanLib_Start()` (MCP) | Chip cannot enter normal mode | Bus shorted / no transceiver power. Try loopback. |
| Bit rate looks wrong on a scope or other node does not understand | Wrong crystal or clock value | Compare `can.cfg.nominal.actual_bitrate` with the request and with reality (measure the crystal). |
| Send works, `tx_ok` stays 0, `tec` rises | No other node ACKs / bit rate mismatch / no termination | Second node present and same rate? 60 Ω across the bus? Transceiver powered and STB low? |
| `bus_state` = `ERROR_PASSIVE` or `BUS_OFF` | Persistent bus errors | Termination, wiring, bit rate, sample point, ground. Auto recovery brings it back, errors return until the cause is fixed. |
| Nothing received | Filters, wrong bit rate, listen-only mode | `CanLib_ClearFilters()`, same bit rate, `can.cfg.mode`, check `can.rx.new_frame`. |
| Frames lost | Receive buffer too small or `Process` too rare (`stats.rx_dropped` also counts frames rejected by your software filters) | `can.rx.queue.high_water`, `dropped`, `stats.rx_dropped`; call `CanLib_Process` more often, raise queue depth, use IRQ or FreeRTOS. |
| CAN FD frames fail but classic works | Transceiver not FD capable, data rate too high, bad data sample point | Use TJA1042/1051/1057 or ISO1042, try 1 Mbit/s data, shorter stubs, proper termination. |
| `CAN_ERR_BAD_STATE` on send | Not started | Call `CanLib_Start()`. |
| `CAN_ERR_TX_FULL` | Bus busy / nobody ACKs and queue filled | Fix the bus, raise `CAN_CFG_TX_QUEUE_DEPTH`, send slower. |
| `CAN_ERR_MUTEX` | Library call inside an interrupt, or the mutex is held too long | Use `CanRtos_NotifyFromISR` / `CanLib_OnRxFrame` in ISRs; check `rtos.lock_timeouts`. |
| Hard fault or stack overflow in the library task | Stack too small | Raise `CAN_CFG_RTOS_TASK_STACK_WORDS`, check `rtos.stack_free_words`. |
| H7: `CanLib_Init()` returns `CAN_ERR_BAD_PARAM` | Message RAM blocks do not fit | Lower `CAN_CFG_FDCAN_RAM_*_ELEMENTS` or `CAN_CFG_MAX_INSTANCES`; override `CAN_CFG_FDCAN_RAM_OFFSET`. |
| Compile error `CAN FD needs the FDCAN, MCP251XFD, or VIRTUAL backend` | FD enabled with a classic-only backend set | Enable an FD backend or set `CAN_CFG_ENABLE_FD 0`. |
| Linker error: undefined `CanPlatform_*` | Platform file not compiled | Add `can_platform_stm32.c` or `can_platform_esp32.c` and set `CAN_CFG_PLATFORM`. |
| Timestamps all 0 | No platform file | Same as above (`CanLib_GetTickMs` is a weak default returning 0). |

---

## 14. Backend notes and limits

**General**

* Hardware acceptance filters are always opened wide; filtering is in software. This keeps all backends identical
  and makes filter changes instant; very high bus load on a slow MCU may justify hardware filters later.
* `CanLib_Process()` runs on one context at a time per instance (without FreeRTOS: you; with FreeRTOS: the mutex).
* `Process` / `Start` / `Init` of the MCP backends use short blocking delays (a few ms) for chip reset and mode changes.

**bxCAN:** timing from `HAL_RCC_GetPCLK1Freq()`; classic CAN only; mailbox full leads to the software queue.

**FDCAN:** uses the global filter (accept all into RX FIFO0). Kernel clock from `CAN_CFG_FDCAN_CLK_HZ`.
Nominal prescaler up to 512, data prescaler up to 32. H7 message RAM is described in 7.2.

**TWAI:** one controller. Single-shot transmit when `auto_retransmit` is false. Loopback uses "no ack + self reception".
Bus-off recovery waits up to `CAN_CFG_TWAI_RECOVER_WAIT_MS`.

**MCP2515:** three transmit buffers, two receive buffers with rollover; classic only; an INT pin is optional.

**MCP2517FD / MCP2518FD:** one TX FIFO (8 objects) and one RX FIFO (32 classic or 16 FD objects), no TXQ, no time
stamps, ECC on with RAM cleared at init. Supported crystals 4, 20, 40 MHz. Polling only (no INT pin handling inside
the backend; use `CanRtos_NotifyFromISR` to wake the task).

**Not supported:** CAN FD on bxCAN / TWAI / MCP2515, J1939 / CANopen / ISO-TP protocol layers (build them on top of
`CanFrame_t`), hardware filter banks, TX event / timestamp FIFOs, sleep / wake-up beyond the transceiver pin.

---

## 15. Compile and test on the PC

No hardware needed. The tests run the real library code against fake HAL / driver layers and simulated MCP chips.

### Requirements

`gcc` (or any C99 compiler), `sh`. For the sanitizer builds: `libasan`, `libubsan` and `libtsan` (they ship with gcc on
Linux). On Windows use WSL or MSYS2.

### Run everything

```sh
cd can_lib
sh run_tests.sh              # all suites
sh run_tests.sh fdcan        # one suite: virtual mcp2515 mcp251xfd bxcan fdcan fdcan_h7 twai rtos compile
```

Each suite builds in a private copy with its own `can_cfg.h` (yours is never changed), with
`-Wall -Wextra -Wpedantic -Werror` and `-fsanitize=address,undefined` (thread sanitizer for the FreeRTOS suite).
`ALL SUITES PASSED` is the expected last line.

| Suite | Covers |
|---|---|
| virtual | core API, queues, filters, stats, manual send, debug, `CanLib_ReceiveWait` without FreeRTOS |
| mcp2515 | MCP2515 backend against a register-level simulated chip |
| mcp251xfd | MCP2517FD / 2518FD backend against a simulated chip with message RAM, FIFO indexes and ECC alignment rule |
| bxcan | bxCAN backend against a fake HAL |
| fdcan, fdcan_h7 | FDCAN backend against fake G4 and H7 HALs (polling and IRQ, message RAM layout) |
| twai | TWAI backend against a fake ESP-IDF driver |
| rtos | FreeRTOS layer on a fake pthread kernel: 4 senders and 1 receiver, ISR rules, lock timeouts |
| compile | STM32 platform glue and FDCAN+FreeRTOS combinations compile with the stub SDK headers |

### Build one test by hand

Tests need a matching `can_cfg.h`. Example for the virtual backend:

```sh
mkdir /tmp/t && cp -r can_lib/* /tmp/t && cd /tmp/t
sed -i 's|^\(#define CAN_CFG_PLATFORM  *\).*|\1CAN_PLATFORM_GENERIC|; \
        s|^\(#define CAN_CFG_ENABLE_BACKEND_STM32_BXCAN  *\)1|\10|; \
        s|^\(#define CAN_CFG_ENABLE_BACKEND_MCP25XX  *\)1|\10|; \
        s|^\(#define CAN_CFG_ENABLE_BACKEND_VIRTUAL  *\)0|\11|' can_cfg.h
gcc -std=c99 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined \
    -o t can_core.c can_debug.c can_port.c can_rtos.c can_port_virtual.c test_step1.c && ./t
```

`hal_stub/`, `esp_stub/` and `freertos_stub/` are test-only stand-ins for the vendor headers. They are never part of a
firmware build; in firmware the real HAL / ESP-IDF / FreeRTOS headers are used.

### Compile checks in your firmware

Build with `-Wall -Wextra` and treat warnings seriously: the library is warning-free with `-Wpedantic -Werror` on the
host. Enable one backend at a time first, then combine.

---

## 16. Porting to another controller or MCU

**A new CAN controller:** fill one `CanPortOps_t` (see `can_port.h`) with `init`, `deinit`, `start`, `stop`,
`transmit`, `receive`, `get_errors`, `recover` and optionally `set_xcvr_mode`, add an enable macro in `can_cfg.h`,
an enum value in `can_types.h` and a `case` in `CanPort_Get()` in `can_port.c`. Rules for backends:

* return result codes, never call `CAN_REPORT` (the core reports),
* keep private data in a static array indexed by `can->instance` and point `can->port_ctx` at it,
* return `CAN_ERR_TX_FULL` when no mailbox is free and `CAN_ERR_RX_EMPTY` when nothing is waiting,
* count controller overruns in `can->stats.rx_dropped`.

**A new MCU for the MCP chips:** only the four platform hooks are needed, all declared in `can_port.h`:
`CanLib_GetTickMs()`, `CanPlatform_DelayMs()`, `CanPlatform_WritePin()` and `CanPlatform_SpiTransfer()`
(a full-duplex SPI transfer with chip select). Provide them in your own file (they are weak in the library).

---

## Keywords

CAN bus library, CAN FD library, CANbus driver, embedded C CAN library, STM32 CAN, STM32 FDCAN, STM32 bxCAN,
STM32H7 FDCAN message RAM, STM32CubeMX CAN, HAL CAN driver, Keil CAN example, ESP32 CAN, ESP32 TWAI, ESP-IDF CAN,
Arduino ESP32 CAN, MCP2515, MCP2517FD, MCP2518FD, MCP25xx SPI CAN controller, TJA1050, TJA1051, TJA1042, TJA1057,
ISO1042, ISO1050, isolated CAN transceiver, CAN transceiver standby pin, FreeRTOS CAN, CAN bus bit timing calculator,
CAN bus debugging, CAN loopback test, automotive, industrial, robotics, vehicle network.

---

## FAQ

**Which CAN library works on both STM32 and ESP32?**
This one. One API (`CanLib_Init`, `CanLib_Send`, `CanLib_Receive`, `CanLib_Process`) with backends for the STM32 bxCAN
and FDCAN peripherals, the ESP32 TWAI controller and the MCP25xx SPI chips. You change the backend in `can_cfg.h`.

**How do I use an MCP2515 (or MCP2517FD / MCP2518FD) CAN module with STM32 or ESP32?**
Enable `CAN_CFG_ENABLE_BACKEND_MCP25XX` (or `..._MCP251XFD` for CAN FD), connect SPI and a chip-select pin, set the
crystal frequency in `can1.hw.osc_hz`, and call `CanLib_Init()` / `CanLib_Start()`. Sections 5.4 and 7.4 / 7.5.

**How do I use CAN FD on STM32?**
Use a chip with the FDCAN peripheral (G0, G4, L5, U5, H5, H7), set `CAN_CFG_ENABLE_BACKEND_STM32_FDCAN 1`,
`CAN_CFG_ENABLE_FD 1`, the FDCAN kernel clock in `CAN_CFG_FDCAN_CLK_HZ`, and `can1.cfg.fd_enabled = true`. Sections 6a and 7.2.

**Does it support the STM32H7 FDCAN message RAM?**
Yes: set `CAN_CFG_FDCAN_MESSAGE_RAM 1` and the library fills the layout for each instance.

**Can I use it with FreeRTOS?**
Yes, optionally: `CAN_CFG_USE_FREERTOS 1` adds a recursive mutex, a blocking `CanLib_ReceiveWait()` and a library task.
Section 10.

**How do I set up CAN in STM32CubeMX?**
Section 6a lists every CubeMX setting for bxCAN, FDCAN, SPI (for the MCP chips), the transceiver pin and FreeRTOS.

**Does it work with the Arduino IDE?**
For the ESP32 (Arduino-ESP32 core): run `make_arduino_lib.sh` to create the library folder with an example. Section 6b.
Not tested on real boards yet.

**How can I test my CAN code without hardware?**
Use the virtual backend or the host test suites (`sh run_tests.sh`): fake HAL layers and simulated MCP chips.

**How do I debug a CAN bus problem quickly?**
Watch `can1.status.last_error`, `can1.debug.last_error_file/line`, the event log `can1.debug.log[]`, the error counters
`can1.bus.tec / rec` and the statistics `can1.stats`. Section 11 and the troubleshooting table in section 13.

**Which CAN transceivers are supported?**
Any (the transceiver is just wiring). The library can drive a mode / standby pin for TJA10xx and ISO10xx parts. Section 5.2.

**Is it production ready?**
No. It is covered by host tests only and has not run on real hardware yet (section 1). Use it for evaluation and bring-up.

---

*Library version: see `CAN_LIB_VERSION_*` in `can_cfg.h`. All functions, variables, enums and structs are commented in the headers.*
