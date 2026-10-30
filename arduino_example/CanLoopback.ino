/*
 * @author: mojik183
 * @github: github.com/mojik183
 * @ai assistant: claude.ai
 * @date: 2026-10-29
 * @version: 0.1.0
 *
 * CanLoopback.ino - first test of the Universal CAN library on an ESP32 (Arduino-ESP32).
 *
 * Needs no bus and no second node: the frame goes out in loopback mode and comes back to the receive side.
 * Wire CAN transceiver TXD / RXD to the GPIOs below for the later bus test and set the mode to NORMAL.
 *
 * Library setup: run make_arduino_lib.sh (see README, section "Arduino"), copy the created UniversalCAN folder
 * to your Arduino "libraries" folder, restart the IDE.
 */
#include <can_lib.h>                    /* the one umbrella header of the library */

#define CAN_TX_GPIO     5               /* ESP32 pin wired to the transceiver TXD */
#define CAN_RX_GPIO     4               /* ESP32 pin wired to the transceiver RXD */

static CanLib_t can1;                   /* main struct: look at it with a debugger or print its fields */
static uint32_t last_send_ms = 0;
static uint8_t  counter      = 0;

/** Stop and blink-print forever when a library call fails. */
static void fail(const char *what, CanResult_e r)
{
    for (;;)
    {
        Serial.printf("%s failed: %s (%d)\n", what, CanLib_ResultToStr(r), (int)r);
        delay(1000);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    CanLib_SetDefaults(&can1, "can1", CAN_BACKEND_ESP32_TWAI);
    can1.hw.tx_gpio          = CAN_TX_GPIO;
    can1.hw.rx_gpio          = CAN_RX_GPIO;
    can1.cfg.nominal.bitrate = CAN_BITRATE_500K;
    can1.cfg.mode            = CANLIB_MODE_LOOPBACK;        /* change to CANLIB_MODE_NORMAL for a real bus */

    CanResult_e r = CanLib_Init(&can1);
    if (CAN_IS_ERR(r)) { fail("CanLib_Init", r); }
    r = CanLib_Start(&can1);
    if (CAN_IS_ERR(r)) { fail("CanLib_Start", r); }

    Serial.printf("CAN started, actual bitrate %lu bit/s\n", (unsigned long)can1.cfg.nominal.actual_bitrate);
}

void loop()
{
    CanFrame_t f;

    (void)CanLib_Process(&can1);                            /* call often: reads, sends queue, checks errors */

    if ((millis() - last_send_ms) >= 1000U)
    {
        uint8_t data[2] = { 0xCA, counter++ };
        last_send_ms = millis();
        CanResult_e r = CanLib_Send(&can1, 0x123, data, 2);
        if (CAN_IS_ERR(r)) { Serial.printf("send: %s\n", CanLib_ResultToStr(r)); }
    }

    while (CanLib_Receive(&can1, &f) == CAN_OK)
    {
        Serial.printf("rx id=0x%03lX len=%u data=", (unsigned long)f.header.id, (unsigned)f.header.data_len);
        for (uint8_t i = 0; i < f.header.data_len; i++) { Serial.printf("%02X ", f.body.data[i]); }
        Serial.println();
    }
    delay(2);
}
