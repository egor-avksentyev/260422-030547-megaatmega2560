# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an Arduino Mega 2560 (PlatformIO env `megaatmega2560`) controlling a hardware audio unit: motor-driven Bass/High/Volume controls with NeoPixel ring level indicators, an SSD1306 OLED menu, IR remote + rotary encoder input, and relay-switched Standby/VU-Meter/LED/Mute/Bypass/Source.

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

## File layout

The firmware is split by responsibility, one `.h`/`.cpp` pair per module (all in `src/`). Every module includes `hardware_settings.h` for pin numbers, calibration tables and screen layout constants, and `main.h` for the shared menu state — those two are the ones nearly everything depends on.

- **`main.cpp`/`main.h`** — `setup()`/`loop()` and the core menu state (`menuItems[]`, `currentMenuItem`, `settings[]`, `inSettingsMode`, `isMuted`, `lastMotorInputTime`). This is the only place that calls into every other module; it owns no device-specific logic itself.
- **`hardware_settings.h`** — every `#define` and calibration table: pin assignments, IR codes, motor/encoder timing, NeoPixel wiring order + color palette, potentiometer calibration arrays, and all on-screen font/position constants. Header-only (no `.cpp`) — nothing here should need runtime state. If you need to move something on the display, change a motor speed, or recalibrate a pot, this is the only file you should have to touch.
- **`display_logic.h`/`.cpp`** — owns the `u8g2` display object and every screen: `drawMenu()`, `drawToggleSwitch()`, `drawArrowIndicator()`, `drawDimmerScreen()`, `drawColorScreen()`, `drawSourceScreen()`, `displayMessage()`.
- **`encoder.h`/`.cpp`** — quadrature encoder ISR (`encoderISR()`, Mazurov state-table algorithm) and its push-button (`checkEncoderButton()`, double-click exits settings mode).
- **`remote_control.h`/`.cpp`** — IR receiver ISR (`IR_ISR()` feeding a `NecDecoder`) and `handleRemoteInput()`, which acts on `IR_RIGHT`/`IR_LEFT`/`IR_ENTER`/`IR_MUTE`/`IR_POWER`.
- **`motor_driver_logic.h`/`.cpp`** — low-level motor control: `motorControl()` (single direction pin + single PWM, Bass/High) and `motorControl2()` (dual direction + dual PWM, Volume), plus `stopAllMotors()`.
- **`motor_position.h`/`.cpp`** — potentiometer position feedback for Bass/High/Volume: oversampled `analogRead()` + calibration-table interpolation (tables live in `hardware_settings.h`).
- **`neopixel.h`/`.cpp`** — the three NeoPixel rings (Bass/High/Volume): ring objects, current color/brightness, dB-scale rendering (`renderDbRing()`), percent-scale rendering (`updateVolumeRing()`), and the 0dB "breathing" animation.
- **`relay.h`/`.cpp`** — `applySourceSelection()` (Source menu item, mutually-exclusive relay), `applyBypassState()` (Bypass relay + indicator LED), and `checkBypassButton()` (physical Bypass button, debounced).
- **`animation_logic.h`/`.cpp`** — the one-shot Bass/High ring animation that plays when Bypass toggles (`triggerBypassAnim()` + the fill/blink renderers). Same animation regardless of whether Bypass was toggled by its physical button, the menu via encoder, or the menu via IR remote.
- **`on_off_logic.h`/`.cpp`** — global power sequencing: `powerOnDevices()`/`powerOffDevices()`, `powerOnScreen()`/`powerOffScreen()`, and the `powerOff` flag.

## Architecture

### State model
A single global state machine drives everything: `currentMenuItem` indexes `menuItems[]` = `{"Bass", "High", "Volume", "VU Meter", "Bypass", "Dimmer", "Color", "Source"}` (8 items — all `% MENU_ITEM_COUNT` wraparound arithmetic must stay in sync with this count; `MENU_ITEM_COUNT` is defined in `main.h`). `inSettingsMode` selects whether the screen shows the menu carousel or the settings view for the current item. Branching on menu item is done by **string comparison** (`menuItems[currentMenuItem] == "Bass"`) rather than an enum.

### Two independent input paths feed the same state
- **Rotary encoder** (`encoder.cpp`): read via interrupt on both pins (`CHANGE`), using Oleg Mazurov's state-table algorithm so fast rotation can't skip steps; `checkEncoderButton()` is polled every `loop()` iteration for the push-button.
- **IR remote** (`remote_control.cpp`): interrupt-driven. `IR_ISR()` (attached on `IR_PIN`, `FALLING`) feeds a `NecDecoder`; `handleRemoteInput()` in `loop()` acts on the decoded command codes.

Both paths converge on the same redraw functions (`drawMenu()`, `drawToggleSwitch()`, `drawArrowIndicator()`) and the same motor/relay/animation side effects — when changing behavior for a menu item, **both input paths need the change applied in parallel** (the logic is duplicated between `handleRemoteInput()` in `remote_control.cpp` and the encoder-handling block in `main.cpp`'s `loop()`).

### Display
`U8g2lib`, hardware SPI, `U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI`, owned by `display_logic.cpp`. CS/DC/RESET are software-selectable (`DISPLAY_CS_PIN`/`DISPLAY_DC_PIN`/`DISPLAY_RESET_PIN` in `hardware_settings.h`, currently 10/9/12), but SCK/MOSI are fixed hardware-SPI pins on the Mega (52/51) and cannot be moved in code.

### Motors (Bass, High, Volume)
Two different driver interfaces, both in `motor_driver_logic.cpp`:
- `motorControl()` — single direction pin + single PWM pin, used for Motor1 (Bass) and Motor2 (High). Assumes an L298N-style board, but only `IN1`/`IN3` are wired — `IN2`/`IN4` are not defined/connected in code, so true reverse rotation is not correctly achieved yet (both direction pins would need to be driven, one inverted from the other).
- `motorControl2()` — dual direction pin + dual PWM pin, used for Motor3 (Volume). Matches a BTS7960/IBT-2-style driver (`RPWM`/`LPWM` + enables).

### Potentiometer position feedback (Bass/High/Volume)
`motor_position.cpp` reads a feedback potentiometer per knob (`BASS_POT_PIN`/`HIGH_POT_PIN`/`VOLUME_POT_PIN`, all defined in `hardware_settings.h`) so the real physical position can be shown on screen — separate from `settings[]`, which is the *commanded* target driving the motor. Volume's pot is a **logarithmic-taper** pot with a noisy wiper, which drove this implementation:

1. **Oversampling** — 64 consecutive `analogRead()` samples are averaged per call (`readPotPercent()`) to cut ADC/wiper noise, done fresh each time (no cross-call state), so there's no lag behind real movement.
2. **Calibration tables, not formulas** — `bassPotCalRaw[]`/`bassPotCalValue[]`, `highPotCalRaw[]`/`highPotCalValue[]`, and `volumePotCalRaw[]`/`volumePotCalPercent[]` (all in `hardware_settings.h`) hold manually-measured `(raw ADC, physical value)` pairs; `potRawToPercent()` linearly interpolates between them (clamping outside the measured range, with a small "snap" tolerance at 0dB/min/max so exact endpoints are reachable — see `*_POT_*_SNAP_RAW` constants). An earlier attempt at Volume used a closed-form log10 correction assuming a textbook audio-taper law; it didn't match this specific pot (a measured physical 50% read back as 42%), so it was replaced with real calibration points.
3. **Live refresh** — `main.cpp`'s `loop()` calls `drawArrowIndicator()` every 200ms while `inSettingsMode` and the current item is Bass/High/Volume, independent of encoder/IR events, so the position keeps updating as the motor (or a hand) moves the knob. The same block also drives the NeoPixel ring for that item, and updates all three rings every 200ms regardless of which menu item is selected (so the rings stay live even while browsing the menu).

If recalibrating: read the `<Bass/High/Volume> pot raw: X -> Y<dB/%> (shown: Z<dB/%>)` line from Serial at known physical stops and update the relevant pair of arrays in `hardware_settings.h`; keep the raw arrays strictly ascending.

### NeoPixel rings (Bass, High, Volume)
`neopixel.cpp` drives three rings (12 LEDs each, 2 unused at the bottom — wiring-dependent index order lives in `hardware_settings.h`). Volume fills like a single-ended level meter (`updateVolumeRing()`, 0-100%). Bass/High are two-sided around a 0dB center (`renderDbRing()`, -10..+10dB): the center pair is green and always lit, breathing through a brightness ramp (`zeroBlinkBrightness()`) whenever the value re-enters the zero zone; the rest of the ring lights in the selected `Color` palette entry toward whichever side the value has moved. Overall ring brightness is the `Dimmer` menu item (`applyRingDimmer()`); the active (non-green) color is the `Color` menu item (`applyRingColorScheme()`, palette in `hardware_settings.h`).

### Bypass
`settings[4]` is the Bypass on/off flag. It can be toggled three ways — the dedicated physical button (`checkBypassButton()` in `relay.cpp`), the "Bypass" menu item via encoder, or via IR remote — and all three paths call the same two functions: `applyBypassState()` (relay + indicator LED, `relay.cpp`) and `triggerBypassAnim()` (starts the Bass/High ring animation, `animation_logic.cpp`). While that animation is playing, `main.cpp`'s `loop()` skips the normal dB-ring redraw for Bass/High so the animation isn't immediately overwritten. `drawMenu()` shows a small "bypass" label top-left while `settings[4] == 1` (mirrors the "mute" label top-right for `isMuted`); both labels' font/position are in `hardware_settings.h`.

### Relays
All relays are **active-HIGH** (`digitalWrite(..., HIGH)` = energized/on) — this was flipped from the original active-LOW wiring when the relay modules were swapped. Standby/VU-Meter/LED are tied to the global power sequence (`powerOnDevices()`/`powerOffDevices()` in `on_off_logic.cpp`), and VU-Meter/LED/Bypass are also individually toggleable from their menu items. Source is mutually-exclusive across its three relays (`applySourceSelection()`). Mute is switched directly by a dedicated IR remote button (`IR_MUTE`), independent of menu navigation — note `isMuted` is not resynced with the relay state during `powerOnDevices()`, so it can drift out of sync across a power-off/power-on cycle.

`powerOffDevices()`/`powerOnDevices()` explicitly drive every relay and LED pin `OUTPUT` + `LOW`/`HIGH` rather than releasing them to `INPUT` — this was a deliberate fix so power-off is deterministic (floating pins can't be relied on to read as "off" — that depended on the old relay modules' own pull resistors, which no longer apply after the active-HIGH swap).

### Pin map reference
See `hardware_settings.h` for the full pin assignment (encoder, motors, relays, LEDs, IR receiver, NeoPixel rings, potentiometer feedback). Comments in Russian throughout the codebase describe each pin's purpose.
