#include "locker.h"
#include "eeprom.h"

/* EEPROM layout
 *   0x000       magic byte: 0xA5 once a code has been saved
 *   0x001-0x004 the 4 code digits, stored as key_t values (KEY_0..KEY_9)
 */
#define EE_MAGIC_ADDR 0x000
#define EE_CODE_ADDR  0x001
#define EE_MAGIC      0xA5

static key_t code[PIN_LEN];          /* RAM copy of the saved code */

static key_t attempt[PIN_LEN];
static unsigned int len = 0;

static int is_digit(key_t k)
{
    return k >= KEY_0 && k <= KEY_9;
}

void locker_set_code(const key_t *new_code)
{
    unsigned int i;
    for (i = 0; i < PIN_LEN; i++) {
        code[i] = new_code[i];
        ee_write(EE_CODE_ADDR + i, (unsigned char)new_code[i]);
    }
    /* Magic goes last: if power dies mid-save on a fresh chip, the next
     * boot sees no magic and falls back to the default code. */
    ee_write(EE_MAGIC_ADDR, EE_MAGIC);
}

void locker_init(void)
{
    static const key_t default_code[PIN_LEN] = {KEY_1, KEY_2, KEY_3, KEY_4};
    unsigned int i;
    int valid = (ee_read(EE_MAGIC_ADDR) == EE_MAGIC);

    for (i = 0; valid && i < PIN_LEN; i++) {
        code[i] = (key_t)ee_read(EE_CODE_ADDR + i);
        if (!is_digit(code[i])) {
            valid = 0;               /* corrupt entry: don't lock the user out */
        }
    }
    if (!valid) {
        locker_set_code(default_code);
    }
    len = 0;
}

void locker_reset(void)
{
    len = 0;
}

void locker_add_digit(key_t key)
{
    if (len == PIN_LEN) {
        /* Buffer full but not yet submitted: start a fresh attempt with
           this key rather than judging stale digits on '#'. */
        len = 0;
    }
    attempt[len++] = key;
}

locker_status_t locker_check(void)
{
    unsigned int i;
    unsigned char diff = 0;

    if (len != PIN_LEN) {
        return LOCKER_IN_PROGRESS;
    }

    /* Compare every digit even after a mismatch, so timing doesn't reveal
       how many leading digits were right. */
    for (i = 0; i < PIN_LEN; i++) {
        diff |= (unsigned char)(attempt[i] ^ code[i]);
    }

    locker_reset();
    return diff ? LOCKER_WRONG : LOCKER_MATCH;
}
