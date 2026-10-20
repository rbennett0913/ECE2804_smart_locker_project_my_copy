#include "pwm.h"

/* TOP = 39999 with /8 prescaler -> 50 Hz, overflow every 20 ms */
#define TICK_MS 20UL

static volatile unsigned long g_ms = 0;

unsigned long milliseconds(void)
{
    return g_ms;
}

/* Non-blocking: bump the clock if a tick happened, then return.
 * With a 20 ms tick and a main loop that runs in microseconds:
 * never miss an update. */
void tick_update(void)
{
    if (TIFR1 & (1 << TOV1)) {  /* read - if 1 then overflow has occured */
        TIFR1 = (1 << TOV1);    /* write 1 to clear */
        g_ms += TICK_MS;
    }
}

void pwm_init(void)
{
    /* OC1A (PB1 / Arduino D9) as output */
    DDRB |= (1 << PB1);

    /* Fast PWM, TOP = ICR1, clear OC1A on match */
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);

    /* TOP -> 20 ms period (50 Hz) */
    ICR1H = (unsigned char)(39999 >> 8);
    ICR1L = (unsigned char)(39999 & 0xFF);
}

static void setPulse(unsigned int counts)
{
    OCR1AH = (unsigned char)(counts >> 8);
    OCR1AL = (unsigned char)(counts & 0xFF);
}

void setPosUnlock(void) { setPulse(1999); }   /* 1.0 ms -> unlocked */
void setPosLock(void)   { setPulse(3999); }   /* 2.0 ms -> locked   */


/*  used to test servo
int main() {
    pwm_init();
    setPosLock();
    tick_update();
    unsigned long time = milliseconds();
    int locked = 1;   // start in the locked position

    while (1) {
       tick_update();
       if (milliseconds() >= (time + 1000)) {
           if (locked) {
               setPosUnlock();
               locked = 0;
           } else {
               setPosLock();
               locked = 1;
           }
           time = milliseconds();
       }
   }
}
*/