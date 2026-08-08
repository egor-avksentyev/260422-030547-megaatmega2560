# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an Arduino Mega 2560 (PlatformIO env `megaatmega2560`) controlling a hardware audio unit: motor-driven Bass/High/Volume controls, an SSD1306 OLED menu, IR remote + rotary encoder input, and relay-switched Standby/VU-Meter/LED/Mute.

All firmware logic lives in a single file: `src/Long_var2.cpp`. There is no header split — declarations and definitions are both in this file (prototypes at the top, implementations below).

## Commands

```bash
pio run                      # build
pio run -t upload            # build + flash to the Mega over serial
pio run -t upload -t monitor # flash then open serial monitor
pio device monitor           # attach serial monitor (monitor_speed = 115200 is set in platformio.ini)
pio run -t clean             # clean build artifacts
```

`test/` and `lib/` only contain PlatformIO's default scaffold README — no unit tests or private libraries currently exist in this project.

If the serial monitor shows garbled text, check for a stale `platformio device monitor` process still holding the port from before a `monitor_speed` change (`lsof /dev/cu.usbmodem*` on macOS, then `kill` it) — a running monitor process keeps using the baud rate it started with.

## Architecture

### State model
A single global state machine drives everything: `currentMenuItem` indexes `menuItems[]` = `{"Bass", "High", "Volume", "VU Meter", "Led"}` (5 items — all `% 5` wraparound arithmetic in the file must stay in sync with this count). `inSettingsMode` selects whether the screen shows the menu carousel or the settings view for the current item. Branching on menu item is done by **string comparison** (`menuItems[currentMenuItem] == "Bass"`) rather than an enum.

### Two independent input paths feed the same state
- **Rotary encoder**: polled every `loop()` iteration via `readEncoder()` (pins `ENCODER_A_PIN`/`ENCODER_B_PIN`, manual debounce) and `checkEncoderButton()` for the push-button (double-click exits settings mode).
- **IR remote**: interrupt-driven. `IR_ISR()` (attached on `IR_PIN`, `FALLING`) feeds a `NecDecoder`; `handleRemoteInput()` in `loop()` acts on the decoded command codes (`IR_RIGHT`/`IR_LEFT`/`IR_ENTER`/`IR_MUTE`/`IR_POWER`).

Both paths converge on the same redraw functions (`drawMenu()`, `drawToggleSwitch()`, `drawArrowIndicator()`) and the same motor/relay side effects — when changing behavior for a menu item, both input paths need the change applied in parallel (logic is duplicated between `handleRemoteInput()` and the encoder-handling block in `loop()`).

### Display
`U8g2lib`, hardware SPI, `U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI`. CS/DC/RESET are software-selectable (currently pins 10/9/12), but SCK/MOSI are fixed hardware-SPI pins on the Mega (52/51) and cannot be moved in code.

### Motors (Bass, High, Volume)
Two different driver interfaces are mixed in the same file:
- `motorControl()` — single direction pin + single PWM pin, used for Motor1 (Bass) and Motor2 (High). Assumes an L298N-style board, but only `IN1`/`IN3` are wired — `IN2`/`IN4` are not defined/connected in code, so true reverse rotation is not correctly achieved yet (both direction pins would need to be driven, one inverted from the other).
- `motorControl2()` — dual direction pin + dual PWM pin, used for Motor3 (Volume). Matches a BTS7960/IBT-2-style driver (`RPWM`/`LPWM` + enables).

### Volume position feedback (potentiometer on A8)
`VOLUME_POT_PIN` (A8) reads a feedback potentiometer wired to the Volume knob (+5V and GND on the outer legs, wiper to A8) so the real physical position can be shown on screen — separate from `settings[]`, which is the *commanded* target driving the motor. This has a specific, non-obvious implementation because the pot is a **logarithmic-taper** pot with a noisy wiper, handled entirely in the `menuItems[currentMenuItem] == "Volume"` block inside `drawArrowIndicator()`:

1. **Oversampling** — 64 consecutive `analogRead()` samples are averaged per redraw to cut ADC/wiper noise, done fresh each call (no cross-call state), so there's no lag behind real movement.
2. **Calibration table, not a formula** — `volumePotCalRaw[]` / `volumePotCalPercent[]` hold manually-measured `(raw ADC, physical %)` pairs, and `volumePotRawToPercent()` linearly interpolates between them (clamping outside the measured range). An earlier attempt used a closed-form log10 correction assuming a textbook audio-taper law; it didn't match this specific pot (a measured physical 50% read back as 42%), so it was replaced with real calibration points. Current points: raw 10→0%, raw 17→25%, raw 181→50%, raw 1010→100%. Note the raw range is extremely compressed in the first quarter of rotation (0–25% spans only raw 10–17) — this is inherent to the pot's taper, not something more averaging can fix, so noise there is expected to be larger relative to displayed %.
3. **Display-level hysteresis** — a `static int lastPotPercent` in `drawArrowIndicator()` only updates the on-screen/reported percentage when the newly computed value differs by ≥2 from what's currently shown, to stop residual noise (especially in the compressed low end) from flickering the digit even after oversampling.
4. **Live refresh** — `loop()` calls `drawArrowIndicator()` every 200ms while `inSettingsMode && menuItems[currentMenuItem] == "Volume"`, independent of encoder/IR events, so the position keeps updating as the motor (or a hand) moves the knob.

If recalibrating: read the `Volume pot raw: X -> Y% (shown: Z%)` line from Serial at known physical stops and update the two calibration arrays; keep `volumePotCalRaw[]` strictly ascending.

### Relays
Four relays, all **active-HIGH** (`digitalWrite(..., HIGH)` = energized/on) on pins 22/24/26/28 (Standby/VU Meter/Led/Mute) — this was flipped from the original active-LOW wiring when the relay modules were swapped, and Ethernet's old CS pin (22) was freed up for reuse here after the weather feature was removed. Standby/VU-Meter/LED are tied to the global power sequence (`powerOnDevices()`/`powerOffDevices()`), and VU-Meter/LED are also individually toggleable from their menu items. Mute is switched directly by a dedicated IR remote button (`IR_MUTE`), independent of menu navigation — note `isMuted` is not resynced with the relay state during `powerOnDevices()`, so it can drift out of sync across a power-off/power-on cycle.

`powerOffDevices()`/`powerOnDevices()` explicitly drive every relay and LED pin `OUTPUT` + `LOW`/`HIGH` rather than releasing them to `INPUT` — this was a deliberate fix so power-off is deterministic (floating pins can't be relied on to read as "off" — that depended on the old relay modules' own pull resistors, which no longer apply after the active-HIGH swap).

### Pin map reference
See in-file `#define`s near the top of `src/Long_var2.cpp` for the full pin assignment (encoder, motors, relays, LEDs, IR receiver, Volume feedback pot). Comments in Russian throughout the file describe each pin's purpose.
