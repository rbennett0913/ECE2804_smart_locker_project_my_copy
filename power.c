#include "power.h"

#define REG8(a) (*(volatile unsigned char *)(a))

#define P_SMCR    REG8(0x53)   /* sleep mode control         */
#define P_PCIFR   REG8(0x3B)   /* pin-change interrupt flags */
#define P_PCICR   REG8(0x68)   /* pin-change interrupt enable */
#define P_PCMSK1  REG8(0x6C)   /* pin-change mask, port C    */
#define P_ADCSRA  REG8(0x7A)
#define P_DIDR0   REG8(0x7E)
#define P_TCCR1A  REG8(0x80)
#define P_PORTB   REG8(0x25)

#define B_SE      0            /* SMCR: sleep enable          */
#define B_SM1     2            /* SMCR: SM2..0 = 010 power-down */
#define B_PCIF1   1
#define B_PCIE1   1
#define B_PCINT8  0            /* PC0 / A0 */
#define B_ADEN    7
#define B_ADC0D   0
#define B_COM1A1  7
#define B_PB1     1            /* servo signal, OC1A / D9 */

/* PCINT1 is interrupt vector 4 on the ATmega328P. The handler is empty:
 * the interrupt exists only to wake the CPU. It must still be defined,
 * because an enabled interrupt with no handler jumps to the reset vector.
 * Declared by hand instead of with ISR() so <avr/interrupt.h> isn't needed. */
void __vector_4(void) __attribute__((signal, used, externally_visible));
void __vector_4(void) { }

void power_init(void)
{
    /* The pin-change logic reads the pin's digital input buffer, so it
       must stay enabled on A0 even though A0 is used as an analog input. */
    P_DIDR0  &= ~(1 << B_ADC0D);
    P_PCMSK1 |=  (1 << B_PCINT8);
    /* PCIE1 is only switched on while asleep (see power_sleep), so normal
       typing doesn't fire a stream of pointless interrupts. */
}

void power_sleep(void)
{
    unsigned char tccr1a = P_TCCR1A;

    /* Servo: Timer1 stops in power-down and could freeze the signal pin
       high mid-pulse. Disconnect OC1A from the pin and hold it low so the
       servo just goes idle (and stops drawing holding current). */
    P_TCCR1A = tccr1a & ~(1 << B_COM1A1);
    P_PORTB &= ~(1 << B_PB1);

    P_ADCSRA &= ~(1 << B_ADEN);              /* ADC off */

    __asm__ __volatile__ ("cli" ::: "memory");
    P_PCIFR = (1 << B_PCIF1);                /* drop any stale wake flag */
    P_PCICR |= (1 << B_PCIE1);
    P_SMCR = (1 << B_SM1) | (1 << B_SE);
    /* The instruction after SEI always runs before any interrupt, so a
       key press arriving now still wakes the chip instead of being missed. */
    __asm__ __volatile__ ("sei"   ::: "memory");
    __asm__ __volatile__ ("sleep" ::: "memory");

    /* ---- awake ---- */
    P_SMCR = 0;
    P_PCICR &= ~(1 << B_PCIE1);
    P_ADCSRA |= (1 << B_ADEN);
    P_TCCR1A = tccr1a;                       /* servo signal back on */
}
