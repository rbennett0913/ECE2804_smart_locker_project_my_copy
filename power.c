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
#define P_PIND    REG8(0x29)

/* I/O-space address for the asm block: only MCUCR is touched with in/out. */
#define IO_MCUCR  0x35

/* TIFR1 */
#define B_TOV1     0
/* SMCR */
#define B_SE       0
#define B_SM1      2           /* SM2..0 = 010 -> power-down */
/* PCICR / PCIFR */
#define B_PCIE1    1           /* port C */
#define B_PCIE2    2           /* port D */
#define B_PCIF1    1
#define B_PCIF2    2
/* PCMSK1 / PCMSK2 */
#define B_PCINT8   0           /* PCINT8  = PC0 / Arduino A0 */
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

/* ---- dedicated wake button -------------------------------------------
 * Separate pushbutton from PD2 (D2) to GND, internal pull-up enabled.
 * This is the reliable wake source: the resistor ladder can't wake the
 * chip on every key, because most keys sit below the pin's digital
 * threshold. Waking here means "user is present", not "this key was
 * pressed", so the FSM ignores which key it was. */
#define WAKE_BUTTON_ENABLE 1

/* PCINT1 is interrupt vector 4, PCINT2 is vector 5. Both handlers are
 * deliberately empty: the interrupts exist only to wake the CPU. They
 * must still be defined, because an enabled interrupt with no handler
 * jumps to the reset vector instead. Declared by hand so
 * <avr/interrupt.h> isn't needed.
 *
 * IMPORTANT: __vector_5 must not be used for anything else. If the
 * servo driver or a future add-on needs a port D interrupt, it has to
 * share this handler, not define a second one. */
void __vector_4(void) __attribute__((signal, used, externally_visible));
void __vector_4(void) { }
void __vector_5(void) __attribute__((signal, used, externally_visible));
void __vector_5(void) { }

/* ---------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------- */
void power_init(void)
{
    /* Keypad wake source: the pin-change logic reads the pin's digital
     * input buffer, so it has to stay enabled on A0 even though A0 is an
     * analog input. This only catches the top-row keys -- the wake button
     * below is what makes waking reliable. */
    P_DIDR0  &= ~(1 << B_ADC0D);
    P_PCMSK1 |=  (1 << B_PCINT8);

#if WAKE_BUTTON_ENABLE
    /* D2 as an input with the internal pull-up. Pressing the button pulls
     * the pin to GND, a clean logic edge that the pin-change logic sees
     * no matter what the ladder is doing. */
    P_DDRD   &= ~(1 << B_PCINT18);
    P_PORTD  |=  (1 << B_PCINT18);
    P_PCMSK2 |=  (1 << B_PCINT18);
#endif

    /* Clear any wake flag left over from boot, so the first sleep isn't
     * entered with an interrupt already latched. */
    if (P_PCIFR & (1 << B_PCIF1)) {
        P_PCIFR = (1 << B_PCIF1);
    }
#if WAKE_BUTTON_ENABLE
    if (P_PCIFR & (1 << B_PCIF2)) {
        P_PCIFR = (1 << B_PCIF2);
    }
#endif

#if HW_SERVO_CUTOFF_ENABLE
    /* 2N3906 base: an output. HIGH at boot holds the servo OFF while the
     * rest of bring-up runs; power_servo_supply(1) switches it on once
     * the servo signal is being driven. */
    P_DDRD  |=  (1 << SERVO_CUTOFF_BIT);
    P_PORTD |=  (1 << SERVO_CUTOFF_BIT);
#endif
#if HW_LOAD_CUTOFF_ENABLE
    P_DDRD  |=  (1 << LOAD_CUTOFF_BIT);
    P_PORTD |=  (1 << LOAD_CUTOFF_BIT);
#endif
}

/* ---------------------------------------------------------------------
 * Hardware cutoffs
 *
 * 2N3906 PNP high side: emitter to +5 V, collector to the load, base to
 * the pin through 1 k, with a 10 k from base to emitter. Pull the base
 * LOW to deliver current; the pull-up keeps the load off whenever the
 * MCU is unpowered or in reset, so nothing turns on by accident.
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

    /* 1. Servo: stop commanding it, then cut its supply. The servo's own
     *    quiescent and holding current is larger than everything on the
     *    MCU put together, so switching its rail off is the biggest single
     *    saving available. */
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

    /* 5. Arm the wake sources, clear any stale flags, enter power-down and
     *    let a key or the button bring us back.
     *
     *    PCIE1 is the ladder on A0, PCIE2 is the button on D2. Both are
     *    switched on only here, so ordinary use doesn't fire interrupts.
     *    sei goes over a single instruction before sleep, and an interrupt
     *    arriving in that window sets its flag rather than being lost, so
     *    no press can fall between the two. */
    P_PCIFR  = (1 << B_PCIF1);
    P_PCICR |= (1 << B_PCIE1);
#if WAKE_BUTTON_ENABLE
    P_PCIFR  = (1 << B_PCIF2);
    P_PCICR |= (1 << B_PCIE2);
#endif
    P_SMCR = (1 << B_SM1) | (1 << B_SE);   /* power-down, sleep enabled */

#if BOD_DISABLE_ON_SLEEP
    __asm__ __volatile__ (
        "cli"               "\n\t"
        "in  r24, %[mcucr]" "\n\t"   /* read, don't clobber the rest */
        "ori r24, 0xC0"     "\n\t"   /* BODS + BODSE                 */
        "out %[mcucr], r24" "\n\t"
        "ori r24, 0x40"     "\n\t"   /* BODS again, BODSE cleared    */
        "andi r24, 0xDF"    "\n\t"
        "out %[mcucr], r24" "\n\t"
        "sei"               "\n\t"
        "sleep"             "\n\t"
        "in  r24, %[mcucr]" "\n\t"   /* switch the BOD back on       */
        "andi r24, 0x3F"    "\n\t"
        "out %[mcucr], r24" "\n\t"
        :
        : [mcucr] "M" (IO_MCUCR)
        : "r24", "memory"
    );
#else
    __asm__ __volatile__ ("sei" "\n\t" "sleep" ::: "memory");
#endif

    /* ---- awake ---- */

    /* Disarm the wake sources immediately, so the rest of this function
     * runs without a stray interrupt. */
    P_SMCR   = 0;                          /* SE clear, idle mode */
    P_PCICR &= ~(1 << B_PCIE1);
#if WAKE_BUTTON_ENABLE
    P_PCICR &= ~(1 << B_PCIE2);
#endif

    /* Undo the clock gating, then restore Timer0 to exactly what the core
     * left it as. */
    P_PRR    = saved_prr;
    P_TCCR0B = saved_tccr0b;
    P_TIMSK0 = saved_timsk0;

    /* The ADC needs a moment after ADEN goes back on before its first
     * conversion means anything; the caller also discards one reading. */
    P_ADCSRA |= (1 << B_ADEN);

    /* 6. Hand the pin back to Timer1, then switch the servo supply on.
     *    Order matters: the caller must re-assert the position, because a
     *    servo that lost its supply has forgotten where it was. */
    P_TCCR1A |= (1 << B_COM1A1);
    power_servo_supply(1);

    return POWER_WAKE_KEY;
}
