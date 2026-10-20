#include "locker.h"
#define PIN_LEN 4

static const key_t code[] = {KEY_1, KEY_2, KEY_3, KEY_4};

static key_t attempt[PIN_LEN];
static unsigned int len = 0;

void locker_reset(void) {
    len = 0;
}

void locker_add_digit(key_t key) {
    if (len == PIN_LEN) {
        /* Buffer full but not yet submitted: locker_check() only runs
           when '#' is pressed. Without this, a 5th (6th, ...) keypress
           would be silently dropped and '#' would judge the stale
           first four digits. Start a fresh attempt with this key
           instead. */
        len = 0;
    }
    attempt[len++] = key;
}

locker_status_t locker_check(void) {
    unsigned int i;

    if (len != PIN_LEN) {
        return LOCKER_IN_PROGRESS;   /* not all digits entered yet */
    }

    for (i = 0; i < PIN_LEN; i++) {
        if (attempt[i] != code[i]) {
            locker_reset();          /* wrong code: start over */
            return LOCKER_WRONG;
        }
    }

    locker_reset();                  /* matched: buffer starts clean */
    return LOCKER_MATCH;
}