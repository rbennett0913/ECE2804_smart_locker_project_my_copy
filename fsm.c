/*
states: locked | unlocked | locking | reprogram | sleep

locked    : digits fill the attempt buffer; '#' checks it
            correct -> unlocked, wrong -> stay locked
unlocked  : '*' -> locking, '#' -> reprogram
locking   : after LOCK_TIMEOUT: block detected -> unlocked, else -> locked
reprogram : digits fill the new-code buffer
            '#' with exactly 4 digits -> save to EEPROM -> unlocked
            '*' -> cancel -> unlocked
any state : SLEEP_TIMEOUT_MS with no key -> sleep (power-down)
sleep     : key press wakes -> back to the state it slept from
            (the waking key is not used as input)
*/

#include "fsm.h"
#include "pwm.h"
#include "keypad.h"
#include "locker.h"
#include "current.h"
#include "power.h"

typedef enum {
  STATE_LOCKED,
  STATE_UNLOCKED,
  STATE_LOCKING,
  STATE_REPROGRAM,
  STATE_SLEEP
} state_t;

static state_t state = STATE_LOCKED;
static state_t prev_state;           /* state to return to after waking */

static unsigned long locking_start;  /* used for checking for block */
static unsigned long last_activity;  /* for the sleep timeout */

static key_t new_code[PIN_LEN];      /* reprogram buffer */
static unsigned int new_len = 0;

static void handle_locked(key_t key, unsigned long now) {
  (void)now;
  if (key == KEY_NONE) {
    return;
  }
  if (key == KEY_POUND) {
    switch (locker_check()) {
      case LOCKER_MATCH:
        setPosUnlock();
        state = STATE_UNLOCKED;
        break;
      case LOCKER_WRONG:
        /* stay locked; buffer already reset for next try */
        break;
      default:
        /* LOCKER_IN_PROGRESS: '#' with fewer than 4 digits. Discard the
           partial entry so leftover digits can't poison the next try. */
        locker_reset();
        break;
    }
  }
  else if (key >= KEY_0 && key <= KEY_9) {
    locker_add_digit(key);
  }
  /* KEY_STAR is meaningless while locked: ignore */
}

static void handle_unlocked(key_t key, unsigned long now) {
  if (key == KEY_STAR) {
    setPosLock();
    locking_start = now;
    state = STATE_LOCKING;
  }
  else if (key == KEY_POUND) {
    new_len = 0;
    state = STATE_REPROGRAM;
  }
}

static void handle_locking(key_t key, unsigned long now) {
  (void)key;
  if (now - locking_start >= LOCK_TIMEOUT) {
    if (block_detected()) {
      setPosUnlock();          /* door is blocked: abort and re-open */
      state = STATE_UNLOCKED;
    } else {
      state = STATE_LOCKED;
    }
  }
}

static void handle_reprogram(key_t key, unsigned long now) {
  (void)now;
  if (key == KEY_NONE) {
    return;
  }
  if (key >= KEY_0 && key <= KEY_9) {
    if (new_len == PIN_LEN) {
      new_len = 0;             /* 5th digit starts the entry over */
    }
    new_code[new_len++] = key;
  }
  else if (key == KEY_POUND) {
    if (new_len == PIN_LEN) {
      locker_set_code(new_code);   /* persists through power loss */
      state = STATE_UNLOCKED;
    } else {
      new_len = 0;             /* incomplete: clear and let them retype */
    }
  }
  else if (key == KEY_STAR) {
    new_len = 0;               /* cancel: keep the old code */
    state = STATE_UNLOCKED;
  }
}

static void handle_sleep(key_t key, unsigned long now) {
  (void)now;
  if (key != KEY_NONE) {
    state = prev_state;        /* the waking key is consumed, not typed */
    return;
  }

  power_sleep();               /* returns only after a key press */

  /* First conversion after re-enabling the ADC is a throwaway. */
  (void)adc_read_channel(KEYPAD_CHANNEL);
  last_activity = milliseconds();
  /* If the wake was just noise, the next loop sees KEY_NONE and this
     handler puts the chip straight back to sleep. */
}

void fsm_init(void) {
  pwm_init();
  adc_init();
  current_init();
  locker_init();               /* load saved code from EEPROM */
  power_init();                /* key press on A0 wakes from sleep */
  setPosLock();  /* physically assert the locked position: the FSM starts
                    in STATE_LOCKED, and an uncommanded servo holds no torque */
  last_activity = milliseconds();
}

void fsm_tick(key_t key) {
  tick_update();
  unsigned long now = milliseconds();

  /* global timeout rule: SLEEP_TIMEOUT_MS of no input -> sleep */
  if (key != KEY_NONE) {
    last_activity = now;
  } else if (state != STATE_SLEEP && state != STATE_LOCKING
             && now - last_activity >= SLEEP_TIMEOUT_MS) {
    /* a half-typed new code doesn't survive the nap either */
    prev_state = (state == STATE_REPROGRAM) ? STATE_UNLOCKED : state;
    new_len = 0;
    locker_reset();            /* a half-typed code must not survive the nap */
    state = STATE_SLEEP;
  }

  switch (state) {
    case STATE_LOCKED:    handle_locked(key, now);    break;
    case STATE_UNLOCKED:  handle_unlocked(key, now);  break;
    case STATE_LOCKING:   handle_locking(key, now);   break;
    case STATE_REPROGRAM: handle_reprogram(key, now); break;
    case STATE_SLEEP:     handle_sleep(key, now);     break;
  }
}
