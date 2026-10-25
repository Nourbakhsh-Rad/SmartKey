# SmartKey

![Version](https://img.shields.io/badge/version-2.2.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![C++](https://img.shields.io/badge/C%2B%2B-11-red.svg)
![Header only](https://img.shields.io/badge/header--only-no%20dynamic%20allocation-lightgrey.svg)
[![CI](https://github.com/Nourbakhsh-Rad/SmartKey/actions/workflows/ci.yml/badge.svg)](https://github.com/Nourbakhsh-Rad/SmartKey/actions/workflows/ci.yml)

**Key and input event management for embedded systems.** SmartKey turns raw key signals
(buttons, matrix keypads, resistor ladders, parallel codes, IR remotes ...) into clean
events: debounced press / release, click, double and triple click, three hold levels with
auto-repeat. On top of that it offers combos, sequences, toggles, macros, a rate limiter
and value controllers (volume, brightness ...).

[Features](#features) · [Installation](#installation) · [Quick start](#quick-start) ·
[How it works](#how-it-works) · [Scanners](#scanners-hardware-abstraction) · [Helpers](#helpers) ·
[API](#api-reference) · [Examples](#examples) · [Troubleshooting](#troubleshooting) ·
[Migration](#migrating-from-20x--21x)

---

## Features

- **Events:** Pressed, Released, Click, DoubleClick, TripleClick, HoldMid, HoldLong,
  HoldVeryLong and HoldRepeat (see [Events](#events)).
- **Debouncing** with a configurable stable time.
- **Single key (`Manager`) or several keys at once (`MultiManager<N>`)**, each key with its own
  debounce, click chain and hold timing.
- **Scanners:** GPIO, matrix (up to 8x8), analog resistor ladder, binary code on 1-8 lines,
  serial (testing) and custom. Easy to write your own (`KeyScanner`).
- **Helpers:** `ValueController`, `ComboDetector`, `SequenceDetector`, `Toggle`, `Macro`, `RateLimiter`,
  `IdleMonitor` (two-stage inactivity: backlight off, then deep sleep).
- **Deterministic and testable:** the library never calls `millis()`. You pass the time to
  `update()`, so everything is unit-tested on a PC.
- **No dynamic allocation**, header-only, C++11, no dependencies. Any core with the Arduino API
  (`architectures=*`); the unit tests run on the PC (native).
- **Optional thread safety** for ESP32 (FreeRTOS recursive mutex); callbacks never run with
  interrupts disabled.

> Hardware note: the logic is covered by unit tests and by PC simulations of the examples.
> Please report anything that behaves differently on your board.

---

## Installation

**Arduino IDE (manual).** Download the release ZIP, then *Sketch → Include Library → Add .ZIP Library*,
or copy the `SmartKey` folder into `Arduino/libraries/`.

**PlatformIO.** In `platformio.ini`:

```ini
lib_deps =
    https://github.com/Nourbakhsh-Rad/SmartKey.git
```

(or `Nourbakhsh-Rad/SmartKey` once it is published in the PlatformIO registry).

**Arduino Library Manager.** Available after the library is accepted into the Library Manager
index; search for "SmartKey".

Include everything with one header:

```cpp
#include <SmartKey.h>
```

or only what you need: `SmartKeyCore.h` (events, `Manager`, `MultiManager`),
`HardwareAbstraction.h` (scanners), `KeyValueController.h`, `KeyboardHelpers.h`.

---

## Quick start

A button on GPIO 4 (to GND, internal pull-up) that reports clicks and holds:

```cpp
#include <SmartKey.h>
using namespace SmartKey;

GPIOScanner scanner;     // reads the hardware
Manager keys;            // turns the raw signal into events

void onKeyEvent(uint8_t keyCode, Event event) {
    Serial.print("key ");
    Serial.print(keyCode);
    Serial.print(": ");
    Serial.println(eventToString(event));
}

void setup() {
    Serial.begin(115200);

    scanner.addPin(4, 1);                  // GPIO 4 -> key code 1 (active low, pull-up)
    scanner.begin();

    keys.setEventCallback(onKeyEvent);
}

void loop() {
    keys.update(scanner.scan(), millis()); // key code or NO_KEY, plus the current time
}
```

Useful settings (all optional):

```cpp
#include <SmartKey.h>
using namespace SmartKey;

Manager keys;

void setup() {
    Config cfg;
    cfg.debounceTime = 30;           // ms
    cfg.enableDoubleClick = false;   // report Click immediately (see below)
    cfg.enableHoldRepeat = true;     // HoldRepeat events while a key is held
    cfg.holdRepeatInterval = 100;    // ms

    if (keys.setConfig(cfg) != ErrorCode::OK) {
        // inconsistent timings (for example midPressTime >= longPressTime)
    }
}

void loop() {}
```

> **Click latency:** while double click is enabled (the default) a single click is reported
> `doubleClickTime` (300 ms) after the release, so it can never be followed by a DoubleClick for
> the same presses. If you only use plain clicks, set `enableDoubleClick = false`.

Several keys at once (combos): use `MultiManager` and `scanMulti()`:

```cpp
#include <SmartKey.h>
using namespace SmartKey;

const uint8_t ROWS[2] = {9, 8};
const uint8_t COLS[2] = {7, 6};

MatrixScanner scanner(ROWS, 2, COLS, 2);
MultiManager<4> keys;                  // up to 4 keys at the same time
ComboDetector combos;

void onCombo(uint8_t* keyCodes, uint8_t count) {
    Serial.println("Key 0 + key 3 pressed together");
}

void onKeyEvent(uint8_t keyCode, Event event) {
    combos.handleKeyEvent(keyCode, event);     // the combo needs Pressed / Released
}

void setup() {
    Serial.begin(115200);
    scanner.begin();
    keys.setEventCallback(onKeyEvent);
    combos.registerCombo2(0, 3, onCombo);
}

void loop() {
    uint8_t pressed[4];
    uint8_t n = scanner.scanMulti(pressed, 4);  // every key that is down right now
    keys.update(pressed, n, millis());
}
```

A volume control with auto-repeat:

```cpp
#include <SmartKey.h>
using namespace SmartKey;

const uint8_t KEY_UP = 1;
const uint8_t KEY_DOWN = 2;

GPIOScanner scanner;
Manager keys;
ValueController volume(KEY_UP, KEY_DOWN, 20, 0, 100, 2);   // start 20, 0..100, step 2
ValueControllerManager<1> controllers;
unsigned long now = 0;

void onKeyEvent(uint8_t keyCode, Event event) {
    controllers.handleKeyEvent(keyCode, event, now);
    Serial.println(volume.getValue());
}

void setup() {
    Serial.begin(115200);
    scanner.addPin(4, KEY_UP);
    scanner.addPin(5, KEY_DOWN);
    scanner.begin();

    Config cfg;
    cfg.enableHoldRepeat = true;           // the controller repeats on HoldRepeat events
    keys.setConfig(cfg);
    keys.setEventCallback(onKeyEvent);

    volume.setHoldSettings(100, 1);
    controllers.addController(&volume);
}

void loop() {
    now = millis();
    keys.update(scanner.scan(), now);
}
```

More complete sketches are in [`examples/`](#examples).

---

## How it works

```
 hardware ──► KeyScanner ──► Manager / MultiManager ──► callbacks ──► helpers / your code
 (GPIO, matrix,  scan() /      debounce, click chain,     (key, Event)   ValueController,
  analog, code,  scanMulti()   hold events                               ComboDetector, Toggle ...
  IR, custom)
```

1. A **scanner** returns the key code that is pressed right now (`scan()`), or all of them
   (`scanMulti()`). `NO_KEY` (`0xFF`) means "nothing pressed" and is therefore not a valid key code.
2. A **manager** runs the debouncer and a state machine for every key and calls your callbacks.
3. **Helpers** consume the `(keyCode, event)` pairs.

The library has no clock of its own: call `update()` often (every loop iteration) and pass
the current time. Timing uses unsigned subtraction, so the 32-bit `millis()` overflow is harmless.

### Events

| Event | When it is reported | Typical use |
|-------|---------------------|-------------|
| `Pressed` | Key is stable-down (after the debounce time) | Start an action, digits |
| `Released` | Key is stable-up | End an action |
| `Click` | Press + release shorter than `midPressTime`, no further click in the window | Main action |
| `DoubleClick` | Two clicks within `doubleClickTime` | Secondary action |
| `TripleClick` | Three clicks (needs `enableTripleClick`) | Special action |
| `HoldMid` | Held for `midPressTime` (500 ms) | Show a menu |
| `HoldLong` | Held for `longPressTime` (1500 ms) | Reset / delete |
| `HoldVeryLong` | Held for `veryLongPressTime` (3000 ms) | Factory reset |
| `HoldRepeat` | Every `holdRepeatInterval` after `HoldMid` (needs `enableHoldRepeat`) | Auto-repeat |

Rules worth knowing:

- A key that triggered a hold event never produces a click when released.
- `Pressed` and `Released` are always reported; `Click` events come on top of them.
- The click chain: with `enableDoubleClick` a one-click chain waits `doubleClickTime` for a second
  press; with `enableTripleClick` a two-click chain waits `tripleClickTime` for a third. A chain is
  reported at once when it reaches the longest enabled length. `Config::effectiveMaxClicks()` gives that length.
- If a hold starts while a click chain is pending, the pending click is reported first.
- If the scanner switches to another key, the previous key is released first.
- If your loop is slow, hold events still arrive in order (Mid → Long → VeryLong).

```
time ─────────────────────────────────────────────────────────────►
key   ┌───┐        ┌───┐                ┌─────────────────────────────
      │   │        │   │                │
──────┘   └────────┘   └────────────────┘
      P   R        P   R                P        HoldMid   HoldLong ...
                       └─ DoubleClick (reported at the 2nd release)
```

### Configuration (`Config`)

| Field | Default | Meaning |
|-------|---------|---------|
| `debounceTime` | 50 ms | A level must be stable this long. `0` = no debounce |
| `midPressTime` | 500 ms | `HoldMid` threshold |
| `longPressTime` | 1500 ms | `HoldLong` threshold |
| `veryLongPressTime` | 3000 ms | `HoldVeryLong` threshold |
| `doubleClickTime` | 300 ms | Max gap before the 2nd press |
| `tripleClickTime` | 400 ms | Max gap before the 3rd press |
| `holdRepeatInterval` | 100 ms | `HoldRepeat` period |
| `enableDoubleClick` | `true` | Report double clicks (delays a single `Click`) |
| `enableTripleClick` | `false` | Report triple clicks (requires double click) |
| `enableHoldRepeat` | `false` | Report `HoldRepeat` |
| `maxClickCount` | 3 | Upper limit (1..3) of the click chain |

`setConfig()` validates the values (`mid < long < veryLong`, non-zero windows for enabled features, ...)
and returns `ErrorCode::InvalidParameter` without applying them if they are inconsistent.

### Callbacks

Use one callback for everything, or one per kind:

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onPress(uint8_t key) {}
void onRelease(uint8_t key, unsigned long pressDuration) {}
void onClick(uint8_t key, uint8_t clickCount) {}                  // 1, 2 or 3
void onHold(uint8_t key, Event holdEvent) {}
void onState(uint8_t key, State state, unsigned long duration) {}
void onAny(void* user, uint8_t key, Event event) {}                 // with a context pointer

Manager keys;

void setup() {
    Callbacks cb;
    cb.onPress = onPress;
    cb.onRelease = onRelease;
    cb.onClick = onClick;
    cb.onHold = onHold;
    cb.onStateChange = onState;
    keys.setCallbacks(cb);

    keys.setEventCallbackEx(onAny, nullptr);   // pass `this` to call into an object
}

void loop() {}
```

Callbacks run inside `update()`. Keep them short (set flags, change values) and do not block.

---

## Scanners (hardware abstraction)

All scanners implement `KeyScanner`: `begin()`, `scan()` (first pressed key or `NO_KEY`),
`scanMulti(out, max)` (all pressed keys) and `getError()`.

| Scanner | Use it for | Notes |
|---------|-----------|-------|
| `GPIOScanner` | One pin per key | `addPin(pin, keyCode, activeLow = true)`, up to `SMARTKEY_MAX_GPIO_PINS` (16) |
| `MatrixScanner` | Keypads up to 8x8 | Key code = `row * colCount + col`. Add a diode per key for reliable multi-key |
| `AnalogScanner` | Resistor ladder on one ADC pin | `addKey(min, max, code)` / `addKeyCentered(center, code)`, `classify(adc)` |
| `BinaryCodeScanner` | A device that outputs the key *code* on 1-8 lines | Pull-up / active-low or direct, optional data-valid line |
| `SerialScanner` | Testing from the Serial Monitor | Typed character = key code, held for 120 ms |
| `CustomScanner` | Anything else with a function pointer | Or derive from `KeyScanner` |

`GPIOScanner` and `MatrixScanner` return every pressed key from `scanMulti()`;
the others report at most one key.

```cpp
#include <SmartKey.h>
using namespace SmartKey;

const uint8_t CODE_PINS[4] = {16, 17, 18, 19};    // bit 0 (LSB) first, internal pull-ups
BinaryCodeScanner codeScanner(CODE_PINS, 4);        // active-low, code 0 = "no key"

AnalogScanner ladder(34);                           // ADC pin

void setup() {
    codeScanner.begin();

    ladder.addKeyCentered(0, 1);                    // each key = center +/- tolerance (50),
    ladder.addKeyCentered(1200, 2);                 // clamped to the 0..65535 range
    ladder.addKeyCentered(2400, 3);
    ladder.begin();
}

void loop() {}
```

Writing your own scanner (for example an I2C expander) takes a few lines:

```cpp
#include <SmartKey.h>
using namespace SmartKey;

class MyScanner : public KeyScanner {
public:
    bool begin() override { return true; }
    uint8_t scan() override {
        // read your hardware here; return a key code (never 0xFF) or NO_KEY
        return NO_KEY;
    }
};

MyScanner scanner;
Manager keys;

void setup() { scanner.begin(); }
void loop() { keys.update(scanner.scan(), millis()); }
```

**Devices that only send pulses (IR remotes, RF).** They send frames while the button is held and
nothing on release. Wrap them in a scanner that keeps the key "pressed" until no frame arrived for a
short timeout; see [`IRRemoteTV`](examples/Professional/IRRemoteTV/IRRemoteTV.ino) and `SerialScanner`.

---

## Helpers

Helpers take `(keyCode, event)` from your callback. Time-dependent ones take the time too.

### ValueController

Changes a number with an "increase" and a "decrease" key (volume, brightness, temperature, channel ...).

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onChange(int32_t oldValue, int32_t newValue) {
    Serial.println(newValue);
}

//                        inc dec start min max step rollover callback
ValueController brightness(1, 2, 50, 0, 100, 10, true, onChange);
ValueControllerManager<4> controllers;      // forwards events to several controllers

void setup() {
    brightness.setHoldSettings(100, 2);     // min ms between hold steps, base acceleration
    brightness.setMultiClickJump(2);        // optional: a double click adds 2 extra steps
    controllers.addController(&brightness);
}

void onKeyEvent(uint8_t key, Event event) {
    controllers.handleKeyEvent(key, event, millis());
}

void loop() {}
```

- `Pressed`: one step. `HoldRepeat`: steps while held (**needs `Config::enableHoldRepeat`**).
- Acceleration: the base value, x3 after `HoldLong`, x5 after `HoldVeryLong`; reset on release.
- `setValue`, `changeValue`, `setPercentage`, `setRange`, `setStep`, `setRollover`, `isAtMin`, `isAtMax` ...

### ComboDetector

Keys held at the same time (needs `MultiManager`).

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onCopy(uint8_t* keys, uint8_t count) { Serial.println("copy"); }

ComboDetector combos;

void setup() {
    combos.registerCombo2(0x11, 0x43, onCopy);      // up to 4 keys per combo, 16 combos
}

void onKeyEvent(uint8_t key, Event event) {
    combos.handleKeyEvent(key, event);
}

void loop() {}
```

A combo fires when exactly its keys are down. The individual keys still produce their own events
(for example a `Click` after release); ignore those yourself if needed (see `Keypad4x4`).

### SequenceDetector

Keys pressed one after another, each within `maxInterval` ms of the previous one
(`0` = no timeout). Every sequence keeps its own progress.

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onSecret(uint8_t id) { Serial.println("unlocked"); }

SequenceDetector sequences;

void setup() {
    const uint8_t code[] = {1, 2, 3, 4};
    sequences.registerSequence(1, code, 4, 1000, onSecret);   // id, keys, length, max gap, callback
}

void onKeyEvent(uint8_t key, Event event) {
    sequences.handleKeyEvent(key, event, millis());
}

void loop() {}
```

### Toggle

An ON/OFF state flipped by every `Click`. If double click is enabled in the `Config`, two quick
presses arrive as one `DoubleClick`, which `Toggle` ignores; use `enableDoubleClick = false` for toggle keys.

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onMute(uint8_t key, bool state) { Serial.println(state ? "muted" : "sound"); }

Toggle toggles;

void setup() {
    toggles.registerToggle(5, false, onMute);       // key, initial state, callback
}

void onKeyEvent(uint8_t key, Event event) {
    toggles.handleKeyEvent(key, event);
}

void loop() {}
```

### Macro

Records the `Pressed` events of up to 32 keys with their timing and plays them back.

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void onStep(uint8_t key) { Serial.println(key); }

Macro macro;

void startRecording() { macro.startRecording(millis()); }
void stopRecording() { macro.stopRecording(); }
void playBack() { macro.play(onStep, millis()); }

void onKeyEvent(uint8_t key, Event event) {
    macro.handleKeyEvent(key, event, millis());     // records while recording
}

void setup() {}
void loop() { macro.update(millis()); }             // drives the playback
```

### RateLimiter

At most N clicks per time window (up to 8), for example to protect against brute-force PIN entry.

```cpp
#include <SmartKey.h>
using namespace SmartKey;

RateLimiter attempts(3, 30000);                     // 3 per 30 s

bool tryPin(bool correct) {
    if (!attempts.checkClick(millis())) return false;   // blocked
    if (correct) attempts.reset();
    return true;
}

void setup() {}
void loop() {}
```

### IdleMonitor

Two-stage inactivity detection. After `idle1` ms without any key the first idle flag is raised
(for example the backlight is switched off); if the inactivity goes on until `idle2` ms the second
flag is raised (for example deep sleep). Each flag has its own callback.
**A key pressed while idle only wakes the system up**: it has no other action, and its release,
click and hold events are ignored too (`handleKeyEvent()` returns `false` for them).

```cpp
#include <SmartKey.h>
using namespace SmartKey;

void backlightOff() { Serial.println("idle 1: backlight off"); }
void deepSleep()    { Serial.println("idle 2: deep sleep"); }
void onWake(IdleState from) { Serial.println("wake-up: backlight on"); }

IdleMonitor idle(10000, 60000);          // idle 1 after 10 s, idle 2 after 60 s (0 = stage off)
GPIOScanner scanner;
Manager keys;
unsigned long now = 0;

void onKeyEvent(uint8_t key, Event event) {
    if (!idle.handleKeyEvent(key, event, now)) return;   // belongs to the wake-up key: ignore
    // ... normal key handling ...
}

void setup() {
    scanner.addPin(4, 1);
    scanner.begin();
    keys.setEventCallback(onKeyEvent);

    idle.setIdle1Callback(backlightOff);
    idle.setIdle2Callback(deepSleep);
    idle.setWakeCallback(onWake);
    idle.reset(millis());                // start counting
}

void loop() {
    now = millis();
    keys.update(scanner.scan(), now);
    idle.update(now);                    // raises the flags and calls the callbacks
}
```

- Both timeouts are measured from the **last activity**; `idle2` must be larger than `idle1`
  (`setTimeouts()` returns `InvalidParameter` otherwise). A key that is held down counts as activity.
- `isIdle1()` stays `true` in stage 2; `getState()` returns `IdleState::Active / Idle1 / Idle2`.
- Activity that is not a key (touch, sensor, serial command): `idle.notifyActivity(now)` wakes the
  system and restarts the timers without swallowing anything.
- `setSwallowWakeKey(false)` lets the wake-up key work as a normal key as well.
- After a deep-sleep wake-up the program restarts, so the key that woke the chip is still down in `setup()`:
  wait for its release (see [`IdleSleep`](examples/Advanced/IdleSleep/IdleSleep.ino)).
- With `Config::enableDoubleClick` the key's `Click` arrives up to `doubleClickTime` after its release;
  the monitor keeps ignoring the wake key until that `Click` has been reported.

---

## API reference

Everything is in `namespace SmartKey`. `now` is a time in milliseconds (`unsigned long`).

### Manager / MultiManager

| Member | Description |
|--------|-------------|
| `Manager(EventCallback = nullptr, Config = Config())`, `Manager(const Callbacks&, Config = Config())` | One key at a time |
| `MultiManager<N>(...)` | Same constructors, up to `N` (1..64) simultaneous keys |
| `update(rawKeyCode, now)` | `Manager`: the key pressed now or `NO_KEY` |
| `update(keys, count, now)` | `MultiManager`: all pressed keys |
| `update(...)` without `now` | Only when `ARDUINO` is defined; uses `millis()` |
| `reset()` | Clear all state and the last error |
| `setConfig(cfg)` → `ErrorCode`, `getConfig()` | Validated configuration |
| `setCallbacks`, `setEventCallback`, `setEventCallbackEx(cb, user)`, `setStateCallback`, `setPressCallback`, `setReleaseCallback`, `setClickCallback`, `setHoldCallback` | Callbacks |
| `getLastError()` | `OK`, `InvalidParameter`, `BufferFull` (MultiManager out of slots) ... |
| `Manager::isAnyKeyActive()`, `getCurrentKey()`, `getCurrentState()`, `getCurrentPressDuration(now)`, `isCurrentKeyHeld()`, `getCurrentClickCount()`, `forceRelease(now)` | Queries |
| `MultiManager::getActiveKeyCount()`, `isKeyPressed(code)`, `getKeyState(code)` | Queries |

### Scanners

| Member | Description |
|--------|-------------|
| `begin()`, `scan()`, `scanMulti(out, max)`, `getError()` | `KeyScanner` interface |
| `GPIOScanner::addPin(pin, keyCode, activeLow = true)`, `getPinCount()` | |
| `MatrixScanner(rows, rowCount, cols, colCount)` | Max 8x8 |
| `AnalogScanner(pin, maxKeys = 8, tolerance = 50)`, `addKey`, `addKeyCentered`, `classify(value)` | |
| `BinaryCodeScanner(pins, bitCount, activeLow = true, noKeyCode = 0, usePullup = true)`, `setValidPin(pin, activeLow = true)`, `decode(rawLevels, dataValid = true)` | `decode` is hardware-free (testable) |
| `SerialScanner(holdMs = 120)`, `CustomScanner(scanFn, initFn = nullptr)` | |

### Helpers

| Class | Main members |
|-------|--------------|
| `ValueController` | `handleKeyEvent(key, event, now)`, `getValue`, `setValue`, `changeValue`, `getPercentage`, `setPercentage`, `setRange`, `setStep`, `setRollover`, `setKeys`, `setCallback`, `setHoldSettings(intervalMs, accel)`, `setMultiClickJump(double, triple)`, `getMin`, `getMax`, `getStep`, `isAtMin`, `isAtMax` |
| `ValueControllerManager<N>` | `addController`, `handleKeyEvent(key, event, now)`, `getCount`, `getController` |
| `ComboDetector` | `registerCombo(keys, count, cb)`, `registerCombo2/3/4`, `handleKeyEvent(key, event)`, `enableCombo`, `clear`, `resetActiveKeys`, `getActiveKeyCount`, `getComboCount` |
| `SequenceDetector` | `registerSequence(id, keys, length, maxInterval, cb)`, `handleKeyEvent(key, event, now)`, `reset`, `enableSequence`, `getSequenceCount` |
| `Toggle` | `registerToggle(key, initial, cb)`, `handleKeyEvent(key, event)`, `getState`, `setState`, `getToggleCount` |
| `Macro` | `startRecording(now)`, `stopRecording`, `isRecording`, `isFull`, `handleKeyEvent(key, event, now)`, `play(cb, now)`, `update(now)`, `isPlaying`, `stop`, `clear`, `getStepCount` |
| `RateLimiter(maxClicks = 5, windowMs = 1000)` | `checkClick(now)`, `reset`, `getCurrentCount`, `setLimit` |
| `IdleMonitor(idle1Ms = 0, idle2Ms = 0)` | `setTimeouts`, `setIdle1Callback`, `setIdle2Callback`, `setWakeCallback(IdleState from)`, `handleKeyEvent(key, event, now)` → `bool` (false = ignore), `update(now)`, `notifyActivity(now)`, `reset(now)`, `setSwallowWakeKey`, `setSwallowTimeout`, `getState`, `isIdle1`, `isIdle2`, `isSwallowing`, `getIdleTime(now)` |

Every `handleKeyEvent` also accepts the event as `uint8_t` for old code. `eventToString`,
`stateToString` and `errorToString` return names for logging.

### Macros (define before including the library)

| Macro | Default | Meaning |
|-------|---------|---------|
| `SMARTKEY_THREAD_SAFE` | `0` | `1` = recursive mutex around the managers (ESP32 only, `update()` from tasks, not ISRs) |
| `SMARTKEY_MAX_GPIO_PINS` | `16` | Capacity of `GPIOScanner` |
| `SMARTKEY_MAX_ANALOG_KEYS` | `8` | Capacity of `AnalogScanner` |
| `SMARTKEY_VERSION`, `SMARTKEY_VERSION_MAJOR/MINOR/PATCH` | | Read-only version |

Legacy names from 1.x/2.0 (`KeyEvent`, `KeyConfig`, `SmartKeyManager`, `kEventClick` ...) still work.

---

## Examples

Each example is a sketch in its own folder. The `Basic` ones need a button on pin 2 (to GND, internal pull-up). `VolumeControl`, `ComboKeys`, `FullSystem` and `AVControlSystem` run without any hardware: they simulate key presses in real time and print the result to the Serial Monitor, and `VolumeControl`, `ComboKeys` and `AVControlSystem` then accept typed characters as keys.

| Example | Shows |
|---------|-------|
| [`Basic/SimpleClick`](examples/Basic/SimpleClick/SimpleClick.ino) | Press, release and click of a GPIO button (LED toggle); `SerialScanner` alternative for testing |
| [`Basic/DoubleClick`](examples/Basic/DoubleClick/DoubleClick.ino) | Single click toggles the LED, double click changes the blink pattern |
| [`Basic/HoldDetection`](examples/Basic/HoldDetection/HoldDetection.ino) | Click toggles the LED, the three hold levels control a brightness fade, hold repeat |
| [`Advanced/VolumeControl`](examples/Advanced/VolumeControl/VolumeControl.ino) | Four `ValueController`s: clamp, rollover, hold acceleration |
| [`Advanced/ComboKeys`](examples/Advanced/ComboKeys/ComboKeys.ino) | Combos, sequences, toggles, macro recording, rate limiter (`MultiManager`) |
| [`Advanced/FullSystem`](examples/Advanced/FullSystem/FullSystem.ino) | Callback kinds, custom `Config`, queries, per-key behaviour |
| [`Advanced/ParallelCodeInput`](examples/Advanced/ParallelCodeInput/ParallelCodeInput.ino) | Key code from an external device on 4 pull-up lines (`BinaryCodeScanner`) — needs the 4 lines |
| [`Advanced/IdleSleep`](examples/Advanced/IdleSleep/IdleSleep.ino) | Two-stage idle: backlight off, then deep sleep; the wake-up key does nothing (`IdleMonitor`) — needs a button (or typed characters) |
| [`Advanced/Keypad4x4`](examples/Advanced/Keypad4x4/Keypad4x4.ino) | PIN door lock on a 4x4 matrix keypad: combo, hold, rate limiter — needs the keypad |
| [`Professional/AVControlSystem`](examples/Professional/AVControlSystem/AVControlSystem.ino) | A/V control system: presets, mute, menu, equalizer |
| [`Professional/IRRemoteTV`](examples/Professional/IRRemoteTV/IRRemoteTV.ino) | 25-button TV remote through Arduino-IRremote 4.x with a learn mode — needs the `IRremote` library and a receiver |

---

## Project structure

```
SmartKey/
├── src/
│   ├── SmartKey.h               # umbrella header (include this)
│   ├── SmartKeyCore.h           # events, Config, Manager, MultiManager
│   ├── HardwareAbstraction.h    # scanners
│   ├── KeyValueController.h     # ValueController
│   └── KeyboardHelpers.h        # combo, sequence, toggle, macro, rate limiter
├── examples/                    # Basic / Advanced / Professional
├── test/test_smartkey.cpp       # unit tests (Unity)
├── scripts/check_version.py     # release helper
├── .github/workflows/ci.yml     # CI
├── library.properties           # Arduino
├── library.json                 # PlatformIO
├── keywords.txt
├── platformio.ini
├── CHANGELOG.md
├── CONTRIBUTING.md
└── LICENSE
```

---

## Testing

The unit tests run on the PC with a simulated clock:

```bash
pip install platformio
pio test -e native
```

They cover the debouncer, click chain, hold events, `MultiManager`, all helpers (including `IdleMonitor`) and the
hardware-independent part of the scanners. The sketches in `examples/` are built by CI for ESP32 (and the basic one for Uno).

---

## Troubleshooting

| Problem | Cause / fix |
|---------|-------------|
| `Click` arrives about 300 ms late | Double click is enabled. Set `enableDoubleClick = false` if you do not need it |
| A click and a double click are both reported | You are on an old version (< 2.1). Update |
| Combo never fires | Use `MultiManager` + `scanner.scanMulti()`. `Manager` follows one key. On matrices add a diode per key |
| No `HoldRepeat` / `ValueController` does not auto-repeat | Set `Config::enableHoldRepeat = true` |
| `setConfig()` returns `InvalidParameter` | Check `mid < long < veryLong`, non-zero windows, triple click needs double click |
| No events when I call `update()` once per test step | Debouncing needs a stable level for `debounceTime`: call `update()` repeatedly while advancing the time |
| IR remote / serial keys never produce events | They are one-shot pulses. Keep the key "pressed" for a short timeout in the scanner (see `IRRemoteTV`, `SerialScanner`) |
| `Event`, `State` or `Config` is ambiguous with another library | Do not use `using namespace SmartKey;`; write `SmartKey::Event` or `namespace SK = SmartKey;` |
| Key code `0xFF` does not work | `NO_KEY` is `0xFF`; use other codes |
| The key that wakes the device also triggers its action | Route every key event through `IdleMonitor::handleKeyEvent()` and ignore the event when it returns `false` |
| The device never goes idle | A key stuck down (or `Manager::reset()` while a key was down) keeps the activity alive: call `idle.reset(now)` |
| Strange timing with delays in `loop()` | Do not block in `loop()` or callbacks. Hold events still come in order, but late |

---

## Migrating from 2.0.x / 2.1.x

| Change | What to do |
|--------|-----------|
| `SmartKeyV2.h` was **removed in 2.2.0** | Use `#include <SmartKey.h>` |
| `Click` is deferred while double click is enabled (2.1) | Set `enableDoubleClick = false` for immediate clicks |
| `Config::maxClickCount` default is 3 (was 2) (2.1) | Nothing, unless you relied on 2 |
| Library no longer calls `millis()` (2.1) | `Macro::startRecording(now)` / `play(cb, now)` take the time; the no-argument forms exist only under `ARDUINO` |
| `ValueController` repeats on `HoldRepeat` (2.1) | Enable `Config::enableHoldRepeat` |
| Double click no longer adds an extra jump to a `ValueController` (2.1) | Opt in with `setMultiClickJump(2)` |
| `setConfig()` returns an `ErrorCode` (2.1) | Optional: check it |
| `SMARTKEY_THREAD_SAFE` is ESP32 only and uses a mutex (2.1) | Callbacks may call the manager's query functions |
| Scanners use fixed buffers (2.1) | `GPIOScanner(maxPins)` is capped at `SMARTKEY_MAX_GPIO_PINS` (16) |

See [CHANGELOG.md](CHANGELOG.md) for every fix.

---

## Contributing

Issues and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md).
Bug reports are most useful with a minimal sketch, the `Config` you use and the board.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2026 Sh. Nourbakhsh Rad.

Questions and bugs: [GitHub Issues](https://github.com/Nourbakhsh-Rad/SmartKey/issues).
