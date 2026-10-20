/* Arduino Uno sketch: servo-actuated keypad lock.
 *


 * The Arduino core supplies main(), which calls setup() once and then
 * calls loop() forever. All real work lives in the C modules (fsm.c,
 * pwm.c, keypad.c, locker.c) next to this sketch; this file is only the
 * glue. Those modules talk to the hardware through direct ATmega328P
 * register access (see their headers) -- no Arduino/Servo libraries are
 * used, by design.
 *
 * Wiring:
 *   keypad (resistor ladder) -> A0  (ADC0 / PC0, see KEYPAD_CHANNEL)
 *   servo signal             -> D9  (OC1A / PB1)
 *   servo current shunt      -> A1  (ADC1 / PC1, see CURRENT_CHANNEL)
 */

#include "fsm.h"
#include "keypad.h"
#include "current.h"

/* Uncomment to print the raw keypad ADC reading twice a second, for
   checking it against the thresholds in keypad.h. */
//#define DEBUG_ADC

/* Uncomment to print the averaged shunt voltage (mV) twice a second, for
   picking BLOCK_THRESHOLD_MV in current.h. */
//#define DEBUG_CURRENT

void setup() {
    fsm_init();
    Serial.begin(9600);
    /* Boot banner: proves the MCU is running and the serial link works
       before any key is pressed. If it only shows up at 600 or 4800 baud
       the chip is on its internal oscillator (1 MHz / 8 MHz), not the
       16 MHz crystal -- see "Standalone ATmega328P" in the README. */
    Serial.println(F("servo_locker boot"));
}

void loop() {
#ifdef DEBUG_ADC
    static unsigned long last_dump;
    if (millis() - last_dump >= 500UL) {
        last_dump = millis();
        Serial.print(F("adc="));
        Serial.println(adc_read_channel(KEYPAD_CHANNEL));
    }
#endif
#ifdef DEBUG_CURRENT
    static unsigned long last_cur;
    if (millis() - last_cur >= 500UL) {
        last_cur = millis();
        Serial.print(F("shunt_mv="));
        Serial.println(current_read_mv());
    }
#endif

    /* Poll the keypad exactly ONCE per iteration: keypad_read() is
       edge-triggered (one event per physical press), so calling it both
       here for debugging and inside fsm_tick() would swallow every
       keypress before the FSM could see it. */
    key_t key = keypad_read();

    switch (key) {
        //case KEY_NONE: Serial.println("none"); break;
        case KEY_0:    Serial.println('0'); break;
        case KEY_1:    Serial.println('1'); break;
        case KEY_2:    Serial.println('2'); break;
        case KEY_3:    Serial.println('3'); break;
        case KEY_4:    Serial.println('4'); break;
        case KEY_5:    Serial.println('5'); break;
        case KEY_6:    Serial.println('6'); break;
        case KEY_7:    Serial.println('7'); break;
        case KEY_8:    Serial.println('8'); break;
        case KEY_9:    Serial.println('9'); break;
        case KEY_STAR: Serial.println('*'); break;
        case KEY_POUND:Serial.println('#'); break;
        default: break;  /* KEY_NONE */
    }

    fsm_tick(key);
}
