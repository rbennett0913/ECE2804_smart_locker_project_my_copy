#ifndef current_h
#define current_h

/* Servo current sensing through a low-side shunt resistor.
 *
 * Hardware: the shunt sits between the servo's GND lead and board GND;
 * A1 connects to the servo side of the shunt, so V(A1) = I_servo * R.
 * The ADC uses the AVcc (5 V) reference shared with the keypad, so the
 * shunt voltage must stay below 5 V.
 *
 * NO SHUNT IS FITTED YET. With nothing on A1 the pin floats, and a floating
 * pin reads random values -- which is what produced false "BLOCKED" reverts.
 * Block detection is therefore switched off until the shunt is on the board.
 * While it is off the watch reports "running" as false, so the FSM ignores
 * it entirely and the door locks on the timeout as it always did. To turn
 * detection on:
 *   1. fit the shunt (1 ohm is a good start) as described above
 *   2. set BLOCK_DETECT_ENABLE to 1
 *   3. tune the numbers below from real idle / moving / stalled readings
 *      (DEBUG_CURRENT in servo_locker.ino prints the mv value) */
#define BLOCK_DETECT_ENABLE 0

#define CURRENT_CHANNEL    1       /* ADC1 = PC1 / A1 */
#define ADC1D              1       /* DIDR0 bit for the ADC1 digital input buffer */
#define CURRENT_SAMPLES    16      /* averaged per reading (~1.7 ms total) */
#define ADC_VREF_MV        5000UL  /* AVcc reference, in mV */

/* One shunt voltage that counts as "the motor is straining". Must sit above
 * the normal travelling reading and at or below the stalled one. */
#define BLOCK_THRESHOLD_MV 200     /* placeholder: tune on hardware */

/* How many samples a move takes, at least, before a verdict is given, and
 * how many of them in a row must be over the threshold. See the note in
 * current.c: inrush decays, a real stall does not. */
#define BLOCK_MIN_SAMPLES      4
#define BLOCK_CONFIRM_SAMPLES  3
#define BLOCK_SAMPLE_PERIOD_MS 100UL   /* FSM's spacing between polls */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after adc_init(). */
void current_init(void);

/* Averaged 10-bit reading of A1, 0-1023. */
unsigned int current_read_raw(void);

/* Averaged shunt voltage in millivolts (0-4995). */
unsigned int current_read_mv(void);

/* Start watching a move. Call when the servo is commanded to a new
 * position, before the motor has finished travelling. */
void block_watch_begin(void);

/* 1 while the watch is still able to produce a verdict, i.e. detection is
 * enabled. Always 0 when BLOCK_DETECT_ENABLE is 0, which is how the FSM
 * knows to ignore the watch completely. */
unsigned char block_watch_running(void);

/* Take one sample of the move in progress. Call it from the state machine
 * every BLOCK_SAMPLE_PERIOD_MS; returns 1 once enough samples have been
 * taken, 0 while the watch is still filling. */
unsigned char block_watch_poll(void);

/* 1 if the watch saw a sustained high reading, i.e. the motor is blocked.
 * Always 0 while BLOCK_DETECT_ENABLE is 0. */
unsigned char block_watch_blocked(void);

/* Same verdict in one call, for code that wants a single answer. */
unsigned char block_detected(void);

#ifdef __cplusplus
}
#endif

#endif
