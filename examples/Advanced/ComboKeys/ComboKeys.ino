/*
 * SmartKey - Keyboard helpers demo
 *
 * Shows every helper class built on top of key events:
 *   1. ComboDetector     - keys held at the same time (Ctrl+C, Alt+F4 ...)
 *   2. SequenceDetector  - keys pressed one after another (cheat codes)
 *   3. Toggle            - ON/OFF state flipped by every click
 *   4. Macro             - record key presses with timing and play them back
 *   5. RateLimiter       - limit how many clicks are accepted per second
 *
 * IMPORTANT: combos need several keys down at the same time, so this
 * example uses MultiManager (Manager follows one key only) and expects
 * the scanner to report all pressed keys (scanner.scanMulti()).
 *
 * No hardware needed: an automated demo feeds the manager with simulated
 * key presses in real time. Afterwards the sketch reads the Serial Monitor
 * (SerialScanner reports one key at a time, so combos cannot be typed;
 * sequences, toggles and the rate limiter can: try 1234, 9, S, space).
 */

#include <SmartKey.h>

using namespace SmartKey;

// Time used by all callbacks (updated in loop() and in the demo helpers)
static unsigned long g_now = 0;

// ===================================
// Demo 1: ComboDetector
// ===================================

ComboDetector comboDetector;

void onCtrlC(uint8_t* /*keys*/, uint8_t /*count*/) {
    Serial.println("📋 Ctrl+C detected → Copy");
}

void onCtrlV(uint8_t* /*keys*/, uint8_t /*count*/) {
    Serial.println("📋 Ctrl+V detected → Paste");
}

void onAltF4(uint8_t* /*keys*/, uint8_t /*count*/) {
    Serial.println("❌ Alt+F4 detected → Close Application");
}

void onCtrlAltDel(uint8_t* /*keys*/, uint8_t /*count*/) {
    Serial.println("🔒 Ctrl+Alt+Del detected → Task Manager");
}

void setupComboDetector() {
    Serial.println("\n=== ComboDetector Setup ===");

    // Ctrl+C (0x11 = Ctrl, 0x43 = C)
    comboDetector.registerCombo2(0x11, 0x43, onCtrlC);

    // Ctrl+V (0x11 = Ctrl, 0x56 = V)
    comboDetector.registerCombo2(0x11, 0x56, onCtrlV);

    // Alt+F4 (0x12 = Alt, 0x73 = F4)
    comboDetector.registerCombo2(0x12, 0x73, onAltF4);

    // Ctrl+Alt+Del
    comboDetector.registerCombo3(0x11, 0x12, 0x2E, onCtrlAltDel);

    Serial.println("Registered combos: Ctrl+C, Ctrl+V, Alt+F4, Ctrl+Alt+Del");
}

// ===================================
// Demo 2: SequenceDetector
// ===================================

SequenceDetector sequenceDetector;

void onKonamiCode(uint8_t /*id*/) {
    Serial.println("🎮 KONAMI CODE! → 30 Lives");
}

void onSecretMenu(uint8_t /*id*/) {
    Serial.println("🔓 Secret Menu Unlocked!");
}

void setupSequenceDetector() {
    Serial.println("\n=== SequenceDetector Setup ===");

    // Konami code: up up down down left right left right B A
    // The 4th argument is the maximum time between two keys of the sequence.
    const uint8_t konami[] = {0x52, 0x52, 0x51, 0x51, 0x50, 0x4F, 0x50, 0x4F, 0x42, 0x41};
    sequenceDetector.registerSequence(1, konami, 10, 500, onKonamiCode);

    // Secret: 1-2-3-4
    const uint8_t secret[] = {0x31, 0x32, 0x33, 0x34};
    sequenceDetector.registerSequence(2, secret, 4, 1000, onSecretMenu);

    Serial.println("Registered sequences: Konami Code, Secret (1234)");
}

// ===================================
// Demo 3: Toggle
// ===================================

Toggle toggleManager;

void onCapsLockToggle(uint8_t /*keyCode*/, bool state) {
    Serial.print("⌨️  CAPS LOCK: ");
    Serial.println(state ? "ON" : "OFF");
}

void onNumLockToggle(uint8_t /*keyCode*/, bool state) {
    Serial.print("🔢 NUM LOCK: ");
    Serial.println(state ? "ON" : "OFF");
}

void onFlashlightToggle(uint8_t /*keyCode*/, bool state) {
    Serial.print("🔦 Flashlight: ");
    Serial.println(state ? "ON" : "OFF");
}

void setupToggleManager() {
    Serial.println("\n=== Toggle Setup ===");

    // CapsLock (0x39)
    toggleManager.registerToggle(0x39, false, onCapsLockToggle);

    // NumLock (0x53)
    toggleManager.registerToggle(0x53, false, onNumLockToggle);

    // Custom: F12 for the flashlight
    toggleManager.registerToggle(0x7B, false, onFlashlightToggle);

    Serial.println("Registered toggles: CapsLock, NumLock, Flashlight (F12)");
}

// ===================================
// Demo 4: Macro
// ===================================

static const uint8_t KEY_MACRO_RECORD = 0x78;   // F9
static const uint8_t KEY_MACRO_STOP   = 0x79;   // F10
static const uint8_t KEY_MACRO_PLAY   = 0x7A;   // F11

Macro macro;

void onMacroPlayback(uint8_t keyCode) {
    Serial.print("▶️  Macro playback: Key 0x");
    Serial.println(keyCode, HEX);
}

void setupMacro() {
    Serial.println("\n=== Macro Setup ===");
    Serial.println("Click F9 to start recording");
    Serial.println("Click F10 to stop recording");
    Serial.println("Click F11 to play back");
}

// ===================================
// Demo 5: RateLimiter
// ===================================

RateLimiter rateLimiter(5, 1000);   // max 5 clicks per second

void setupRateLimiter() {
    Serial.println("\n=== RateLimiter Setup ===");
    Serial.println("Max 5 clicks per second allowed");
}

// ===================================
// MultiManager + integration
// ===================================

// MultiManager tracks several keys at once (up to 4 here), each with its own
// debounce and click chain. This is what makes combos possible.
MultiManager<4> keyManager;

void onKeyEvent(uint8_t keyCode, Event event) {
    const unsigned long now = g_now;

    // Log
    Serial.print("[0x");
    Serial.print(keyCode, HEX);
    Serial.print(" ");
    Serial.print(eventToString(event));
    Serial.println("]");

    // Forward to the helpers
    comboDetector.handleKeyEvent(keyCode, event);
    sequenceDetector.handleKeyEvent(keyCode, event, now);
    toggleManager.handleKeyEvent(keyCode, event);

    // Macro control keys (F9 / F10 / F11) act on Click
    if (event == Event::Click) {
        if (keyCode == KEY_MACRO_RECORD) {
            macro.startRecording(now);
            Serial.println("🔴 RECORDING STARTED");
        } else if (keyCode == KEY_MACRO_STOP) {
            macro.stopRecording();
            Serial.print("⏹️  RECORDING STOPPED - ");
            Serial.print(macro.getStepCount());
            Serial.println(" steps captured");
        } else if (keyCode == KEY_MACRO_PLAY) {
            Serial.println("▶️  PLAYBACK STARTED");
            macro.play(onMacroPlayback, now);
        }
    }

    // Record every other key press (not the macro control keys themselves)
    if (macro.isRecording() &&
        keyCode != KEY_MACRO_RECORD && keyCode != KEY_MACRO_STOP && keyCode != KEY_MACRO_PLAY) {
        macro.handleKeyEvent(keyCode, event, now);
    }

    // Rate limiter (example: Space key)
    if (keyCode == 0x20 && event == Event::Click) {
        if (rateLimiter.checkClick(now)) {
            Serial.println("✅ Click allowed");
        } else {
            Serial.println("❌ Rate limit exceeded! Slow down!");
        }
    }
}

// ===================================
// Input
// ===================================

// SerialScanner: typed characters act as keys (ASCII code = key code).
// For real combos use GPIOScanner or MatrixScanner: their scanMulti()
// reports every pressed key.
SerialScanner scanner;

static uint8_t readKeys(uint8_t* out, uint8_t maxKeys) {
    const uint8_t n = scanner.scanMulti(out, maxKeys);
    for (uint8_t i = 0; i < n; i++) {
        if (out[i] >= 'a' && out[i] <= 'z') out[i] -= 32;   // case-insensitive
    }
    return n;
}

// ===================================
// Setup and loop
// ===================================

void runAutomatedDemo();

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   Keyboard Helpers Demo Suite          ║");
    Serial.println("╚════════════════════════════════════════╝");

    keyManager.setEventCallback(onKeyEvent);

    // Toggle, Macro control and RateLimiter work on Click. With double click
    // enabled a Click is reported only after the double-click window, so we
    // switch double click off to get immediate, unambiguous clicks.
    Config cfg;
    cfg.enableDoubleClick = false;
    keyManager.setConfig(cfg);

    // Set up the helpers
    setupComboDetector();
    setupSequenceDetector();
    setupToggleManager();
    setupMacro();
    setupRateLimiter();

    Serial.println("\n✅ All helpers initialized!");
    Serial.println("===========================================\n");

    runAutomatedDemo();
}

void loop() {
    g_now = millis();

    uint8_t keys[4];
    const uint8_t n = readKeys(keys, 4);
    keyManager.update(keys, n, g_now);

    // Macro playback is driven by update()
    macro.update(g_now);
}

// ===================================
// Automated demo (simulated key presses, real time)
// ===================================

// Keep the keys `keys[0..n-1]` pressed for `ms` milliseconds.
// A single update() call is not enough: debouncing needs a stable level.
static void simulate(const uint8_t* keys, uint8_t n, unsigned long ms) {
    const unsigned long end = millis() + ms;
    while ((long)(end - millis()) > 0) {
        g_now = millis();
        keyManager.update(keys, n, g_now);
        macro.update(g_now);
        delay(10);
    }
}

// Press one key for holdMs, then release it and wait gapMs.
static void tap(uint8_t key, unsigned long holdMs, unsigned long gapMs) {
    simulate(&key, 1, holdMs);
    simulate(nullptr, 0, gapMs);
}

void demoCombo() {
    Serial.println("Simulating Ctrl+C...");
    const uint8_t ctrl[]  = {0x11};
    const uint8_t ctrlC[] = {0x11, 0x43};
    simulate(ctrl, 1, 100);     // hold Ctrl
    simulate(ctrlC, 2, 200);    // C joins → combo fires
    simulate(ctrl, 1, 100);     // C released
    simulate(nullptr, 0, 100);  // everything released
}

void demoToggle() {
    Serial.println("Toggling CapsLock...");
    tap(0x39, 100, 300);

    Serial.println("Toggling CapsLock again...");
    tap(0x39, 100, 300);
}

void demoSequence() {
    Serial.println("Simulating the 1-2-3-4 sequence...");
    const uint8_t seq[] = {0x31, 0x32, 0x33, 0x34};
    for (int i = 0; i < 4; i++) {
        tap(seq[i], 100, 150);
    }
}

void demoMacro() {
    Serial.println("Recording a macro (keys 1, 2, 3)...");
    tap(KEY_MACRO_RECORD, 100, 200);
    tap(0x31, 100, 200);
    tap(0x32, 100, 400);
    tap(0x33, 100, 200);
    tap(KEY_MACRO_STOP, 100, 300);

    Serial.println("Playing the macro back...");
    tap(KEY_MACRO_PLAY, 100, 1500);     // macro.update() runs inside simulate()
}

void demoRateLimiter() {
    Serial.println("Rapidly pressing Space (8 times)...");
    for (int i = 0; i < 8; i++) {
        tap(0x20, 70, 70);
    }
}

void runAutomatedDemo() {
    Serial.println("--- Auto Demo: Combo Detection ---");
    demoCombo();

    Serial.println("\n--- Auto Demo: Toggle ---");
    demoToggle();

    Serial.println("\n--- Auto Demo: Sequence ---");
    demoSequence();

    Serial.println("\n--- Auto Demo: Macro ---");
    demoMacro();

    Serial.println("\n--- Auto Demo: Rate Limiter ---");
    demoRateLimiter();

    Serial.println("\n=== Demo complete ===");
}

/*
 * ===================================
 * Sample output (automated part)
 * ===================================
 *
 *
 * ╔════════════════════════════════════════╗
 * ║   Keyboard Helpers Demo Suite          ║
 * ╚════════════════════════════════════════╝
 *
 * === ComboDetector Setup ===
 * Registered combos: Ctrl+C, Ctrl+V, Alt+F4, Ctrl+Alt+Del
 *
 * === SequenceDetector Setup ===
 * Registered sequences: Konami Code, Secret (1234)
 *
 * === Toggle Setup ===
 * Registered toggles: CapsLock, NumLock, Flashlight (F12)
 *
 * === Macro Setup ===
 * Click F9 to start recording
 * Click F10 to stop recording
 * Click F11 to play back
 *
 * === RateLimiter Setup ===
 * Max 5 clicks per second allowed
 *
 * ✅ All helpers initialized!
 * ===========================================
 *
 * --- Auto Demo: Combo Detection ---
 * Simulating Ctrl+C...
 * [0x11 Pressed]
 * [0x43 Pressed]
 * 📋 Ctrl+C detected → Copy
 * [0x43 Released]
 * [0x43 Click]
 * [0x11 Released]
 * [0x11 Click]
 *
 * --- Auto Demo: Toggle ---
 * Toggling CapsLock...
 * [0x39 Pressed]
 * [0x39 Released]
 * [0x39 Click]
 * ⌨️  CAPS LOCK: ON
 * Toggling CapsLock again...
 * [0x39 Pressed]
 * [0x39 Released]
 * [0x39 Click]
 * ⌨️  CAPS LOCK: OFF
 *
 * --- Auto Demo: Sequence ---
 * Simulating the 1-2-3-4 sequence...
 * [0x31 Pressed]
 * [0x31 Released]
 * [0x31 Click]
 * [0x32 Pressed]
 * [0x32 Released]
 * [0x32 Click]
 * [0x33 Pressed]
 * [0x33 Released]
 * [0x33 Click]
 * [0x34 Pressed]
 * 🔓 Secret Menu Unlocked!
 * [0x34 Released]
 * [0x34 Click]
 *
 * --- Auto Demo: Macro ---
 * Recording a macro (keys 1, 2, 3)...
 * [0x78 Pressed]
 * [0x78 Released]
 * [0x78 Click]
 * 🔴 RECORDING STARTED
 * [0x31 Pressed]
 * [0x31 Released]
 * [0x31 Click]
 * [0x32 Pressed]
 * [0x32 Released]
 * [0x32 Click]
 * [0x33 Pressed]
 * [0x33 Released]
 * [0x33 Click]
 * [0x79 Pressed]
 * [0x79 Released]
 * [0x79 Click]
 * ⏹️  RECORDING STOPPED - 3 steps captured
 * Playing the macro back...
 * [0x7A Pressed]
 * [0x7A Released]
 * [... output truncated ...]
 */
