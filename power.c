#include "power.h"

/* =====================================================================
 * Registers, declared by hand so this project needs no <avr/io.h>.
 * Addresses are data-space addresses from the ATmega328P datasheet.
 * ===================================================================== */
#define REG8(a) (*(volatile unsigned char *)(a))

#define P_TIFR1   REG8(0x36)
#define P_PCIFR   REG8(0x3B)
#define P_SMCR    REG8(0x53)
#define P_MCUCR   REG8(0x55)
#define P_PCICR   REG8(0x68)
#define P_PCMSK1  REG8(0x6C)
#define P_TIMSK0  REG8(0x6E)
#define P_ADCSRA  REG8(0x7A)
#define P_DIDR0   REG8(0x7E)
#define P_TCCR1A  REG8(0x80)
#define P_PORTB   REG8(0x25)
#define P_PRR     REG8(0x64)
#define P_TCCR0B  REG8(0x45)
#define P_DDRD    REG8(0x2A)
#define P_PORTD   REG8(0x2B)

/* TIFR1 */
#define B_TOV1     0
/* SMCR */
#define B_SE       0
#define B_SM1      2           /* SM2..0 = 010 -> power-down */
/* PCICR / PCIFR / PCMSK1 */
#define B_PCIE1    1
#define B_PCIF1    1
#define B_PCINT8   0           /* PCINT8 = PC0 / Arduino A0 */
/* ADCSRA / DIDR0 */
#define B_ADEN     7
#define B_ADC0D    0
/* TCCR1A / PORTB: servo signal is OC1A on PB1 (Arduino D9) */
#define B_COM1A1   7
#define B_PB1      1
/* PRR: gate the clocks we aren't using while asleep */
#define B_PRUSART0 1
#define B_PRSPI0   2
#define B_PRTIM1   3

/* Hardware cutoffs. Wire these to the high-side switches when the
 * transistors are on the board; with the flags at 0 the firmware runs
 * unchanged and sleeps with nothing connected up. */
#define HW_SERVO_CUTOFF_ENABLE 0
#define HW_LOAD_CUTOFF_ENABLE  0

#define SERVO_CUTOFF_BIT  4            /* PD4 / Arduino D4 */
#define LOAD_CUTOFF_BIT   5            /* PD5 / Arduino D5 */

/* PCINT1 is interrupt vector 4. The handler is deliberately empty: the
 * interrupt exists only to wake the CPU. It must be defined, because an
 * enabled interrupt with no handler jumps to the reset vector instead.
 * Declared by hand so <avr/interrupt.h> isn't needed. */
void __vector_4(void) __attribute__((signal, used, externally_visible));
void __vector_4(void) { }

/* ---------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------- */
void power_init(void)
{
    /* The pin-change logic reads the pin's digital input buffer, so it
     * must stay enabled on A0 even though A0 is an analog input. */
    P_DIDR0  &= ~(1 << B_ADC0D);
    P_PCMSK1 |=  (1 << B_PCINT8);
    /* PCIE1 stays off until sleep, so ordinary typing doesn't fire a
     * stream of interrupts. */

#if HW_SERVO_CUTOFF_ENABLE
    /* High-side switch: drive low = servo supplied. Assert that at boot
     * so the servo has power whenever the MCU is running. */
    P_DDRD  |=  (1 << SERVO_CUTOFF_BIT);
    P_PORTD &= ~(1 << SERVO_CUTOFF_BIT);
#endif
#if HW_LOAD_CUTOFF_ENABLE
    P_DDRD  |=  (1 << LOAD_CUTOFF_BIT);
    P_PORTD &= ~(1 << LOAD_CUTOFF_BIT);
#endif
}

/* ---------------------------------------------------------------------
 * Hardware cutoffs
 * ------------------------------------------------------------------- */
void power_servo_supply(unsigned char on)
{
#if HW_SERVO_CUTOFF_ENABLE
    /* P-channel MOSFET / PNP high side: drive low to supply the servo. */
    if (on) {
        P_PORTD &= ~(1 << SERVO_CUTOFF_BIT);
    } else {
        P_PORTD |= (1 << SERVO_CUTOFF_BIT);
    }
#else
    (void)on;
#endif
}

void power_load_supply(unsigned char on)
{
#if HW_LOAD_CUTOFF_ENABLE
    if (on) {
        P_PORTD &= ~(1 << LOAD_CUTOFF_BIT);
    } else {
        P_PORTD |= (1 << LOAD_CUTOFF_BIT);
    }
#else
    (void)on;
#endif
}

/* ---------------------------------------------------------------------
 * Servo signal
 * ------------------------------------------------------------------- */
static void servo_signal_off(void)
{
    /* Let any in-flight servo pulse finish before disconnecting the pin.
     * TOV1 is set once per 20 ms servo period and is only cleared by
     * writing a 1 to it, so this waits at most one period and never
     * hangs on a stale flag. */
    while (!(P_TIFR1 & (1 << B_TOV1))) { }
    P_TIFR1 = (1 << B_TOV1);

    P_TCCR1A &= ~(1 << B_COM1A1);   /* OC1A no longer drives PB1 */
    P_PORTB  &= ~(1 << B_PB1);      /* hold the signal low */
}

/* ---------------------------------------------------------------------
 * Sleep
 * ------------------------------------------------------------------- */
unsigned char power_sleep(void)
{
    unsigned char saved_tccr0b = P_TCCR0B;
    unsigned char saved_timsk0 = P_TIMSK0;
    unsigned char saved_prr    = P_PRR;

    /* 1. Servo: stop commanding it, then cut its supply if fitted. The
     *    servo's own quiescent draw is larger than everything on the MCU
     *    put together, so this is the biggest single saving. */
    servo_signal_off();
    power_servo_supply(0);

    /* 2. ADC off. */
    P_ADCSRA &= ~(1 << B_ADEN);

    /* 3. Timer0 belongs to the Arduino core's millis(). Its overflow
     *    interrupt would wake the CPU roughly every millisecond, so stop
     *    the clock and mask the interrupt. On the bare chip millis() is
     *    unused, and this is what lets the chip actually stay down. */
    P_TCCR0B = 0;
    P_TIMSK0 = 0;

    /* 4. Gate the clocks we aren't using. PRUSART0 matters most: with the
     *    serial messages added for user feedback, the transmitter would
     *    otherwise keep drawing through the whole sleep. The register is
     *    saved above and restored on wake. */
    P_PRR |= (1 << B_PRUSART0) | (1 << B_PRSPI0) | (1 << B_PRTIM1);

    /* 5. Drop into power-down with the brown-out detector off for the
     *    duration. The BOD-disable sequence has a hard timing rule: after
     *    BODS and BODSE are set together, BODS must be written again (with
     *    BODSE clear) within the next four clock cycles, and the sleep must
     *    follow immediately. All of it lives in one asm block so no
     *    function call or interrupt can slip in between. BODS and BODSE are
     *    bits 6 and 5 of MCUCR (0x55), so 0xC0 sets both and 0x40 leaves
     *    BODS alone. */
    __asm__ __volatile__ (
        "cli"                "\n\t"
        "in  r24, 0x3B"      "\n\t"   /* PCIFR : drop any stale wake flag */
        "out 0x3B, r24"      "\n\t"
        "in  r24, 0x55"      "\n\t"   /* MCUCR : set BODS + BODSE         */
        "ori r24, 0xC0"      "\n\t"
        "out 0x55, r24"      "\n\t"
        "ldi r25, 0x40"      "\n\t"   /* MCUCR : BODS only, BODSE clear   */
        "out 0x55, r25"      "\n\t"
        "ldi r25, 0x21"      "\n\t"   /* SMCR  : SM1 | SE, power-down     */
        "out 0x53, r25"      "\n\t"
        "sei"                "\n\t"   /* the instruction after SEI runs   */
        "sleep"              "\n\t"   /* BOD takes effect only now        */
        "in  r24, 0x55"      "\n\t"   /* MCUCR : BODS/BODSE back off      */
        "andi r24, 0x3F"     "\n\t"
        "out 0x55, r24"      "\n\t"
        "in  r24, 0x53"      "\n\t"   /* SMCR  : SE clear, mode idle      */
        "andi r24, 0xFE"     "\n\t"
        "out 0x53, r24"      "\n\t"
        ::: "r24", "r25", "memory"
    );

    /* ---- awake ---- */

    /* Undo the clock gating, then restore Timer0 to exactly what the core
     * left it as. */
    P_PRR     = saved_prr;
    P_TCCR0B  = saved_tccr0b;
    P_TIMSK0  = saved_timsk0;

    /* The ADC needs a moment after ADEN goes back on before its first
     * conversion means anything; the caller also discards one reading. */
    P_ADCSRA |= (1 << B_ADEN);

    /* 6. Servo supply back on, and hand the pin back to Timer1. Holding
     *    the supply off means the servo forgot where it was, so the caller
     *    must re-assert the position rather than assume the last commanded
     *    angle survived. */
    power_servo_supply(1);
    P_TCCR1A |= (1 << B_COM1A1);

    return POWER_WAKE_KEY;
}
