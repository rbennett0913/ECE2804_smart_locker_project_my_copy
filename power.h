#ifndef power_h
#define power_h

/* Power-down sleep with wake-on-keypress (pin-change interrupt on the
 * keypad's ADC pin, PC0 / A0). Direct register access only -- no
 * <avr/sleep.h> or <avr/interrupt.h>. */

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the wake source. Call once at boot. */
void power_init(void);

/* Shut down the servo signal and ADC, enter power-down mode, and return
 * once a key press wakes the chip (with servo signal and ADC restored). */
void power_sleep(void);

#ifdef __cplusplus
}
#endif

#endif
