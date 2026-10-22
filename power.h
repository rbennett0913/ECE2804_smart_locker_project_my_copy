#ifndef power_h
#define power_h

/* Power-down sleep. The ONLY wake source is a pushbutton on D2 (PD2,
 * pin-change interrupt PCINT18); the keypad does not wake the chip.
 * Direct register access only -- no <avr/sleep.h> or <avr/interrupt.h>.
 *
 * While asleep: servo signal disconnected and its supply cut through the
 * 2N3906 on D4, ADC off, Timer0 stopped, unused peripheral clocks gated.
 *
 * The servo stays on 5 V (an SG90 is a 4.8-6 V part; the RAW 9 V rail
 * would destroy it). Its supply is switched off between moves, which is
 * where the power saving comes from -- see servo_power_window(). */

#ifdef __cplusplus
extern "C" {
#endif

/* How long to keep the servo powered for a move, in ms. Must be long
 * enough for the servo to travel and settle; 600 ms matches the FSM's
 * LOCK_TIMEOUT and is inside the spec's 1 s blocked-motor deadline. */
#define SERVO_MOVE_MS  600UL

/* How power_sleep() came back. Only the button today. */
#define POWER_WAKE_KEY       0
#define POWER_WAKE_UNKNOWN   1

/* Configure the wake button and the servo supply switch. Call once at
 * boot, before the FSM commands any position. */
void power_init(void);

/* Shut everything down and enter power-down mode. Returns once the wake
 * button is pressed, with the ADC and clocks restored and the servo left
 * unpowered. The millisecond clock pauses while asleep. */
unsigned char power_sleep(void);

/* Servo supply on until a move finishes, then off again. Blocks for `ms`,
 * servicing the millisecond tick while it waits, and takes care of the
 * signal/supply ordering at both ends. Use this instead of commanding a
 * position and leaving the servo powered. */
void servo_power_window(unsigned int ms);

/* Hardware cutoffs. The servo switch (D4) is enabled in power.c; the
 * whole-load switch (D5) compiles to a no-op unless enabled there. */
void power_servo_supply(unsigned char on);
void power_load_supply(unsigned char on);

#ifdef __cplusplus
}
#endif

#endif
