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
sleep     : ONLY the wake button (D2) wakes the chip. On wake the FSM goes
            straight back to the state it slept from and starts a fresh
            timeout window; no keypad press is needed.

Servo power: the servo's supply is switched off between moves. Every move
is run through move_servo(), which powers the servo, holds it for the
length of the travel, then cuts it again. That is what saves the servo's
idle and holding current, which is larger than the whole MCU asleep.

Clock note: milliseconds() counts Timer1 overflows and Timer1 is gated
during power-down, so the clock simply pauses while asleep. That is why
last_activity is re-stamped on wake; nothing else spans a sleep.

User feedback: every state change calls a status hook so the user can see
what the system just did. The hooks are implemented with the sketch, so
this file needs no serial or LED code of its own.
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

static void announce(unsigned char event) { fsm_on_event(event); }

/* Command a new position and power the servo for the move only.
 * Blocks for SERVO_MOVE_MS, then cuts the supply. */
static void move_servo(unsigned char to_locked, unsigned long now) {
  if (to_locked) {
    setPosLock();
  } else {
    setPosUnlock();
  }
  locking_start = now;
  servo_power_window(SERVO_MOVE_MS);
}

static void handle_locked(key_t key, unsigned long now) {
  if (key == KEY_NONE) {
    return;
  }
  if (key == KEY_POUND) {
    switch (locker_check()) {
      case LOCKER_MATCH:
        fsm_on_password_result(1);
        move_servo(0, now);          /* correct code: unlock */
        state = STATE_UNLOCKED;
        break;
      case LOCKER_WRONG:
        fsm_on_password_result(0);
        /* stay locked; buffer already reset for next try */
        break;
      default:
        /* LOCKER_IN_PROGRESS: '#' with fewer than 4 digits. Discard the
           partial entry so leftover digits can't poison the next try. */
        locker_reset();
        announce(FSM_EVT_ENTRY_INCOMPLETE);
        break;
    }
  }
  else if (key >= KEY_0 && key <= KEY_9) {
    locker_add_digit(key);
    fsm_on_digit((unsigned char)(key - KEY_0));
  }
  /* KEY_STAR is meaningless while locked: ignore */
}

static void handle_unlocked(key_t key, unsigned long now) {
  if (key == KEY_STAR) {
    move_servo(1, now);            /* lock the door */
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
  /* The move already happened inside move_servo(); just let the timeout
   * elapse before deciding, so a blocked servo has had time to show it. */
  if (now - locking_start >= LOCK_TIMEOUT) {
    if (block_detected()) {
      move_servo(0, now);          /* blocked: back to unlocked */
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
      new_len = 0;             /* 5th digit starts the entry over */
    }
    new_code[new_len++] = key;
    fsm_on_digit((unsigned char)(key - KEY_0));
  }
  else if (key == KEY_POUND) {
    if (new_len == PIN_LEN) {
      locker_set_code(new_code);   /* persists through power loss */
      state = STATE_UNLOCKED;
      announce(FSM_EVT_CODE_SAVED);
    } else {
      new_len = 0;             /* incomplete: clear and let them retype */
      announce(FSM_EVT_ENTRY_INCOMPLETE);
    }
  }
  else if (key == KEY_STAR) {
    new_len = 0;               /* cancel: keep the old code */
    state = STATE_UNLOCKED;
    announce(FSM_EVT_REPROGRAM_CANCEL);
  }
}

static void handle_sleep(void) {
  /* Blocks until the wake button brings the chip back. */
  power_sleep();

  /* First conversion after re-enabling the ADC is a throwaway. */
  (void)adc_read_channel(KEYPAD_CHANNEL);

  /* The button is the only wake source, so returning from power_sleep()
   * IS the wake. Go straight back to the previous state and start a full
   * timeout window. */
  state = prev_state;
  last_activity = milliseconds();
  announce(FSM_EVT_WAKE);
}

void fsm_init(void) {
  pwm_init();
  adc_init();
  current_init();
  locker_init();               /* load saved code from EEPROM */
  power_init();                /* wake button on D2, servo switch on D4 */
  /* Assert the locked position for real: the servo starts unpowered, so
   * this is what actually moves it to locked and leaves it there. */
  setPosLock();
  servo_power_window(SERVO_MOVE_MS);
  locking_start = milliseconds();
  last_activity = milliseconds();
  announce(FSM_EVT_BOOT);
}

void fsm_tick(key_t key) {
  tick_update();
  unsigned long now = milliseconds();

  /* global timeout rule: SLEEP_TIMEOUT_MS of no input -> sleep */
  if (key != KEY_NONE) {
    last_activity = now;
  } else if (state != STATE_SLEEP && state != STATE_LOCKING
             && now - last_activity >= SLEEP_TIMEOUT_MS) {
    prev_state = (state == STATE_REPROGRAM) ? STATE_UNLOCKED : state;
    new_len = 0;
    locker_reset();            /* a half-typed code must not survive the nap */
    state = STATE_SLEEP;
    /* the sketch flushes serial in this hook, before the USART is gated */
    announce(FSM_EVT_SLEEP_ENTERING);
  }

  switch (state) {
    case STATE_LOCKED:    handle_locked(key, now);    break;
    case STATE_UNLOCKED:  handle_unlocked(key, now);  break;
    case STATE_LOCKING:   handle_locking(key, now);   break;
    case STATE_REPROGRAM: handle_reprogram(key, now); break;
    case STATE_SLEEP:     handle_sleep();             break;
  }
}
