#ifndef battery_h
#define battery_h

/* 9 V battery monitoring through a resistor divider onto A2.
 *
 * Hardware: 1M from battery+ to A2, 1M from A2 to GND.
 *
 *   battery+ -- 1M --+-- A2 (ADC2 / PC2)
 *                    |
 *                   1M        100 nF from A2 to GND
 *                    |
 *                   GND
 *
 * With 1M/1M the divider draws about 4.5 uA from the cell, which is
 * negligible against a 600 mAh battery, so the monitoring itself does not
 * shorten its life.
 *
 * The 100 nF cap is required, not optional: the divider's source impedance
 * is about 500k, far above the ~10k the ADC wants, so the cap has to supply
 * the sample-and-hold charge. RC = 500k * 100nF = 50 ms, so readings settle
 * a few hundred ms after the ADC is enabled -- which is why the check waits
 * BATTERY_CHECK_DELAY_MS rather than reading immediately.
 *
 * A fresh 9.6 V cell puts 4.8 V on A2, just under the 5 V AVcc reference,
 * so there is little headroom. The reading is only valid while the
 * regulator still holds 5 V (battery above about 6 V); below that the
 * reference sags with the battery and the reading comes out high. The alert
 * fires long before that point. */

#define BATTERY_CHANNEL        2        /* ADC2 = PC2 / A2 */
#define ADC2D                  2        /* DIDR0 bit for the ADC2 digital input buffer */
#define BATTERY_SAMPLES        16       /* averaged per reading (~1.7 ms total) */
#define BATTERY_R1_KOHM        1000UL   /* divider top: battery+ to A2 */
#define BATTERY_R2_KOHM        1000UL   /* divider bottom: A2 to GND    */

/* Alert threshold in millivolts at the battery, not at the pin.
 * Placeholder: tune against a multimeter with DEBUG_BATTERY. A 9 V alkaline
 * sits near 9 V for most of its life and then falls quickly, and the servo
 * needs the regulator to hold 5 V, so the alert should give warning while
 * the cell still has useful capacity. */
#define BATTERY_LOW_MV         7200

/* Wait this long after boot/wake before reading, so the servo's inrush
 * current has passed and the divider's cap has settled. */
#define BATTERY_CHECK_DELAY_MS 500UL

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after adc_init(). Disables the digital input buffer on A2,
 * which is what keeps an analog pin from drawing current at mid-rail. */
void battery_init(void);

/* Averaged battery voltage in millivolts, divider ratio applied.
 * With 1M/1M the range is 0-9990 mV. Each call averages
 * BATTERY_SAMPLES conversions (~1.7 ms). */
unsigned int battery_read_mv(void);

/* 1 if the battery is below BATTERY_LOW_MV, else 0. */
unsigned char battery_low(void);

#ifdef __cplusplus
}
#endif

#endif
