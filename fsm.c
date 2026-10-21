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
sleep     : key press or wake button -> back to the state it slept from

============ TEMPORARY DIAGNOSTIC BUILD ============
SLEEP_DEBUG is on. Every branch that enters or leaves the sleep state
prints to the serial port through the FSM_EVT_DEBUG hook, so the sequence
around a wake can be read off the console. Set SLEEP_DEBUG to 0 to remove
it. Nothing else in the FSM depends on it.
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

/* Turn off when the wake behaviour is understood. */
#define SLEEP_DEBUG 1

static state_t state = STATE_LOCKED;
static state_t prev_state;           /* state to return to after waking */

static unsigned long locking_start;  /* used for checking for block */
static unsigned long last_activity;  /* for the sleep timeout */

/* Wall time spent asleep. The millisecond counter is driven by Timer1
 * overflows and Timer1 is gated during power-down, so it does not advance
 * while the chip is down. Every duration comparison adds this back in. */
static unsigned long sleep_offset = 0;

static key_t new_code[PIN_LEN];      /* reprogram buffer */
static unsigned int new_len = 0;

static void announce(unsigned char event) { fsm_on_event(event); }

/* Clock reading with the asleep time added back in. */
static unsigned long now_ms(void) {
  return milliseconds() + sleep_offset;
}

#if SLEEP_DEBUG
/* one character per reason the FSM decided something */
#define DBG(c)  fsm_on_event(FSM_EVT_DEBUG), fsm_on_debug(c)
#else
#define DBG(c)  do { } while (0)
#endif

static void handle_locked(key_t key, unsigned long now) {
  (void)now;
  if (key == KEY_NONE) {
    return;
  }
  if (key == KEY_POUND) {
    switch (locker_check()) {
      case LOCKER_MATCH:
        fsm_on_password_result(1);
        setPosUnlock();
        state = STATE_UNLOCKED;
        break;
      case LOCKER_WRONG:
        fsm_on_password_result(0);
        /* stay locked; buffer already reset for next try */
        break;
      default:
        locker_reset();
        announce(FSM_EVT_ENTRY_INCOMPLETE);
        break;
    }
  }
  else if (key >= KEY_0 && key <= KEY_9) {
    locker_add_digit(key);
    fsm_on_digit((unsigned char)(key - KEY_0));
  }
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
    announce(FSM_EVT_REPROGRAM_START);
  }
}

static void handle_locking(key_t key, unsigned long now) {
  (void)key;
  if (now - locking_start >= LOCK_TIMEOUT) {
    if (block_detected()) {
      setPosUnlock();
      state = STATE_UNLOCKED;
      announce(FSM_EVT_BLOCKED_REVERT);
    } else {
      state = STATE_LOCKED;
      announce(FSM_EVT_LOCKED);
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
      new_len = 0;
    }
    new_code[new_len++] = key;
    fsm_on_digit((unsigned char)(key - KEY_0));
  }
  else if (key == KEY_POUND) {
    if (new_len == PIN_LEN) {
      locker_set_code(new_code);
      state = STATE_UNLOCKED;
      announce(FSM_EVT_CODE_SAVED);
    } else {
      new_len = 0;
      announce(FSM_EVT_ENTRY_INCOMPLETE);
    }
  }
  else if (key == KEY_STAR) {
    new_len = 0;
    state = STATE_UNLOCKED;
    announce(FSM_EVT_REPROGRAM_CANCEL);
  }
}

/* One pass through STATE_SLEEP. Returns after power_sleep() has put the
 * chip down and something has brought it back. */
static void sleep_pass(key_t key) {
  if (key != KEY_NONE) {
    state = prev_state;
    announce(FSM_EVT_WAKE);
    return;
  }

  unsigned long before = milliseconds();
  power_sleep();
  sleep_offset += milliseconds() - before;

  /* Throwaway conversion: the first one after ADEN is re-enabled does
   * not reflect the pin. */
  (void)adc_read_channel(KEYPAD_CHANNEL);

  /* Report what the keypad reads now, after the discard, so the console
   * shows whether the ADC has really settled. */
  DBG((unsigned char)(adc_read_channel(KEYPAD_CHANNEL) >> 2));

  last_activity = now_ms();
}

void fsm_init(void) {
  pwm_init();
  adc_init();
  current_init();
  locker_init();
  power_init();
  setPosLock();
  sleep_offset = 0;
  last_activity = milliseconds();
  announce(FSM_EVT_BOOT);
}

void fsm_tick(key_t key) {
  tick_update();
  unsigned long now = now_ms();

  /* ---- TEMPORARY DIAGNOSTIC -------------------------------------------------
   * Every loop that reaches here while awake is logged, along with the two
   * test conditions the sleep decision rests on. Three things can be true at
   * once and the log is what separates them. */
  static key_t last_logged_key = KEY_NONE;
  static unsigned long awake_loops = 0;

  if (state != STATE_SLEEP) {
    awake_loops++;
  }
  if (key != last_logged_key) {
    last_logged_key = key;
    DBG((unsigned char)(key == KEY_NONE ? 0x10 : 0x20));
  }
  /* -------------------------------------------------------------------------- */

  if (key != KEY_NONE) {
    last_activity = now;
  } else if (state != STATE_SLEEP && state != STATE_LOCKING
             && now - last_activity >= SLEEP_TIMEOUT_MS) {
    prev_state = (state == STATE_REPROGRAM) ? STATE_UNLOCKED : state;
    new_len = 0;
    locker_reset();
    (void)awake_loops;
    DBG(0x30);                 /* decided to sleep */
    state = STATE_SLEEP;
    announce(FSM_EVT_SLEEP_ENTERING);
  }

  switch (state) {
    case STATE_LOCKED:    handle_locked(key, now);    break;
    case STATE_UNLOCKED:  handle_unlocked(key, now);  break;
    case STATE_LOCKING:   handle_locking(key, now);   break;
    case STATE_REPROGRAM: handle_reprogram(key, now); break;
    case STATE_SLEEP:     sleep_pass(key);            break;
  }
}
