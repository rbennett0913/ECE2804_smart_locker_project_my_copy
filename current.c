#include "current.h"
#include "keypad.h"   /* ADC registers, adc_read_channel() */

void current_init(void)
{
    /* disable the digital input buffer on the analog pin, as for ADC0 */
    DIDR0 |= (1 << ADC1D);
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

unsigned char block_detected(void)
{
    return current_read_mv() > BLOCK_THRESHOLD_MV;
}
