/* Arduino sketch: servo-actuated keypad lock.
 *
 * The Arduino core supplies main(), which calls setup() once and then
 * calls loop() forever. All real work lives in the C modules (fsm.c,
 * pwm.c, keypad.c, locker.c, power.c); this file is the glue, and it also
 * owns every serial message the user sees.
 *
 * Wiring:
 *   keypad (resistor ladder) -> A0  (ADC0 / PC0, see KEYPAD_CHANNEL)
 *   servo signal             -> D9  (OC1A / PB1)
 *   servo supply switch      -> D4  (2N3906 base, LOW = servo on)
 *   wake button              -> D2  (to GND, internal pull-up)
 *   servo current shunt      -> A1  (not fitted yet; detection disabled)
 */

#include "fsm.h"
#include "keypad.h"
#include "current.h"

/* Uncomment to print the raw keypad ADC reading twice a second, for
   checking it against the thresholds in keypad.h. */
//#define DEBUG_ADC

/* ---- the user-facing messages ---------------------------------------- */

/* One '*' per accepted digit: the code itself never goes out over serial. */
void fsm_on_digit(unsigned char digit) {
    (void)digit;
    Serial.print('*');
}

void fsm_on_password_result(unsigned char ok) {
    if (ok) {
        Serial.println(F("\r\nACCESS GRANTED - unlocked. '*' locks, '#' sets a new code."));
    } else {
        Serial.println(F("\r\nACCESS DENIED - wrong passcode."));
    }
}

void fsm_on_event(unsigned char event) {
    switch (event) {
        case FSM_EVT_BOOT:
            Serial.println(F("\r\n=== Personal Smart Locker ==="));
            Serial.println(F("Status: LOCKED. Enter passcode, then '#'."));
            break;
        case FSM_EVT_SLEEP_ENTERING:
            Serial.println(F("\r\nEntering power-down sleep mode..."));
            /* power_sleep() gates the USART clock next; finish sending
               first or the message is cut off mid-transmission. */
            Serial.flush();
            break;
        case FSM_EVT_WAKE:
            Serial.println(F("\r\nWoke up from sleep."));
            break;
        case FSM_EVT_LOCKED:
            Serial.println(F("\r\nLOCKED."));
            break;
        case FSM_EVT_BLOCKED_REVERT:
            Serial.println(F("\r\nBLOCKED! Motor could not turn - reverted to unlocked."));
            break;
        case FSM_EVT_REPROGRAM_START:
            Serial.println(F("\r\nSet new passcode: enter 4 digits, '#' to save, '*' to cancel."));
            break;
        case FSM_EVT_CODE_SAVED:
            Serial.println(F("\r\nNew passcode saved to EEPROM."));
            break;
        case FSM_EVT_REPROGRAM_CANCEL:
            Serial.println(F("\r\nNew passcode cancelled - old code unchanged."));
            break;
        case FSM_EVT_ENTRY_INCOMPLETE:
            Serial.println(F("\r\nIncomplete entry - cleared."));
            break;
        default:
            break;
    }
}

/* ---- setup / loop ---------------------------------------------------- */

void setup() {
    /* Serial first, so fsm_init()'s boot message has somewhere to go. */
    Serial.begin(9600);
    fsm_init();

    /* Boot banner: proves the MCU is running and the serial link works.
       If it only shows up at 600 or 4800 baud the chip is on its internal
       oscillator, not the 16 MHz crystal -- see the README. */
    Serial.println(F("servo_locker boot"));
    Serial.flush();
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

    /* Poll the keypad exactly ONCE per iteration: keypad_read() is
       edge-triggered (one event per physical press). */
    key_t key = keypad_read();

    switch (key) {
        case KEY_0:    Serial.println(F("Key Pressed: 0")); break;
        case KEY_1:    Serial.println(F("Key Pressed: 1")); break;
        case KEY_2:    Serial.println(F("Key Pressed: 2")); break;
        case KEY_3:    Serial.println(F("Key Pressed: 3")); break;
        case KEY_4:    Serial.println(F("Key Pressed: 4")); break;
        case KEY_5:    Serial.println(F("Key Pressed: 5")); break;
        case KEY_6:    Serial.println(F("Key Pressed: 6")); break;
        case KEY_7:    Serial.println(F("Key Pressed: 7")); break;
        case KEY_8:    Serial.println(F("Key Pressed: 8")); break;
        case KEY_9:    Serial.println(F("Key Pressed: 9")); break;
        case KEY_STAR: Serial.println(F("Key Pressed: *")); break;
        case KEY_POUND:Serial.println(F("Key Pressed: #")); break;
        default: break;  /* KEY_NONE */
    }

    fsm_tick(key);
}
