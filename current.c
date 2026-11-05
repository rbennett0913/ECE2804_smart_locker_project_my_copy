#include "current.h"
#include "keypad.h"   /* ADC registers, adc_read_channel() */

void current_init(void)
{
#if BLOCK_DETECT_ENABLE
    /* disable the digital input buffer on the analog pin, as for ADC0 */
    DIDR0 |= (1 << ADC1D);
#endif
}

/* The servo draws current in pulses, not a steady level, so a single
 * sample can land anywhere between idle and peak. Average a burst. */
unsigned int current_read_raw(void)
{
    unsigned long sum = 0;
    unsigned char i;

    for (i = 0; i < CURRENT_SAMPLES; ++i) {
        sum += adc_read_channel(CURRENT_CHANNEL);
    }
    return (unsigned int)(sum / CURRENT_SAMPLES);
}

unsigned int current_read_mv(void)
{
    /* 32-bit math: 1023 * 5000 overflows a 16-bit int */
    return (unsigned int)((unsigned long)current_read_raw() * ADC_VREF_MV / 1024);
}

/* ---------------------------------------------------------------------
 * Block detection by persistence
 *
 * A single high reading cannot tell a stall from a normal move:
 *
 *   - every move starts with an inrush spike, which is also a high
 *     current, so a check taken during travel reports a block on a
 *     healthy servo
 *   - the travelling and stalled readings overlap from motor to motor,
 *     so no one threshold separates them
 *
 * What does separate them is how long the high reading lasts. Inrush
 * decays within tens of milliseconds. A stalled motor keeps drawing for as
 * long as it is pushed against the load. So the watch samples through the
 * whole move and only calls it blocked when the high reading has held for
 * BLOCK_CONFIRM_SAMPLES consecutive samples, and never before
 * BLOCK_MIN_SAMPLES have been taken.
 *
 * The FSM spaces the polls out, so each sample lands in a different part
 * of the servo's 20 ms PWM cycle instead of all of them inside one pulse.
 * ------------------------------------------------------------------- */
static unsigned samples_taken;   /* since block_watch_begin() */
static unsigned samples_high;    /* consecutive over-threshold samples */
static unsigned char verdict;    /* 0 = not blocked, 1 = blocked */

void block_watch_begin(void)
{
    samples_taken = 0;
    samples_high = 0;
    verdict = 0;
}

unsigned char block_watch_poll(void)
{
#if BLOCK_DETECT_ENABLE
    samples_taken++;
    if (current_read_mv() > BLOCK_THRESHOLD_MV) {
        if (samples_high < BLOCK_CONFIRM_SAMPLES) {
            samples_high++;
        }
    } else {
        samples_high = 0;          /* a normal reading breaks the run */
    }

    /* Verdict only once the move has had time to finish, and only if the
     * high reading held all the way through. */
    if (samples_taken >= BLOCK_MIN_SAMPLES
        && samples_high >= BLOCK_CONFIRM_SAMPLES) {
        verdict = 1;
    }
#endif

    return (samples_taken >= BLOCK_MIN_SAMPLES);
}

unsigned char block_watch_blocked(void)
{
    return verdict;
}

unsigned char block_detected(void)
{
    if (samples_taken < BLOCK_MIN_SAMPLES) {
        return 0;                  /* no verdict yet */
    }
    return block_watch_blocked();
}
