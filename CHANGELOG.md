# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.2.0] - 2026-10-20

Release preparation: documentation rewrite, repository files, one breaking cleanup.

### Removed (breaking)
- `src/SmartKeyV2.h`, the legacy include that only forwarded to `SmartKey.h`. Replace
  `#include <SmartKeyV2.h>` with `#include <SmartKey.h>` (or include the individual headers).

### Added
- `BinaryCodeScanner`: reads a key code from 1-8 input lines (pull-up, active-low or direct, optional "data valid" line).
- Examples `Advanced/IdleSleep`, `Advanced/ParallelCodeInput` (4-line key code from external hardware), `Advanced/Keypad4x4` (PIN door lock on a 4x4 matrix keypad with combo and brute-force protection) and `Professional/IRRemoteTV` (25-button TV remote through Arduino-IRremote 4.x with a built-in LEARN_MODE).
- `IdleMonitor` (+ `IdleState`, `IdleCallback`, `WakeCallback`): two-stage inactivity detection with one callback per idle flag; a key pressed while idle only wakes the system (its release / click / hold events are reported as "ignore"). Example `Advanced/IdleSleep`.
- `SMARTKEY_VERSION_MAJOR / _MINOR / _PATCH` macros.
- Repository files: `library.json` (PlatformIO registry), GitHub Actions workflow (`.github/workflows/ci.yml`), `.gitignore`, `.gitattributes`, `CONTRIBUTING.md`, `scripts/check_version.py`.
- 15 new unit tests (48 in total).

### Changed
- `ReadMe.md` rewritten: current Quick Start, configuration and API tables, scanner guide, troubleshooting and a migration guide.
  Removed placeholder contact details (e-mail, Discord, wiki) and unmeasured memory figures.
- `LICENSE`: the copyright line is plain text (no Markdown link).
- `keywords.txt` regenerated.

## [2.1.0] - 2026-10-19

Bug-fix and robustness release. Public names are unchanged; the behaviour changes are listed under "Changed".

### Added
- `MultiManager<N>`: tracks several simultaneous keys (own debounce / click chain / hold per key). `ComboDetector` now works with real hardware.
- `KeyScanner::scanMulti()` (GPIO and Matrix scanners implement it for all pressed keys).
- `SmartKey.h` umbrella header (`SmartKeyV2.h` stays as a thin compatibility include).
- `Config::validate()`; `setConfig()` now returns an `ErrorCode` and rejects inconsistent timings. `ErrorCode::BufferFull` is reported by `MultiManager`.
- `Callbacks::onEventEx` + `userData` and `setEventCallbackEx()` (bind callbacks to an object without static trampolines).
- `AnalogScanner::classify()`, `Macro::isFull()`, `ValueController::setMultiClickJump()`.
- Unit tests for Manager, MultiManager, ValueController, Sequence, Toggle, Macro, RateLimiter and scanners (33 tests).

### Fixed
- Click was reported together with DoubleClick; Click is now deferred until the double-click window expires (when double click is enabled).
- TripleClick could never fire (`tripleClickTime` was unused, click counter reset at 2).
- `onStateChange` never reported `Pressed`.
- Hold events could arrive out of order (`HoldVeryLong` before `HoldLong`) after a slow loop iteration.
- Uninitialised `lastDebounceTime` / `pressStartTime`.
- The library called `millis()` internally while `update()` also took a time argument (wrong durations in tests, no native build). The library no longer calls `millis()`; convenience overloads without a time argument exist only under `ARDUINO`.
- User callbacks ran inside `portENTER_CRITICAL`. Thread safety is now an optional recursive mutex (ESP32 only) and callbacks run with it held, never with interrupts disabled. `freertos/*` is no longer included on ESP8266.
- `SequenceDetector` ignored `maxInterval` (1000 ms was hard-coded); each sequence now keeps its own progress.
- `ValueController`: acceleration stayed at x5 for every later hold; HoldLong/VeryLong multipliers compounded with a duration multiplier; double click added a second jump on top of two presses (now opt-in); percentage / rollover arithmetic uses 64-bit intermediates.
- `AnalogScanner::addKeyCentered()` underflowed `uint16_t` near 0 / 65535.
- `SerialScanner` produced one-sample pulses that the debouncer always filtered out; a received key is now held for 120 ms.
- `Macro` delay was stored in `uint16_t` (overflow after ~65 s) and used `millis()`.
- `RateLimiter` silently never limited when `maxClicks > 8` (now clamped to 8).
- `KeyValueControllerManager` was a non-template alias, so the documented `KeyValueControllerManager<4>` did not compile.
- Examples: Advanced and Professional ones lacked `using namespace SmartKey;`, and their automated demos called `update()` once per key change, so debouncing swallowed every event. All demos now keep keys pressed in real time and the "sample output" blocks are real program output. ComboKeys uses `MultiManager`, FullSystem no longer relies on C++20 designated initializers, and the AV demo gained working menu navigation and extra-jump keys.
- All examples are now written in English and use the new names (`Manager`, `Event`, `ValueController`, `SmartKey.h`); the legacy names still work.
- Scanners no longer allocate with `new[]` (fixed buffers, `SMARTKEY_MAX_GPIO_PINS`, `SMARTKEY_MAX_ANALOG_KEYS`, matrix up to 8x8).

### Changed
- With `enableDoubleClick = true` (default) a single `Click` arrives `doubleClickTime` (300 ms) after release. Set `enableDoubleClick = false` for immediate clicks.
- `Config::maxClickCount` default is 3 (it was 2, which capped triple click). `enableTripleClick` requires `enableDoubleClick`.
- `Macro::startRecording()` / `play()` take the current time (no-argument forms remain under `ARDUINO`).
- `ValueController` hold repeat is driven by `Event::HoldRepeat`, so `Config::enableHoldRepeat` must be true.
- `SMARTKEY_THREAD_SAFE` supports ESP32 only (other platforms get a clear `#error`).
- Line endings normalised to LF.

## [2.0.0] - 2024-12-XX

### Added
- Namespace `SmartKey` for better code organization and preventing name conflicts
- Hardware abstraction layer with multiple scanner types:
  - `GPIOScanner` for direct pin scanning
  - `MatrixScanner` for matrix keyboards
  - `AnalogScanner` for resistor ladder buttons
  - `SerialScanner` for testing
  - Base class `KeyScanner` for custom implementations
- Thread safety support for RTOS platforms (ESP32, STM32)
- Error handling system with `ErrorCode` enum
- Comprehensive unit tests using Unity framework
- CI/CD pipelines with GitHub Actions
- PlatformIO configuration for multi-platform development
- English documentation and comments throughout codebase
- Library metadata files (`library.properties`, `keywords.txt`)
- `.gitignore` for clean repository

### Changed
- Event and State enums converted to `enum class` for type safety
- All comments and documentation translated to English
- Improved memory efficiency with bitfields
- Enhanced callback system with specialized callbacks
- Reorganized project structure for PlatformIO compatibility
- Updated examples with comprehensive English comments

### Fixed
- Debouncing edge cases
- Memory leaks in dynamic allocation (now stack-only)
- Thread safety issues on multi-core platforms

### Deprecated
- Legacy typedef names (kept for backward compatibility)
- Old naming conventions (aliases provided)

## [1.0.0] - 2024-11-XX

### Added
- Initial release
- Core `SmartKeyManager` class
- Basic event detection (Press, Release, Click)
- Double-click support
- Hold detection (Mid, Long, Very Long)
- Debouncing mechanism
- `KeyValueController` for numeric value control
- Helper modules:
  - `KeyComboDetector`
  - `KeySequenceDetector`
  - `KeyToggle`
  - `KeyMacro`
  - `KeyRateLimiter`
- Persian documentation
- Basic examples

[2.2.0]: https://github.com/Nourbakhsh-Rad/SmartKey/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/Nourbakhsh-Rad/SmartKey/compare/v2.0.0...v2.1.0
[2.0.0]: https://github.com/Nourbakhsh-Rad/SmartKey/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/Nourbakhsh-Rad/SmartKey/releases/tag/v1.0.0

