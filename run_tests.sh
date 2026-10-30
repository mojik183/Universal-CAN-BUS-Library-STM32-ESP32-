#!/bin/sh
# @author: mojik183
# @github: github.com/mojik183
# @ai assistant: claude.ai
# @date: 2026-10-29
# @version: 0.1.0
# run_tests.sh - host tests of the CAN library (no hardware needed).
#
# Every test builds in a private copy of this folder with its own can_cfg.h,
# so your can_cfg.h is never touched. Needs gcc (and libasan / libtsan for the sanitizers).
#
#   sh run_tests.sh          run all suites
#   sh run_tests.sh fdcan    run one suite (virtual, mcp2515, mcp251xfd, bxcan, fdcan, fdcan_h7, twai, wait, rtos, compile)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${TMPDIR:-/tmp}/canlib_tests_$$
CC=${CC:-gcc}
CFLAGS="-std=c99 -Wall -Wextra -Wpedantic -Werror -g"
ASAN="-fsanitize=address,undefined"
CORE="can_core.c can_debug.c can_port.c can_rtos.c"
FAILED=0
ONLY=${1:-all}

# Edit one macro value in the private can_cfg.h:  setcfg NAME VALUE
setcfg() { sed -i "s|^\(#define $1  *\)[^ ]*|\1$2|" can_cfg.h; }

# Fresh private copy: prep NAME
prep() {
    rm -rf "$WORK/$1"; mkdir -p "$WORK/$1"; cp -r "$HERE"/* "$WORK/$1"/; chmod -R u+w "$WORK/$1"
    cd "$WORK/$1" || exit 1
}

# Common config: host build, no vendor SDK, no hardware backend.
host_cfg() {
    setcfg CAN_CFG_PLATFORM CAN_PLATFORM_GENERIC
    setcfg CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 0
    setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 0
}

result() { if [ "$2" -eq 0 ]; then echo "PASS  $1"; else echo "FAIL  $1"; FAILED=$((FAILED+1)); fi; }

want() { [ "$ONLY" = all ] || [ "$ONLY" = "$1" ]; }

if want virtual; then
    for t in test_step1 test_step2 test_wait; do
        prep virtual; host_cfg; setcfg CAN_CFG_ENABLE_BACKEND_VIRTUAL 1
        $CC $CFLAGS $ASAN -o t $CORE can_port_virtual.c $t.c && ./t >/dev/null; result "virtual/$t" $?
    done
fi

if want mcp2515; then
    prep mcp2515; host_cfg; setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 1
    $CC $CFLAGS $ASAN -o t $CORE can_mcp25xx.c test_mcp25xx.c && ./t >/dev/null; result "mcp2515 (simulated chip)" $?
fi

if want mcp251xfd; then
    prep mcp251xfd; host_cfg; setcfg CAN_CFG_ENABLE_BACKEND_MCP251XFD 1; setcfg CAN_CFG_ENABLE_FD 1
    $CC $CFLAGS $ASAN -o t $CORE can_mcp251xfd.c test_mcp251xfd.c && ./t >/dev/null; result "mcp2517fd/2518fd (simulated chip)" $?
fi

if want bxcan; then
    prep bxcan; setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 0
    $CC $CFLAGS $ASAN -Ihal_stub -o t $CORE can_stm32_bxcan.c test_bxcan.c && ./t >/dev/null; result "stm32 bxcan (fake HAL)" $?
fi

if want fdcan; then
    for irq in 0 1; do
        prep fdcan; setcfg CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 0; setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 0
        setcfg CAN_CFG_ENABLE_BACKEND_STM32_FDCAN 1; setcfg CAN_CFG_ENABLE_FD 1
        setcfg CAN_CFG_STM32_HAL_HEADER '"stm32g4xx_hal.h"'; setcfg CAN_CFG_FDCAN_USE_IRQ $irq
        $CC $CFLAGS $ASAN -Ihal_stub -o t $CORE can_stm32_fdcan.c test_fdcan.c && ./t >/dev/null; result "stm32 fdcan G4 (fake HAL, irq=$irq)" $?
    done
fi

if want fdcan_h7; then
    prep fdcan_h7; setcfg CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 0; setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 0
    setcfg CAN_CFG_ENABLE_BACKEND_STM32_FDCAN 1; setcfg CAN_CFG_ENABLE_FD 1; setcfg CAN_CFG_FDCAN_MESSAGE_RAM 1
    setcfg CAN_CFG_STM32_HAL_HEADER '"stm32h7xx_hal.h"'
    $CC $CFLAGS $ASAN -Ihal_stub -o t $CORE can_stm32_fdcan.c test_fdcan.c && ./t >/dev/null; result "stm32 fdcan H7 message RAM (fake HAL)" $?
fi

if want twai; then
    prep twai; host_cfg; setcfg CAN_CFG_PLATFORM CAN_PLATFORM_ESP32; setcfg CAN_CFG_ENABLE_BACKEND_ESP32_TWAI 1
    $CC $CFLAGS $ASAN -Iesp_stub -o t $CORE can_esp32_twai.c test_twai.c && ./t >/dev/null; result "esp32 twai (fake IDF driver)" $?
fi

if want rtos; then
    for auto in 1 0; do
        prep rtos; host_cfg; setcfg CAN_CFG_ENABLE_BACKEND_VIRTUAL 1; setcfg CAN_CFG_USE_FREERTOS 1; setcfg CAN_CFG_RTOS_AUTO_TASK $auto
        $CC $CFLAGS -fsanitize=thread -pthread -D'CAN_CFG_RTOS_IN_ISR()=(fake_in_isr)' -Ifreertos_stub -o t $CORE can_port_virtual.c freertos_stub/freertos_fake.c test_rtos.c && ./t >/dev/null 2>&1; result "freertos layer (fake kernel, thread sanitizer, auto_task=$auto)" $?
    done
fi

if want compile; then
    # Compile-only checks of the vendor glue (needs the stubs, not a real SDK).
    prep compile; setcfg CAN_CFG_ENABLE_BACKEND_MCP251XFD 1; setcfg CAN_CFG_ENABLE_FD 1
    ok=0; for f in can_core.c can_port.c can_mcp25xx.c can_mcp251xfd.c can_stm32_bxcan.c can_platform_stm32.c; do
        $CC $CFLAGS -c -Ihal_stub -o /dev/null $f || ok=1; done; result "compile: stm32 + bxcan + both MCP backends" $ok
    prep compile; setcfg CAN_CFG_ENABLE_BACKEND_STM32_BXCAN 0; setcfg CAN_CFG_ENABLE_BACKEND_MCP25XX 0
    setcfg CAN_CFG_ENABLE_BACKEND_STM32_FDCAN 1; setcfg CAN_CFG_ENABLE_FD 1; setcfg CAN_CFG_USE_FREERTOS 1
    setcfg CAN_CFG_STM32_HAL_HEADER '"stm32g4xx_hal.h"'
    ok=0; for f in can_core.c can_rtos.c can_stm32_fdcan.c can_platform_stm32.c; do
        $CC $CFLAGS -c -Ihal_stub -Ifreertos_stub -o /dev/null $f || ok=1; done; result "compile: stm32 fdcan + freertos" $ok
fi

rm -rf "$WORK"
if [ "$FAILED" -eq 0 ]; then echo "ALL SUITES PASSED"; exit 0; fi
echo "$FAILED SUITE(S) FAILED"; exit 1
