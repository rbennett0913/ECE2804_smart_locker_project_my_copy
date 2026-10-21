#ifndef fsm_h
#define fsm_h

#include "keypad.h"   /* key_t */
#define LOCK_TIMEOUT      250UL     /* ms to let the servo move before the block check */
#define SLEEP_TIMEOUT_MS  15000UL   /* ms of no key presses before power-down */

#ifdef __cplusplus
extern "C" {
#endif

/* One-time bring-up: PWM, ADC, current sense, saved code from EEPROM,
 * wake button; physically asserts the locked position and starts the
 * sleep-timeout clock. Call exactly once (from setup()). */
void fsm_init(void);

/* Runs one iteration of the main loop: services the millisecond tick
 * and advances the state machine.
 * `key` is the debounced keypad event for this iteration (KEY_NONE if
 * no key). The caller owns the single keypad_read() poll per loop so
 * that edge-triggered events are not consumed twice.
 * Call repeatedly (from loop()). */
void fsm_tick(key_t key);

/* ---- user-feedback hooks ----------------------------------------------
 * The FSM calls these as things happen; the implementations live with the
 * sketch (servo_locker.ino) so the C modules stay free of serial/LED code. */

/* event is one of the FSM_EVT_* values below */
void fsm_on_event(unsigned char event);

/* A digit key was accepted, in either the lock attempt or the new code.
 * digit is 0..9. */
void fsm_on_digit(unsigned char digit);

/* A full code was submitted: ok = 1 correct, 0 wrong. */
void fsm_on_password_result(unsigned char ok);

#define FSM_EVT_BOOT              0   /* boot finished, system is locked */
#define FSM_EVT_SLEEP_ENTERING    1   /* about to power down */
#define FSM_EVT_WAKE              2   /* wake button pressed, back awake */
#define FSM_EVT_LOCKED            3   /* servo finished moving to locked */
#define FSM_EVT_BLOCKED_REVERT    4   /* motor blocked: went back to unlocked */
#define FSM_EVT_REPROGRAM_START   5   /* entering new-code mode */
#define FSM_EVT_CODE_SAVED        6   /* new code written to EEPROM */
#define FSM_EVT_REPROGRAM_CANCEL  7   /* new-code entry abandoned */
#define FSM_EVT_ENTRY_INCOMPLETE  8   /* '#' with fewer than PIN_LEN digits */

#ifdef __cplusplus
}
#endif

#endif
