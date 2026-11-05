#include "battery.h"
#include "keypad.h"    /* DIDR0, adc_read_channel() */
#include "current.h"   /* ADC_VREF_MV */

void battery_init(void)
{
    /* A2 is analog only: this stops digital logic reacting to the divider's
     * mid-rail level and saves the buffer's current. */
    DIDR0 |= (1 << ADC2D);
}

unsigned int battery_read_mv(void)
{
    unsigned long sum = 0;
    unsigned char i;
    unsigned long pin_mv;

    /* Average a burst: the cell's voltage moves under load, and a single
     * conversion can land mid-ripple. */
    for (i = 0; i < BATTERY_SAMPLES; ++i) {
        sum += adc_read_channel(BATTERY_CHANNEL);
    }

    /* Two steps: 1023 * 5000 * 2000 overflows 32 bits, so convert the pin
     * voltage first (0-4995 mV) and then undo the divider. */
    pin_mv = (sum / BATTERY_SAMPLES) * ADC_VREF_MV / 1024;
    return (unsigned int)(pin_mv * (BATTERY_R1_KOHM + BATTERY_R2_KOHM)
                          / BATTERY_R2_KOHM);
}

unsigned char battery_low(void)
{
    return (battery_read_mv() < BATTERY_LOW_MV);
}
