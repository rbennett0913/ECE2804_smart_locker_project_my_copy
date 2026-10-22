#ifndef power_h
#define power_h

/* Power-down sleep. The ONLY wake source is a pushbutton on D2 (PD2,
 * pin-change interrupt PCINT18); the keypad does not wake the chip.
 * Direct register access only -- no <avr/sleep.h> or <avr/interrupt.h>.
 *
 * While asleep: servo signal disconnected and its supply cut through the
 * 2N3906 on D4, ADC off, Timer0 stopped, unused peripheral clocks gated. */

#ifdef __cplusplus
extern "C" {
#endif

/* How power_sleep() came back. Only the button today. */
#define POWER_WAKE_KEY       0
#define POWER_WAKE_UNKNOWN   1

/* Configure the wake button and the servo supply switch (servo ON).
 * Call once at boot. */
void power_init(void);

/* Shut everything down and enter power-down mode. Returns once the wake
 * button is pressed, with the servo signal, its supply, the ADC and the
 * clocks restored. The millisecond clock pauses while asleep. */
unsigned char power_sleep(void);

/* Hardware cutoffs. The servo switch (D4) is enabled in power.c; the
 * whole-load switch (D5) compiles to a no-op unless enabled there. */
void power_servo_supply(unsigned char on);
void power_load_supply(unsigned char on);

#ifdef __cplusplus
}
#endif

#endif
