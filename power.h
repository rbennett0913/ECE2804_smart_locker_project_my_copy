#ifndef power_h
#define power_h

/* Power-down sleep. The ONLY wake source is a pushbutton on D2 (PD2,
 * pin-change interrupt PCINT18); the keypad does not wake the chip.
 * Direct register access only -- no <avr/sleep.h> or <avr/interrupt.h>.
 *
 * While asleep: servo signal disconnected and its supply switched off,
 * ADC off, Timer0 stopped, unused peripheral clocks gated.
 *
 * Servo switch: LOW-SIDE N-channel (TP0606) in the servo's ground return.
 * D4 HIGH = servo supplied (inverted from a P-channel high-side switch).
 * The servo's ground rides above board ground by I * Rds(on) -- see the
 * note in power.c; a P-channel high-side switch is the cleaner option if
 * the noisy ground ever causes trouble. */

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

/* Servo supply switch. LOW-SIDE N-channel: on=1 closes the switch and
 * supplies the servo, on=0 opens it. */
void power_servo_supply(unsigned char on);

/* Whole-load switch (D5) for the current-draw measurement. Compiles to a
 * no-op unless HW_LOAD_CUTOFF_ENABLE is set in power.c. */
void power_load_supply(unsigned char on);

#ifdef __cplusplus
}
#endif

#endif
