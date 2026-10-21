#ifndef power_h
#define power_h

/* Power-down sleep with wake-on-keypress (pin-change interrupt on the
 * keypad's ADC pin, PC0 / A0). Direct register access only -- no
 * <avr/sleep.h> or <avr/interrupt.h>.
 *
 * Target hardware is the bare ATmega328P on the 9 V battery, so this path
 * is written for microamps: Timer0 is stopped, the brown-out detector is
 * switched off for the sleep window, and the unused peripheral clocks are
 * gated. See power.c for the details and for the timing rule on the BOD
 * disable sequence. */

#ifdef __cplusplus
extern "C" {
#endif

/* How power_sleep() came back. Only one source today, but the FSM and any
 * eventual hardware wake switch can branch on this without a rewrite. */
#define POWER_WAKE_KEY       0
#define POWER_WAKE_UNKNOWN   1

/* Configure the wake source and the hardware cutoff pins. Call once. */
void power_init(void);

/* Shut everything down and enter power-down mode. Returns once a key press
 * wakes the chip, with the servo signal, ADC and clocks restored.
 *
 * NOTE: the millisecond clock stops during power-down, because it is driven
 * by Timer1 overflows and Timer1 is gated here. Any timing measured across
 * a sleep is short by however long the chip was down. The FSM resets its
 * activity timer on wake for exactly this reason. */
unsigned char power_sleep(void);

/* Hardware cutoffs. Both compile to no-ops unless the matching *_ENABLE
 * macro in power.c is set, so the firmware runs unchanged with nothing
 * wired up. Wire the pin to the gate of a high-side switch (P-channel
 * MOSFET or PNP) between the 5 V rail and the load. */
void power_servo_supply(unsigned char on);
void power_load_supply(unsigned char on);

#ifdef __cplusplus
}
#endif

#endif
