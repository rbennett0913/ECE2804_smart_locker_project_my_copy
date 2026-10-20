#ifndef current_h
#define current_h

/* Servo current sensing through a low-side shunt resistor.
 *
 * Hardware: the shunt sits between the servo's GND lead and board GND;
 * A1 connects to the servo side of the shunt, so V(A1) = I_servo * R.
 * The ADC uses the AVcc (5 V) reference shared with the keypad, so the
 * shunt voltage must stay below 5 V. An RC filter on A1 (e.g. 1 k /
 * 1 uF) helps smooth the motor's pulsed current. */

#define CURRENT_CHANNEL    1       /* ADC1 = PC1 / A1 */
#define ADC1D              1       /* DIDR0 bit for the ADC1 digital input buffer */
#define CURRENT_SAMPLES    16      /* averaged per reading (~1.7 ms total) */
#define ADC_VREF_MV        5000UL  /* AVcc reference, in mV */
#define BLOCK_THRESHOLD_MV 200     /* placeholder: tune on hardware with DEBUG_CURRENT */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after adc_init(). */
void current_init(void);

/* Averaged 10-bit reading of A1, 0-1023. */
unsigned int current_read_raw(void);

/* Averaged shunt voltage in millivolts (0-4995). */
unsigned int current_read_mv(void);

/* 1 if the shunt voltage exceeds BLOCK_THRESHOLD_MV, else 0.
 * Only meaningful once the servo has had time to finish its move:
 * inrush current at the start of a move also looks like a stall. */
unsigned char block_detected(void);

#ifdef __cplusplus
}
#endif

#endif
