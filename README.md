# ECE2804_smart_locker_project_my_copy
# Servo Locker — Arduino Uno sketch

Same firmware as the bare-metal C project in the repo root, packaged as
an Arduino IDE sketch. The implementation is unchanged: all hardware
access is direct ATmega328P register manipulation (ADC polling on the
keypad's resistor ladder, Timer1 fast PWM at 50 Hz for the servo). No
Arduino/Servo library calls are used — the modules are plain C and never
see `Arduino.h`; only the thin `.ino` wrapper does.

## Files

| File            | Purpose |
|-----------------|---------|
| `servo_locker.ino` | Sketch entry point: `setup()` → `fsm_init()`, `loop()` → `fsm_tick()` |
| `fsm.c/.h`      | State machine (was `main.c`); split into init + per-iteration tick |
| `keypad.c/.h`   | ADC + debounced keypad decoding (resistor ladder on ADC0) |
| `pwm.c/.h`      | Timer1 50 Hz servo PWM on OC1A + millisecond tick clock |
| `locker.c/.h`   | 4-digit passcode check |
| `current.c/.h`  | Servo current sensing (shunt on ADC1) + `block_detected()` |

## Flashing

1. Open `servo_locker/servo_locker.ino` in the Arduino IDE (or `arduino
   --verify servo_locker/servo_locker.ino` from the CLI). The IDE
   automatically compiles and links the `.c` files in the sketch folder.
2. Tools → Board → **Arduino Uno**.
3. Connect the board, pick its serial port, and click **Upload**.

## Wiring

| Signal                     | Uno pin | MCU pin |
|----------------------------|---------|---------|
| Keypad resistor-ladder out | A0      | PC0 / ADC0 (`KEYPAD_CHANNEL` in `keypad.h`) |
| Servo signal               | D9      | PB1 / OC1A |
| Servo current shunt (top)  | A1      | PC1 / ADC1 (`CURRENT_CHANNEL` in `current.h`) |

Default code is `1 2 3 4` (`locker.c`).

## Standalone ATmega328P

The firmware assumes a 16 MHz clock (50 Hz PWM, 9600 baud, ADC prescaler).
The clock source is selected by fuses, not by the code, and a factory-fresh
chip runs on its internal oscillator at 1 MHz and ignores the crystal.
"Upload Using Programmer" does not write fuses; "Burn Bootloader" does.

1. Wire the chip (DIP-28 pin numbers):

   | Pin    | Connection |
   |--------|------------|
   | 1 RESET | 10 k to 5 V (not to the Uno's RESET header) |
   | 7 VCC, 20 AVCC | 5 V, 100 nF to GND at each |
   | 8 GND, 22 GND | GND (both) |
   | 9, 10 XTAL | 16 MHz crystal across them, 22 pF from each to GND |
   | 15 PB1 / OC1A | Servo signal |
   | 23 PC0 / ADC0 | Keypad resistor-ladder out |
   | 24 PC1 / ADC1 | Servo current shunt (top) |

2. With the Uno running ArduinoISP: Tools → Board → **Arduino Uno**,
   Programmer → **Arduino as ISP**, then **Burn Bootloader** once to set
   the fuses (low `0xFF`, high `0xDE`, extended `0xFD`). The crystal must
   already be working: once the fuses select it, the chip does not answer
   the programmer without it.
3. Sketch → **Upload Using Programmer**.

To check the fuses without changing them:

    avrdude -c stk500v1 -P <port> -b 19200 -p m328p -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h

`0x62 0xD9 0xFF` is the factory setting (1 MHz internal).

### Serial monitor through the Uno

Hold the Uno's own MCU in reset (RESET header to GND) and wire straight,
not crossed: chip pin 3 (TXD) → Uno pin 1 "TX", chip pin 2 (RXD) → Uno
pin 0 "RX", grounds common. The sketch prints `servo_locker boot` at
9600 baud on reset. If the banner only appears at 600 or 4800 baud, the
chip is on the internal oscillator at 1 MHz or 8 MHz.

If the banner prints but keys do not, uncomment `DEBUG_ADC` in
`servo_locker.ino` to print raw ADC readings and compare them with the
thresholds in `keypad.h`.
