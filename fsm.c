/*
states: locked | unlocked | locking | reprogram | sleep

locked    : digits fill the attempt buffer; '#' checks it
            correct -> unlocked, wrong -> stay locked
unlocked  : '*' -> locking, '#' -> reprogram
locking   : the move runs while block_watch polls the shunt:
            sustained high -> unlocked, otherwise -> locked
reprogram : digits fill the new-code buffer
            '#' with exactly 4 digits -> save to EEPROM -> unlocked
            '*' -> cancel -> unlocked
any state : SLEEP_TIMEOUT_MS with no key -> sleep (power-down)
sleep     : ONLY the wake button (D2) wakes the chip. On wake the FSM goes
            straight back to the state it slept from and starts a fresh
            timeout window; no keypad press is needed.

Block detection: the watch is opened when the move starts and sampled once
every BLOCK_SAMPLE_PERIOD_MS from fsm_tick(), so the reading is spread
through the whole move instead of depending on when the timeout happens to
expire. A verdict is only given once BLOCK_CONFIRM_SAMPLES samples in a row
have read high, which is what separates a real stall from the inrush spike
at the start of every move.

Servo switch: power_servo_supply() is driven LOW-SIDE, so D4 HIGH means
servo supplied and D4 LOW means off. The FSM does not touch that polarity
itself -- it only calls the function -- so switching back to a P-channel
high-side part is a change in power.c alone.

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

static unsigned long locking_start;  /* when the current move began */
static unsigned long last_activity;  /* for the sleep timeout */
static unsigned long last_sample;    /* when the watch last took a sample */
static unsigned char watch_done;     /* the current watch has a verdict */

static key_t new_code[PIN_LEN];      /* reprogram buffer */
static unsigned int new_len = 0;

static void announce(unsigned char event) { fsm_on_event(event); }

/* Command a position and open a block watch over the move. */
static void start_move(unsigned char to_locked, unsigned long now) {
  if (to_locked) {
    setPosLock();
  } else {
    setPosUnlock();
  }
  locking_start = now;
  last_sample = now;
  watch_done = 0;
  block_watch_begin();
}

static void handle_locked(key_t key, unsigned long now) {
  if (key == KEY_NONE) {
    return;
  }
  if (key == KEY_POUND) {
    switch (locker_check()) {
      case LOCKER_MATCH:
        fsm_on_password_result(1);
        start_move(0, now);          /* correct code: unlock */
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
    start_move(1, now);            /* lock the door */
    state = STATE_LOCKING;
  }
  else if (key == KEY_POUND) {
    new_len = 0;
    state = STATE_REPROGRAM;
    announce(FSM_EVT_REPROGRAM_START);
  }
}

/* The move is already running; this state exists to watch it. Samples are
 * taken on a fixed spacing so the readings land all over the servo's PWM
 * cycle, and the verdict comes from how many of them in a row read high. */
static void handle_locking(key_t key, unsigned long now) {
  (void)key;

  if (!watch_done && (now - last_sample) >= BLOCK_SAMPLE_PERIOD_MS) {
    last_sample = now;
    if (block_watch_poll()) {
      watch_done = 1;
    }
  }

  if (watch_done && (now - locking_start) >= LOCK_TIMEOUT) {
    if (block_watch_blocked()) {
      /* Blocked: go back to unlocked. No watch on this move -- the servo
         has already failed to reach the position, so there is nothing
         left to strain against. */
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
  setPosLock();  /* physically assert the locked position: the FSM starts
                    in STATE_LOCKED, and an uncommanded servo holds no torque */
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
