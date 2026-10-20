#ifndef locker_h
#define locker_h

#include "keypad.h"

#define PIN_LEN 4

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LOCKER_IN_PROGRESS, LOCKER_MATCH, LOCKER_WRONG } locker_status_t;

/* Load the saved code from EEPROM. On a blank or corrupt EEPROM, writes
 * the default code 1-2-3-4 once. Call once at boot. */
void locker_init(void);

locker_status_t locker_check(void);
void locker_reset(void);
void locker_add_digit(key_t key);

/* Replace the code and save it to EEPROM. `new_code` must hold exactly
 * PIN_LEN digit keys (KEY_0..KEY_9). */
void locker_set_code(const key_t *new_code);

#ifdef __cplusplus
}
#endif

#endif
