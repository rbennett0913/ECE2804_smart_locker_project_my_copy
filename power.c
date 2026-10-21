#include "power.h"

/* =====================================================================
 * Registers, declared by hand so this project needs no <avr/io.h>.
 * The REG8() addresses below are DATA-space addresses, used with a
 * normal C load/store. The inline asm needs the I/O-space address
 * instead, which is 0x20 lower and must fit in six bits.
 * ===================================================================== */
#define REG8(a) (*(volatile unsigned char *)(a))

#define P_TIFR1   REG8(0x36)
#define P_PCIFR   REG8(0x3B)
#define P_SMCR    REG8(0x53)
#define P_PCICR   REG8(0x68)
#define P_PCMSK2  REG8(0x6D)   /* port D */
#define P_TIMSK0  REG8(0x6E)
#define P_ADCSRA  REG8(0x7A)
#define P_DIDR0   REG8(0x7E)
#define P_TCCR1A  REG8(0x80)
#define P_PORTB   REG8(0x25)
#define P_PRR     REG8(0x64)
#define P_TCCR0B  REG8(0x45)
#define P_DDRD    REG8(0x2A)
#define P_PORTD   REG8(0x2B)

/* I/O-space address for the asm block: only MCUCR is touched with in/out. */
#define IO_MCUCR  0x35

/* TIFR1 */
#define B_TOV1     0
/* SMCR */
#define B_SE       0
#define B_SM1      2           /* SM2..0 = 010 -> power-down */
/* PCICR / PCIFR / PCMSK2 */
#define B_PCIE2    2           /* port D */
#define B_PCIF2    2
#define B_PCINT18  2           /* PCINT18 = PD2 / Arduino D2 */
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

/* Brown-out detector disable for the sleep window. See power.h: the
 * clean route is the extended fuse (efuse 0xFF) rather than this. */
#define BOD_DISABLE_ON_SLEEP 0

/* ---- hardware cutoffs ------------------------------------------------ */

/* Servo supply: a 2N3906 PNP high side, base driven from PD4 (D4).
 * LOW on the base turns the servo ON; HIGH turns it OFF. */
#define HW_SERVO_CUTOFF_ENABLE 1
#define SERVO_CUTOFF_BIT      4        /* PD4 / D4 */

/* Whole 5 V load cutoff: PD5 (D5), same polarity. Leave this at 0 while
 * the MCU itself is on the switched rail, or it can't wake itself. */
#define HW_LOAD_CUTOFF_ENABLE 0
#define LOAD_CUTOFF_BIT       5        /* PD5 / D5 */

/* ---- wake button ------------------------------------------------------
 * Pushbutton from PD2 (D2) to GND, internal pull-up enabled. This is the
 * ONLY wake source: the keypad no longer wakes the chip. */

/* PCINT2 (port D) is interrupt vector 5. The handler is deliberately
 * empty: the interrupt exists only to wake the CPU. It must be defined,
 * because an enabled interrupt with no handler jumps to the reset vector.
 * Declared by hand so <avr/interrupt.h> isn't needed.
 *
 * __vector_5 is reserved for this: any future port D pin-change use has
 * to share this handler rather than define a second one. */
void __vector_5(void) __attribute__((signal, used, externally_visible));
void __vector_5(void) { }

/* ---------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------- */
void power_init(void)
{
    /* A0 is now a pure analog input: nothing needs its digital buffer, and
     * leaving it on wastes current at the ladder's mid-rail voltages. */
    P_DIDR0 |= (1 << B_ADC0D);

    /* D2 as an input with the internal pull-up. Pressing the button pulls
     * the pin to GND, a clean edge the pin-change logic always sees. */
    P_DDRD   &= ~(1 << B_PCINT18);
    P_PORTD  |=  (1 << B_PCINT18);
    P_PCMSK2 |=  (1 << B_PCINT18);

    /* Clear any wake flag left over from boot. */
    P_PCIFR = (1 << B_PCIF2);

#if HW_SERVO_CUTOFF_ENABLE
    /* 2N3906 base as an output, LOW = servo supplied. The FSM commands
     * the lock position right after this, so the servo needs its supply. */
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
 *
 * 2N3906 PNP high side: emitter to +5 V, collector to the load, base to
 * the pin through 1 k, with a 10 k from base to emitter. Pull the base
 * LOW to deliver current; the pull-up keeps the load off whenever the
 * MCU is unpowered or in reset.
 * ------------------------------------------------------------------- */
void power_servo_supply(unsigned char on)
{
#if HW_SERVO_CUTOFF_ENABLE
    if (on) {
        P_PORTD &= ~(1 << SERVO_CUTOFF_BIT);   /* base low  -> servo on  */
    } else {
        P_PORTD |=  (1 << SERVO_CUTOFF_BIT);   /* base high -> servo off */
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
        P_PORTD |=  (1 << LOAD_CUTOFF_BIT);
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
     * TOV1 is set once per 20 ms servo period, so this waits at most one
     * period. */
    P_TIFR1 = (1 << B_TOV1);
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

    /* 1. Servo: stop commanding it, then cut its supply. */
    servo_signal_off();
    power_servo_supply(0);

    /* 2. ADC off. */
    P_ADCSRA &= ~(1 << B_ADEN);

    /* 3. Timer0 (Arduino core's millis()) would wake the CPU about every
     *    millisecond, so stop its clock and mask its interrupt. */
    P_TCCR0B = 0;
    P_TIMSK0 = 0;

    /* 4. Gate the clocks we aren't using while asleep. */
    P_PRR |= (1 << B_PRUSART0) | (1 << B_PRSPI0) | (1 << B_PRTIM1);

    /* 5. Arm the button, clear any stale flag, enter power-down. sei runs
     *    one more instruction before an interrupt is taken, so a press in
     *    that window still wakes the chip. */
    P_PCIFR  = (1 << B_PCIF2);
    P_PCICR |= (1 << B_PCIE2);
    P_SMCR = (1 << B_SM1) | (1 << B_SE);   /* power-down, sleep enabled */

#if BOD_DISABLE_ON_SLEEP
    __asm__ __volatile__ (
        "cli"               "\n\t"
        "in  r24, %[mcucr]" "\n\t"
        "ori r24, 0x60"     "\n\t"   /* BODS + BODSE              */
        "out %[mcucr], r24" "\n\t"
        "andi r24, 0xDF"    "\n\t"   /* BODS only, BODSE cleared  */
        "out %[mcucr], r24" "\n\t"
        "sei"               "\n\t"
        "sleep"             "\n\t"
        :
        : [mcucr] "M" (IO_MCUCR)
        : "r24", "memory"
    );
#else
    __asm__ __volatile__ ("sei" "\n\t" "sleep" ::: "memory");
#endif

    /* ---- awake ---- */

    P_SMCR   = 0;                          /* SE clear */
    P_PCICR &= ~(1 << B_PCIE2);            /* button only wakes while asleep */

    P_PRR    = saved_prr;
    P_TCCR0B = saved_tccr0b;
    P_TIMSK0 = saved_timsk0;

    P_ADCSRA |= (1 << B_ADEN);

    /* 6. Hand the pin back to Timer1, then switch the servo supply on, so
     *    the servo sees a valid signal as it powers up. */
    P_TCCR1A |= (1 << B_COM1A1);
    power_servo_supply(1);

    return POWER_WAKE_KEY;
}
