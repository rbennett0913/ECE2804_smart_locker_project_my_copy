#include "eeprom.h"

/* Register addresses (data-memory mapped) from the ATmega328P datasheet.
 * Private names so they can't clash with <avr/io.h> if it is pulled in
 * somewhere else in the build. */
#define REG8(a)  (*(volatile unsigned char *)(a))
#define REG16(a) (*(volatile unsigned int  *)(a))

#define EE_SREG  REG8(0x5F)    /* status register (I bit = global interrupts) */
#define EE_CR    REG8(0x3F)    /* EECR  */
#define EE_DR    REG8(0x40)    /* EEDR  */
#define EE_AR    REG16(0x41)   /* EEARL/EEARH */

#define B_EERE   0             /* read enable          */
#define B_EEPE   1             /* write enable         */
#define B_EEMPE  2             /* master write enable  */

unsigned char ee_read(unsigned int addr)
{
    while (EE_CR & (1 << B_EEPE)) { }   /* wait for any write in progress */
    EE_AR = addr;
    EE_CR |= (1 << B_EERE);             /* read happens immediately */
    return EE_DR;
}

void ee_write(unsigned int addr, unsigned char data)
{
    unsigned char sreg;

    /* Skip identical writes: each cell is rated for ~100k erase/write cycles. */
    if (ee_read(addr) == data) {
        return;
    }

    while (EE_CR & (1 << B_EEPE)) { }
    EE_AR = addr;
    EE_DR = data;

    /* EEPE must be set within 4 clock cycles of EEMPE. An interrupt (e.g.
     * the Arduino core's Timer0 millis() ISR) landing between the two would
     * silently cancel the write, so interrupts are held off for these two
     * instructions and then restored to whatever they were. */
    sreg = EE_SREG;
    __asm__ __volatile__ ("cli" ::: "memory");
    EE_CR = (1 << B_EEMPE);                   /* EEPM = 00: erase + write */
    EE_CR = (1 << B_EEMPE) | (1 << B_EEPE);
    EE_SREG = sreg;
    /* The write itself takes ~3.4 ms and finishes in the background;
     * the next ee_read/ee_write waits for it. */
}
