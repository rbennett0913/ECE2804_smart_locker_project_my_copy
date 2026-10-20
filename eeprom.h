#ifndef eeprom_h
#define eeprom_h

/* Bare-metal EEPROM access for the ATmega328P (1 KB, addresses 0..1023).
 * Direct register access only -- no <avr/eeprom.h>. */

#ifdef __cplusplus
extern "C" {
#endif

unsigned char ee_read(unsigned int addr);
void ee_write(unsigned int addr, unsigned char data);

#ifdef __cplusplus
}
#endif

#endif
