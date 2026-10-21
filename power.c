#include "power.h"

/* =====================================================================
 * Registers, declared by hand so this project needs no <avr/io.h>.
 * The REG8() addresses below are DATA-space addresses, used with a
 * normal C load/store. The inline asm at the bottom needs the I/O-space
 * addresses instead, which are 0x20 lower and must fit in six bits.
 * ===================================================================== */
#define REG8(a) (*(volatile unsigned char *)(a))

#define P_TIFR1   REG8(0x36)   /* I/O 0x16 */
#define P_PCIFR   REG8(0x3B)   /* I/O 0x1B */
#define P_SMCR    REG8(0x53)   /* I/O 0x33 */
#define P_MCUCR   REG8(0x55)   /* I/O 0x35 */
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

/* I/O-space addresses for the asm block. Only these are ever touched
 * with in/out; everything else uses the C macros above. */
#define IO_PCIFR  0x1B
#define IO_SMCR   0x33
#define IO_MCUCR  0x35

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

/* Brown-out detector disable for the sleep window.
 *
 * The MCUCR BODS/BODSE sequence has a hard four-cycle rule and is the
 * fiddliest part of this file, so it is switchable. It is OFF by default
 * because it is what stops the chip waking when the timing is wrong.
 *
 * The clean way to get the same saving is the extended fuse instead:
 * set efuse to 0xFF and the BOD is simply never enabled, so no sequence
 * is needed at all. See the notes in power.h. */
#define BOD_DISABLE_ON_SLEEP 0

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

    /* Clear any wake flag left over from boot, so the first sleep isn't
     * entered with a pending interrupt already latched. */
    if (P_PCIFR & (1 << B_PCIF1)) {
        P_PCIFR = (1 << B_PCIF1);
    }

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

    /* 5. Arm the wake source, drop any stale flag, enter power-down and
     *    let a key press bring us back.
     *
     *    PCMSK1 already includes PCINT8 from power_init(); PCIE1 is
     *    switched on only here, so ordinary typing doesn't fire interrupts.
     *    sei goes over a single instruction before sleep, and an interrupt
     *    arriving in that window sets its flag rather than being lost, so
     *    no key press can fall between the two. */
    P_PCICR |= (1 << B_PCIE1);
    P_PCIFR  = (1 << B_PCIF1);            /* writing 1 clears the flag */
    P_SMCR   = (1 << B_SM1) | (1 << B_SE);/* power-down, sleep enabled */

#if BOD_DISABLE_ON_SLEEP
    /* The four-cycle window is the fragile part, so all of it is in one
     * block and nothing can slip between the two MCUCR writes and the
     * sleep. MCUCR is read-modify-written so its other bits survive. */
    __asm__ __volatile__ (
        "cli"                    "\n\t"
        "in  r24, %[mcucr]"      "\n\t"   /* read, don't clobber the rest */
        "ori r24, 0xC0"          "\n\t"   /* BODS + BODSE                 */
        "out %[mcucr], r24"      "\n\t"
        "ori r24, 0x40"          "\n\t"   /* BODS again, BODSE cleared    */
        "andi r24, 0xDF"         "\n\t"
        "out %[mcucr], r24"      "\n\t"
        "sei"                    "\n\t"
        "sleep"                  "\n\t"
        "in  r24, %[mcucr]"      "\n\t"   /* switch the BOD back on       */
        "andi r24, 0x3F"         "\n\t"
        "out %[mcucr], r24"      "\n\t"
        :
        : [mcucr] "M" (IO_MCUCR)
        : "r24", "memory"
    );
#else
    /* BOD left alone: plain sleep. */
    __asm__ __volatile__ ("sei" "\n\t" "sleep" ::: "memory");
#endif

    /* ---- awake ---- */

    /* Disarm the wake source immediately, so the rest of this function
     * runs without a stray interrupt. */
    P_SMCR   = 0;                        /* SE clear, idle mode */
    P_PCICR &= ~(1 << B_PCIE1);

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
