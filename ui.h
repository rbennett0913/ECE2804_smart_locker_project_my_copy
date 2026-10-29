#ifndef ui_h
#define ui_h

/* State LEDs (D6/D7/D8) and a passive piezo (D3) for Milestone 4b.
 *
 * The FSM already reports everything through its hooks, so this module is
 * driven entirely from servo_locker.ino -- fsm.c needs no changes.
 * Nothing here blocks: sequences advance in ui_tick(). */

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the LED pins and the piezo. Call BEFORE fsm_init(), because
 * fsm_init() raises FSM_EVT_BOOT, which this module displays. */
void ui_init(void);

/* Advance the sound and blink sequences. Call once per loop pass, after
 * fsm_tick(), so the two stay in step. */
void ui_tick(void);

/* event is one of the FSM_EVT_* values from fsm.h */
void ui_event(unsigned char event);

/* A digit key was accepted. */
void ui_digit(void);

/* A full code was submitted: ok = 1 correct, 0 wrong. */
void ui_result(unsigned char ok);

#ifdef __cplusplus
}
#endif

#endif
