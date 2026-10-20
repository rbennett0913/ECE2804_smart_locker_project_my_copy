#ifndef fsm_h
#define fsm_h

#include "keypad.h"   /* key_t */
#define LOCK_TIMEOUT      250UL     /* ms to let the servo move before the block check */
#define SLEEP_TIMEOUT_MS  15000UL   /* ms of no key presses before power-down */

#ifdef __cplusplus
extern "C" {
#endif

/* One-time bring-up: PWM, ADC, current sense, saved code from EEPROM,
 * wake source; physically asserts the locked position and starts the
 * sleep-timeout clock. Call exactly once (from setup()). */
void fsm_init(void);

/* Runs one iteration of the main loop: services the millisecond tick
 * and advances the state machine.
 * `key` is the debounced keypad event for this iteration (KEY_NONE if
 * no key). The caller owns the single keypad_read() poll per loop so
 * that edge-triggered events are not consumed twice.
 * Call repeatedly (from loop()). */
void fsm_tick(key_t key);

#ifdef __cplusplus
}
#endif

#endif
